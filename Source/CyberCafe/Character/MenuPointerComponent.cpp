#include "MenuPointerComponent.h"

#include "VRPawn.h"
#include "Serving/OrderHologramActor.h"
#include "UI/MenuPointerTarget.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/PackageName.h"
#include "MotionControllerComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

struct FMenuPointerSample
{
    UMotionControllerComponent* Origin = nullptr;
    TWeakObjectPtr<AOrderHologramActor> Screen;
    TWeakObjectPtr<AActor> NativeTarget;
    FMenuPointerHit ScreenHit;
    FHitResult SurfaceHit;
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ZeroVector;
    bool bHasTarget = false;
    FString Reason = TEXT("empty");
};

struct FMenuPointerInputSample
{
    const UEnhancedPlayerInput* Input = nullptr;
    const UInputAction* Action = nullptr;
    float RawMax[2] = {0.f, 0.f};
    float NormalizedMax[2] = {0.f, 0.f};
    float Threshold[2] = {.5f, .5f};
    bool bDown[2] = {false, false};
    float NativeAxes[2] = {0.f, 0.f};
    bool bObserverInstanceValid[2] = {false, false};
    bool bNativePair = false;
    FString ValueSource = TEXT("PhysicalKey");
    FString ObserverReason = TEXT("observer pair unavailable");
    FString MappingKeys;
    FString Diagnostics = TEXT("source=none LMax=0 RMax=0 threshold=none keys=none");
    FString Reason = TEXT("original input unavailable");
};

UMenuPointerComponent::UMenuPointerComponent()
{
    // 只由 Pawn 原 Tick 调用，避免独立 Tick 次序和重复更新。
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);
    StatusStrings[0] = TEXT("L origin=none src=none -> not sampled");
    StatusStrings[1] = TEXT("R origin=none src=none -> not sampled");
    LastClickStatus = TEXT("no original trigger attempt");

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    BeamMesh = Cylinder.Object;
    EndpointMesh = Sphere.Object;
    FallbackMaterial = BasicMaterial.Object;
    RayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
        TEXT("/Game/CyberCafe/Player/Pointer/M_MenuPointer.M_MenuPointer")));
}

bool UMenuPointerComponent::ReadOriginalTriggerInput(const AVRPawn* Pawn, FMenuPointerInputSample& Sample) const
{
    Sample = FMenuPointerInputSample();
    if (!IsValid(Pawn)) return false;
    const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    Sample.Input = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
    Sample.Action = Pawn->IA_SkipDialog;
    if (!Sample.Input || !IsValid(Sample.Action)) return false;
    const FInputActionInstance* Instance = Sample.Input->FindActionInstanceData(Sample.Action);
    if (Instance && Instance->GetSourceAction() != Sample.Action)
    {
        Sample.Reason = TEXT("original action instance source mismatch");
        return false;
    }
    const auto& ActionTriggers = Instance ? Instance->GetTriggers() : Sample.Action->Triggers;
    const auto& ActionModifiers = Instance ? Instance->GetModifiers() : Sample.Action->Modifiers;

    // 只读公开阈值；Chord/Blocker/自定义条件不能当成扳机行程阈值。
    const auto FindThreshold = [](const TArray<TObjectPtr<UInputTrigger>>& Triggers,
        float& Threshold, FString& Source)
    {
        for (const UInputTrigger* Trigger : Triggers)
        {
            if (!IsValid(Trigger) || !(Trigger->IsA<UInputTriggerPressed>() || Trigger->IsA<UInputTriggerDown>()
                    || Trigger->IsA<UInputTriggerHold>()) || Trigger->GetTriggerType() == ETriggerType::Blocker) continue;
            const float Value = Trigger->ActuationThreshold;
            if (FMath::IsFinite(Value) && Value > 0.f && Value < Threshold)
            {
                Threshold = Value;
                Source = Trigger->GetPathName();
            }
        }
    };
    // 不调用运行期 Modifier/Trigger 更新函数，避免重复改变原输入状态。
    const auto ApplyScalarModifiers = [](const TArray<TObjectPtr<UInputModifier>>& Modifiers,
        EInputActionValueType ValueType, float& Value, FString& Failure)
    {
        for (const UInputModifier* Modifier : Modifiers)
        {
            if (!IsValid(Modifier)) continue;
            if (Modifier->GetClass() == UInputModifierScalar::StaticClass())
            {
                if (ValueType != EInputActionValueType::Boolean)
                    Value *= CastChecked<UInputModifierScalar>(Modifier)->Scalar.X;
            }
            else if (Modifier->GetClass() == UInputModifierDeadZone::StaticClass())
            {
                if (ValueType == EInputActionValueType::Boolean) continue;
                const UInputModifierDeadZone* Zone = CastChecked<UInputModifierDeadZone>(Modifier);
                if (!FMath::IsFinite(Zone->LowerThreshold) || !FMath::IsFinite(Zone->UpperThreshold)
                    || Zone->UpperThreshold <= Zone->LowerThreshold)
                {
                    Failure = TEXT("invalid deadzone");
                    return false;
                }
                Value = Zone->Type == EDeadZoneType::UnscaledRadial
                    ? (FMath::Abs(Value) < Zone->LowerThreshold ? 0.f
                        : FMath::Min(FMath::Abs(Value), Zone->UpperThreshold) * FMath::Sign(Value))
                    : FMath::Min(1.f, FMath::Max(0.f, FMath::Abs(Value) - Zone->LowerThreshold)
                        / (Zone->UpperThreshold - Zone->LowerThreshold)) * FMath::Sign(Value);
            }
            else
            {
                Failure = FString::Printf(TEXT("unsupported modifier %s"), *Modifier->GetClass()->GetName());
                return false;
            }
            if (!FMath::IsFinite(Value))
            {
                Failure = TEXT("nonfinite modified value");
                return false;
            }
        }
        return true;
    };

    // UE 5.6 的 GetEnhancedActionMappings 为 protected；只读同一反射 runtime 数组。
    // 不用源 IMC 替代编译后映射，不强转 PlayerInput 到伪派生类，也不修改引擎。
    static const FArrayProperty* MappingsProperty = FindFProperty<FArrayProperty>(
        UEnhancedPlayerInput::StaticClass(), TEXT("EnhancedActionMappings"));
    const FStructProperty* MappingInner = MappingsProperty ? CastField<FStructProperty>(MappingsProperty->Inner) : nullptr;
    if (!MappingInner || MappingInner->Struct != FEnhancedActionKeyMapping::StaticStruct())
    {
        Sample.Reason = TEXT("runtime mappings reflection unavailable");
        return false;
    }
    const TArray<FEnhancedActionKeyMapping>* RuntimeMappings =
        MappingsProperty->ContainerPtrToValuePtr<TArray<FEnhancedActionKeyMapping>>(Sample.Input);
    // 仅认原运行期 Skip 映射中的原生 XR 扳机轴；泛用 Gamepad 键不是手部来源。
    const auto NativeHand = [](const FKey& Key) -> int32
    {
        if (!Key.IsValid() || !Key.IsAxis1D()) return INDEX_NONE;
        if (Key == EKeys::OculusTouch_Left_Trigger_Axis || Key == EKeys::Vive_Left_Trigger_Axis
            || Key == EKeys::ValveIndex_Left_Trigger_Axis || Key == EKeys::MixedReality_Left_Trigger_Axis) return 0;
        if (Key == EKeys::OculusTouch_Right_Trigger_Axis || Key == EKeys::Vive_Right_Trigger_Axis
            || Key == EKeys::ValveIndex_Right_Trigger_Axis || Key == EKeys::MixedReality_Right_Trigger_Axis) return 1;
        return INDEX_NONE;
    };
    // OpenXR's enhanced-input backend injects Actions directly and may omit XR
    // physical keys from EnhancedActionMappings. Validate the active authored
    // context in that case, and require actual per-hand Action instances below.
    // Never invent a hand from the shared Boolean or from the hovered panel.
    bool bInjectedLayout = false;
    TArray<FEnhancedActionKeyMapping> InjectedMappings;
    bool bHasCompiledXRMapping = false;
    for (const FEnhancedActionKeyMapping& Mapping : *RuntimeMappings)
        bHasCompiledXRMapping |= Mapping.Action == Sample.Action && !Mapping.bShouldBeIgnored && NativeHand(Mapping.Key) != INDEX_NONE;
    const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
    const auto* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!bHasCompiledXRMapping && Subsystem && IsValid(Pawn->DefaultMappingContext) &&
        Subsystem->HasMappingContext(Pawn->DefaultMappingContext))
    {
        InjectedMappings = Pawn->DefaultMappingContext->GetMappings();
        RuntimeMappings = &InjectedMappings;
        bInjectedLayout = true;
    }
    TArray<FKey> OriginalKeys[2];
    for (const FEnhancedActionKeyMapping& Mapping : *RuntimeMappings)
    {
        if (Mapping.Action != Sample.Action || Mapping.bShouldBeIgnored) continue;
        const int32 Hand = NativeHand(Mapping.Key);
        if (Hand != INDEX_NONE) OriginalKeys[Hand].AddUnique(Mapping.Key);
    }
    const UInputAction* Observers[] = {LeftTriggerObserver.Get(), RightTriggerObserver.Get()};
    bool bObserverMappingsValid = !OriginalKeys[0].IsEmpty() && !OriginalKeys[1].IsEmpty()
        && IsValid(Observers[0]) && IsValid(Observers[1]) && Observers[0] != Observers[1];
    for (int32 Hand = 0; bObserverMappingsValid && Hand < 2; ++Hand)
    {
        const UInputAction* Observer = Observers[Hand];
        bObserverMappingsValid = Observer != Sample.Action && Observer->ValueType == EInputActionValueType::Axis1D
            && !Observer->bConsumeInput && Observer->Triggers.IsEmpty() && Observer->Modifiers.IsEmpty()
            && Observer->AccumulationBehavior == EInputActionAccumulationBehavior::TakeHighestAbsoluteValue
            && Observer->GetPathName().StartsWith(TEXT("/Game/CyberCafe/Player/Input/Actions/"));
        TArray<FKey> ObserverKeys;
        for (const FEnhancedActionKeyMapping& Mapping : *RuntimeMappings)
        {
            if (Mapping.Action != Observer) continue;
            bObserverMappingsValid &= !Mapping.bShouldBeIgnored && NativeHand(Mapping.Key) == Hand
                && OriginalKeys[Hand].Contains(Mapping.Key) && Mapping.Triggers.IsEmpty() && Mapping.Modifiers.IsEmpty();
            ObserverKeys.AddUnique(Mapping.Key);
        }
        bObserverMappingsValid &= ObserverKeys.Num() == OriginalKeys[Hand].Num();
    }
    for (int32 Hand = 0; Hand < 2; ++Hand)
    {
        const FInputActionInstance* ObserverInstance = bObserverMappingsValid
            ? Sample.Input->FindActionInstanceData(Observers[Hand]) : nullptr;
        Sample.bObserverInstanceValid[Hand] = ObserverInstance && ObserverInstance->GetSourceAction() == Observers[Hand]
            && ObserverInstance->GetValue().GetValueType() == EInputActionValueType::Axis1D
            && ObserverInstance->GetTriggers().IsEmpty() && ObserverInstance->GetModifiers().IsEmpty();
        if (Sample.bObserverInstanceValid[Hand])
        {
            Sample.NativeAxes[Hand] = ObserverInstance->GetValue().Get<float>();
            Sample.bObserverInstanceValid[Hand] = FMath::IsFinite(Sample.NativeAxes[Hand]);
        }
    }
    Sample.bNativePair = Sample.bObserverInstanceValid[0] && Sample.bObserverInstanceValid[1];
    Sample.ValueSource = Sample.bNativePair ? TEXT("NativeAction") : TEXT("PhysicalKey");
    Sample.ObserverReason = Sample.bNativePair ? TEXT("valid pair (zero is authoritative)")
        : (bObserverMappingsValid ? TEXT("observer instance unavailable or invalid") : TEXT("observer pair/mapping invalid"));
    int32 MappingCount = 0;
    FString Keys;
    for (const FEnhancedActionKeyMapping& Mapping : *RuntimeMappings)
    {
        if (Mapping.Action != Sample.Action || Mapping.bShouldBeIgnored) continue;
        const int32 Index = NativeHand(Mapping.Key);
        if (Index == INDEX_NONE) continue;
        const FString KeyName = Mapping.Key.GetFName().ToString();
        ++MappingCount;
        Sample.MappingKeys += KeyName + TEXT(";");
        // 原共享 Boolean 的 1 仅用于诊断；绝不拿它重算两手行程。
        const float PhysicalValue = Sample.bNativePair ? 0.f : PC->GetInputAnalogKeyState(Mapping.Key);
        const float RawValue = Sample.bNativePair ? Sample.NativeAxes[Index] : PhysicalValue;
        if (FMath::IsFinite(RawValue)) Sample.RawMax[Index] = FMath::Max(Sample.RawMax[Index], RawValue);
        float Threshold = TNumericLimits<float>::Max();
        FString ThresholdSource;
        FindThreshold(Mapping.Triggers, Threshold, ThresholdSource);
        FindThreshold(ActionTriggers, Threshold, ThresholdSource);
        if (ThresholdSource.IsEmpty())
        {
            Threshold = .5f;
            ThresholdSource = TEXT("none(default=0.5)");
        }
        float Value = RawValue;
        FString Failure;
        const bool bScalarAction = Sample.Action->ValueType == EInputActionValueType::Boolean
            || Sample.Action->ValueType == EInputActionValueType::Axis1D;
        // 空观察轴已经经过引擎处理；不重复施加 Skip 的 Boolean DeadZone 或其他 Modifier。
        const bool bValidValue = bScalarAction && FMath::IsFinite(Value)
            && (Sample.bNativePair || (ApplyScalarModifiers(Mapping.Modifiers, Sample.Action->ValueType, Value, Failure)
                && ApplyScalarModifiers(ActionModifiers, Sample.Action->ValueType, Value, Failure)));
        const float Normalized = bValidValue && Value > 0.f ? Value / Threshold : 0.f;
        if (FMath::IsFinite(Normalized) && Normalized >= Sample.NormalizedMax[Index])
        {
            Sample.NormalizedMax[Index] = Normalized;
            Sample.Threshold[Index] = Threshold;
        }
        Sample.bDown[Index] |= bValidValue && Value > 0.f && Value >= Threshold;
        const FString PhysicalText = Sample.bNativePair ? TEXT("not-read(native)") : FString::Printf(TEXT("%.4f"), PhysicalValue);
        Keys += FString::Printf(TEXT("%s raw=%.4f physical=%s modified=%.4f threshold=%.4f from=%s %s; "),
            *KeyName, RawValue, *PhysicalText, Value, Threshold, *ThresholdSource,
            bValidValue ? TEXT("valid") : (Failure.IsEmpty() ? TEXT("invalid scalar source") : *Failure));
    }
    const bool bOriginalPair = !OriginalKeys[0].IsEmpty() && !OriginalKeys[1].IsEmpty();
    Sample.Reason = bOriginalPair ? TEXT("under threshold") : TEXT("original hand mappings unavailable");
    if (bOriginalPair && !Sample.bDown[0] && !Sample.bDown[1] && Instance && Instance->GetValue().IsNonZero())
        Sample.Reason = Sample.bNativePair
            ? TEXT("under threshold: original Boolean active but observer axes below original gate")
            : TEXT("critical original Action active but observer unavailable and PhysicalKey axes zero/below gate");
    if (bInjectedLayout && !Sample.bNativePair)
        Sample.Reason = TEXT("OpenXR context active but per-hand observer instances unavailable");
    const FString LeftAxis = Sample.bObserverInstanceValid[0] ? FString::Printf(TEXT("%.4f"), Sample.NativeAxes[0]) : TEXT("None");
    const FString RightAxis = Sample.bObserverInstanceValid[1] ? FString::Printf(TEXT("%.4f"), Sample.NativeAxes[1]) : TEXT("None");
    // 读取真实 GetValue/GetTriggerEvent；不伪造 Started 的共享 Boolean 值或 XR 阈值。
    Sample.Diagnostics = FString::Printf(
        TEXT("source=%s runtimeSource=%s event=%d actionMagnitude=%.4f OriginalActionValueNative=%.4f ValueSource=%s ObserverPairValid=%d LObserver=%s RObserver=%s LInstanceValid=%d RInstanceValid=%d HandAxes=[L=%s R=%s] observerReason=%s LMax=%.4f RMax=%.4f LThreshold=%.4f RThreshold=%.4f LNorm=%.4f RNorm=%.4f LDown=%d RDown=%d keys=[%s] MappingSource=%s"),
        *Sample.Action->GetPathName(), Instance ? *GetNameSafe(Instance->GetSourceAction().Get()) : TEXT("none"),
        Instance ? static_cast<int32>(Instance->GetTriggerEvent()) : 0,
        Instance ? Instance->GetValue().GetMagnitude() : 0.f, Instance ? Instance->GetValue().GetMagnitude() : 0.f,
        *Sample.ValueSource, Sample.bNativePair,
        *GetPathNameSafe(Observers[0]), *GetPathNameSafe(Observers[1]),
        Sample.bObserverInstanceValid[0], Sample.bObserverInstanceValid[1],
        *LeftAxis, *RightAxis, *Sample.ObserverReason,
        Sample.RawMax[0], Sample.RawMax[1], Sample.Threshold[0], Sample.Threshold[1],
        Sample.NormalizedMax[0], Sample.NormalizedMax[1], Sample.bDown[0], Sample.bDown[1],
        Keys.IsEmpty() ? TEXT("none threshold=none") : *Keys,
        bInjectedLayout ? TEXT("OpenXRActiveContext") : TEXT("CompiledPhysicalKeys"));
    return MappingCount > 0 && bOriginalPair && (!bInjectedLayout || Sample.bNativePair);
}

void UMenuPointerComponent::RememberOriginalTriggerInput(const FMenuPointerInputSample& Sample)
{
    PriorInput = Sample.Input;
    PriorSkipAction = Sample.Action;
    PriorMappingKeys = Sample.MappingKeys;
    if (!Sample.MappingKeys.IsEmpty()) LastOriginalMappingKeys = Sample.MappingKeys;
    bHasPriorAxes = Sample.Input && Sample.Action && !Sample.MappingKeys.IsEmpty();
    for (int32 Index = 0; Index < 2; ++Index) HandPriorAxes[Index] = Sample.NormalizedMax[Index];
}

bool UMenuPointerComponent::ResolveOriginalTriggerHand(const FMenuPointerInputSample& Sample,
    bool& bRightHand, FString& Reason) const
{
    Reason = Sample.Reason;
    if (Sample.bDown[0] != Sample.bDown[1])
    {
        bRightHand = Sample.bDown[1];
        Reason = TEXT("single actuated hand");
        return true;
    }
    // 双手越阈无唯一来源：不按历史 edge、悬停或强度猜手，也不合成原 Started。
    if (Sample.bDown[0]) Reason = TEXT("ambiguous two actuated hands -> OriginalSkip");
    return false;
}

UMotionControllerComponent* UMenuPointerComponent::FindPointerOrigin(const AVRPawn* Pawn, bool bRightHand) const
{
    const auto IsUsable = [Pawn](const UMotionControllerComponent* Controller)
    {
        if (!IsValid(Controller) || Controller->GetOwner() != Pawn || !Controller->IsRegistered()
            || !Controller->IsActive() || !Controller->IsTracked()) return false;
        const FTransform Transform = Controller->GetComponentTransform();
        const FVector Scale = Transform.GetScale3D();
        return !Transform.ContainsNaN() && Scale.X > 0 && Scale.Y > 0 && Scale.Z > 0;
    };
    const FName AimSource(bRightHand ? TEXT("RightAim") : TEXT("LeftAim"));
    UMotionControllerComponent* Aim = bRightHand ? Pawn->MotionControllerRightAim.Get() : Pawn->MotionControllerLeftAim.Get();
    if (IsUsable(Aim) && Aim->MotionSource == AimSource) return Aim;

    // 同样适用于 AVRPawn 的蓝图子类；只认真实注册且仍在追踪的同手来源。
    TArray<UMotionControllerComponent*> Controllers;
    Pawn->GetComponents<UMotionControllerComponent>(Controllers);
    for (UMotionControllerComponent* Controller : Controllers)
        if (IsUsable(Controller) && Controller->MotionSource == AimSource) return Controller;

    const FName GripSource(bRightHand ? TEXT("RightGrip") : TEXT("LeftGrip"));
    const FName HandSource(bRightHand ? TEXT("Right") : TEXT("Left"));
    UMotionControllerComponent* Grip = bRightHand ? Pawn->MotionControllerRightGrip.Get() : Pawn->MotionControllerLeftGrip.Get();
    if (IsUsable(Grip) && (Grip->MotionSource == GripSource || Grip->MotionSource == HandSource)) return Grip;
    for (UMotionControllerComponent* Controller : Controllers)
        if (IsUsable(Controller) && (Controller->MotionSource == GripSource || Controller->MotionSource == HandSource)) return Controller;
    return nullptr;
}

bool UMenuPointerComponent::SamplePhysicalSurface(const AVRPawn* Pawn, bool bRightHand, FMenuPointerSample& Sample) const
{
    Sample = FMenuPointerSample();
    const auto Reject = [&Sample](const TCHAR* Reason) { Sample.Reason = Reason; return false; };
    if (bEndingPlay || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return Reject(TEXT("ending play"));
    if (!Pawn->IsLocallyControlled()) return Reject(TEXT("notlocal"));
    UWorld* World = GetWorld();
    if (!World) return Reject(TEXT("world unavailable"));
    if (!FMath::IsFinite(RayMaxDistance) || RayMaxDistance <= 0.f) return Reject(TEXT("invalid ray distance"));
    Sample.Origin = FindPointerOrigin(Pawn, bRightHand);
    if (!Sample.Origin) return Reject(TEXT("tracking unavailable"));
    Sample.Start = Sample.Origin->GetComponentLocation();
    const FVector Direction = Sample.Origin->GetForwardVector().GetSafeNormal();
    if (Sample.Start.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero())
        return Reject(TEXT("invalid origin transform"));
    const double Limit = FMath::Min(RayMaxDistance, 250.f);
    Sample.End = Sample.Start + Direction * Limit;

    // 物理面只决定指示终点和遮挡；绝不调用 Pawn 的抓取扫描/高亮函数。
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MenuPointerSurface), true);
    Params.AddIgnoredActor(Pawn);
    FHitResult PhysicalHit;
    // 仅真实 Visibility 阻挡遮住 UI；不可见 overlap-only 区域不截断菜单。
    const bool bPhysicalHit = World->LineTraceSingleByChannel(PhysicalHit, Sample.Start, Sample.End, ECC_Visibility, Params);
    if (bPhysicalHit && !PhysicalHit.ImpactPoint.ContainsNaN() && FMath::IsFinite(PhysicalHit.Distance))
    {
        Sample.SurfaceHit = PhysicalHit;
        Sample.End = PhysicalHit.ImpactPoint;
        Sample.bHasTarget = true;
    }
    Sample.Reason = Sample.bHasTarget ? TEXT("surface") : TEXT("empty");
    return true;
}

bool UMenuPointerComponent::SamplePointer(const AVRPawn* Pawn, bool bRightHand, FMenuPointerSample& Sample) const
{
    Sample = FMenuPointerSample();
    const auto Reject = [&Sample](const TCHAR* Reason) { Sample.Reason = Reason; return false; };
    if (!bEnabled) return Reject(TEXT("disabled"));
    if (bEndingPlay || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return Reject(TEXT("ending play"));
    if (!Pawn->IsLocallyControlled()) return Reject(TEXT("notlocal"));
    if (Pawn->bTeleportTraceActive) return Reject(TEXT("teleport"));
    // 只读引用，不判断/改变抓取资格，也不清除现有抓取和拉拽。
    if (bRightHand ? (Pawn->HeldComponentRight || Pawn->PulledGrabComponentRight)
                   : (Pawn->HeldComponentLeft || Pawn->PulledGrabComponentLeft)) return Reject(TEXT("held"));
    if (!SamplePhysicalSurface(Pawn, bRightHand, Sample)) return false;
    UWorld* World = GetWorld();
    const FVector Direction = Sample.Origin->GetForwardVector().GetSafeNormal();
    const double Limit = FMath::Min(RayMaxDistance, 250.f);
    double Closest = Sample.SurfaceHit.bBlockingHit ? Sample.SurfaceHit.Distance : Limit;

    // 无碰撞全息面使用现有解析接口；每次取当前几何，不缓存上一帧可点击状态。
    for (TActorIterator<AOrderHologramActor> It(World); It; ++It)
    {
        AOrderHologramActor* Screen = *It;
        if (!IsValid(Screen) || Screen->IsActorBeingDestroyed() || Screen->IsHidden()) continue;
        FMenuPointerHit Hit;
        if (!Screen->TraceMenuPointer(Sample.Start, Direction, Limit, Hit)
            || Hit.Point.ContainsNaN() || !FMath::IsFinite(Hit.Distance) || Hit.Distance <= 0
            || Hit.Distance > Closest + .001) continue;
        Closest = Hit.Distance;
        Sample.Screen = Screen;
        Sample.ScreenHit = Hit;
        Sample.End = Hit.Point;
        Sample.bHasTarget = true;
    }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Target = *It;
        IMenuPointerTarget* Interface = IsValid(Target) && !Target->IsActorBeingDestroyed() ? Cast<IMenuPointerTarget>(Target) : nullptr;
        if (!Interface || Target->IsHidden()) continue;
        FMenuPointerHit Hit;
        if (!Interface->TracePointer(Sample.Start, Direction, Limit, Hit) || Hit.Point.ContainsNaN() ||
            !FMath::IsFinite(Hit.Distance) || Hit.Distance <= 0 || Hit.Distance > Closest + .001) continue;
        Closest = Hit.Distance;
        Sample.NativeTarget = Target;
        Sample.Screen.Reset();
        Sample.ScreenHit = Hit;
        Sample.End = Hit.Point;
        Sample.bHasTarget = true;
    }
    if (Sample.Screen.IsValid() || Sample.NativeTarget.IsValid())
        Sample.Reason = Sample.ScreenHit.bPower ? TEXT("UIpower")
            : (Sample.ScreenHit.Action != INDEX_NONE
                ? FString::Printf(TEXT("UIaction%d"), Sample.ScreenHit.Action) : FString(TEXT("UIblank")));
    else Sample.Reason = Sample.bHasTarget ? TEXT("surface") : TEXT("empty");
    return true;
}

bool UMenuPointerComponent::TraceSurfaceForHand(bool bRightHand, FHitResult& OutHit) const
{
    OutHit = FHitResult();
    const AVRPawn* Pawn = Cast<AVRPawn>(GetOwner());
    FMenuPointerSample Sample;
    // 关闭菜单视觉不禁用原远程物理面查询；同用追踪 Origin、Visibility 和 250cm 上限。
    if (!(bEnabled ? SamplePointer(Pawn, bRightHand, Sample)
                   : SamplePhysicalSurface(Pawn, bRightHand, Sample))) return false;
    if (AOrderHologramActor* Screen = Sample.Screen.Get())
    {
        OutHit = FHitResult(Screen, Screen->Panel.Get(), Sample.ScreenHit.Point, Screen->Panel->GetForwardVector());
        OutHit.bBlockingHit = true;
        OutHit.Distance = Sample.ScreenHit.Distance;
        OutHit.Time = Sample.ScreenHit.Distance / FMath::Min(RayMaxDistance, 250.f);
        OutHit.Location = OutHit.ImpactPoint = Sample.ScreenHit.Point;
        OutHit.TraceStart = Sample.Start;
        OutHit.TraceEnd = Sample.Start + Sample.Origin->GetForwardVector().GetSafeNormal() * FMath::Min(RayMaxDistance, 250.f);
        return true; // 空白屏幕也阻挡，不能抓到可见菜单背后的物体。
    }
    if (AActor* Target = Sample.NativeTarget.Get())
    {
        OutHit = FHitResult(Target, nullptr, Sample.ScreenHit.Point, Target->GetActorForwardVector());
        OutHit.bBlockingHit = true;
        OutHit.Distance = Sample.ScreenHit.Distance;
        OutHit.Location = OutHit.ImpactPoint = Sample.ScreenHit.Point;
        OutHit.TraceStart = Sample.Start;
        OutHit.TraceEnd = Sample.Start + Sample.Origin->GetForwardVector().GetSafeNormal() * FMath::Min(RayMaxDistance, 250.f);
        return true;
    }
    if (!Sample.SurfaceHit.bBlockingHit) return false;
    OutHit = Sample.SurfaceHit;
    return true;
}

FString UMenuPointerComponent::GetPointerStatus(EControllerHand Hand) const
{
    if (Hand == EControllerHand::Left) return StatusStrings[0];
    if (Hand == EControllerHand::Right) return StatusStrings[1];
    return TEXT("invalid hand");
}

UStaticMeshComponent* UMenuPointerComponent::GetBeam(EControllerHand Hand) const
{
    const int32 Index = Hand == EControllerHand::Left ? 0 : (Hand == EControllerHand::Right ? 1 : INDEX_NONE);
    return Beams.IsValidIndex(Index) && IsValid(Beams[Index]) ? Beams[Index].Get() : nullptr;
}

UStaticMeshComponent* UMenuPointerComponent::GetEndpoint(EControllerHand Hand) const
{
    const int32 Index = Hand == EControllerHand::Left ? 0 : (Hand == EControllerHand::Right ? 1 : INDEX_NONE);
    return Endpoints.IsValidIndex(Index) && IsValid(Endpoints[Index]) ? Endpoints[Index].Get() : nullptr;
}

void UMenuPointerComponent::CachePointerStatus(int32 HandIndex, const FMenuPointerSample& Sample)
{
    const bool bOrigin = IsValid(Sample.Origin);
    const FString Source = bOrigin ? Sample.Origin->MotionSource.ToString() : TEXT("none");
    StatusStrings[HandIndex] = FString::Printf(TEXT("%s %s origin=%s src=%s tracked=%d -> %s"),
        HandIndex == 1 ? TEXT("R") : TEXT("L"), bOrigin ? (Source.Contains(TEXT("Aim"))
            ? TEXT("Aim") : TEXT("GripFallback")) : TEXT("none"),
        *GetNameSafe(Sample.Origin), *Source, bOrigin && Sample.Origin->IsTracked(), *Sample.Reason);
}

bool UMenuPointerComponent::TryClickFromOriginalTrigger()
{
    ++OriginalTriggerAttemptCount; // 放在本地/Origin 门禁之前，证明原 Skip 回调确实进入。
    AVRPawn* Pawn = Cast<AVRPawn>(GetOwner());
    FMenuPointerInputSample InputSample;
    const bool bHasInput = ReadOriginalTriggerInput(Pawn, InputSample);
    if (!InputSample.MappingKeys.IsEmpty()) LastOriginalMappingKeys = InputSample.MappingKeys;
    FMenuPointerSample Sample;
    int32 HandIndex = INDEX_NONE;
    FString Selection = TEXT("unresolved");
    const auto Describe = [&]()
    {
        return FString::Printf(TEXT("attempt=%d acceptedCount=%d %s triggerHand=%s selection=%s origin=%s MotionSource=%s tracked=%d screen=%s actualAction=%d power=%d Hit.Point=%s CurrentPanelOpen=%d pointer=%s L=[%s] R=[%s] lastKeys=[%s]"),
            OriginalTriggerAttemptCount, AcceptedClickCount, *InputSample.Diagnostics,
            HandIndex == INDEX_NONE ? TEXT("none") : (HandIndex == 1 ? TEXT("R") : TEXT("L")), *Selection,
            *GetNameSafe(Sample.Origin), IsValid(Sample.Origin) ? *Sample.Origin->MotionSource.ToString() : TEXT("none"),
            IsValid(Sample.Origin) && Sample.Origin->IsTracked(),
            *GetNameSafe(Sample.NativeTarget.IsValid() ? Sample.NativeTarget.Get() : Sample.Screen.Get()),
            Sample.ScreenHit.Action, Sample.ScreenHit.bPower, *Sample.ScreenHit.Point.ToString(),
            Sample.Screen.IsValid() && Sample.Screen->IsPanelOpen(), *Sample.Reason,
            *StatusStrings[0], *StatusStrings[1], LastOriginalMappingKeys.IsEmpty() ? TEXT("none") : *LastOriginalMappingKeys);
    };
    const auto Reject = [&](const FString& Reason)
    {
        LastClickStatus = Describe() + TEXT(" result=OriginalSkip reason=") + Reason;
        UE_LOG(LogTemp, Log, TEXT("[MenuPointer] %s"), *LastClickStatus);
        return false;
    };
    if (!bEnabled || bEndingPlay)
    {
        bHasPriorAxes = false;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            HidePointer(Index);
            FMenuPointerSample Disabled;
            Disabled.Reason = bEnabled ? TEXT("ending play") : TEXT("disabled");
            CachePointerStatus(Index, Disabled);
        }
        return Reject(bEnabled ? TEXT("ending play") : TEXT("disabled"));
    }
    if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return Reject(TEXT("owner unavailable"));
    if (!Pawn->IsLocallyControlled()) return Reject(TEXT("notlocal"));
    if (bClickInProgress) return Reject(TEXT("callback in progress"));
    if (!bHasInput) return Reject(InputSample.Reason);
    bool bRightHand = false;
    const bool bResolved = ResolveOriginalTriggerHand(InputSample, bRightHand, Selection);
    RememberOriginalTriggerInput(InputSample);
    if (!bResolved) return Reject(Selection);
    HandIndex = bRightHand ? 1 : 0;
    const bool bSampled = SamplePointer(Pawn, bRightHand, Sample);
    CachePointerStatus(HandIndex, Sample);
    if (!bSampled)
    {
        HidePointer(HandIndex);
        return Reject(Sample.Reason);
    }
    if (!Sample.Screen.IsValid() && !Sample.NativeTarget.IsValid()) return Reject(Sample.Reason);
    if (!Sample.ScreenHit.bPower && Sample.ScreenHit.Action == INDEX_NONE) return Reject(TEXT("UIblank"));
    if (Sample.Screen.IsValid() && !Sample.ScreenHit.bPower
        && Sample.Screen->GetPointerActionAtWorldPoint(Sample.ScreenHit.Point) != Sample.ScreenHit.Action)
        return Reject(TEXT("current UI action mismatch"));

    const TWeakObjectPtr<UMenuPointerComponent> Guard(this);
    const TWeakObjectPtr<AVRPawn> OwnerGuard(Pawn);
    const FString Details = Describe(); // 回调可能销毁 Owner/组件；先截取诊断，之后不读旧裸指针。
    LastClickStatus = Details + TEXT(" result=calling UI reason=") + Selection;
    UE_LOG(LogTemp, Log, TEXT("[MenuPointer] %s"), *LastClickStatus);
    bClickInProgress = true;
    IMenuPointerTarget* Native = Cast<IMenuPointerTarget>(Sample.NativeTarget.Get());
    const bool bAccepted = Native ? Native->ActivatePointer(bRightHand ? EControllerHand::Right : EControllerHand::Left, Sample.ScreenHit)
        : Sample.Screen->TryActivateMenuPointer(bRightHand ? EControllerHand::Right : EControllerHand::Left, Sample.ScreenHit);
    const FString ResultStatus = Details + (bAccepted ? TEXT(" result=accepted reason=UI accepted")
        : TEXT(" result=OriginalSkip reason=UI rejected"));
    UE_LOG(LogTemp, Log, TEXT("[MenuPointer] %s"), *ResultStatus);
    // 只有现有 UI 接受动作才返回 true；拒绝/空白/失追/持物均让原 Skip 总线可达。
    if (Guard.IsValid() && !Guard->bEndingPlay && OwnerGuard.IsValid() && !OwnerGuard->IsActorBeingDestroyed())
    {
        Guard->bClickInProgress = false;
        Guard->LastClickStatus = ResultStatus;
        if (bAccepted)
        {
            ++Guard->AcceptedClickCount;
            Guard->FlashScreens[HandIndex] = Sample.Screen;
            Guard->AcceptedFlashRemaining[HandIndex] = .12f; // 仅视觉闪烁，不用于输入门禁。
        }
    }
    return bAccepted;
}

void UMenuPointerComponent::EnsureVisuals()
{
    if (Beams.Num() == 2) return;
    AActor* Owner = GetOwner();
    if (!bEnabled || bEndingPlay || !IsValid(Owner) || Owner->IsActorBeingDestroyed() || !GetWorld()) return;

    UMaterialInterface* Material = RayMaterial.Get();
    if (!Material && !RayMaterial.IsNull()
        && FPackageName::DoesPackageExist(RayMaterial.ToSoftObjectPath().GetLongPackageName()))
        Material = RayMaterial.LoadSynchronous();
    if (!Material) Material = FallbackMaterial;

    Beams.SetNum(2);
    Endpoints.SetNum(2);
    PointerMaterials.SetNum(2);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        PointerMaterials[Index] = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
        for (int32 Part = 0; Part < 2; ++Part)
        {
            UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
            Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(Part == 0 ? BeamMesh.Get() : EndpointMesh.Get());
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
            Mesh->SetGenerateOverlapEvents(false);
            Mesh->SetSimulatePhysics(false);
            Mesh->SetCastShadow(false);
            Mesh->SetCanEverAffectNavigation(false);
            Mesh->SetIsReplicated(false);
            Mesh->SetOnlyOwnerSee(true);
            Mesh->SetVisibility(false);
            Mesh->SetHiddenInGame(true);
            Mesh->PrimaryComponentTick.bCanEverTick = false;
            if (PointerMaterials[Index]) Mesh->SetMaterial(0, PointerMaterials[Index]);
            if (Owner->GetRootComponent()) Mesh->SetupAttachment(Owner->GetRootComponent());
            Owner->AddInstanceComponent(Mesh);
            Mesh->RegisterComponent();
            (Part == 0 ? Beams[Index] : Endpoints[Index]) = Mesh;
        }
    }
}

void UMenuPointerComponent::DrawPointer(int32 HandIndex, const FMenuPointerSample& Sample)
{
    if (!Beams.IsValidIndex(HandIndex) || !Endpoints.IsValidIndex(HandIndex)) return;
    UStaticMeshComponent* Beam = Beams[HandIndex];
    UStaticMeshComponent* Dot = Endpoints[HandIndex];
    if (!IsValid(Beam) || !IsValid(Dot) || !Sample.Origin) return;

    FLinearColor Color(.02f, .85f, 1.f);
    if (Sample.Screen.IsValid() || Sample.NativeTarget.IsValid())
        Color = Sample.ScreenHit.bPower || Sample.ScreenHit.Action != INDEX_NONE
            ? FLinearColor(1.f, .38f, .02f) : FLinearColor(.35f, .35f, .35f);
    if (AcceptedFlashRemaining[HandIndex] > 0.f && Sample.Screen.IsValid()
        && FlashScreens[HandIndex] == Sample.Screen) Color = FLinearColor(.05f, 1.f, .12f);
    if (UMaterialInstanceDynamic* Material = PointerMaterials[HandIndex])
    {
        Material->SetVectorParameterValue(TintParameterName, Color);
        // 引擎基础材质未暴露 Tint 时仍保留可见几何；不改任何源材质。
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
    }

    for (UStaticMeshComponent* Mesh : {Beam, Dot})
        if (Mesh->GetAttachParent() != Sample.Origin)
            Mesh->AttachToComponent(Sample.Origin, FAttachmentTransformRules::KeepWorldTransform);
    const FVector Segment = Sample.End - Sample.Start;
    const double Length = Segment.Size();
    // 引擎 Cylinder/Sphere 单位尺寸为 100cm；使用实际父变换，保留手柄 Late Update。
    Beam->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Sample.Origin->GetForwardVector()).ToQuat(),
        (Sample.Start + Sample.End) * .5,
        FVector(FMath::Max(.01f, BeamDiameterCM) / 100.f, FMath::Max(.01f, BeamDiameterCM) / 100.f, Length / 100.)));
    Dot->SetWorldLocation(Sample.End);
    Dot->SetWorldScale3D(FVector(FMath::Max(.01f, EndpointDiameterCM) / 100.f));
    Beam->SetHiddenInGame(false);
    Beam->SetVisibility(Length > 0.);
    Dot->SetHiddenInGame(!Sample.bHasTarget);
    Dot->SetVisibility(Sample.bHasTarget);
}

void UMenuPointerComponent::HidePointer(int32 HandIndex)
{
    if (Beams.IsValidIndex(HandIndex) && IsValid(Beams[HandIndex]))
    {
        Beams[HandIndex]->SetVisibility(false);
        Beams[HandIndex]->SetHiddenInGame(true);
    }
    if (Endpoints.IsValidIndex(HandIndex) && IsValid(Endpoints[HandIndex]))
    {
        Endpoints[HandIndex]->SetVisibility(false);
        Endpoints[HandIndex]->SetHiddenInGame(true);
    }
}

void UMenuPointerComponent::UpdatePointer(float DeltaTime)
{
    if (!bEnabled || bEndingPlay)
    {
        for (int32 Index = 0; Index < 2; ++Index)
        {
            HidePointer(Index);
            AcceptedFlashRemaining[Index] = 0.f;
            FlashScreens[Index].Reset();
            FMenuPointerSample Sample;
            Sample.Reason = bEnabled ? TEXT("ending play") : TEXT("disabled");
            CachePointerStatus(Index, Sample);
        }
        bHasPriorAxes = false;
        return;
    }
    const AVRPawn* Pawn = Cast<AVRPawn>(GetOwner());
    FMenuPointerInputSample InputSample;
    ReadOriginalTriggerInput(Pawn, InputSample);
    RememberOriginalTriggerInput(InputSample);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        AcceptedFlashRemaining[Index] = FMath::Max(0.f,
            AcceptedFlashRemaining[Index] - (FMath::IsFinite(DeltaTime) ? FMath::Max(0.f, DeltaTime) : 0.f));
        FMenuPointerSample Sample;
        const bool bSampled = SamplePointer(Pawn, Index == 1, Sample);
        CachePointerStatus(Index, Sample);
        if (!bSampled)
        {
            HidePointer(Index);
            AcceptedFlashRemaining[Index] = 0.f;
            FlashScreens[Index].Reset();
            continue;
        }
        EnsureVisuals();
        DrawPointer(Index, Sample);
    }
}

void UMenuPointerComponent::DestroyVisuals()
{
    for (const auto& Mesh : Beams) if (IsValid(Mesh)) Mesh->DestroyComponent();
    for (const auto& Mesh : Endpoints) if (IsValid(Mesh)) Mesh->DestroyComponent();
    Beams.Reset();
    Endpoints.Reset();
    PointerMaterials.Reset();
    for (int32 Index = 0; Index < 2; ++Index)
    {
        AcceptedFlashRemaining[Index] = 0.f;
        FlashScreens[Index].Reset();
    }
}

void UMenuPointerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bEndingPlay = true;
    bHasPriorAxes = false;
    DestroyVisuals();
    Super::EndPlay(EndPlayReason);
}

void UMenuPointerComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    bEndingPlay = true;
    bHasPriorAxes = false;
    DestroyVisuals();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}
