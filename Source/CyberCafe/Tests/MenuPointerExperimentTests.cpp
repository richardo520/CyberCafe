// UE 5.6：原保存 Boolean IA_SkipDialog，经 InputKey + ProcessInputStack 处理原 IMC/原 Pawn 绑定。
// 仅模拟设备轴事件及 IMotionController 姿态，不代表实际头显、设备驱动或渲染像素验收。
// 保留真实 InputKey 及原 Pressed 阈值 .5；.02 的 Boolean getter 为 true，但底层幅值仍为 .02、未 actuated，不产生 Started/UI 点击。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "CoreGlobals.h"
#include "Character/MenuPointerComponent.h"
#include "Character/VRPawn.h"
#include "GrabComponent.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"
#include "Core/Events/CyberCafeEventSubsystem.h"
#include "UI/OrderHologramWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/Engine.h"
#include "Engine/InputDelegateBinding.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "IMotionController.h"
#include "MotionControllerComponent.h"
#include "Features/IModularFeatures.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "NiagaraComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "RenderingThread.h"
#include "UObject/GCObject.h"
#include "UObject/UnrealType.h"

namespace CyberCafeMenuPointerExperimentTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr int32 ControllerId = 1721;
constexpr float Dt = 1.f / 60.f;
const TCHAR* MapPath = TEXT("/Game/CyberCafe/Maps/L_Cafe.L_Cafe");
const FName Sources[] = {TEXT("LeftGrip"), TEXT("LeftAim"), TEXT("RightGrip"), TEXT("RightAim")};

// 保持四个原 MotionSource；用独立玩家索引隔离已有 XR 服务，不注销现有服务。
class FTrackingProvider final : public IMotionController
{
    mutable FCriticalSection Mutex;
    TMap<FName, FTransform> Poses;
public:
    FTrackingProvider() { IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this); }
    ~FTrackingProvider() { IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this); }
    void Set(FName Source, const FTransform& Pose, bool bTracked)
    {
        FScopeLock Lock(&Mutex);
        if (bTracked) Poses.Add(Source, Pose); else Poses.Remove(Source);
    }
    virtual FName GetMotionControllerDeviceTypeName() const override { return TEXT("CyberCafeMenuPointerExperimentProvider1721"); }
    virtual bool GetControllerOrientationAndPosition(int32 Id, FName Source, FRotator& Rotation,
        FVector& Position, float) const override
    {
        FScopeLock Lock(&Mutex);
        const FTransform* Pose = Id == ControllerId ? Poses.Find(Source) : nullptr;
        if (!Pose) return false;
        Rotation = Pose->Rotator(); Position = Pose->GetLocation(); return true;
    }
    virtual bool GetControllerOrientationAndPosition(int32 Id, FName Source, FRotator& Rotation,
        FVector& Position, bool& HasVelocity, FVector& Velocity, bool& HasAngular, FVector& Angular,
        bool& HasAcceleration, FVector& Acceleration, float Scale) const override
    {
        HasVelocity = HasAngular = HasAcceleration = false;
        Velocity = Angular = Acceleration = FVector::ZeroVector;
        return GetControllerOrientationAndPosition(Id, Source, Rotation, Position, Scale);
    }
    virtual bool GetControllerOrientationAndPositionForTime(int32 Id, FName Source, FTimespan,
        bool& UsedTime, FRotator& Rotation, FVector& Position, bool& HasVelocity, FVector& Velocity,
        bool& HasAngular, FVector& Angular, bool& HasAcceleration, FVector& Acceleration, float Scale) const override
    {
        UsedTime = false;
        return GetControllerOrientationAndPosition(Id, Source, Rotation, Position, HasVelocity,
            Velocity, HasAngular, Angular, HasAcceleration, Acceleration, Scale);
    }
    virtual ETrackingStatus GetControllerTrackingStatus(int32 Id, FName Source) const override
    {
        FScopeLock Lock(&Mutex);
        return Id == ControllerId && Poses.Contains(Source) ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;
    }
    virtual void EnumerateSources(TArray<FMotionControllerSource>& Out) const override
    {
        for (FName Source : Sources) Out.Emplace(Source);
    }
    virtual float GetCustomParameterValue(FName, FName, bool& Found) const override { Found = false; return 0.f; }
    virtual bool GetHandJointPosition(FName, int, FVector&) const override { return false; }
};

void Quiet(AActor* Actor)
{
    if (!IsValid(Actor)) return;
    Actor->SetActorTickEnabled(false);
    TInlineComponentArray<UActorComponent*> Components;
    Actor->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        Component->PrimaryComponentTick.bStartWithTickEnabled = false;
        Component->SetComponentTickEnabled(false);
        if (auto* Mesh = Cast<USkeletalMeshComponent>(Component))
        {
            Mesh->bPauseAnims = true;
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        if (auto* Primitive = Cast<UPrimitiveComponent>(Component))
        {
            Primitive->SetSimulatePhysics(false); Primitive->SetEnableGravity(false);
        }
        if (auto* FX = Cast<UNiagaraComponent>(Component))
        {
            FX->bAutoActivate = false; FX->DeactivateImmediate();
        }
    }
}

FTransform RayPose(const FVector& Start, const FVector& Direction)
{
    return FTransform(FRotationMatrix::MakeFromX(Direction).ToQuat(), Start);
}
FVector PixelPoint(const AOrderHologramActor* Screen, const FVector2D& Pixel)
{
    return Screen->Panel->GetComponentTransform().TransformPosition(FVector(0, 600 - Pixel.X, 375 - Pixel.Y));
}
FVector ActionPoint(const AOrderHologramActor* Screen, int32 Action)
{
    const FIntRect Rect = UOrderHologramWidget::GetScreenActionRect(Action);
    return PixelPoint(Screen, FVector2D((Rect.Min.X + Rect.Max.X) * .5, (Rect.Min.Y + Rect.Max.Y) * .5));
}
FTransform PanelPose(const AOrderHologramActor* Screen, const FVector& Point, double Distance = 80.)
{
    const FVector Normal = Screen->Panel->GetForwardVector();
    return RayPose(Point + Normal * Distance, -Normal);
}

const TArray<FEnhancedActionKeyMapping>* RuntimeMappings(const UEnhancedPlayerInput* Input)
{
    const FArrayProperty* Property = FindFProperty<FArrayProperty>(UEnhancedPlayerInput::StaticClass(), TEXT("EnhancedActionMappings"));
    const FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
    return Input && Inner && Inner->Struct == FEnhancedActionKeyMapping::StaticStruct()
        ? Property->ContainerPtrToValuePtr<TArray<FEnhancedActionKeyMapping>>(Input) : nullptr;
}

int32 BindingCount(const UEnhancedInputComponent* Input, const UInputAction* Action, ETriggerEvent Event)
{
    int32 Count = 0;
    for (const auto& Binding : Input->GetActionEventBindings())
        if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event) ++Count;
    return Count;
}

struct FFixture final : public FGCObject
{
    FTrackingProvider Tracking;
    UWorld* World = nullptr;
    UWorld* SavedMap = nullptr;
    UGameInstance* GI = nullptr;
    ULocalPlayer* LP = nullptr;
    APlayerController* PC = nullptr;
    AVRPawn* Pawn = nullptr;
    UEnhancedPlayerInput* PlayerInput = nullptr;
    UEnhancedInputComponent* Input = nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
    UMenuPointerComponent* Pointer = nullptr;
    AOrderHologramActor* SavedScreen = nullptr;
    AOrderHologramActor* Screen = nullptr;
    AOrderHologramActor* SkipProbe = nullptr;
    UCyberCafeEventSubsystem* Events = nullptr;
    UClass* PawnClass = nullptr;
    FKey Keys[2];
    FScriptDelegate SkipDelegate;
    FDelegateHandle MenuHandle, SkipHandle;
    bool bPlayerAdded = false;
    int32 MenuNotifications = 0, SkipNotifications = 0, InputBatches = 0, InputKeyCalls = 0;
    bool bNativeInjection = false;
    UInputAction* NativeObservers[2] = {nullptr, nullptr};

    explicit FFixture(bool bInNativeInjection = false) : bNativeInjection(bInNativeInjection) {}

    virtual FString GetReferencerName() const override { return TEXT("CyberCafeMenuPointerExperimentFixture"); }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddReferencedObject(World); Collector.AddReferencedObject(SavedMap);
        Collector.AddReferencedObject(GI); Collector.AddReferencedObject(LP); Collector.AddReferencedObject(PC);
        Collector.AddReferencedObject(Pawn); Collector.AddReferencedObject(PlayerInput); Collector.AddReferencedObject(Input);
        Collector.AddReferencedObject(Subsystem); Collector.AddReferencedObject(Pointer); Collector.AddReferencedObject(PawnClass);
        Collector.AddReferencedObject(SavedScreen); Collector.AddReferencedObject(Screen); Collector.AddReferencedObject(SkipProbe);
        Collector.AddReferencedObject(Events);
        Collector.AddReferencedObject(NativeObservers[0]); Collector.AddReferencedObject(NativeObservers[1]);
    }
    bool CheckInputContract(FAutomationTestBase& Test)
    {
        UInputAction* Skip = Pawn->IA_SkipDialog;
        if (!Test.TestNotNull(TEXT("原保存 Skip Action"), Skip)
            || !Test.TestNotNull(TEXT("原 Default IMC"), Pawn->DefaultMappingContext.Get())
            || !Test.TestNotNull(TEXT("原 Hands IMC"), Pawn->HandsMappingContext.Get())) return false;
        Test.TestEqual(TEXT("禁止用 Axis1D 替换 Boolean Action"), int32(Skip->ValueType), int32(EInputActionValueType::Boolean));
        Test.TestTrue(TEXT("原 consume_input 保持 true"), Skip->bConsumeInput);
        Test.TestEqual(TEXT("原 Action 只有一个 Trigger"), Skip->Triggers.Num(), 1);
        if (Skip->Triggers.Num() != 1) return false;
        const auto* Pressed = Cast<UInputTriggerPressed>(Skip->Triggers[0]);
        if (!Test.TestNotNull(TEXT("原 Trigger 必须是 Pressed"), Pressed)) return false;
        Test.TestEqual(TEXT("原 Pressed 阈值 .5，不为了测试改资产"), Pressed->ActuationThreshold, .5f);
        Test.TestEqual(TEXT("原 Hands 全部38条映射保留，不误把8条IndexCurl当总数"), Pawn->HandsMappingContext->GetMappings().Num(), 38);
        int32 IndexCurlMappings = 0;
        for (const auto& Mapping : Pawn->HandsMappingContext->GetMappings())
            if (Mapping.Action && Mapping.Action->GetName().StartsWith(TEXT("IA_Hand_IndexCurl_"))) ++IndexCurlMappings;
        Test.TestEqual(TEXT("原IndexCurl四设备双手8条保持"), IndexCurlMappings, 8);
        for (const auto& Mapping : Pawn->HandsMappingContext->GetMappings())
            Test.TestTrue(TEXT("Hands 不混入 Skip 或新增菜单 Action"), Mapping.Action && Mapping.Action != Skip
                && Mapping.Action->GetPathName().StartsWith(TEXT("/Game/CyberCafe/Player/Input/Actions/Hands/")));
        Test.TestEqual(TEXT("原 Skip Started 只绑定一次"), BindingCount(Input, Skip, ETriggerEvent::Started), 1);
        Test.TestEqual(TEXT("原 Skip 无新 Triggered 绑定"), BindingCount(Input, Skip, ETriggerEvent::Triggered), 0);
        for (const auto& Binding : Input->GetActionEventBindings())
            Test.TestTrue(TEXT("事件绑定目标均为原 Pawn，不附加测试观察绑定"), Binding->IsBoundToObject(Pawn));
        int32 Priority = INDEX_NONE;
        Test.TestTrue(TEXT("原 BeginPlay 挂 Default IMC，优先级零"), Subsystem->HasMappingContext(Pawn->DefaultMappingContext, Priority) && Priority == 0);
        Test.TestTrue(TEXT("原 BeginPlay 独立挂 Hands IMC，优先级零"), Subsystem->HasMappingContext(Pawn->HandsMappingContext, Priority) && Priority == 0);
        const auto* Mappings = RuntimeMappings(PlayerInput);
        if (!Test.TestNotNull(TEXT("只读反射实际编译后的映射数组"), Mappings)) return false;
        int32 SkipMappings = 0;
        for (const auto& Mapping : *Mappings)
        {
            if (Mapping.Action != Skip) continue;
            ++SkipMappings;
            const FString Name = Mapping.Key.GetFName().ToString();
            const int32 Hand = Name == TEXT("OculusTouch_Left_Trigger_Axis") ? 0
                : (Name == TEXT("OculusTouch_Right_Trigger_Axis") ? 1 : INDEX_NONE);
            if (!Test.TestTrue(TEXT("Skip 仅原左右 Oculus Trigger Axis"), Hand != INDEX_NONE)) return false;
            Keys[Hand] = Mapping.Key;
            Test.TestTrue(TEXT("原键注册为 gamepad 一维轴"), Mapping.Key.IsValid() && Mapping.Key.IsAxis1D() && Mapping.Key.IsGamepadKey());
            Test.TestFalse(TEXT("中性输入重建后映射未被忽略"), Mapping.bShouldBeIgnored);
            for (const auto& Modifier : Mapping.Modifiers)
                Test.AddInfo(TEXT("原运行期 Mapping Modifier：") + GetNameSafe(Modifier));
        }
        Test.TestEqual(TEXT("原共享 Skip 恰好两条运行期映射"), SkipMappings, 2);
        return !Test.HasAnyErrors() && Keys[0].IsValid() && Keys[1].IsValid();
    }
    bool Begin(FAutomationTestBase& Test)
    {
        if (!Test.TestTrue(TEXT("夹具仅在游戏线程运行"), IsInGameThread()) || !GEngine) return false;
        SavedMap = LoadObject<UWorld>(nullptr, MapPath);
        if (!Test.TestNotNull(TEXT("只读实际保存地图"), SavedMap)
            || !Test.TestNotNull(TEXT("实际保存 PersistentLevel"), SavedMap->PersistentLevel.Get())) return false;
        UClass* ModeClass = SavedMap->GetWorldSettings()->DefaultGameMode.Get();
        if (!ModeClass)
        {
            FString Path;
            if (!Test.TestTrue(TEXT("读取原默认 GameMode 配置"), GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),
                TEXT("GlobalDefaultGameMode"), Path, GEngineIni))) return false;
            ModeClass = LoadClass<AGameModeBase>(nullptr, *Path);
        }
        if (!Test.TestNotNull(TEXT("实际 GameMode 类"), ModeClass)) return false;
        PawnClass = ModeClass->GetDefaultObject<AGameModeBase>()->DefaultPawnClass.Get();
        if (!Test.TestNotNull(TEXT("实际 DefaultPawnClass"), PawnClass)
            || !Test.TestEqual(TEXT("必须使用保存 VRCharacter，不用伪 Pawn"), PawnClass->GetPathName(),
                FString(TEXT("/Game/CyberCafe/Player/Blueprints/VRCharacter.VRCharacter_C")))) return false;
        SavedScreen = FindObject<AOrderHologramActor>(SavedMap->PersistentLevel, TEXT("BP_OrderHologram_Tutorial"));
        if (!Test.TestNotNull(TEXT("保存教学菜单提供原数据及显示类"), SavedScreen)) return false;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("独立 GameWorld，不 BeginPlay 保存地图"), World)) return false;
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI; World->SetGameInstance(GI); Context.SetCurrentWorld(World); GI->Init();
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        PC = World->SpawnActor<APlayerController>(Params);
        LP = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
        if (!Test.TestNotNull(TEXT("原生 PlayerController"), PC) || !Test.TestNotNull(TEXT("本地玩家"), LP)) return false;
        PC->SetActorTickEnabled(false); PC->SetPlayer(LP);
        LP->PlayerAdded(nullptr, 0); bPlayerAdded = true;
        PlayerInput = Cast<UEnhancedPlayerInput>(PC->PlayerInput);
        // 合法的夹具 PlayerInput 类配置，不替换 Action、IMC 或生产 CDO。
        if (!PlayerInput) { PlayerInput = NewObject<UEnhancedPlayerInput>(PC); PC->PlayerInput = PlayerInput; }
        Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
        if (!Test.TestNotNull(TEXT("真实 EnhancedInput 本地子系统"), Subsystem)
            || !Test.TestTrue(TEXT("子系统读取同一个 PC.PlayerInput"), Subsystem->GetPlayerInput() == PlayerInput)) return false;
        const FTransform PawnTransform(FVector(0, 0, 1000));
        Pawn = World->SpawnActorDeferred<AVRPawn>(PawnClass, PawnTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Test.TestNotNull(TEXT("实际 Pawn 实例"), Pawn)) return false;
        Quiet(Pawn); Pawn->FinishSpawning(PawnTransform); Quiet(Pawn); PC->Possess(Pawn);
        UMotionControllerComponent* Controllers[] = {Pawn->MotionControllerLeftGrip.Get(), Pawn->MotionControllerLeftAim.Get(),
            Pawn->MotionControllerRightGrip.Get(), Pawn->MotionControllerRightAim.Get()};
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (!Test.TestNotNull(TEXT("原 Grip/Aim 组件"), Controllers[Index])) return false;
            if (!Test.TestEqual(TEXT("MotionSource 保留原名"), Controllers[Index]->MotionSource, Sources[Index])) return false;
            Controllers[Index]->SetAssociatedPlayerIndex(ControllerId);
        }
        Pointer = Pawn->MenuPointerComp.Get();
        Input = NewObject<UEnhancedInputComponent>(Pawn, NAME_None, RF_Transient);
        Pawn->AddInstanceComponent(Input); Input->RegisterComponent(); Pawn->SetupPlayerInputComponent(Input);
        Pawn->InputComponent = Input; PC->PushInputComponent(Input);
        const FTransform ScreenTransform(FVector(1000, 0, 1400));
        Screen = World->SpawnActorDeferred<AOrderHologramActor>(SavedScreen->GetClass(), ScreenTransform,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Test.TestNotNull(TEXT("新鲜菜单实例，每例独立同帧锁"), Screen)) return false;
        Screen->RecipeMenu = SavedScreen->RecipeMenu; Screen->DisplayWidgetClass = SavedScreen->DisplayWidgetClass;
        Screen->PanelWidthCM = SavedScreen->PanelWidthCM; Screen->PanelCenter = SavedScreen->PanelCenter;
        Screen->PanelRotation = SavedScreen->PanelRotation; Screen->AnimationSeconds = SavedScreen->AnimationSeconds;
        Screen->PanelMaterial = SavedScreen->PanelMaterial; Screen->BeamMaterial = SavedScreen->BeamMaterial;
        Screen->ShellMaterial = SavedScreen->ShellMaterial; Screen->SignalMaterial = SavedScreen->SignalMaterial;
        Screen->bOpenOnRecipePage = SavedScreen->bOpenOnRecipePage; Screen->bFaceHeadset = false;
        Quiet(Screen); Screen->FinishSpawning(ScreenTransform); Quiet(Screen);
        Quiet(Pawn); World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
        Quiet(Pawn); Quiet(Screen); PC->SetActorTickEnabled(false);
        Events = GI->GetSubsystem<UCyberCafeEventSubsystem>();
        const FWorldContext* ActualContext = GEngine->GetWorldContextFromWorld(World);
        if (!Test.TestNotNull(TEXT("实验菜单组件"), Pointer)
            || !Test.TestNotNull(TEXT("真实菜单 UMG"), Screen->GetDisplayWidget())
            || !Test.TestNotNull(TEXT("原菜单数据"), Screen->RecipeMenu.Get())
            || !Test.TestTrue(TEXT("原菜单数据有效"), Screen->RecipeMenu->IsValidMenu())
            || !Test.TestNotNull(TEXT("原 Skip 总线"), Events)) return false;
        Test.TestTrue(TEXT("本地 standalone 权限及原生 BeginPlay"), Pawn->IsLocallyControlled() && PC->IsLocalController()
            && World->GetNetMode() == NM_Standalone && Pawn->HasActorBegunPlay());
        Test.TestTrue(TEXT("GI 与 WorldContext 真实一致"), ActualContext && GI->GetWorldContext() == ActualContext
            && GI->GetWorld() == World && LP->GetPlayerController(World) == PC);
        Test.TestFalse(TEXT("保存地图未启动"), SavedMap->HasBegunPlay());
        Test.TestFalse(TEXT("菜单组件没有第二条 Tick 链"), Pointer->PrimaryComponentTick.bCanEverTick);
        FModifyContextOptions Options; Options.bForceImmediately = true;
        Subsystem->RequestRebuildControlMappings(Options, EInputMappingRebuildType::Rebuild);
        if (!CheckInputContract(Test) || !AttachSkipProbe(Test)) return false;
        if (bNativeInjection)
        {
            NativeObservers[0] = Pointer->LeftTriggerObserver.Get(); NativeObservers[1] = Pointer->RightTriggerObserver.Get();
            if (!CheckObserverContract(Test)) return false;
            return StepNative(Test, 0.f, 0.f);
        }
        return Step(Test, 0.f, 0.f);
    }
    bool AttachSkipProbe(FAutomationTestBase& Test)
    {
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        SkipProbe = World->SpawnActor<AOrderHologramActor>(AOrderHologramActor::StaticClass(), FVector(-3000, 0, 0), FRotator::ZeroRotator, Params);
        if (!Test.TestNotNull(TEXT("复用既存无参 UFUNCTION 观察真实 Skip 总线"), SkipProbe)) return false;
        Quiet(SkipProbe); SkipProbe->RecipeMenu = SavedScreen->RecipeMenu;
        SkipProbe->PressButton(); SkipProbe->Tick(1.f);
        const auto* Widget = SkipProbe->GetDisplayWidget();
        if (!Test.TestNotNull(TEXT("总线观察器控件树"), Widget ? Widget->WidgetTree.Get() : nullptr)) return false;
        TArray<UWidget*> Controls; Widget->WidgetTree->GetAllWidgets(Controls);
        URecipeScreenButton* Adapter = nullptr;
        for (UWidget* Control : Controls)
            if (auto* Button = Cast<URecipeScreenButton>(Control); Button && Button->Action == 1) { Adapter = Button; break; }
        if (!Test.TestNotNull(TEXT("既存 HandleClick 适配器"), Adapter)) return false;
        SkipHandle = SkipProbe->OnMenuInteractionNative.AddLambda([this](AOrderHologramActor*) { ++SkipNotifications; });
        SkipDelegate.BindUFunction(Adapter, TEXT("HandleClick"));
        if (!Test.TestTrue(TEXT("总线接收器绑定成功，不主动广播"), SkipDelegate.IsBound())) return false;
        Events->OnPlayerSkipRequested.Add(SkipDelegate);
        MenuHandle = Screen->OnMenuInteractionNative.AddLambda([this](AOrderHologramActor* Source)
            { if (Source == Screen) ++MenuNotifications; });
        return true;
    }
    bool Open(FAutomationTestBase& Test)
    {
        if (!Screen->IsPanelOpen()) Screen->PressButton();
        Screen->Tick(1.f);
        Screen->Panel->TickComponent(Dt, LEVELTICK_All, nullptr);
        Test.AddInfo(FString::Printf(TEXT("真实面板 IsVisible=%d IsWidgetVisible=%d hidden=%d widgetVisible=%d"),
            Screen->Panel->IsVisible(), Screen->Panel->IsWidgetVisible(), Screen->Panel->bHiddenInGame,
            Screen->GetDisplayWidget() && Screen->GetDisplayWidget()->IsVisible()));
        return Test.TestTrue(TEXT("原电源接口及动画展开当前菜单"), Screen->IsPanelOpen())
            && Test.TestTrue(TEXT("原 Panel 为无碰撞全平面"), Screen->Panel->GetCollisionEnabled() == ECollisionEnabled::NoCollision
                && Screen->Panel->GetGeometryMode() == EWidgetGeometryMode::Plane && Screen->Panel->GetWidgetSpace() == EWidgetSpace::World)
            && Test.TestTrue(TEXT("当前几何真实 1200×750，未反射伪造尺寸"), Screen->Panel->GetCurrentDrawSize() == FIntPoint(1200, 750));
    }
    void Pose(EControllerHand Hand, const FTransform& Transform, bool bGripTracked = true, bool bAimTracked = true)
    {
        UMotionControllerComponent* Grip = Hand == EControllerHand::Left ? Pawn->MotionControllerLeftGrip.Get() : Pawn->MotionControllerRightGrip.Get();
        UMotionControllerComponent* Aim = Hand == EControllerHand::Left ? Pawn->MotionControllerLeftAim.Get() : Pawn->MotionControllerRightAim.Get();
        for (UMotionControllerComponent* MC : {Grip, Aim})
        {
            Tracking.Set(MC->MotionSource, Transform.GetRelativeTransform(MC->GetAttachParent()->GetComponentTransform()),
                MC == Grip ? bGripTracked : bAimTracked);
            MC->Activate(); MC->TickComponent(Dt, LEVELTICK_All, nullptr);
        }
    }
    bool AimAction(FAutomationTestBase& Test, EControllerHand Hand, int32 Action = 1)
    {
        const FVector Point = ActionPoint(Screen, Action);
        const FTransform Transform = PanelPose(Screen, Point);
        Pose(Hand, Transform);
        const UMotionControllerComponent* Aim = Hand == EControllerHand::Left
            ? Pawn->MotionControllerLeftAim.Get() : Pawn->MotionControllerRightAim.Get();
        if (!Test.TestTrue(TEXT("原 Aim 真实追踪，不以解析射线冒充追踪"), Aim->IsTracked())
            || !Test.TestTrue(TEXT("真实采样后的 Aim 世界变换与提供器一致"), Aim->GetComponentTransform().Equals(Transform, .001))) return false;
        FMenuPointerHit Hit;
        return Test.TestTrue(TEXT("当前无碰撞面解析命中"), Screen->TraceMenuPointer(Transform.GetLocation(), Transform.GetRotation().GetForwardVector(), 250., Hit))
            && Test.TestTrue(TEXT("当前真实 UI，不用物理面或电源冒充"), Hit.bPanel && !Hit.bPower && Hit.Action == Action)
            && Test.TestEqual(TEXT("当前点仍属于同一个 UI action"), Screen->GetPointerActionAtWorldPoint(Hit.Point), Action);
    }
    bool Step(FAutomationTestBase& Test, float Left, float Right)
    {
        // Dedicated fixture device id; no platform mapper state or extra runtime module is changed.
        const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(0);
        const float Values[] = {Left, Right};
        for (int32 Hand = 0; Hand < 2; ++Hand)
        {
            // UE 5.6 实际新 API；旧 FInputKeyParams 在该版本已弃用，不能直接传给派生类重载。
            const FInputKeyEventArgs Args(nullptr, Device, Keys[Hand], Values[Hand], Dt, 1, FPlatformTime::Cycles64());
            // UPlayerInput 对 analog 正常累积样本后返回 false；是否进入键状态由下方 ProcessInputStack 后读数证明。
            PlayerInput->InputKey(Args);
            ++InputKeyCalls;
        }
        const TArray<UInputComponent*> Stack = {Input};
        PlayerInput->ProcessInputStack(Stack, Dt, false);
        ++InputBatches;
        Test.TestTrue(TEXT("ProcessInputStack 后 PC 左 raw 来自真实键状态"), FMath::IsNearlyEqual(PC->GetInputAnalogKeyState(Keys[0]), Left, .000001f));
        Test.TestTrue(TEXT("ProcessInputStack 后 PC 右 raw 来自真实键状态"), FMath::IsNearlyEqual(PC->GetInputAnalogKeyState(Keys[1]), Right, .000001f));
        const FInputActionInstance* Instance = PlayerInput->FindActionInstanceData(Pawn->IA_SkipDialog);
        Test.AddInfo(FString::Printf(TEXT("输入批次=%d 引擎帧=%llu rawL=%.4f rawR=%.4f event=%d actionMagnitude=%.4f bool=%d attempts=%d accepted=%d menu=%d Skip=%d；%s"),
            InputBatches, GFrameCounter, Left, Right, Instance ? int32(Instance->GetTriggerEvent()) : 0,
            Instance ? Instance->GetValue().GetMagnitude() : 0.f, Instance && Instance->GetValue().Get<bool>(),
            Pointer->GetOriginalTriggerAttemptCount(), Pointer->GetAcceptedClickCount(), MenuNotifications, SkipNotifications,
            *Pointer->GetLastClickStatus()));
        return true;
    }
    bool CheckObserverContract(FAutomationTestBase& Test) const
    {
        const AVRPawn* CDO = PawnClass->GetDefaultObject<AVRPawn>();
        if (!Test.TestNotNull(TEXT("实际 VRCharacter CDO 的原 MenuPointer"), CDO->MenuPointerComp.Get())) return false;
        const UInputAction* Observers[] = {Pointer->LeftTriggerObserver.Get(), Pointer->RightTriggerObserver.Get()};
        const UInputAction* SavedObservers[] = {CDO->MenuPointerComp->LeftTriggerObserver.Get(), CDO->MenuPointerComp->RightTriggerObserver.Get()};
        const auto& Mappings = Pawn->DefaultMappingContext->GetMappings();
        if (!Test.TestEqual(TEXT("Default 原37条后只追加两条观察映射"), Mappings.Num(), 39)) return false;
        for (int32 Hand = 0; Hand < 2; ++Hand)
        {
            if (!Test.TestNotNull(TEXT("已保存观察 Action 硬引用，不创建替身"), Observers[Hand])) return false;
            Test.TestTrue(TEXT("运行实例继承实际 VRCharacter 组件 CDO 硬引用"), Observers[Hand] == SavedObservers[Hand]);
            Test.TestTrue(TEXT("观察 Action 属于原输入资产目录"), Observers[Hand]->GetPathName().StartsWith(TEXT("/Game/CyberCafe/Player/Input/Actions/")));
            Test.TestTrue(TEXT("独立 Axis1D 非消费、空 Triggers/Modifiers"), Observers[Hand] != Pawn->IA_SkipDialog
                && Observers[Hand]->ValueType == EInputActionValueType::Axis1D && !Observers[Hand]->bConsumeInput
                && Observers[Hand]->Triggers.IsEmpty() && Observers[Hand]->Modifiers.IsEmpty());
            Test.TestEqual(TEXT("观察轴只取最高绝对值，不累计共享源"), int32(Observers[Hand]->AccumulationBehavior),
                int32(EInputActionAccumulationBehavior::TakeHighestAbsoluteValue));
            const auto& Mapping = Mappings[37 + Hand];
            Test.TestTrue(TEXT("追加观察键与原35/36 Skip 键严格一致"), Mapping.Action == Observers[Hand]
                && Mapping.Key == Keys[Hand] && Mapping.Key == Mappings[35 + Hand].Key
                && Mappings[35 + Hand].Action == Pawn->IA_SkipDialog && Mapping.Triggers.IsEmpty() && Mapping.Modifiers.IsEmpty());
            Test.TestEqual(TEXT("观察 Action 不绑定任何事件"), BindingCount(Input, Observers[Hand], ETriggerEvent::Started), 0);
            for (const auto& Binding : Input->GetActionEventBindings())
                Test.TestTrue(TEXT("不为观察 Action 增加任何事件链"), Binding->GetAction() != Observers[Hand]);
        }
        Test.TestTrue(TEXT("左右观察 Action 为两个独立原资产"), Observers[0] != Observers[1]);
        Test.TestEqual(TEXT("保留全部19个原生绑定"), Input->GetActionEventBindings().Num(), 19);
        return !Test.HasAnyErrors();
    }
    // 仅模拟 XR 每帧共享 Boolean 注入；这不是实际设备的 Boolean 原生触发阈值证明。
    static bool OriginalSourceBoolean(float Left, float Right) { return Left >= .5f || Right >= .5f; }
    bool StepNative(FAutomationTestBase& Test, float Left, float Right, TOptional<bool> OriginalBoolean = {})
    {
        if (!Test.TestNotNull(TEXT("左原观察资产"), NativeObservers[0])
            || !Test.TestNotNull(TEXT("右原观察资产"), NativeObservers[1])) return false;
        PlayerInput->InjectInputForAction(Pawn->IA_SkipDialog,
            FInputActionValue(OriginalBoolean.IsSet() ? OriginalBoolean.GetValue() : OriginalSourceBoolean(Left, Right)));
        PlayerInput->InjectInputForAction(NativeObservers[0], FInputActionValue(Left));
        PlayerInput->InjectInputForAction(NativeObservers[1], FInputActionValue(Right));
        const TArray<UInputComponent*> Stack = {Input};
        PlayerInput->ProcessInputStack(Stack, Dt, false);
        ++InputBatches;
        Test.TestEqual(TEXT("原生注入夹具从启动至今没有调用 InputKey"), InputKeyCalls, 0);
        Test.TestEqual(TEXT("PC 左物理键状态保持真实零"), PC->GetInputAnalogKeyState(Keys[0]), 0.f);
        Test.TestEqual(TEXT("PC 右物理键状态保持真实零"), PC->GetInputAnalogKeyState(Keys[1]), 0.f);
        const float Values[] = {Left, Right};
        for (int32 Hand = 0; Hand < 2; ++Hand)
        {
            const auto* Instance = PlayerInput->FindActionInstanceData(NativeObservers[Hand]);
            if (!Test.TestNotNull(TEXT("无绑定观察 Action 仍有引擎实例"), Instance)) return false;
            Test.TestTrue(TEXT("实例来源为实际原观察资产"), Instance->GetSourceAction() == NativeObservers[Hand]);
            Test.TestEqual(TEXT("真实 GetValue 是 Axis1D"), int32(Instance->GetValue().GetValueType()), int32(EInputActionValueType::Axis1D));
            Test.TestTrue(TEXT("真实观察 float 保留行程且未施加 Boolean DeadZone"), FMath::IsNearlyEqual(Instance->GetValue().Get<float>(), Values[Hand], .000001f));
        }
        Test.AddInfo(FString::Printf(TEXT("Native批次=%d 真实帧=%llu PCraw=(0,0) axes=(%.4f,%.4f)；%s"),
            InputBatches, GFrameCounter, Left, Right, *Pointer->GetLastClickStatus()));
        return !Test.HasAnyErrors();
    }
    void Counts(FAutomationTestBase& Test, int32 Attempts, int32 Accepted, int32 Menu, int32 Skip) const
    {
        Test.TestEqual(TEXT("原 Started 回调实际进入次数"), Pointer->GetOriginalTriggerAttemptCount(), Attempts);
        Test.TestEqual(TEXT("组件实际接受次数"), Pointer->GetAcceptedClickCount(), Accepted);
        Test.TestEqual(TEXT("被测菜单实际通知次数"), MenuNotifications, Menu);
        Test.TestEqual(TEXT("原事件总线实际通知次数"), SkipNotifications, Skip);
    }
    UStaticMeshComponent* Cube(const FTransform& Transform, bool bCollision = true)
    {
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        AActor* Actor = World->SpawnActor<AActor>(Params);
        if (!Actor) return nullptr;
        auto* Mesh = NewObject<UStaticMeshComponent>(Actor, NAME_None, RF_Transient);
        Actor->AddInstanceComponent(Mesh); Actor->SetRootComponent(Mesh); Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        if (!Mesh->GetStaticMesh() || !Mesh->GetStaticMesh()->GetBodySetup()) return nullptr;
        Mesh->GetStaticMesh()->GetBodySetup()->CreatePhysicsMeshes();
        Mesh->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
        Mesh->SetCollisionObjectType(ECC_WorldDynamic); Mesh->SetCollisionResponseToAllChannels(ECR_Block);
        Mesh->SetGenerateOverlapEvents(false); Mesh->SetSimulatePhysics(false); Mesh->SetEnableGravity(false);
        Mesh->RegisterComponent(); Mesh->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
        return Mesh;
    }
    void NoOrderMutation(FAutomationTestBase& Test) const
    {
        Test.TestNull(TEXT("指针不建立订单"), Screen->GetCurrentRecipe());
        Test.TestEqual(TEXT("指针不改订单序号"), Screen->GetCurrentOrderSerial(), int64(0));
        Test.TestEqual(TEXT("指针不造交付历史"), Screen->GetDeliveryRecords().Num(), 0);
        Test.TestFalse(TEXT("指针不进入待提交"), Screen->IsPending());
    }
    ~FFixture()
    {
        if (!World) return;
        if (IsValid(Screen)) Screen->OnMenuInteractionNative.Remove(MenuHandle);
        if (IsValid(SkipProbe)) SkipProbe->OnMenuInteractionNative.Remove(SkipHandle);
        if (IsValid(Events) && SkipDelegate.IsBound()) Events->OnPlayerSkipRequested.Remove(SkipDelegate);
        for (TActorIterator<AActor> It(World); It; ++It) Quiet(*It);
        if (IsValid(Subsystem) && IsValid(Pawn))
        {
            Subsystem->RemoveMappingContext(Pawn->DefaultMappingContext);
            Subsystem->RemoveMappingContext(Pawn->HandsMappingContext);
        }
        if (IsValid(PC)) { if (Input) PC->PopInputComponent(Input); PC->UnPossess(); PC->Destroy(); }
        if (LP) { LP->PlayerController = nullptr; if (bPlayerAdded) LP->PlayerRemoved(); }
        if (IsValid(Pawn)) Pawn->Destroy();
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        FlushRenderingCommands();
    }
};

// 每次 Update 最多处理一个真实引擎帧；不调用 GEngine::Tick，也不写任何全局帧计数。
class FEngineFrames final : public IAutomationLatentCommand
{
    FAutomationTestBase& Test;
    TSharedPtr<FFixture> Fixture;
    TArray<FVector2D> Samples;
    TFunction<void(FFixture&, int32)> Check;
    int32 Index = 0;
    uint64 LastFrame = GFrameCounter;
public:
    FEngineFrames(FAutomationTestBase& InTest, TSharedPtr<FFixture> InFixture,
        TArray<FVector2D> InSamples, TFunction<void(FFixture&, int32)> InCheck)
        : Test(InTest), Fixture(MoveTemp(InFixture)), Samples(MoveTemp(InSamples)), Check(MoveTemp(InCheck)) {}
    virtual bool Update() override
    {
        if (Index >= Samples.Num()) return true;
        if (GFrameCounter == LastFrame) return false;
        LastFrame = GFrameCounter;
        if (!Fixture->Step(Test, Samples[Index].X, Samples[Index].Y)) return true;
        Check(*Fixture, Index);
        ++Index;
        return Index == Samples.Num();
    }
};

bool RampedUI(FAutomationTestBase& Test, EControllerHand Hand)
{
    auto F = MakeShared<FFixture>();
    if (!F->Begin(Test) || !F->Open(Test) || !F->AimAction(Test, Hand)) return false;
    const EControllerHand Other = Hand == EControllerHand::Left ? EControllerHand::Right : EControllerHand::Left;
    F->Pose(Other, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    F->Pointer->UpdatePointer(Dt);
    TArray<FVector2D> Samples;
    for (float Value : {0.f, .02f, .15f, .35f, .55f, .8f, .8f, 0.f})
        Samples.Add(Hand == EControllerHand::Left ? FVector2D(Value, 0) : FVector2D(0, Value));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEngineFrames>(Test, F, MoveTemp(Samples),
        [&Test, Hand](FFixture& State, int32 Index)
        {
            // 样本索引不受 Begin 的中性输入批次影响；只有 .55 首次跨过原 .5 阈值。
            if (Index < 4) State.Counts(Test, 0, 0, 0, 0);
            else State.Counts(Test, 1, 1, 1, 0);
            if (Index == 4)
            {
                Test.TestEqual(TEXT("索引4的 .55 首次原 Started 接受当前 UI"), State.Pointer->GetAcceptedClickCount(), 1);
                const FString Status = State.Pointer->GetLastClickStatus();
                Test.TestTrue(TEXT("真实点击诊断为 UI accepted"), Status.Contains(TEXT("result=accepted reason=UI accepted")));
                Test.TestTrue(TEXT("诊断选择实际扣扳机的手"), Status.Contains(
                    Hand == EControllerHand::Left ? TEXT("triggerHand=L") : TEXT("triggerHand=R")));
            }
            State.Pointer->UpdatePointer(Dt);
            State.NoOrderMutation(Test);
        }));
    return true;
}

struct FNativeSample
{
    float Left, Right;
    TOptional<bool> OriginalBoolean;
    FNativeSample(float L, float R, TOptional<bool> Original = {}) : Left(L), Right(R), OriginalBoolean(Original) {}
};

class FNativeEngineFrames final : public IAutomationLatentCommand
{
    FAutomationTestBase& Test;
    TSharedPtr<FFixture> Fixture;
    TArray<FNativeSample> Samples;
    TFunction<void(FFixture&, int32)> Check;
    int32 Index = 0;
    uint64 LastFrame = GFrameCounter;
public:
    FNativeEngineFrames(FAutomationTestBase& InTest, TSharedPtr<FFixture> InFixture,
        TArray<FNativeSample> InSamples, TFunction<void(FFixture&, int32)> InCheck)
        : Test(InTest), Fixture(MoveTemp(InFixture)), Samples(MoveTemp(InSamples)), Check(MoveTemp(InCheck)) {}
    virtual bool Update() override
    {
        if (Index >= Samples.Num()) return true;
        if (GFrameCounter == LastFrame) return false;
        LastFrame = GFrameCounter;
        const FNativeSample& Sample = Samples[Index];
        if (!Fixture->StepNative(Test, Sample.Left, Sample.Right, Sample.OriginalBoolean)) return true;
        Check(*Fixture, Index++);
        return Test.HasAnyErrors() || Index == Samples.Num();
    }
};

bool NativeSingleUI(FAutomationTestBase& Test, EControllerHand Hand)
{
    auto F = MakeShared<FFixture>(true);
    if (!F->Begin(Test) || !F->Open(Test) || !F->AimAction(Test, Hand)) return false;
    const auto Other = Hand == EControllerHand::Left ? EControllerHand::Right : EControllerHand::Left;
    F->Pose(Other, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    F->Pointer->UpdatePointer(Dt);
    Test.TestTrue(TEXT("实际视觉先悬停 UI，但无输入点击"), F->Pointer->GetPointerStatus(Hand).Contains(TEXT("UIaction1")));
    F->Counts(Test, 0, 0, 0, 0);
    const FNativeSample Press(Hand == EControllerHand::Left ? .8f : 0.f, Hand == EControllerHand::Right ? .8f : 0.f);
    TArray<FNativeSample> Samples = {{0, 0}, Press, Press, {0, 0}, {0, 0}, {0, 0}, Press, Press, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FNativeEngineFrames>(Test, F, MoveTemp(Samples),
        [&Test, Hand](FFixture& State, int32 Index)
        {
            const int32 Expected = Index == 0 ? 0 : (Index < 6 ? 1 : 2);
            State.Counts(Test, Expected, Expected, Expected, 0);
            if (Index == 1 || Index == 6)
            {
                const FString Status = State.Pointer->GetLastClickStatus();
                Test.TestTrue(TEXT("原 Started 在 PCraw 为零时使用真实观察轴接受 UI"), Status.Contains(TEXT("ValueSource=NativeAction"))
                    && Status.Contains(TEXT("result=accepted reason=UI accepted")) && Status.Contains(TEXT("ObserverPairValid=1")));
                Test.TestTrue(TEXT("选手仅来自实际越阈观察轴"), Status.Contains(Hand == EControllerHand::Left ? TEXT("triggerHand=L") : TEXT("triggerHand=R")));
                Test.TestTrue(TEXT("记录原回调真实 Boolean 幅值，不伪造为零"), Status.Contains(TEXT("OriginalActionValueNative=1.0000")));
            }
            State.Pointer->UpdatePointer(Dt); State.NoOrderMutation(Test);
        }));
    return true;
}

bool PawnFunctionSource(const FString& Source, const FString& Function, FString& Out)
{
    const FRegexPattern Signature(TEXT("(?m)^[^\\r\\n;{}]*\\bAVRPawn::") + Function + TEXT("\\([^;{}]*\\)[^;{}]*\\{"));
    FRegexMatcher Match(Signature, Source);
    if (!Match.FindNext()) return false;
    const int32 Start = Match.GetMatchBeginning();
    int32 Depth = 1;
    TCHAR Quote = 0;
    bool bLineComment = false, bBlockComment = false;
    for (int32 Index = Match.GetMatchEnding(); Index < Source.Len(); ++Index)
    {
        const TCHAR C = Source[Index], Next = Index + 1 < Source.Len() ? Source[Index + 1] : 0;
        if (bLineComment) { if (C == '\n') bLineComment = false; continue; }
        if (bBlockComment) { if (C == '*' && Next == '/') { bBlockComment = false; ++Index; } continue; }
        if (Quote) { if (C == '\\') ++Index; else if (C == Quote) Quote = 0; continue; }
        if (C == '/' && Next == '/') { bLineComment = true; ++Index; continue; }
        if (C == '/' && Next == '*') { bBlockComment = true; ++Index; continue; }
        if (C == '\"' || C == '\'') { Quote = C; continue; }
        if (C == '{') ++Depth;
        if (C == '}' && --Depth == 0)
        {
            Out = Source.Mid(Start, Index + 1 - Start);
            return !Match.FindNext(); // 当前 Pawn 无重载；重复定义不能静默漏比较。
        }
    }
    return false;
}

bool CheckOriginalDefaultRows(FAutomationTestBase& Test, const UInputMappingContext* Context)
{
    // 主流程必须在追加资产前导出不可变 JSON，rows 为原37个 FEnhancedActionKeyMapping 的完整 ExportText 字符串。
    // 不能 LoadObject 冻结目录的同名 package：它会错误复用当前已修改资产。
    FString Path, Json;
    if (!Test.TestTrue(TEXT("必须提供预追加完整映射 JSON：-MenuPointerOriginalMappings=<绝对路径>"),
        FParse::Value(FCommandLine::Get(), TEXT("MenuPointerOriginalMappings="), Path))
        || !Test.TestTrue(TEXT("只读主流程的不可变映射快照"), FFileHelper::LoadFileToString(Json, *Path))) return false;
    TSharedPtr<FJsonObject> Root;
    if (!Test.TestTrue(TEXT("映射快照 JSON 可解析"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid())) return false;
    FString OriginalHash;
    if (!Test.TestTrue(TEXT("映射快照记录预追加原 IMC_Default SHA256"), Root->TryGetStringField(TEXT("original_sha256"), OriginalHash))
        || !Test.TestEqual(TEXT("映射快照必须来自已核验 Shipping 的原37行资产"), OriginalHash,
            FString(TEXT("b580c3d047cc5f9578ebbf7205a42071afc773be628aea3fd6869a746af054a7")))) return false;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Test.TestTrue(TEXT("快照具有完整 rows 数组"), Root->TryGetArrayField(TEXT("rows"), Rows))
        || !Test.TestEqual(TEXT("预追加快照恰好37行"), Rows->Num(), 37)) return false;
    const auto& Mappings = Context->GetMappings();
    if (!Test.TestEqual(TEXT("当前仅原37行+左右观察行"), Mappings.Num(), 39)) return false;
    for (int32 Index = 0; Index < 37; ++Index)
    {
        FString Before, Current;
        if (!Test.TestTrue(TEXT("快照行必须为全属性字符串"), (*Rows)[Index]->TryGetString(Before))) return false;
        FEnhancedActionKeyMapping::StaticStruct()->ExportText(Current, &Mappings[Index], nullptr,
            const_cast<UInputMappingContext*>(Context), PPF_None, nullptr, false);
        Test.TestEqual(FString::Printf(TEXT("原映射[%d]全属性及顺序保留"), Index), Current, Before);
    }
    return !Test.HasAnyErrors();
}

void CheckTint(FAutomationTestBase& Test, FFixture& F, EControllerHand Hand, const FLinearColor& Expected)
{
    auto* Beam = F.Pointer->GetBeam(Hand);
    if (!Test.TestNotNull(TEXT("实际光束几何"), Beam)) return;
    auto* MID = Cast<UMaterialInstanceDynamic>(Beam->GetMaterial(0));
    if (!Test.TestNotNull(TEXT("实际光束 MID，包括基础材质回退"), MID)) return;
    FLinearColor Tint;
    if (MID->GetVectorParameterValue(FMaterialParameterInfo(F.Pointer->TintParameterName), Tint))
        Test.TestTrue(TEXT("当前材质真实 Tint"), Tint.Equals(Expected, .0001f));
    else if (!F.Pointer->RayMaterial.IsValid())
        Test.AddInfo(TEXT("专用材质未加载且基础材质无 Tint：仅验真实可见几何，不把缺省材质当输入故障。"));
    else Test.AddError(TEXT("已加载专用材质却没有约定 Tint 参数。"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerSourceInputBindingsUnchanged,
    "CyberCafe.MenuPointerExperiment.SourceInputBindingsUnchanged", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerSourceInputBindingsUnchanged::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    FFixture F; if (!F.Begin(*this)) return false;
    // 已核验 Shipping 冻结快照含原指针接点；本轮只授权 Default 追加观察轴、VRCharacter 保存观察属性。
    const FString OriginalRoot(TEXT("C:/Users/cloudcheng/WorkBuddy/2026-11-01-16-35-18/dialogue_release_20261110/shipping/project"));
    for (const TCHAR* Relative : {TEXT("Content/VRTemplate/Input/IMC_Hands.uasset"),
        TEXT("Content/VRTemplate/Input/Actions/UI/IA_SkipDialog.uasset"), TEXT("Config/DefaultInput.ini")})
    {
        TArray<uint8> Original, Experiment;
        if (!TestTrue(TEXT("只读原文件：") + FString(Relative), FFileHelper::LoadFileToArray(Original, *FPaths::Combine(OriginalRoot, Relative)))
            || !TestTrue(TEXT("只读实验文件：") + FString(Relative), FFileHelper::LoadFileToArray(Experiment, *FPaths::Combine(FPaths::ProjectDir(), Relative)))) return false;
        TestTrue(TEXT("原输入/角色资产字节未改：") + FString(Relative), Original == Experiment);
    }
    FString Original, Current;
    const FString Relative(TEXT("Source/CyberCafe/Character/VRPawn.cpp"));
    if (!TestTrue(TEXT("读取 J 原 Pawn 源码"), FFileHelper::LoadFileToString(Original, *FPaths::Combine(OriginalRoot, Relative)))
        || !TestTrue(TEXT("读取实验 Pawn 源码"), FFileHelper::LoadFileToString(Current, *FPaths::Combine(FPaths::ProjectDir(), Relative)))) return false;
    // 只允许主流程改 TraceAim/GetGrabComponentUnderAim；Setup、OnGrab、TryGrab、近抓和拉拽等完整函数逐字符比较。
    const FRegexPattern Names(TEXT("(?m)^[^\\r\\n;{}]*\\bAVRPawn::([A-Za-z_][A-Za-z_0-9]*)\\("));
    FRegexMatcher OriginalNames(Names, Original), CurrentNames(Names, Current);
    TArray<FString> BeforeNames, AfterNames;
    while (OriginalNames.FindNext()) BeforeNames.Add(OriginalNames.GetCaptureGroup(1));
    while (CurrentNames.FindNext()) AfterNames.Add(CurrentNames.GetCaptureGroup(1));
    TestTrue(TEXT("Pawn 函数清单和顺序保持，不抹掉全部生产代码"), BeforeNames == AfterNames && !BeforeNames.IsEmpty());
    for (const FString& Name : BeforeNames)
    {
        if (Name == TEXT("TraceAim") || Name == TEXT("GetGrabComponentUnderAim")) continue;
        FString BeforeFunction, AfterFunction;
        if (!TestTrue(TEXT("读取冻结完整函数：") + Name, PawnFunctionSource(Original, Name, BeforeFunction))
            || !TestTrue(TEXT("读取当前完整函数：") + Name, PawnFunctionSource(Current, Name, AfterFunction))) return false;
        TestEqual(TEXT("原函数完整内容保持：") + Name, AfterFunction, BeforeFunction);
    }
    if (!F.CheckObserverContract(*this) || !CheckOriginalDefaultRows(*this, F.Pawn->DefaultMappingContext)) return false;
    TestEqual(TEXT("当前生产 Setup 只建立原十九个绑定"), F.Input->GetActionEventBindings().Num(), 19);
    const FMapProperty* ContextProperty = FindFProperty<FMapProperty>(UEnhancedPlayerInput::StaticClass(), TEXT("AppliedInputContextData"));
    if (!TestNotNull(TEXT("只读运行期 Context 反射"), ContextProperty)) return false;
    const auto* Contexts = ContextProperty->ContainerPtrToValuePtr<TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>>(F.PlayerInput);
    TestEqual(TEXT("无新增菜单 IMC，仅原 Default 与 Hands"), Contexts->Num(), 2);
    TestFalse(TEXT("原旧手部射线已移除"), FPaths::FileExists(
        FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/CyberCafe/Character/HandRayInteractionComponent.cpp"))));
    F.Counts(*this, 0, 0, 0, 0);
    UInputDelegateBinding::BindInputDelegatesWithSubojects(F.Pawn, F.Input);
    TestEqual(TEXT("含原蓝图仍为43个绑定"), F.Input->GetActionEventBindings().Num(), 43);
    TestEqual(TEXT("含原蓝图 Skip Started 仍唯一"), BindingCount(F.Input, F.Pawn->IA_SkipDialog, ETriggerEvent::Started), 1);
    for (const auto& Binding : F.Input->GetActionEventBindings())
        TestTrue(TEXT("蓝图不新增观察 Action 事件"), Binding->GetAction() != F.Pointer->LeftTriggerObserver
            && Binding->GetAction() != F.Pointer->RightTriggerObserver);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerPhysicalRampedLeft,
    "CyberCafe.MenuPointerExperiment.PhysicalRampedLeft", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerPhysicalRampedLeft::RunTest(const FString&)
{
    return CyberCafeMenuPointerExperimentTests::RampedUI(*this, EControllerHand::Left);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerPhysicalRampedRight,
    "CyberCafe.MenuPointerExperiment.PhysicalRampedRight", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerPhysicalRampedRight::RunTest(const FString&)
{
    return CyberCafeMenuPointerExperimentTests::RampedUI(*this, EControllerHand::Right);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerBooleanSemanticGuard,
    "CyberCafe.MenuPointerExperiment.BooleanSemanticGuard", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerBooleanSemanticGuard::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>(); if (!F->Begin(*this)) return false;
    F->Pose(EControllerHand::Left, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    TArray<FVector2D> Samples = {{0, 0}, {.02, 0}, {.15, 0}, {.35, 0}, {.55, 0}, {.8, 0}, {.8, 0}, {0, 0}, {0, 0}};
    auto FirstStarted = MakeShared<int32>(INDEX_NONE);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEngineFrames>(*this, F, MoveTemp(Samples),
        [this, FirstStarted](FFixture& State, int32 Index)
        {
            const FInputActionInstance* Instance = State.PlayerInput->FindActionInstanceData(State.Pawn->IA_SkipDialog);
            if (!TestNotNull(TEXT("真实运行期原 Action Instance"), Instance)) return;
            TestEqual(TEXT("运行期值类型仍为 Boolean"), int32(Instance->GetValue().GetValueType()), int32(EInputActionValueType::Boolean));
            TestEqual(TEXT("运行期实际只有一个原 Pressed Trigger"), Instance->GetTriggers().Num(), 1);
            for (const auto& Trigger : Instance->GetTriggers())
            {
                const auto* Pressed = Cast<UInputTriggerPressed>(Trigger);
                if (!TestNotNull(TEXT("实际运行期 Pressed，不构造测试 Trigger"), Pressed)) continue;
                TestEqual(TEXT("运行期阈值仍是 .5"), Pressed->ActuationThreshold, .5f);
                AddInfo(FString::Printf(TEXT("原 Trigger.LastValue：幅值=%.4f Get<bool>=%d IsActuated=%d"),
                    Pressed->LastValue.GetMagnitude(), Pressed->LastValue.Get<bool>(), Pressed->IsActuated(Pressed->LastValue)));
                if (Index == 1)
                {
                    // Boolean getter 不会把真实轴幅值 .02 改成 1；原 Pressed 仍按 .5 判断是否 actuated。
                    TestTrue(TEXT("真实 .02 的 Trigger.LastValue Boolean getter 为 true"), Pressed->LastValue.Get<bool>());
                    TestTrue(TEXT("真实 .02 的 Trigger.LastValue 底层幅值仍为 .02"), FMath::IsNearlyEqual(Pressed->LastValue.GetMagnitude(), .02f, .000001f));
                    TestFalse(TEXT("真实 .02 尚未达到原 Pressed 的 .5 阈值"), Pressed->IsActuated(Pressed->LastValue));
                }
            }
            const int32 Attempts = State.Pointer->GetOriginalTriggerAttemptCount();
            if (Attempts && *FirstStarted == INDEX_NONE) *FirstStarted = Index;
            const int32 Expected = Index < 4 ? 0 : 1;
            State.Counts(*this, Expected, 0, 0, Expected);
            if (Index == 1)
            {
                TestEqual(TEXT(".02 getter 为 true 但原 Started 回调进入次数为零"), Attempts, 0);
                TestTrue(TEXT(".02 不产生原 Started 事件"), Instance->GetTriggerEvent() != ETriggerEvent::Started);
            }
            if (Index == 4 || Index == 8)
                TestEqual(TEXT("保留原 .5 阈值，真实首个 Started 固定为索引4的 .55"), *FirstStarted, 4);
            if (Index == 8)
            {
                TestEqual(TEXT("一次无回落 ramp 只有一次原 Skip"), Attempts, 1);
                TestFalse(TEXT("全零稳定后真实原 Action 返回 false"), Instance->GetValue().Get<bool>());
                AddInfo(FString::Printf(TEXT("真实首个 Started 样本索引=%d；.02 getter=true、幅值=.02、未 actuated；原 Pressed 阈值 .5 保持不变。"), *FirstStarted));
            }
            State.Pointer->UpdatePointer(Dt);
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerBounceCrossBooleanAndRelease,
    "CyberCafe.MenuPointerExperiment.BounceCrossBooleanAndRelease", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerBounceCrossBooleanAndRelease::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>();
    if (!F->Begin(*this) || !F->Open(*this) || !F->AimAction(*this, EControllerHand::Left)) return false;
    TArray<FVector2D> Samples = {{0, 0}, {.02, 0}, {.49, 0}, {.55, 0}, {.49, 0}, {.51, 0}, {.8, 0}, {.35, 0}, {.55, 0}, {0, 0}, {.02, 0}, {.8, 0}, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEngineFrames>(*this, F, MoveTemp(Samples),
        [this](FFixture& State, int32 Index)
        {
            // 原 Pressed 在幅值回落到 .5 以下即重新武装，不要求完全归零；真实帧让每次 UI 点击独立可接受。
            const int32 ExpectedCounts[] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4};
            const int32 Expected = ExpectedCounts[Index];
            State.Counts(*this, Expected, Expected, Expected, 0);
            if (Index == 5 || Index == 8)
                TestEqual(TEXT("回落到原 .5 阈值以下后，再次跨阈值产生真实 Started/UI 点击"), State.Pointer->GetAcceptedClickCount(), Expected);
            if (Index == 10) TestEqual(TEXT("全零释放后的 .02 仍未跨过原 .5 阈值"), State.Pointer->GetAcceptedClickCount(), 3);
            if (Index == 11) TestEqual(TEXT("随后 .8 再次跨过原阈值，累计四次真实 UI 点击"), State.Pointer->GetAcceptedClickCount(), 4);
            State.Pointer->UpdatePointer(Dt);
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerRejectedInputPreservesOriginalSkip,
    "CyberCafe.MenuPointerExperiment.RejectedInputPreservesOriginalSkip", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerRejectedInputPreservesOriginalSkip::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    // 每个拒绝条件都是新鲜 Actor，不能受上一例 LastAcceptedPointerFrame 污染。
    for (const TCHAR* Case : {TEXT("empty"), TEXT("UIblank"), TEXT("tracking unavailable"), TEXT("held"), TEXT("teleport"), TEXT("surface"), TEXT("disabled")})
    {
        FFixture F; if (!F.Begin(*this) || !F.Open(*this) || !F.AimAction(*this, EControllerHand::Left)) return false;
        const FString Reason(Case); UGrabComponent* Grab = nullptr;
        if (Reason == TEXT("empty")) F.Pose(EControllerHand::Left, RayPose(FVector(0, 0, 1400), FVector::UpVector));
        if (Reason == TEXT("UIblank"))
        {
            const FVector Point = PixelPoint(F.Screen, FVector2D(1175, 725));
            FMenuPointerHit Hit; const FTransform Pose = PanelPose(F.Screen, Point);
            TestTrue(TEXT("空白仍属于实际全平面"), F.Screen->TraceMenuPointer(Pose.GetLocation(), Pose.GetRotation().GetForwardVector(), 250., Hit) && Hit.bPanel);
            TestEqual(TEXT("空白没有 UI Action"), Hit.Action, INDEX_NONE); F.Pose(EControllerHand::Left, Pose);
        }
        if (Reason == TEXT("tracking unavailable"))
        {
            F.Pose(EControllerHand::Left, PanelPose(F.Screen, ActionPoint(F.Screen, 1)), false, false);
            TestFalse(TEXT("失追是提供器真实采样结果，不写 tracking 字段"), F.Pawn->MotionControllerLeftAim->IsTracked());
            TestFalse(TEXT("Grip 同时真实失追"), F.Pawn->MotionControllerLeftGrip->IsTracked());
        }
        if (Reason == TEXT("held"))
        {
            auto* Mesh = F.Cube(FTransform(F.Pawn->MotionControllerLeftGrip->GetComponentLocation()), false);
            if (!TestNotNull(TEXT("夹具持物几何"), Mesh)) return false;
            Grab = NewObject<UGrabComponent>(Mesh->GetOwner(), NAME_None, RF_Transient);
            Mesh->GetOwner()->AddInstanceComponent(Grab); Grab->SetupAttachment(Mesh);
            Grab->GrabType = EGrabType::Custom; Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
            Grab->bSimulateOnDrop = false; Grab->RegisterComponent(); Grab->Activate();
            if (!TestTrue(TEXT("通过原 TryGrabLeft 建立持物，不写 Held 或 bIsHeld"), F.Pawn->TryGrabLeft(Grab) && Grab->IsHeld())) return false;
        }
        if (Reason == TEXT("teleport")) F.Pawn->StartTeleportTrace();
        if (Reason == TEXT("surface"))
        {
            const FVector Point = ActionPoint(F.Screen, 1), Normal = F.Screen->Panel->GetForwardVector();
            auto* Mesh = F.Cube(FTransform(FQuat::Identity, Point + Normal * 40., FVector(.1)));
            if (!TestNotNull(TEXT("菜单前真实遮挡物"), Mesh)) return false;
            FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MenuPointerExperimentOcclusion), true); Params.AddIgnoredActor(F.Pawn);
            FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
            if (!TestTrue(TEXT("与生产一致的 complex 物理查询确实命中遮挡物"), F.World->LineTraceSingleByObjectType(Hit,
                Point + Normal * 80., Point - Normal * 10., Objects, Params) && Hit.GetComponent() == Mesh && Hit.Distance < 80.)) return false;
        }
        if (Reason == TEXT("disabled")) F.Pointer->bEnabled = false;
        F.Pointer->UpdatePointer(Dt);
        if (!F.Step(*this, .8f, 0.f)) return false;
        AddInfo(TEXT("拒绝条件：") + Reason); F.Counts(*this, 1, 0, 0, 1);
        TestTrue(TEXT("诊断证明具体拒绝原因，不能只凭 Skip 计数"), F.Pointer->GetLastClickStatus().Contains(TEXT("reason=") + Reason));
        if (Grab) TestTrue(TEXT("拒绝后原持物及抓握控制器均未改变"), F.Pawn->HeldComponentLeft == Grab && Grab->IsHeld()
            && Grab->GetHoldingController() == F.Pawn->MotionControllerLeftGrip);
        if (Reason == TEXT("teleport")) TestTrue(TEXT("指针没有关闭传送"), F.Pawn->bTeleportTraceActive);
        F.NoOrderMutation(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerVisibilityOnlyOcclusion,
    "CyberCafe.MenuPointerExperiment.VisibilityOnlyOcclusion", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerVisibilityOnlyOcclusion::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    // 两例各用独立 World/Actor，同帧首击不受上一例 LastAcceptedPointerFrame 影响。
    for (bool bWall : {false, true})
    {
        FFixture F;
        if (!F.Begin(*this) || !F.Open(*this) || !F.AimAction(*this, EControllerHand::Left)) return false;
        const FVector Point = ActionPoint(F.Screen, 1), Normal = F.Screen->Panel->GetForwardVector();
        auto* Mesh = F.Cube(FTransform(FQuat::Identity, Point + Normal * 40., FVector(.1)));
        if (!TestNotNull(TEXT("射线起点与真实 UI 平面之间的原 Cube 物理几何"), Mesh)) return false;
        if (!bWall)
        {
            Mesh->SetVisibility(false); Mesh->SetHiddenInGame(true);
            Mesh->SetCollisionResponseToAllChannels(ECR_Overlap);
            Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        }
        TestTrue(TEXT("两例均保留真实 QueryOnly WorldDynamic Mesh"), Mesh->GetCollisionEnabled() == ECollisionEnabled::QueryOnly
            && Mesh->GetCollisionObjectType() == ECC_WorldDynamic);
        TestTrue(TEXT("墙与不可见 overlap-only 区域的 Visibility 响应不同"),
            Mesh->GetCollisionResponseToChannel(ECC_Visibility) == (bWall ? ECR_Block : ECR_Ignore));
        if (!bWall)
            TestTrue(TEXT("不可见 gameplay 区域只重叠、不遮挡 Visibility"), !Mesh->IsVisible() && Mesh->bHiddenInGame
                && Mesh->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Overlap);
        const FVector Start = F.Pawn->MotionControllerLeftAim->GetComponentLocation();
        const FVector End = Start + F.Pawn->MotionControllerLeftAim->GetForwardVector() * 250.;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MenuPointerExperimentVisibility), true);
        Params.AddIgnoredActor(F.Pawn);
        FHitResult ObjectHit;
        FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        if (!TestTrue(TEXT("complex 对象查询真实命中 UI 前的 Mesh，证明 overlap 区域并非不存在"),
            F.World->LineTraceSingleByObjectType(ObjectHit, Start, End, Objects, Params)
            && ObjectHit.GetComponent() == Mesh && ObjectHit.Distance < 80.)) return false;
        FHitResult VisibilityHit;
        const bool bVisibilityHit = F.World->LineTraceSingleByChannel(VisibilityHit, Start, End, ECC_Visibility, Params);
        if (bWall)
        {
            if (!TestTrue(TEXT("complex Visibility 查询确实被真实墙阻挡"), bVisibilityHit
                && VisibilityHit.bBlockingHit && VisibilityHit.GetComponent() == Mesh && VisibilityHit.Distance < 80.)) return false;
        }
        else if (!TestFalse(TEXT("不可见 overlap-only Mesh 不产生 Visibility 阻挡"), bVisibilityHit)) return false;
        F.Pointer->UpdatePointer(Dt);
        const uint64 SameFrame = GFrameCounter;
        // 仅原生中性输入及首次 .8 跨阈值；首击后不追加输入或直接激活菜单。
        if (!F.Step(*this, 0.f, 0.f)) return false;
        F.Counts(*this, 0, 0, 0, 0);
        if (!F.Step(*this, .8f, 0.f)) return false;
        TestEqual(TEXT("首次原生菜单点击未合成引擎帧"), GFrameCounter, SameFrame);
        const FString Status = F.Pointer->GetLastClickStatus();
        if (bWall)
        {
            F.Counts(*this, 1, 0, 0, 1);
            TestTrue(TEXT("真实墙拒绝 UI，保留原 Skip 路径"), Status.Contains(TEXT("result=OriginalSkip reason=surface")));
        }
        else
        {
            F.Counts(*this, 1, 1, 1, 0);
            TestTrue(TEXT("不可见 overlap-only 区域后的 UI 真实接受左手原生输入"),
                Status.Contains(TEXT("result=accepted reason=UI accepted")) && Status.Contains(TEXT("triggerHand=L")));
        }
        AddInfo(bWall ? TEXT("Visibility Block 墙：accepted=0 Menu=0 Skip=1")
            : TEXT("不可见 overlap-only / Visibility Ignore：accepted=1 Menu=1 Skip=0"));
        F.NoOrderMutation(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerCurrentGeometryAndPower,
    "CyberCafe.MenuPointerExperiment.CurrentGeometryAndPower", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerCurrentGeometryAndPower::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    {
        FFixture F; if (!F.Begin(*this) || !F.Open(*this) || !F.AimAction(*this, EControllerHand::Left)) return false;
        F.Pointer->UpdatePointer(Dt);
        TestTrue(TEXT("旧采样实际命中 UI"), F.Pointer->GetPointerStatus(EControllerHand::Left).Contains(TEXT("UIaction1")));
        F.Screen->AddActorWorldOffset(FVector(0, 500, 0));
        // 不更新指针缓存：原 Skip 回调必须重新查询当前几何。
        if (!F.Step(*this, .8f, 0.f)) return false;
        F.Counts(*this, 1, 0, 0, 1);
        if (!F.Step(*this, 0.f, 0.f) || !F.AimAction(*this, EControllerHand::Left, 0)) return false;
        if (!F.Step(*this, .8f, 0.f)) return false;
        F.Counts(*this, 2, 1, 1, 1);
        TestEqual(TEXT("点击的是移动后的当前 UI Action"), F.Screen->GetLastScreenAction(), 0);
    }
    {
        FFixture F; if (!F.Begin(*this)) return false;
        TestFalse(TEXT("新鲜菜单原本关闭"), F.Screen->IsPanelOpen());
        const FVector Point = F.Screen->ButtonZone->GetComponentLocation();
        const FTransform Pose = RayPose(Point + FVector(80, 0, 0), -FVector::ForwardVector);
        F.Pose(EControllerHand::Right, Pose); FMenuPointerHit Hit;
        if (!TestTrue(TEXT("关闭时真实解析电源面"), F.Screen->TraceMenuPointer(Pose.GetLocation(), Pose.GetRotation().GetForwardVector(), 250., Hit)
            && Hit.bPower && !Hit.bPanel)) return false;
        if (!F.Step(*this, 0.f, .8f)) return false;
        F.Counts(*this, 1, 1, 0, 0);
        TestTrue(TEXT("原电源确实打开菜单，不伪造菜单动作通知"), F.Screen->IsPanelOpen());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerGripFallbackOriginalSources,
    "CyberCafe.MenuPointerExperiment.GripFallbackOriginalSources", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerGripFallbackOriginalSources::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    for (EControllerHand Hand : {EControllerHand::Left, EControllerHand::Right})
    {
        FFixture F; if (!F.Begin(*this) || !F.Open(*this)) return false;
        F.Pose(Hand, PanelPose(F.Screen, ActionPoint(F.Screen, 1)), true, false);
        F.Pointer->UpdatePointer(Dt);
        TestTrue(TEXT("原同手 Grip 真实采样回退"), F.Pointer->GetPointerStatus(Hand).Contains(TEXT("GripFallback")));
        TestTrue(TEXT("回退保留严格原 Source 名"), F.Pointer->GetPointerStatus(Hand).Contains(
            Hand == EControllerHand::Left ? TEXT("src=LeftGrip") : TEXT("src=RightGrip")));
        if (!F.Step(*this, Hand == EControllerHand::Left ? .8f : 0.f, Hand == EControllerHand::Right ? .8f : 0.f)) return false;
        F.Counts(*this, 1, 1, 1, 0); F.NoOrderMutation(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerVisualsDoNotGenerateInput,
    "CyberCafe.MenuPointerExperiment.VisualsDoNotGenerateInput", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerVisualsDoNotGenerateInput::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    FFixture F; if (!F.Begin(*this) || !F.Open(*this) || !F.AimAction(*this, EControllerHand::Left)) return false;
    F.Pose(EControllerHand::Right, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    F.Pointer->UpdatePointer(Dt); F.Counts(*this, 0, 0, 0, 0);
    auto* Beam = F.Pointer->GetBeam(EControllerHand::Left); auto* Dot = F.Pointer->GetEndpoint(EControllerHand::Left);
    if (!TestNotNull(TEXT("左实际 Beam"), Beam) || !TestNotNull(TEXT("左实际 Endpoint"), Dot)) return false;
    TestTrue(TEXT("悬停 UI 可见几何，无碰撞无导航"), Beam->IsVisible() && Dot->IsVisible()
        && Beam->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Beam->CanEverAffectNavigation());
    TestTrue(TEXT("Aim 附着保留原组件"), Beam->GetAttachParent() == F.Pawn->MotionControllerLeftAim);
    CheckTint(*this, F, EControllerHand::Left, FLinearColor(1.f, .38f, .02f));
    auto* EmptyDot = F.Pointer->GetEndpoint(EControllerHand::Right);
    if (!TestNotNull(TEXT("空射线仍有光束但不显示端点"), EmptyDot)) return false;
    TestFalse(TEXT("空射线端点不可见"), EmptyDot->IsVisible());
    const FVector Blank = PixelPoint(F.Screen, FVector2D(1175, 725));
    F.Pose(EControllerHand::Left, PanelPose(F.Screen, Blank)); F.Pointer->UpdatePointer(Dt);
    TestTrue(TEXT("空白仍显示端点，诊断 UIblank"), Dot->IsVisible() && F.Pointer->GetPointerStatus(EControllerHand::Left).Contains(TEXT("UIblank")));
    CheckTint(*this, F, EControllerHand::Left, FLinearColor(.35f, .35f, .35f));
    F.Counts(*this, 0, 0, 0, 0);
    if (!F.AimAction(*this, EControllerHand::Left) || !F.Step(*this, .8f, 0.f)) return false;
    F.Pointer->UpdatePointer(0.f); F.Counts(*this, 1, 1, 1, 0);
    CheckTint(*this, F, EControllerHand::Left, FLinearColor(.05f, 1.f, .12f));
    TestFalse(TEXT("更新视觉没有激活原 WidgetInteraction Tick"), F.Pawn->WidgetInteractionLeft->IsComponentTickEnabled());
    F.NoOrderMutation(*this); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerSharedActionHeldOppositeNoSyntheticEdge,
    "CyberCafe.MenuPointerExperiment.SharedActionHeldOppositeNoSyntheticEdge", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerSharedActionHeldOppositeNoSyntheticEdge::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>();
    if (!F->Begin(*this) || !F->Open(*this) || !F->AimAction(*this, EControllerHand::Right)) return false;
    F->Pose(EControllerHand::Left, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    TArray<FVector2D> Samples = {{0, 0}, {.8, 0}, {.8, .8}, {0, .8}, {0, 0}, {0, .8}, {0, .8}, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEngineFrames>(*this, F, MoveTemp(Samples),
        [this](FFixture& State, int32 Index)
        {
            if (Index == 0) State.Counts(*this, 0, 0, 0, 0);
            else if (Index < 5) State.Counts(*this, 1, 0, 0, 1);
            else State.Counts(*this, 2, 1, 1, 1);
            if (Index == 2 || Index == 3)
                TestEqual(TEXT("共享 Action 已持有时不为另一手合成 Started 或菜单点击"), State.Pointer->GetOriginalTriggerAttemptCount(), 1);
            if (Index == 5)
            {
                const FString Status = State.Pointer->GetLastClickStatus();
                TestTrue(TEXT("共享 Action 全零释放后真实新 edge 选择右手 raw"), Status.Contains(TEXT("triggerHand=R")) && Status.Contains(TEXT("RMax=0.8000")));
                TestTrue(TEXT("保留实际共享输入值并确认真实右手UI接受，不伪造回调值为零"),
                    Status.Contains(TEXT("triggerHand=R")) && Status.Contains(TEXT("result=accepted")));
                AddInfo(TEXT("共享Action幅值仅诊断；本次真实输入为.8，不声称测到了幅值为零的Started。"));
            }
            State.Pointer->UpdatePointer(Dt); State.NoOrderMutation(*this);
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerSameFrameDebounceAndLatentRearm,
    "CyberCafe.MenuPointerExperiment.SameFrameDebounceAndLatentRearm", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerSameFrameDebounceAndLatentRearm::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>();
    if (!F->Begin(*this) || !F->Open(*this) || !F->AimAction(*this, EControllerHand::Left)) return false;
    const uint64 SameFrame = GFrameCounter;
    if (!F->Step(*this, .8f, 0.f) || !F->Step(*this, 0.f, 0.f) || !F->Step(*this, .8f, 0.f)) return false;
    TestEqual(TEXT("同帧输入批次没有伪造下一帧"), GFrameCounter, SameFrame);
    F->Counts(*this, 2, 1, 1, 1);
    TArray<FVector2D> Samples = {{0, 0}, {.8, 0}, {.8, 0}, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEngineFrames>(*this, F, MoveTemp(Samples),
        [this, SameFrame](FFixture& State, int32 Index)
        {
            TestTrue(TEXT("跨帧重入由引擎真实推进"), GFrameCounter > SameFrame);
            if (Index >= 1) State.Counts(*this, 3, 2, 2, 1);
            State.Pointer->UpdatePointer(Dt);
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerOriginalBlueprintDelegates,
    "CyberCafe.MenuPointerExperiment.OriginalBlueprintDelegates", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerOriginalBlueprintDelegates::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>();
    if (!F->Begin(*this) || !F->Open(*this) || !F->AimAction(*this, EControllerHand::Left)) return false;
    const int32 NativeBindingCount = F->Input->GetActionEventBindings().Num();
    UInputDelegateBinding::BindInputDelegatesWithSubojects(F->Pawn, F->Input);
    AddInfo(FString::Printf(TEXT("原Blueprint绑定：Native=%d，含原BP=%d，Skip Started=%d"), NativeBindingCount,
        F->Input->GetActionEventBindings().Num(), BindingCount(F->Input, F->Pawn->IA_SkipDialog, ETriggerEvent::Started)));
    if (!TestEqual(TEXT("包含原蓝图绑定后原Skip Started仍唯一"),
        BindingCount(F->Input, F->Pawn->IA_SkipDialog, ETriggerEvent::Started), 1)) return false;
    TArray<FVector2D> Samples = {{0, 0}, {.8f, 0}, {0, 0}, {.8f, 0}, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEngineFrames>(*this, F, MoveTemp(Samples),
        [this](FFixture& State, int32 Index)
        {
            if (Index == 0) State.Counts(*this, 0, 0, 0, 0);
            else if (Index < 3) State.Counts(*this, 1, 1, 1, 0);
            else State.Counts(*this, 2, 1, 1, 1);
            if (Index == 1) State.Pose(EControllerHand::Left, RayPose(FVector(0, 0, 1400), FVector::UpVector));
            State.Pointer->UpdatePointer(Dt);
        }));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerNativeInjectedLeft,
    "CyberCafe.MenuPointerExperiment.NativeInjection.LeftUI", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerNativeInjectedLeft::RunTest(const FString&)
{
    return CyberCafeMenuPointerExperimentTests::NativeSingleUI(*this, EControllerHand::Left);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerNativeInjectedRight,
    "CyberCafe.MenuPointerExperiment.NativeInjection.RightUI", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerNativeInjectedRight::RunTest(const FString&)
{
    return CyberCafeMenuPointerExperimentTests::NativeSingleUI(*this, EControllerHand::Right);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerNativeOppositeHover,
    "CyberCafe.MenuPointerExperiment.NativeInjection.OppositeHoverAndSharedHold", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerNativeOppositeHover::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>(true);
    if (!F->Begin(*this) || !F->Open(*this) || !F->AimAction(*this, EControllerHand::Left)) return false;
    F->Pose(EControllerHand::Right, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    F->Pointer->UpdatePointer(Dt);
    TArray<FNativeSample> Samples = {{0, 0}, {0, .8}, {.8, .8}, {0, .8}, {0, 0}, {0, 0}, {0, 0}, {.8, 0}, {.8, 0}, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FNativeEngineFrames>(*this, F, MoveTemp(Samples),
        [this](FFixture& State, int32 Index)
        {
            if (Index == 0) State.Counts(*this, 0, 0, 0, 0);
            else if (Index < 7) State.Counts(*this, 1, 0, 0, 1);
            else State.Counts(*this, 2, 1, 1, 1);
            if (Index == 1)
                TestTrue(TEXT("左手悬停不抢右手真实扳机，右手空处保留原 Skip"),
                    State.Pointer->GetLastClickStatus().Contains(TEXT("triggerHand=R"))
                    && State.Pointer->GetLastClickStatus().Contains(TEXT("result=OriginalSkip reason=empty")));
            if (Index == 2 || Index == 3)
                TestEqual(TEXT("共享 Boolean 持有期间不为另一手合成 Started"), State.Pointer->GetOriginalTriggerAttemptCount(), 1);
            State.Pointer->UpdatePointer(Dt); State.NoOrderMutation(*this);
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerNativeBothAmbiguous,
    "CyberCafe.MenuPointerExperiment.NativeInjection.BothAmbiguous", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerNativeBothAmbiguous::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>(true);
    if (!F->Begin(*this) || !F->Open(*this) || !F->AimAction(*this, EControllerHand::Left)) return false;
    F->Pose(EControllerHand::Right, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    F->Pointer->UpdatePointer(Dt);
    TArray<FNativeSample> Samples = {{0, 0}, {.8, .8}, {.8, .95}, {0, 0}, {0, 0}, {0, 0}, {.55, .95}, {.55, .95}, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FNativeEngineFrames>(*this, F, MoveTemp(Samples),
        [this](FFixture& State, int32 Index)
        {
            const int32 Expected = Index == 0 ? 0 : (Index < 6 ? 1 : 2);
            State.Counts(*this, Expected, 0, 0, Expected);
            if (Index == 1 || Index == 6)
                TestTrue(TEXT("两手越阈，即使只有一手悬停或强度不同也拒绝猜手"),
                    State.Pointer->GetLastClickStatus().Contains(TEXT("triggerHand=none"))
                    && State.Pointer->GetLastClickStatus().Contains(TEXT("reason=ambiguous two actuated hands")));
            State.Pointer->UpdatePointer(Dt); State.NoOrderMutation(*this);
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerNativeZeroAndSubThreshold,
    "CyberCafe.MenuPointerExperiment.NativeInjection.ValidZeroAndOriginalGate", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerNativeZeroAndSubThreshold::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    auto F = MakeShared<FFixture>(true);
    if (!F->Begin(*this) || !F->Open(*this) || !F->AimAction(*this, EControllerHand::Left)) return false;
    F->Pose(EControllerHand::Right, RayPose(FVector(0, 0, 1400), FVector::UpVector));
    F->Pointer->UpdatePointer(Dt);
    // 强制 Boolean=true 只模拟可能早于 .5 的 XR 原生 Boolean；观察 float 仍使用真实引擎值。
    TArray<FNativeSample> Samples = {{0, 0}, {0, 0, true}, {0, 0}, {0, 0}, {0, 0}, {.2, 0, true},
        {.8, 0, true}, {0, 0}, {0, 0}, {0, 0}, {.2, .8, true}, {0, 0}, {0, 0}, {0, 0}, {.8, 0, true}, {.8, 0, true}, {0, 0}};
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FNativeEngineFrames>(*this, F, MoveTemp(Samples),
        [this](FFixture& State, int32 Index)
        {
            const int32 Attempts = Index == 0 ? 0 : (Index < 5 ? 1 : (Index < 10 ? 2 : (Index < 14 ? 3 : 4)));
            const int32 Accepted = Index < 14 ? 0 : 1;
            State.Counts(*this, Attempts, Accepted, Accepted, Attempts - Accepted);
            const FString Status = State.Pointer->GetLastClickStatus();
            if (Index == 1 || Index == 5)
                TestTrue(TEXT("观察实例有效的真实0/.2不回退、不把原 Boolean=1变成两手越阈"),
                    Status.Contains(TEXT("ValueSource=NativeAction")) && Status.Contains(TEXT("ObserverPairValid=1"))
                    && Status.Contains(TEXT("reason=under threshold")) && Status.Contains(TEXT("OriginalActionValueNative=1.0000")));
            if (Index == 6)
                TestEqual(TEXT("早期 Boolean 已持有后观察轴跨阈值，不合成新的原事件"), State.Pointer->GetAcceptedClickCount(), 0);
            if (Index == 10)
                TestTrue(TEXT("悬停左手实际.2、另一手.8：只选右手并回原Skip，不能强选UI手"),
                    Status.Contains(TEXT("LMax=0.2000")) && Status.Contains(TEXT("RMax=0.8000"))
                    && Status.Contains(TEXT("triggerHand=R")) && Status.Contains(TEXT("reason=empty")));
            State.Pointer->UpdatePointer(Dt); State.NoOrderMutation(*this);
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerNativeUnavailableAndRejectGates,
    "CyberCafe.MenuPointerExperiment.NativeInjection.UnavailablePairAndRejectGates", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerNativeUnavailableAndRejectGates::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    for (const TCHAR* Case : {TEXT("unavailable"), TEXT("UIblank"), TEXT("tracking unavailable"), TEXT("held"), TEXT("teleport"), TEXT("surface"), TEXT("disabled")})
    {
        FFixture F(true); if (!F.Begin(*this) || !F.Open(*this) || !F.AimAction(*this, EControllerHand::Left)) return false;
        const FString Reason(Case); UGrabComponent* Grab = nullptr;
        if (Reason == TEXT("unavailable")) F.Pointer->RightTriggerObserver = nullptr; // 只改本例实例，不改生产 CDO/资产。
        if (Reason == TEXT("UIblank")) F.Pose(EControllerHand::Left, PanelPose(F.Screen, PixelPoint(F.Screen, FVector2D(1175, 725))));
        if (Reason == TEXT("tracking unavailable")) F.Pose(EControllerHand::Left, PanelPose(F.Screen, ActionPoint(F.Screen, 1)), false, false);
        if (Reason == TEXT("held"))
        {
            auto* Mesh = F.Cube(FTransform(F.Pawn->MotionControllerLeftGrip->GetComponentLocation()), false);
            if (!TestNotNull(TEXT("本例原持物几何"), Mesh)) return false;
            Grab = NewObject<UGrabComponent>(Mesh->GetOwner(), NAME_None, RF_Transient);
            Mesh->GetOwner()->AddInstanceComponent(Grab); Grab->SetupAttachment(Mesh);
            Grab->GrabType = EGrabType::Custom; Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
            Grab->bSimulateOnDrop = false; Grab->RegisterComponent(); Grab->Activate();
            if (!TestTrue(TEXT("原 TryGrabLeft 实际建立持物，不反射伪造状态"), F.Pawn->TryGrabLeft(Grab) && Grab->IsHeld())) return false;
        }
        if (Reason == TEXT("teleport")) F.Pawn->StartTeleportTrace();
        if (Reason == TEXT("surface"))
        {
            const FVector Point = ActionPoint(F.Screen, 1), Normal = F.Screen->Panel->GetForwardVector();
            if (!TestNotNull(TEXT("实际 Visibility 墙"), F.Cube(FTransform(FQuat::Identity, Point + Normal * 40., FVector(.1))))) return false;
        }
        if (Reason == TEXT("disabled")) F.Pointer->bEnabled = false;
        F.Pointer->UpdatePointer(Dt);
        if (!F.StepNative(*this, .8f, 0.f)) return false;
        F.Counts(*this, 1, 0, 0, 1);
        const FString Status = F.Pointer->GetLastClickStatus();
        if (Reason == TEXT("unavailable"))
            TestTrue(TEXT("缺失成对观察源与真实零键状态明确诊断，不凭悬停选手"),
                Status.Contains(TEXT("ValueSource=PhysicalKey")) && Status.Contains(TEXT("ObserverPairValid=0"))
                && Status.Contains(TEXT("critical original Action active")));
        else TestTrue(TEXT("原生 float 下具体拒绝原因保留原 Skip：") + Reason, Status.Contains(TEXT("reason=") + Reason));
        if (Grab)
        {
            TestTrue(TEXT("拒绝后原持物关系完全保持"), F.Pawn->HeldComponentLeft == Grab && Grab->IsHeld());
            FHitResult HeldHit;
            TestFalse(TEXT("启用指针时持物手的共享表面查询不扫描其他目标"), F.Pointer->TraceSurfaceForHand(false, HeldHit));
            TestNull(TEXT("持物拒绝不保留旧表面 Actor"), HeldHit.GetActor());
        }
        F.NoOrderMutation(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerSharedSurfaceSceneUIAim,
    "CyberCafe.MenuPointerExperiment.SharedSurface.SceneUIAim", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerSharedSurfaceSceneUIAim::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    for (const TCHAR* Case : {TEXT("UI"), TEXT("blank"), TEXT("wall"), TEXT("disabled"), TEXT("Grip"), TEXT("range"), TEXT("empty"), TEXT("untracked")})
    {
        FFixture F; if (!F.Begin(*this) || !F.Open(*this) || !F.AimAction(*this, EControllerHand::Left)) return false;
        const FString Reason(Case);
        const FVector Point = Reason == TEXT("blank") ? PixelPoint(F.Screen, FVector2D(1175, 725)) : ActionPoint(F.Screen, 1);
        const FVector Normal = F.Screen->Panel->GetForwardVector();
        F.Pose(EControllerHand::Left, PanelPose(F.Screen, Point, Reason == TEXT("range") ? 300. : 80.), true, Reason != TEXT("Grip"));
        auto* Behind = F.Cube(FTransform(FQuat::Identity, Point - Normal * 40., FVector(.1)));
        if (!TestNotNull(TEXT("无碰撞菜单后的实际物理几何"), Behind)) return false;
        UStaticMeshComponent* Wall = nullptr;
        if (Reason == TEXT("wall")) Wall = F.Cube(FTransform(FQuat::Identity, Point + Normal * 40., FVector(.1)));
        if (Reason == TEXT("disabled")) F.Pointer->bEnabled = false;
        if (Reason == TEXT("empty")) F.Pose(EControllerHand::Left, RayPose(FVector(0, 0, 1400), FVector::UpVector));
        if (Reason == TEXT("untracked")) F.Pose(EControllerHand::Left, PanelPose(F.Screen, Point), false, false);
        F.Pointer->RayMaxDistance = 10000.f; // 故意越过配置上限，生产查询仍必须夹到250cm。
        F.Pointer->UpdatePointer(Dt);
        FHitResult Hit;
        const bool bHit = F.Pointer->TraceSurfaceForHand(false, Hit);
        if (Reason == TEXT("range") || Reason == TEXT("empty") || Reason == TEXT("untracked"))
        {
            TestFalse(TEXT("超距、空处或失追不返回旧命中：") + Reason, bHit);
            TestNull(TEXT("失败结果已清空 Actor"), Hit.GetActor());
            continue;
        }
        if (!TestTrue(TEXT("共享当前表面查询成功：") + Reason, bHit)) return false;
        const auto* Origin = Reason == TEXT("Grip") ? F.Pawn->MotionControllerLeftGrip.Get() : F.Pawn->MotionControllerLeftAim.Get();
        TestTrue(TEXT("TraceStart 严格为视觉同手 Origin"), Hit.TraceStart.Equals(Origin->GetComponentLocation(), .001));
        TestTrue(TEXT("TraceEnd 为同方向250cm，没有角度吸附"), Hit.TraceEnd.Equals(Hit.TraceStart + Origin->GetForwardVector() * 250., .001));
        TestTrue(TEXT("Distance 与命中点及 Time 一致"), FMath::IsNearlyEqual(double(Hit.Distance), FVector::Distance(Hit.TraceStart, Hit.ImpactPoint), .001)
            && FMath::IsNearlyEqual(double(Hit.Time), double(Hit.Distance) / 250., .00001));
        if (Reason == TEXT("wall")) TestTrue(TEXT("UI前真实墙返回原物理 FHitResult"), Wall && Hit.GetComponent() == Wall);
        else if (Reason == TEXT("disabled")) TestTrue(TEXT("关闭视觉仍保留裸物理查询，不禁止原远程抓取"), Hit.GetComponent() == Behind);
        else TestTrue(TEXT("全菜单屏幕包括空白都挡住背后物体"), Hit.GetActor() == F.Screen && Hit.GetComponent() == F.Screen->Panel
            && Hit.ImpactPoint.Equals(Point, .001) && Hit.bBlockingHit);
        if (F.Pointer->bEnabled)
        {
            const auto* Dot = F.Pointer->GetEndpoint(EControllerHand::Left);
            if (!TestNotNull(TEXT("实际视觉端点"), Dot)) return false;
            TestTrue(TEXT("共享表面与视觉端点逐点相同"), Dot->GetComponentLocation().Equals(Hit.ImpactPoint, .001));
        }
        F.Counts(*this, 0, 0, 0, 0); F.NoOrderMutation(*this);
    }
    return !HasAnyErrors();
}
namespace CyberCafeMenuPointerExperimentTests
{
UGrabComponent* RemoteTargetGrab(UStaticMeshComponent* Mesh, int32 Priority = 0)
{
    if (!Mesh) return nullptr;
    AActor* Actor = Mesh->GetOwner();
    auto* Grab = NewObject<UGrabComponent>(Actor, NAME_None, RF_Transient);
    Actor->AddInstanceComponent(Grab); Grab->SetupAttachment(Mesh);
    Grab->GrabType = EGrabType::Free; Grab->GrabPriority = Priority;
    Grab->bAllowRemoteGrab = true; Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
    Grab->RegisterComponent(); Grab->bSimulateOnDrop = false;
    // 原 Grab BeginPlay 会写 PhysicsActor；注册后恢复真实测试几何的查询响应。
    Mesh->SetSimulatePhysics(false); Mesh->SetEnableGravity(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetCollisionObjectType(ECC_WorldDynamic);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    return Grab;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerRemoteExactLineActorPriority,
    "CyberCafe.MenuPointerExperiment.RemoteTarget.ExactLineActorPriority", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerRemoteExactLineActorPriority::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    FFixture F; if (!F.Begin(*this)) return false;
    const FVector Start(0, 0, 1400);
    F.Pose(EControllerHand::Left, RayPose(Start, FVector::ForwardVector));
    auto* Main = F.Cube(FTransform(FQuat::Identity, Start + FVector(80, 0, 0), FVector(.1)));
    auto* Neighbor = F.Cube(FTransform(FQuat::Identity, Start + FVector(70, 8, 0), FVector(.01)));
    auto* BodyGrab = RemoteTargetGrab(Main);
    auto* CapGrab = RemoteTargetGrab(Main, 2);
    auto* NeighborGrab = RemoteTargetGrab(Neighbor, 100);
    if (!TestNotNull(TEXT("主 Actor 原生抓点"), BodyGrab) || !TestNotNull(TEXT("同 Actor 高优先级抓点"), CapGrab)
        || !TestNotNull(TEXT("离轴高优先级邻物"), NeighborGrab)) return false;
    TestTrue(TEXT("实际 Cube 为已注册 QueryOnly Visibility Block，不模拟物理"), Main->IsRegistered()
        && Main->GetCollisionEnabled() == ECollisionEnabled::QueryOnly
        && Main->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block && !Main->IsSimulatingPhysics());
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MenuPointerRemoteOldSweep), false);
    Params.AddIgnoredActor(F.Pawn);
    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FHitResult OldSweep;
    if (!TestTrue(TEXT("原半径10cm球扫真实先碰离轴邻物，证明测试能区分旧实现"),
        F.World->SweepSingleByObjectType(OldSweep, Start, Start + FVector(250, 0, 0), FQuat::Identity,
            Objects, FCollisionShape::MakeSphere(10.f), Params) && OldSweep.GetActor() == Neighbor->GetOwner())) return false;
    FHitResult Hit;
    if (!TestTrue(TEXT("新零半径线检只命中中心线上的实际 Actor"), F.Pawn->TraceAim(F.Pawn->MotionControllerLeftAim, Hit)
        && Hit.GetActor() == Main->GetOwner() && Hit.GetComponent() == Main)) return false;
    TestTrue(TEXT("优先级只在命中 Actor 内选盖，不选离轴高优先级物"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, NeighborGrab) == CapGrab);
    F.Pointer->UpdatePointer(Dt);
    auto* Dot = F.Pointer->GetEndpoint(EControllerHand::Left);
    if (!TestNotNull(TEXT("当前实际视觉端点"), Dot)) return false;
    TestTrue(TEXT("候选、首表面和可见端点一致"), Dot->GetComponentLocation().Equals(Hit.ImpactPoint, .001));
    TestTrue(TEXT("自身新视觉几何不截断下一次目标线检"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, NeighborGrab) == CapGrab);
    TestFalse(TEXT("目标查询没有抓握主物"), BodyGrab->IsHeld() || CapGrab->IsHeld());
    TestFalse(TEXT("目标查询没有拉取邻物"), NeighborGrab->IsPulled());
    F.Counts(*this, 0, 0, 0, 0); F.NoOrderMutation(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerRemoteWallAndCandidateGates,
    "CyberCafe.MenuPointerExperiment.RemoteTarget.WallAndCandidateGates", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerRemoteWallAndCandidateGates::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    FFixture F; if (!F.Begin(*this)) return false;
    const FVector Start(0, 0, 1400);
    F.Pose(EControllerHand::Left, RayPose(Start, FVector::ForwardVector));
    F.Pose(EControllerHand::Right, RayPose(Start + FVector(0, -100, 0), FVector::ForwardVector));
    auto* Front = F.Cube(FTransform(FQuat::Identity, Start + FVector(80, 0, 0), FVector(.1)));
    auto* Behind = F.Cube(FTransform(FQuat::Identity, Start + FVector(120, 0, 0), FVector(.1)));
    auto* Grab = RemoteTargetGrab(Front);
    auto* BehindGrab = RemoteTargetGrab(Behind, 100);
    auto* Wall = F.Cube(FTransform(FQuat::Identity, Start + FVector(40, 0, 0), FVector(.1)));
    if (!TestNotNull(TEXT("前方实际可抓物"), Grab) || !TestNotNull(TEXT("背后高优先级可抓物"), BehindGrab)
        || !TestNotNull(TEXT("前方无 Grab 的真实墙"), Wall)) return false;
    Wall->SetCollisionObjectType(ECC_WorldStatic);
    FHitResult Hit;
    if (!TestTrue(TEXT("Visibility 首先返回原球扫对象过滤漏掉的 WorldStatic 墙"),
        F.Pawn->TraceAim(F.Pawn->MotionControllerLeftAim, Hit) && Hit.GetComponent() == Wall)) return false;
    TestNull(TEXT("不能信旧缓存绕过前方不可抓墙"), F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, BehindGrab));
    Wall->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    TestTrue(TEXT("真正的 Visibility Ignore 响应放行到第一可抓 Actor"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, BehindGrab) == Grab);
    Grab->bAllowRemoteGrab = false;
    TestNull(TEXT("首物禁止远抓时不跳到背后 Actor，也不接受自身缓存"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, Grab));
    Grab->bAllowRemoteGrab = true; Grab->GrabType = EGrabType::None;
    TestNull(TEXT("首物 None 类型拒绝，不跳过几何遮挡"), F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, BehindGrab));
    Grab->GrabType = EGrabType::Free;
    if (!TestTrue(TEXT("通过原组件接口建立真实被拉状态，无推进或输入回调"), Grab->TryPull(EControllerHand::Right) && Grab->IsPulled())) return false;
    TestNull(TEXT("首物已被拉时拒绝缓存，不越过它"), F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, Grab));
    TestTrue(TEXT("只读候选查询不停止原拉取"), Grab->IsPulled());
    Grab->StopPull();
    if (!TestTrue(TEXT("原 Kinematic TryGrab 建立真实持有，不调用 OnGrabPressed"),
        Grab->TryGrab(F.Pawn->MotionControllerRightGrip) && Grab->IsHeld())) return false;
    TestNull(TEXT("首物已持有时拒绝远程目标，不从背后选更高优先级"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, BehindGrab));
    TestTrue(TEXT("查询不换手或释放现有持有"), Grab->IsHeld() && Grab->GetHoldingController() == F.Pawn->MotionControllerRightGrip);
    if (!TestTrue(TEXT("原组件释放成功"), Grab->TryRelease())) return false;
    TestTrue(TEXT("恢复有效状态后仍只选第一 Actor"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, BehindGrab) == Grab);
    F.Counts(*this, 0, 0, 0, 0); F.NoOrderMutation(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerRemoteFreshPoseRejectsCache,
    "CyberCafe.MenuPointerExperiment.RemoteTarget.FreshPoseRejectsCache", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerRemoteFreshPoseRejectsCache::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    FFixture F; if (!F.Begin(*this)) return false;
    const FVector Start(0, 0, 1400);
    auto* A = F.Cube(FTransform(FQuat::Identity, Start + FVector(80, 0, 0), FVector(.1)));
    auto* B = F.Cube(FTransform(FQuat::Identity, Start + FVector(120, 40, 0), FVector(.1)));
    auto* GrabA = RemoteTargetGrab(A);
    auto* GrabB = RemoteTargetGrab(B);
    if (!TestNotNull(TEXT("旧姿态中心线物 A"), GrabA) || !TestNotNull(TEXT("新姿态中心线物 B"), GrabB)) return false;
    F.Pose(EControllerHand::Left, RayPose(Start, FVector::ForwardVector));
    auto* CachedA = F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, nullptr);
    if (!TestTrue(TEXT("旧姿态真实取得 A，不人工伪造缓存命中"), CachedA == GrabA)) return false;
    F.Pawn->TargetGrabComponentLeft = CachedA;
    const uint64 Frame = GFrameCounter;
    F.Pose(EControllerHand::Left, RayPose(Start, B->GetComponentLocation() - Start));
    FHitResult Current;
    if (!TestTrue(TEXT("移动输入姿态后第一实际 Actor 已变 B"),
        F.Pawn->TraceAim(F.Pawn->MotionControllerLeftAim, Current) && Current.GetActor() == B->GetOwner())) return false;
    TestTrue(TEXT("不更新 Tick 高亮缓存，调用仍重新取 B"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, F.Pawn->TargetGrabComponentLeft) == GrabB);
    TestTrue(TEXT("纯查询不写入原高亮缓存"), F.Pawn->TargetGrabComponentLeft == CachedA);
    B->SetWorldLocation(Start + FVector(120, 80, 0), false, nullptr, ETeleportType::TeleportPhysics);
    TestNull(TEXT("几何再移出当前射线时不返回旧 B 或旧 A"),
        F.Pawn->GetGrabComponentUnderAim(F.Pawn->MotionControllerLeftAim, GrabB));
    TestEqual(TEXT("重查不合成输入帧"), GFrameCounter, Frame);
    F.Counts(*this, 0, 0, 0, 0); F.NoOrderMutation(*this);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerRemoteMenuPlaneBlocksCache,
    "CyberCafe.MenuPointerExperiment.RemoteTarget.MenuPlaneBlocksCache", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerRemoteMenuPlaneBlocksCache::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    for (EControllerHand Hand : {EControllerHand::Left, EControllerHand::Right})
    for (bool bBlank : {false, true})
    {
        FFixture F; if (!F.Begin(*this) || !F.Open(*this) || !F.AimAction(*this, Hand)) return false;
        const FVector Point = bBlank ? PixelPoint(F.Screen, FVector2D(1175, 725)) : ActionPoint(F.Screen, 1);
        const FVector Normal = F.Screen->Panel->GetForwardVector();
        F.Pose(Hand, PanelPose(F.Screen, Point));
        auto* Behind = F.Cube(FTransform(FQuat::Identity, Point - Normal * 40., FVector(.1)));
        auto* Grab = RemoteTargetGrab(Behind);
        if (!TestNotNull(TEXT("真实无碰撞 UI 后的可远抓 Cube"), Grab)) return false;
        auto* Aim = Hand == EControllerHand::Right ? F.Pawn->MotionControllerRightAim.Get() : F.Pawn->MotionControllerLeftAim.Get();
        FHitResult Shared, PawnHit;
        if (!TestTrue(TEXT("共享表面与 Pawn 都返回当前 UI Actor/Panel，不穿透到后物"),
            F.Pointer->TraceSurfaceForHand(Hand == EControllerHand::Right, Shared) && F.Pawn->TraceAim(Aim, PawnHit)
            && Shared.GetActor() == F.Screen && PawnHit.GetActor() == F.Screen && PawnHit.GetComponent() == F.Screen->Panel)) return false;
        TestTrue(TEXT("Pawn 表面与同手 helper 起点、终点、命中点同源"),
            PawnHit.TraceStart.Equals(Shared.TraceStart, .001) && PawnHit.TraceEnd.Equals(Shared.TraceEnd, .001)
            && PawnHit.ImpactPoint.Equals(Shared.ImpactPoint, .001));
        TestNull(bBlank ? TEXT("空白菜单也挡住缓存中的后物") : TEXT("真实菜单动作面挡住缓存中的后物"),
            F.Pawn->GetGrabComponentUnderAim(Aim, Grab));
        F.Pointer->UpdatePointer(Dt);
        auto* Dot = F.Pointer->GetEndpoint(Hand);
        if (!TestNotNull(TEXT("同手可见端点"), Dot)) return false;
        TestTrue(TEXT("可见端点与远程第一表面完全一致"), Dot->GetComponentLocation().Equals(PawnHit.ImpactPoint, .001));
        TestFalse(TEXT("UI 遮挡查询不拉取背面物体"), Grab->IsPulled());
        F.Counts(*this, 0, 0, 0, 0); F.NoOrderMutation(*this);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPointerRemoteHandRangeAndNullPointer,
    "CyberCafe.MenuPointerExperiment.RemoteTarget.HandRangeAndNullPointer", CyberCafeMenuPointerExperimentTests::Flags)
bool FMenuPointerRemoteHandRangeAndNullPointer::RunTest(const FString&)
{
    using namespace CyberCafeMenuPointerExperimentTests;
    for (EControllerHand Hand : {EControllerHand::Left, EControllerHand::Right})
    {
        FFixture F; if (!F.Begin(*this)) return false;
        const FVector Start(0, 0, 1400), GripStart(0, 500, 1400);
        const FTransform AimPose = RayPose(Start, FVector::ForwardVector);
        const FTransform GripPose = RayPose(GripStart, FVector::ForwardVector);
        F.Pose(Hand, AimPose);
        auto* Aim = Hand == EControllerHand::Right ? F.Pawn->MotionControllerRightAim.Get() : F.Pawn->MotionControllerLeftAim.Get();
        auto* Grip = Hand == EControllerHand::Right ? F.Pawn->MotionControllerRightGrip.Get() : F.Pawn->MotionControllerLeftGrip.Get();
        auto* Mesh = F.Cube(FTransform(FQuat::Identity, Start + FVector(300, 0, 0), FVector(.1)));
        auto* Grab = RemoteTargetGrab(Mesh);
        if (!TestNotNull(TEXT("真实超距可远抓物"), Grab)) return false;
        F.Pointer->RayMaxDistance = 10000.f;
        FHitResult Hit;
        TestFalse(TEXT("共享远程表面仍限制250cm，不保留旧100m能力"), F.Pawn->TraceAim(Aim, Hit));
        TestNull(TEXT("超距物缓存不能越过250cm"), F.Pawn->GetGrabComponentUnderAim(Aim, Grab));
        Mesh->SetWorldLocation(Start + FVector(80, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        if (!TestTrue(TEXT("移到实际线内后可命中"), F.Pawn->TraceAim(Aim, Hit) && Hit.GetComponent() == Mesh)) return false;
        TestTrue(TEXT("真实 Aim 作为共享起点且 TraceEnd 为250cm"), Hit.TraceStart.Equals(Start, .001)
            && Hit.TraceEnd.Equals(Start + FVector(250, 0, 0), .001));
        F.Tracking.Set(Grip->MotionSource, GripPose.GetRelativeTransform(Grip->GetAttachParent()->GetComponentTransform()), true);
        Grip->TickComponent(Dt, LEVELTICK_All, nullptr);
        if (!TestTrue(TEXT("同手 Grip 原生追踪姿态与 Aim 确实不同"), Grip->IsTracked()
            && Grip->GetComponentLocation().Equals(GripStart, .001))) return false;
        TestTrue(TEXT("通过同手 MotionSource 识别 Grip，但仍优先共享真实 Aim"),
            F.Pawn->TraceAim(Grip, Hit) && Hit.GetComponent() == Mesh && Hit.TraceStart.Equals(Start, .001));
        F.Tracking.Set(Aim->MotionSource, FTransform::Identity, false);
        Aim->TickComponent(Dt, LEVELTICK_All, nullptr);
        Mesh->SetWorldLocation(GripStart + FVector(80, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        if (!TestFalse(TEXT("Aim 真实失追，不修改内部 tracking 标志"), Aim->IsTracked())) return false;
        TestTrue(TEXT("传入失追 Aim 时使用 helper 的同手 Grip，而非旧 Aim Origin"),
            F.Pawn->TraceAim(Aim, Hit) && Hit.GetComponent() == Mesh && Hit.TraceStart.Equals(GripStart, .001));
        Aim->Deactivate(); Grip->Deactivate();
        TestFalse(TEXT("同手来源全部 inactive 时不另开旧扫描"), F.Pawn->TraceAim(Aim, Hit));
        TestNull(TEXT("inactive 失败清空上次命中"), Hit.GetActor());
        F.Pose(Hand, AimPose, false, false);
        TestFalse(TEXT("同手来源全部 untracked 时返回 helper false"), F.Pawn->TraceAim(Aim, Hit));
        TestNull(TEXT("untracked 失败不留旧 Actor"), Hit.GetActor());
        F.Pose(Hand, AimPose);
        Mesh->SetWorldLocation(Start + FVector(80, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        F.Pointer->bEnabled = false;
        TestTrue(TEXT("仅禁用视觉时仍可裸物理远抓"), F.Pawn->GetGrabComponentUnderAim(Aim, Grab) == Grab);
        Mesh->SetWorldLocation(Start + FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        TestFalse(TEXT("禁用视觉的裸物理射线也只有250cm"), F.Pawn->TraceAim(Aim, Hit));
        Mesh->SetWorldLocation(Start + FVector(80, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        // UE 原生轮询要求本地网络 Owner；组件仍属于外部 Actor，不能冒充 Pawn 组件。
        Mesh->GetOwner()->SetOwner(F.PC);
        auto* Foreign = NewObject<UMotionControllerComponent>(Mesh->GetOwner(), NAME_None, RF_Transient);
        Mesh->GetOwner()->AddInstanceComponent(Foreign); Foreign->SetupAttachment(Mesh);
        Foreign->MotionSource = Aim->MotionSource; Foreign->SetAssociatedPlayerIndex(ControllerId);
        Foreign->RegisterComponent(); Foreign->Activate(); Foreign->TickComponent(Dt, LEVELTICK_All, nullptr);
        if (!TestTrue(TEXT("外部 Actor 的同名 controller 真实注册和追踪"), Foreign->IsRegistered() && Foreign->IsTracked()
            && Foreign->GetOwner() != F.Pawn)) return false;
        TestFalse(TEXT("组件存在时拒绝不属于本 Pawn 的 controller"), F.Pawn->TraceAim(Foreign, Hit));
        TestNull(TEXT("外部来源失败清空 Actor"), Hit.GetActor());
        F.Pawn->MenuPointerComp = nullptr; // 仅本例实例；不销毁组件，不写 CDO 或资产。
        if (!TestTrue(TEXT("组件缺失时从传入 Aim 做真实 Visibility 线检"),
            F.Pawn->TraceAim(Aim, Hit) && Hit.GetComponent() == Mesh && Hit.TraceStart.Equals(Start, .001))) return false;
        F.Tracking.Set(Grip->MotionSource, GripPose.GetRelativeTransform(Grip->GetAttachParent()->GetComponentTransform()), true);
        Grip->TickComponent(Dt, LEVELTICK_All, nullptr);
        Mesh->SetWorldLocation(GripStart + FVector(80, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        TestTrue(TEXT("缺失组件回退严格从传入 Grip 起点，不偷换 Origin"), F.Pawn->TraceAim(Grip, Hit)
            && Hit.GetComponent() == Mesh && Hit.TraceStart.Equals(GripStart, .001)
            && Hit.TraceEnd.Equals(GripStart + FVector(250, 0, 0), .001));
        TestFalse(TEXT("传入 Aim 回退不命中偏离自身射线的 Grip 前物"), F.Pawn->TraceAim(Aim, Hit));
        Mesh->SetWorldLocation(GripStart + FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        TestFalse(TEXT("缺失组件时也不超过250cm"), F.Pawn->TraceAim(Grip, Hit));
        TestFalse(TEXT("缺失组件也拒绝外部 controller"), F.Pawn->TraceAim(Foreign, Hit));
        Grip->Deactivate();
        TestFalse(TEXT("缺失组件不绕过 inactive 门禁"), F.Pawn->TraceAim(Grip, Hit));
        F.Pose(Hand, AimPose, false, false);
        TestFalse(TEXT("缺失组件不绕过 untracked 门禁"), F.Pawn->TraceAim(Aim, Hit));
        TestFalse(TEXT("空 controller 安全失败"), F.Pawn->TraceAim(nullptr, Hit));
        TestNull(TEXT("最终失败结果无旧命中"), Hit.GetActor());
        F.Counts(*this, 0, 0, 0, 0); F.NoOrderMutation(*this);
    }
    return !HasAnyErrors();
}
#endif
