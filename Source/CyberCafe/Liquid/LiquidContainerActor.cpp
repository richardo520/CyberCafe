// Fill out your copyright notice in the Description page of Project Settings.

// @trace LQ-01 LQ-02 LQ-05
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraTypes.h"
#include "NiagaraVariant.h"
#include "NiagaraDataInterfaceExport.h"
#include "GrabComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/CustomerDrinkComponent.h"

ALiquidContainerActor::ALiquidContainerActor()
{
    // 打开 Tick：基类统一驱动"动态波动 + 倒液"两条主流程，子类无需重复
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;

    // 容器外壳作为 Root（模拟物理 + 抓取目标）
    ContainerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContainerMesh"));
    SetRootComponent(ContainerMesh);
    ContainerMesh->SetSimulatePhysics(true);
    ContainerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));

    // 抓取组件（附在 ContainerMesh 上）
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(ContainerMesh);
    GrabComp->GrabType = EGrabType::Snap;
    GrabComp->GrabPriority = 0;

    // P_Liquid Niagara 组件：负责液体网格渲染
    LiquidFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("LiquidFX"));
    LiquidFX->SetupAttachment(ContainerMesh);
    LiquidFX->SetAutoActivate(true);
    LiquidFX->bAutoActivate = true;

    // 预挂载的 P_Ribbon Niagara 组件（默认不自动激活；由 StartPouring 触发）
    PourFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PourFX"));
    PourFX->SetupAttachment(ContainerMesh);
    PourFX->SetAutoActivate(false);
    PourFX->bAutoActivate = false;

    // 杯底锚点：美术在蓝图里拖到杯子内腽底部中心，仅作视觉/调试参考。
    // 真正的接液判定由 PourEntryPoint + PourEntryRadius 完成。
    PourTargetPoint = CreateDefaultSubobject<USceneComponent>(TEXT("PourTargetPoint"));
    PourTargetPoint->SetupAttachment(ContainerMesh);

    // 杯口入口锚点：美术在蓝图里拖到杯口平面中心，搭配 PourEntryRadius 使用。
    // 局部 +Z 需朝向杯口外（默认旋转下就是向上）。
    PourEntryPoint = CreateDefaultSubobject<USceneComponent>(TEXT("PourEntryPoint"));
    PourEntryPoint->SetupAttachment(ContainerMesh);

    // 液体状态默认值
    FillAmount    = 0.5f;
    MaxVolumeML   = 750.f;   // 一瓶红酒 = 750mL
    LiquidColor   = FLinearColor(0.35f, 0.05f, 0.08f, 1.f); // 默认红酒色

    // P_Liquid 表现默认值
    LiquidOpacity = 0.85f;
    AddWaves      = 1.0f;
    WavesScale    = 1.0f;
    Viscosity     = 1.0f;

    // 动态波动默认值（静止时完全平静，抓握晃动时渐起波动）
    bDynamicWaves          = true;
    IdleAddWaves           = 0.0f;
    MaxAddWaves            = 1.0f;
    LinearVelocityRefCms   = 200.f;  // 2 m/s 时达到最大波动
    AngularVelocityRefDegs = 360.f;  // 每秒一圈时达到最大波动
    WavesRiseSpeed         = 6.0f;   // 上升很快（晃一下立即反应）
    WavesDecaySpeed        = 2.5f;   // 下降略慢（停下后逐渐平静）
    CurrentDynamicWaves    = 0.0f;
    PrevLocation           = FVector::ZeroVector;
    PrevRotation           = FQuat::Identity;

    // 默认包围盒仅作内部占位（现已不再自动写入 LiquidFX，User.BottleSize 由美术在 LiquidFX 中配置）

    LiquidFXTemplate    = nullptr;
    LiquidMaterialAsset = nullptr;
    LiquidColorParamName = FName(TEXT("Liquid_Color01"));

    // 倒液（Pour）默认值——瓶子/杯子子类构造里可各自覆盖
    bCanPour                = true;
    bAcceptLiquidFromOthers = false;   // 基类默认不接液，杯子子类构造里改为 true
    // 新版不再使用固定阈值，临界角由 ComputeCriticalTiltAngleDegrees() 基于物理公式自动算出：
    //   满杯  → 0°就洒，空杯  → 约 atan(H/R) 才会倒出。
    // PourAngleTolerance 提前触发的容差，默认 5°。子类/蓝图可覆盖。
    PourAngleTolerance      = 5.f;
    PourRatePerSecond       = 60.f;    // 每秒 60mL
    FlowStrength            = 1.f;
    PourTraceDistance       = 60.f;    // 60cm，足够从桌面高度倒到杯子
    PourTraceRadius         = 6.f;     // 6cm "胖射线"，兜住水流弧度的落点偏差
    PourTraceForwardOffset  = 0.f;
    bDebugDrawTrace         = false;

    // 杯口入口圆盘半径默认 0（未启用接液，对瓶子无影响）；
    // 杯子蓝图中将其设为杯口真实内径（例红酒杯 3cm）即可启用接液。
    PourEntryRadius         = 0.f;

    SplashEffectTemplate    = nullptr;
    bEnableSplash           = true;
    bEnableDecal            = true;
    bNoSplashes             = false;
    bNoList                 = true;

    bIsPouring              = false;

    static ConstructorHelpers::FObjectFinder<USoundBase> DefaultPourSound(
        TEXT("/Game/VRTemplate/Audio/Pouring/S_PourWaterLoop.S_PourWaterLoop"));
    PourLoopSound = DefaultPourSound.Object;
}

void ALiquidContainerActor::BeginPlay()
{
    Super::BeginPlay();
    InitializeLiquidState();
    if (bBroken) return;
    if (GlassBreakProfile && !FindComponentByClass<UGlassBreakableComponent>())
    {
        auto* Breakable=NewObject<UGlassBreakableComponent>(this);
        Breakable->Profile=GlassBreakProfile;AddInstanceComponent(Breakable);Breakable->RegisterComponent();
    }

    // 从 LiquidFX 组件上反查美术配置的液体材质 Override，回填到 C++ 缓存，
    // 作为后续“倒液传材质 / 读取颜色”的唯一数据源。
    ResolveLiquidMaterialFromFX();

    // 启动时尝试从材质里读出液体颜色（覆盖蓝图里默认的 LiquidColor），
    // 这样 PourFX/Splash 拿到的颜色和瓶内液体自然一致。
    TryReadColorFromMaterial();

    // 派生 MID 并绑定到 LiquidFX.User.Material。之后所有"改颜色"都走 MID 参数写入，
    // 让杯子在被倒入不同颜色液体时颜色是平滑渐变，而不是整块材质突变。
    EnsureLiquidMID(/*bForceRecreate=*/false);

    InitLiquidFX();
    RefreshLiquidFX();

    InitPourFX();

    // 初始化上一帧参考位置/旋转
    if (ContainerMesh)
    {
        PrevLocation = ContainerMesh->GetComponentLocation();
        PrevRotation = ContainerMesh->GetComponentQuat();
    }
}

void ALiquidContainerActor::EndPlay(const EEndPlayReason::Type Reason)
{
    StopPouring();
    StopPourAudio(true);
    if (bLiquidStateInitialized && GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            S->UnregisterContainer(this, CurrentVolumeML);
    Super::EndPlay(Reason);
}

void ALiquidContainerActor::Destroyed()
{
    StopPourAudio(true);
    if (bLiquidStateInitialized && GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            S->UnregisterContainer(this, CurrentVolumeML);
    Super::Destroyed();
}

void ALiquidContainerActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (bPourAudioStopOnNextTick) StopPourAudio();
    RestoreReceiveCooldown=FMath::Max(0.f,RestoreReceiveCooldown-DeltaTime);
    if (!bEnablePourAudio || !IsValid(PourLoopSound.Get()) || !GetWorld() ||
        !GetWorld()->AllowAudioPlayback() || !GetWorld()->GetAudioDeviceRaw())
    {
        StopPourAudio(true);
    }
    if (bBroken) return;

    // 1) 根据容器运动状态实时调节液面波动
    UpdateDynamicWaves(DeltaTime);

    // 2) 倒液流程：允许倒液 + 有液体 + 超过“液面物理临界角” → StartPouring；否则 StopPouring
    if (bCanPour)
    {
        const float CriticalAngle = ComputeCriticalTiltAngleDegrees();
        const bool bShouldPour =
            (GetPreciseVolumeML() > 0.0) &&
            (GetTiltAngleDegrees() >= FMath::Max(MinPourTiltDegrees, CriticalAngle - PourAngleTolerance));

        if (bShouldPour && !bIsPouring)
        {
            StartPouring();
        }
        else if (!bShouldPour && (bIsPouring || bPourAudioRequested))
        {
            StopPouring();
        }

        if (bIsPouring)
        {
            UpdatePouring(DeltaTime);
        }
    }
    else if (bIsPouring)
    {
        // 运行时被关掉 bCanPour 时兜底
        StopPouring();
    }
}

//=====================================================================
// 液体 API
//=====================================================================

void ALiquidContainerActor::InitializeLiquidState()
{
    if (bLiquidStateInitialized) return;
    MaxVolumeML = FMath::IsFinite(MaxVolumeML) ? FMath::Max(0.f, MaxVolumeML) : 0.f;
    const double InitialFill = FMath::IsFinite(FillAmount) ? FMath::Clamp(static_cast<double>(FillAmount), 0.0, 1.0) : 0.0;
    CurrentVolumeML = SupportsLiquidInventory() ? InitialFill * MaxVolumeML : 0.0;
    Composition = FLiquidComposition::Pure(InitialIngredientId, CurrentVolumeML);
    bLiquidStateInitialized = true;
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            S->RegisterContainer(this, CurrentVolumeML);
}

double ALiquidContainerActor::GetPreciseVolumeML() const
{
    if (!SupportsLiquidInventory()) return 0.0;
    if (bLiquidStateInitialized) return CurrentVolumeML;
    return FMath::IsFinite(FillAmount) && FMath::IsFinite(MaxVolumeML)
        ? FMath::Clamp(static_cast<double>(FillAmount), 0.0, 1.0) * FMath::Max(0.f, MaxVolumeML) : 0.0;
}

FLiquidComposition ALiquidContainerActor::GetComposition() const
{
    return bLiquidStateInitialized ? Composition : FLiquidComposition::Pure(InitialIngredientId, GetPreciseVolumeML());
}

bool ALiquidContainerActor::BreakContainerForImpact()
{
    if (bLiquidFrozen || bBroken) return false;
    // 先锁定破碎状态，结算完成后才通知外部，防止回调重入重复记账。
    bBroken = true;
    InitializeLiquidState();
    const double RemainingML = CurrentVolumeML;
    DebitVolumeInternal(RemainingML);
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            S->RecordSpilled(RemainingML);

    bCanPour = false;
    bAcceptLiquidFromOthers = false;
    StopPouring();
    StopPourAudio(true);
    if (PourFX)
    {
        PourFX->DeactivateImmediate();
        PourFX->SetVisibility(false);
    }
    NotifyLiquidState();
    return true;
}

void ALiquidContainerActor::FreezeLiquidForServing()
{
    InitializeLiquidState();
    bLiquidFrozen = true;
    bCanPour = false;
    bAcceptLiquidFromOthers = false;
    StopPouring();
    StopPourAudio(true);
}

double ALiquidContainerActor::ConsumeForCustomer(UCustomerDrinkComponent* Consumer,double RequestedML)
{
    if(!IsValid(Consumer)||CustomerConsumer.Get()!=Consumer||Consumer->GetDrinkingCup()!=this||!Consumer->IsDrinking()||!bLiquidFrozen||bBroken||IsActorBeingDestroyed()||!FMath::IsFinite(RequestedML)||RequestedML<=0||CustomerSipBudgetML<=0)return 0;
    auto* S=GetWorld()?GetWorld()->GetSubsystem<ULiquidTransferSubsystem>():nullptr;if(!S)return 0;
    const double Actual=DebitVolumeInternal(FMath::Min(RequestedML,CustomerSipBudgetML));
    CustomerSipBudgetML-=Actual;S->RecordRemoved(Actual);
    // Caller commits its counters before notifying external liquid subscribers.
    return Actual;
}
double ALiquidContainerActor::DebitVolumeInternal(double RequestedML, FLiquidComposition* OutComposition)
{
    if (!SupportsLiquidInventory()) return 0;
    InitializeLiquidState();
    if (!FMath::IsFinite(RequestedML) || RequestedML <= 0) return 0;
    const double Actual = FMath::Min(RequestedML, CurrentVolumeML);
    if (Actual <= 0) return 0;
    const FLiquidComposition Taken = Composition.Portion(Actual);
    if (OutComposition) *OutComposition = Taken;
    Composition.Remove(Taken);
    CurrentVolumeML -= Actual;
    if (CurrentVolumeML == 0) Composition.Ingredients.Empty();
    ++LiquidRevision;
    return Actual;
}

double ALiquidContainerActor::CreditVolumeInternal(double RequestedML, const FLinearColor& InColor, const FLiquidComposition* InComposition)
{
    if (!SupportsLiquidInventory() || bLiquidFrozen || bBroken) return 0;
    InitializeLiquidState();
    if (!FMath::IsFinite(RequestedML) || RequestedML <= 0 || !FMath::IsFinite(InColor.R) || !FMath::IsFinite(InColor.G) || !FMath::IsFinite(InColor.B) || !FMath::IsFinite(InColor.A)) return 0;
    const double Capacity = FMath::IsFinite(MaxVolumeML) ? FMath::Max(0.f, MaxVolumeML) : 0.0;
    const double Actual = FMath::Min(RequestedML, FMath::Max(0.0, Capacity - CurrentVolumeML));
    if (Actual <= 0) return 0;
    const FLiquidComposition Received = InComposition ? InComposition->Portion(Actual) : FLiquidComposition::Pure(TEXT("Unknown"), Actual);
    if (FMath::Abs(Received.Total() - Actual) > 1.e-6) return 0;
    Composition.Add(Received);
    ++LiquidRevision;
    const double Total = CurrentVolumeML + Actual;
    LiquidColor = LiquidColor * static_cast<float>(CurrentVolumeML / Total) + InColor * static_cast<float>(Actual / Total);
    LiquidColor.A = 1.f;
    CurrentVolumeML = Total;
    return Actual;
}

void ALiquidContainerActor::NotifyLiquidState()
{
    RefreshLiquidFX();
    OnLiquidChanged.Broadcast(FillAmount, LiquidColor);
}

void ALiquidContainerActor::SetCapacityML(float NewCapacityML)
{
    if (bLiquidFrozen || bBroken || !FMath::IsFinite(NewCapacityML) || NewCapacityML < 0) return;
    if (!bLiquidStateInitialized)
    {
        // Construction/default setup changes capacity while retaining saved initial
        // fill. Do not lock the old capacity into runtime mL before BeginPlay.
        MaxVolumeML = NewCapacityML;
        RefreshLiquidFX();
        return;
    }
    MaxVolumeML = NewCapacityML;
    const double Excess = DebitVolumeInternal(FMath::Max(0.0, CurrentVolumeML - MaxVolumeML));
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>()) S->RecordSpilled(Excess);
    NotifyLiquidState();
}

float ALiquidContainerActor::AddLiquid(float DeltaML, FLinearColor InColor)
{
    if (bBroken) return 0;
    const double Actual = CreditVolumeInternal(DeltaML, InColor);
    if (Actual <= 0) return 0;
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>()) S->RecordInjected(Actual);
    NotifyLiquidState();
    return static_cast<float>(Actual);
}

float ALiquidContainerActor::AddIngredientLiquid(float DeltaML, FName IngredientId, FLinearColor InColor)
{
    if (bBroken) return 0;
    const FLiquidComposition Added = FLiquidComposition::Pure(IngredientId, DeltaML);
    const double Actual = CreditVolumeInternal(DeltaML, InColor, &Added);
    if (Actual <= 0) return 0;
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>()) S->RecordInjected(Actual);
    NotifyLiquidState();
    return static_cast<float>(Actual);
}

float ALiquidContainerActor::ConsumeLiquid(float DeltaML)
{
    if (bBroken || bLiquidFrozen) return 0;
    const double Actual = DebitVolumeInternal(DeltaML);
    if (Actual <= 0) return 0;
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>()) S->RecordRemoved(Actual);
    NotifyLiquidState();
    return static_cast<float>(Actual);
}

void ALiquidContainerActor::RefreshLiquidFX()
{
    if (bBroken)
    {
        FillAmount = 0.f;
        if (LiquidFX)
        {
            LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"), 0.f);
            LiquidFX->DeactivateImmediate();
            LiquidFX->SetVisibility(false);
        }
        return;
    }
    if (bLiquidStateInitialized)
        FillAmount = MaxVolumeML > 0.f ? static_cast<float>(FMath::Clamp(CurrentVolumeML / MaxVolumeML, 0.0, 1.0)) : 0.f;
    if (!LiquidFX)
    {
        return;
    }

    // 容器空了则隐藏整个液体网格：
    // P_Liquid 内部即使 Fill=0 也会保留一个"最小可见层"，
    // 且普通 Deactivate 只停止 Spawn、已有粒子会走完 lifetime 才消失（视觉上像"倒不干净"）。
    // 因此这里：
    //   1) 先把 User.Fill = 0 写下去，防止残留渲染时还用旧值
    //   2) 用 DeactivateImmediate 立即清除所有存活粒子，让瓶底那点液面瞬间消失
    if (FillAmount <= KINDA_SMALL_NUMBER)
    {
        LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"), 0.f);
        if (LiquidFX->IsActive())
        {
            LiquidFX->DeactivateImmediate();
        }
        return;
    }

    // 从空变非空：重新启动 LiquidFX（bReset=true 确保 Niagara 用当前 User 参数重新生成液面）
    if (!LiquidFX->IsActive())
    {
        LiquidFX->Activate(/*bReset=*/true);
    }

    // 只写入 P_Liquid 定义的 User 参数。
    // 颜色不再通过 Niagara 参数或整块材质替换来切，而是通过 LiquidMID 的
    // Liquid_Color01 向量参数实时写入——这样 AddLiquid 的加权混色结果会自然
    // 反映在杯子液体上，避免"整帧突变"造成的僵硬感。
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"),       FillAmount);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.Opacity"),    LiquidOpacity);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.AddWaves"),   AddWaves);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.WavesScale"), WavesScale);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.Viscosity"),  Viscosity);

    // 把当前混色结果同步到 MID，驱动液体材质颜色平滑过渡。
    ApplyLiquidColorToMID();
}

bool ALiquidContainerActor::IsHeld() const
{
    return GrabComp && GrabComp->IsHeld();
}

//=====================================================================
// 动态波动：根据容器运动强度实时调节 P_Liquid.User.AddWaves
//=====================================================================

void ALiquidContainerActor::UpdateDynamicWaves(float DeltaTime)
{
    if (!LiquidFX)
    {
        return;
    }

    // 关闭动态波动时，直接使用静态 AddWaves
    if (!bDynamicWaves)
    {
        LiquidFX->SetNiagaraVariableFloat(TEXT("User.AddWaves"), AddWaves);
        return;
    }

    // 1) 采集容器当前的线速度与角速度
    float LinearSpeed  = 0.f;
    float AngularSpeed = 0.f;
    if (ContainerMesh)
    {
        // 优先用物理速度（仅模拟物理时有效）
        LinearSpeed = ContainerMesh->GetPhysicsLinearVelocity().Size();
        AngularSpeed = FMath::RadiansToDegrees(ContainerMesh->GetPhysicsAngularVelocityInRadians().Size());

        // 若没上物理（比如被 Snap 到手上），退化用位移差分估算
        if (LinearSpeed < KINDA_SMALL_NUMBER && AngularSpeed < KINDA_SMALL_NUMBER && DeltaTime > KINDA_SMALL_NUMBER)
        {
            const FVector CurLoc = ContainerMesh->GetComponentLocation();
            LinearSpeed = ((CurLoc - PrevLocation) / DeltaTime).Size();
            PrevLocation = CurLoc;

            const FQuat CurRot = ContainerMesh->GetComponentQuat();
            const FQuat DeltaQuat = CurRot * PrevRotation.Inverse();
            FVector Axis; float AngleRad = 0.f;
            DeltaQuat.ToAxisAndAngle(Axis, AngleRad);
            AngleRad = FMath::UnwindRadians(AngleRad);
            AngularSpeed = FMath::RadiansToDegrees(FMath::Abs(AngleRad) / DeltaTime);
            PrevRotation = CurRot;
        }
        else
        {
            PrevLocation = ContainerMesh->GetComponentLocation();
            PrevRotation = ContainerMesh->GetComponentQuat();
        }
    }

    // 2) 将速度归一化到 0~1，取两者最大作为"运动强度"
    const float LinearNorm  = FMath::Clamp(LinearSpeed  / FMath::Max(LinearVelocityRefCms,  1.f), 0.f, 1.f);
    const float AngularNorm = FMath::Clamp(AngularSpeed / FMath::Max(AngularVelocityRefDegs, 1.f), 0.f, 1.f);
    const float MotionNorm  = FMath::Max(LinearNorm, AngularNorm);

    // 3) 目标波动强度：在 [IdleAddWaves, MaxAddWaves] 之间插值
    const float TargetWaves = FMath::Lerp(IdleAddWaves, MaxAddWaves, MotionNorm);

    // 4) 平滑：上升时快、下降时慢，避免抖动
    const bool bRising = TargetWaves > CurrentDynamicWaves;
    const float Speed  = bRising ? WavesRiseSpeed : WavesDecaySpeed;
    const float Alpha  = FMath::Clamp(Speed * DeltaTime, 0.f, 1.f);
    CurrentDynamicWaves = FMath::Lerp(CurrentDynamicWaves, TargetWaves, Alpha);

    // 5) 写入 P_Liquid
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.AddWaves"), CurrentDynamicWaves);
}

//=====================================================================
// 内部：初始化 LiquidFX（P_Liquid User 参数）
//=====================================================================

void ALiquidContainerActor::InitLiquidFX()
{
    if (bBroken || !LiquidFX)
    {
        return;
    }

    // 1) 附上 Niagara 模板（P_Liquid）
    if (LiquidFXTemplate && LiquidFX->GetAsset() != LiquidFXTemplate)
    {
        LiquidFX->SetAsset(LiquidFXTemplate);
    }

    // 注意：User.Mesh / User.Material / User.BottleSize 不再在此处写入，
    // 美术直接在蓝图的 LiquidFX 组件 Details 面板上配置即可（可在蓝图编辑器实时预览）。
    // C++ 仅在 SetLiquidMaterialAsset（倒液到杯子时）主动覆写 User.Material。

    // 2) 重启以确保 Niagara 与美术在组件上配置的 Override 正常生效
    LiquidFX->ReinitializeSystem();
}

void ALiquidContainerActor::ResolveLiquidMaterialFromFX()
{
    if (!LiquidFX)
    {
        return;
    }

    // 从 Niagara 组件反查美术在 Details 面板上为 User.Material 指定的 Override。
    // FindParameterOverride 会查 InstanceParameterOverrides + TemplateParameterOverrides，
    // 覆盖美术在蓝图 LiquidFX 组件里配置的所有情形。
    // 使用精确的 GetUMaterialDef() 类型，与 P_Liquid 里 User.Material 的类型声明保持一致，
    // 也与写入端 SetVariableMaterial 使用的类型对齐。
    const FNiagaraVariableBase UserMaterialVar(FNiagaraTypeDefinition::GetUMaterialDef(), FName(TEXT("User.Material")));
    const FNiagaraVariant Variant = LiquidFX->FindParameterOverride(UserMaterialVar);
    if (Variant.IsValid())
    {
        if (UMaterialInterface* Resolved = Cast<UMaterialInterface>(Variant.GetUObject()))
        {
            LiquidMaterialAsset = Resolved;
        }
    }
}

//=====================================================================
// 内部：初始化 PourFX（P_Ribbon User 参数）
//=====================================================================

void ALiquidContainerActor::InitPourFX()
{
    if (!PourFX)
    {
        return;
    }

    // 安全检查：提醒美术在蓝图 PourFX 组件的 Niagara Asset 一栏指定 P_Ribbon
    if (!PourFX->GetAsset())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[%s] PourFX 未指定 Niagara Asset，请在蓝图选中 PourFX 组件，Details → Niagara → Asset 里指定 P_Ribbon。"),
            *GetName());
    }

    // 预写入静态参数
    PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),        LiquidColor);
    PourFX->SetNiagaraVariableFloat      (TEXT("User.FlowStrength"), FlowStrength);
    PourFX->SetNiagaraVariableBool       (TEXT("User.NoSplashes"),   bNoSplashes);
    PourFX->SetNiagaraVariableBool       (TEXT("User.NoList"),       bNoList);
    PourFX->SetNiagaraVariableBool       (TEXT("User.Decal"),        bEnableDecal);

    // 将本 Actor 注册为 P_Ribbon 里“Export Particle Data to Blueprint”模块的
    // CallbackHandler——仅保留旧资产接口兼容，不再由粒子决定液量；数据仍会回调
    // 到 ReceiveParticleData_Implementation。
    // 注意：不同的 Niagara 资产可能把 Handler Parameter 绑定到不同名字上，
    // 这里一次写入三个最常见命名。Niagara 对不存在的 User 参数会静默忽略，
    // 不会报错，也不会重复回调（回调取决于 Export DI 实际绑定的那个）。
    PourFX->SetNiagaraVariableObject(TEXT("User.Data"),            this);
    PourFX->SetNiagaraVariableObject(TEXT("User.Handler"),         this);
    PourFX->SetNiagaraVariableObject(TEXT("User.CallbackHandler"), this);

    if (bUseRibbonTrajectory && PourFX->GetAsset() && PourFX->GetAsset()->GetFName() == TEXT("P_Ribbon"))
    {
        PourFX->SetNiagaraVariableFloat(TEXT("User.Velocity"), PourRibbonVelocity);
        PourFX->SetNiagaraVariableFloat(TEXT("User.Offset"), PourRibbonOffset);
    }
    // 默认关闭，要倒液时才 Activate
    PourFX->Deactivate();
}

//=====================================================================
// 出液口变换 / 倾角
//=====================================================================

FTransform ALiquidContainerActor::GetPourWorldTransform() const
{
    // Lowest lip point is the shared origin. +X stays aligned with mouth +Z.
    // The exported P_Ribbon uses age-driven displacement along +X; its first
    // 0.15s starts behind the lip. Do not assume generic -X particle emission.

    if (PourEntryPoint && PourEntryRadius > KINDA_SMALL_NUMBER)
    {
        const FTransform EntryTM = PourEntryPoint->GetComponentTransform();
        const FVector C = EntryTM.GetLocation();
        // 口沿平面法线（PourEntryPoint 局部 +Z 的世界方向）—— 用于计算最低点方向 D，
        // 【不再】作为 PourFX 的 +Z 轴（那样会让水柱沿杯身轴方向斜射出，见下方注释）。
        const FVector N = EntryTM.GetUnitAxis(EAxis::Z).GetSafeNormal();

        // 世界 -Z 在口沿平面上的投影：D = (-Up) - dot(-Up, N) * N
        //   D 表示"沿口沿哪个方向重力最先把液面拉下去"，
        //   将 D 单位化后放大 PourEntryRadius，就是圆环上世界 Z 最低点相对圆心的位移。
        const FVector Down = -FVector::UpVector;
        FVector D = Down - FVector::DotProduct(Down, N) * N;

        const float DLen = D.Size();
        // 阈值 0.05 ≈ 容器口沿与水平面夹角约 3°，太小则最低点不稳定，回退旧逻辑
        if (DLen > 0.05f)
        {
            D /= DLen; // 单位化 —— D 本身就是"水沿口沿自然流出"的方向向量

            // 圆环上世界 Z 最低点——水柱的起点
            const FVector Lowest = C + D * PourEntryRadius;

            // Match the existing Ribbon owner axis; build an orthonormal basis.
            const FVector AxisX = N;
            FVector AxisY = FVector::CrossProduct(N, D);
            if (!AxisY.Normalize())
            {
                // 理论不可达：D 与 N 由构造保证正交，此处仅兜底
                AxisY = FVector::CrossProduct(N, FVector::ForwardVector).GetSafeNormal();
                if (AxisY.IsNearlyZero())
                {
                    AxisY = FVector::RightVector;
                }
            }
            const FVector AxisZ = FVector::CrossProduct(AxisX, AxisY).GetSafeNormal();

            const FMatrix RotMat(AxisX, AxisY, AxisZ, FVector::ZeroVector);
            const FQuat Rot(RotMat);

            return FTransform(Rot, Lowest, FVector::OneVector);
        }
    }

    // 回退：直接使用 PourFX 的世界 Transform——美术在蓝图里拖动的位置就是出液口。
    if (PourFX)
    {
        return PourFX->GetComponentTransform();
    }
    // 二级 fallback：PourFX 意外不存在时退回 Root
    if (ContainerMesh)
    {
        return ContainerMesh->GetComponentTransform();
    }
    return GetActorTransform();
}

float ALiquidContainerActor::GetTiltAngleDegrees() const
{
    if (!ContainerMesh)
    {
        return 0.f;
    }
    // 容器局部 +Z 在世界空间的方向
    const FVector UpWS = ContainerMesh->GetUpVector();
    const float Dot = FVector::DotProduct(UpWS.GetSafeNormal(), FVector::UpVector);
    const float AngleRad = FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f));
    return FMath::RadiansToDegrees(AngleRad);
}

float ALiquidContainerActor::ComputeCriticalTiltAngleDegrees() const
{
    // PourEntryRadius 未启用 → 无法建立液面几何模型，保守拒绝倒液（返回 90°）
    if (PourEntryRadius <= KINDA_SMALL_NUMBER)
    {
        return 90.f;
    }

    // 通过 PourEntryPoint 与 PourTargetPoint 的相对位置，自动算出“液柱内腔高度”：
    //   把杯底点转到杯口锚点的局部空间，取其 |Z| 作为沿容器主轴方向的距离。
    //   —— 这样即便美术给 PourEntryPoint 加了一点旋转，也能正确沿"杯口朝外方向"度量。
    float LiquidColumnHeight = 0.f;
    if (PourEntryPoint && PourTargetPoint)
    {
        const FVector TargetWS = PourTargetPoint->GetComponentLocation();
        const FVector TargetInEntryLS =
            PourEntryPoint->GetComponentTransform().InverseTransformPositionNoScale(TargetWS);
        LiquidColumnHeight = FMath::Abs(TargetInEntryLS.Z);
    }

    // 两锚点重合或缺失 → 内腔高度为 0，视为"满杯语义"（任何倾斜都会溢出，返回 0°）
    if (LiquidColumnHeight <= KINDA_SMALL_NUMBER)
    {
        return 0.f;
    }

    // 液面开始溢出的临界角度（正圆柱几何模型）：
    //   tan(θ) = 剩余高度 / 杯口半径
    //   剩余高度 = LiquidColumnHeight * (1 - FillAmount)
    const float RemainingHeight = LiquidColumnHeight * FMath::Max(1.f - FillAmount, 0.f);
    const float AngleRad = FMath::Atan2(RemainingHeight, PourEntryRadius);
    return FMath::RadiansToDegrees(AngleRad);
}

//=====================================================================
// 倒液开始 / 结束
//=====================================================================

void ALiquidContainerActor::StartPouring()
{
    if (bBroken) return;
    bIsPouring = true;

    if (PourFX)
    {
        // 【重要】把 PourFX 组件放到动态出液口位置。
        // 由于 GetPourWorldTransform() 现在会在 PourEntryPoint 的圆环上算出
        // 世界 Z 最低点，我们需要在启动瞬间就把 PourFX 摆过去，避免第一帧闪现
        // 在旧的固定 Transform 位置上（尤其在容器已经倾斜时视觉突兀）。
        //
        // 使用 SetWorldLocationAndRotation：只覆盖位置和朝向、保留美术在组件上
        // 配置的相对缩放；同时 PourFX 仍然 attach 在 ContainerMesh 上，运动跟随
        // 容器整体走。
        //   bSweep = false ：不做碰撞扫描，避免"扫到杯壁"被卡回原地。
        //   Teleport = TeleportPhysics ：让 Niagara 的运动插值把 Previous/Current
        //     Transform 都对齐到新位置，防止粒子在旧位置继续被 spawn 出来。
        const FTransform PourTM = GetPourWorldTransform();
        PourFX->SetWorldLocationAndRotation(PourTM.GetLocation(), PourTM.GetRotation(),
            /*bSweep=*/false, /*OutHit=*/nullptr, ETeleportType::TeleportPhysics);

        // 重新同步一次颜色/强度/开关，防止编辑器运行时改变后未生效
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),        LiquidColor);
        PourFX->SetNiagaraVariableFloat      (TEXT("User.FlowStrength"), FlowStrength);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoSplashes"),   bNoSplashes);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoList"),       bNoList);
        PourFX->SetNiagaraVariableBool       (TEXT("User.Decal"),        bEnableDecal);
        PourFX->SetNiagaraVariableObject     (TEXT("User.Data"),         this);

        // Resume without killing the already emitted visual tail.
        PourFX->Activate(/*bReset=*/false);
    }
}

void ALiquidContainerActor::StopPouring()
{
    ++PourStopGeneration;
    bIsPouring = false;
    if (!bPreserveLastEmissionAudio) StopPourAudio();

    if (PourFX)
    {
        // 不销毁，只停止 Spawn；已在飞的 Ribbon 粒子按 lifetime 自然消散，
        // 视觉上更贴近真实的"水流断尾"过渡。下次 Activate 可直接重启。
        PourFX->Deactivate();
    }
}

UAudioComponent* ALiquidContainerActor::GetPourAudioComponent() const
{
    return PourAudioComponent.Get();
}

void ALiquidContainerActor::UpdatePourAudio(double ActualFlowMLPerSecond, const FTransform& SourceTransform)
{
    UWorld* World = GetWorld();
    if (!IsValid(this) || IsActorBeingDestroyed() || bLiquidFrozen || bBroken ||
        !bEnablePourAudio || !IsValid(PourLoopSound.Get()) || !IsValid(ContainerMesh.Get()) ||
        !World || !World->AllowAudioPlayback() || !World->GetAudioDeviceRaw())
    {
        StopPourAudio(true);
        return;
    }
    if (!bIsPouring || !FMath::IsFinite(ActualFlowMLPerSecond) || ActualFlowMLPerSecond <= 0.0 ||
        SourceTransform.GetLocation().ContainsNaN() || SourceTransform.GetRotation().ContainsNaN())
    {
        StopPourAudio();
        return;
    }

    if (!IsValid(PourAudioComponent.Get()))
    {
        PourAudioComponent = NewObject<UAudioComponent>(this, NAME_None, RF_Transient);
        PourAudioComponent->bAutoActivate = false;
        PourAudioComponent->bAutoDestroy = false;
        PourAudioComponent->SetUISound(false);
        PourAudioComponent->bAllowSpatialization = true;
        PourAudioComponent->bOverrideAttenuation = true;
        FSoundAttenuationSettings Attenuation;
        Attenuation.bAttenuate = true;
        Attenuation.bSpatialize = true;
        Attenuation.AttenuationShape = EAttenuationShape::Sphere;
        Attenuation.AttenuationShapeExtents = FVector(35.f);
        Attenuation.FalloffDistance = 180.f;
        Attenuation.SpatializationAlgorithm = SPATIALIZATION_Default;
        PourAudioComponent->AttenuationOverrides = Attenuation;
        PourAudioComponent->SetupAttachment(ContainerMesh.Get());
        PourAudioComponent->SetAbsolute(false, false, true);
        AddInstanceComponent(PourAudioComponent.Get());
        PourAudioComponent->RegisterComponent();
        bPourAudioRunning = false;
    }

    const float ReferenceFlow = FMath::Clamp(FMath::IsFinite(PourAudioReferenceFlow) ? PourAudioReferenceFlow : 40.f, 0.01f, 1000000.f);
    const double FlowRatio = ActualFlowMLPerSecond / ReferenceFlow;
    const float BaseVolume = FMath::Clamp(FMath::IsFinite(PourAudioVolume) ? PourAudioVolume : 0.45f, 0.f, 1.f);
    const float Volume = BaseVolume * FMath::Sqrt(static_cast<float>(FMath::Clamp(FlowRatio, 0.1, 1.25)));
    const float Pitch = 0.94f + 0.1f * static_cast<float>(FMath::Clamp(FlowRatio, 0.0, 1.0));
    PourAudioComponent->SetWorldTransform(FTransform(SourceTransform.GetRotation(), SourceTransform.GetLocation(), FVector::OneVector),
        false, nullptr, ETeleportType::TeleportPhysics);
    PourAudioComponent->SetPitchMultiplier(Pitch);
    bPourAudioRequested = true;
    PourAudioActualFlow = ActualFlowMLPerSecond;

    // 使用自己的请求门禁；没有实际 voice 时也不按 IsPlaying 每帧重启。
    if (!bPourAudioRunning || PourAudioComponent->Sound.Get() != PourLoopSound.Get())
    {
        PourAudioComponent->Stop();
        PourAudioComponent->SetSound(PourLoopSound.Get());
        const float FadeInSeconds = FMath::Clamp(FMath::IsFinite(PourAudioFadeIn) ? PourAudioFadeIn : 0.06f, 0.f, 10.f);
        // VolumeMultiplier carries the flow gain; FadeIn fades to unity so it
        // does not multiply the configured flow volume a second time.
        PourAudioComponent->SetVolumeMultiplier(Volume);
        PourAudioComponent->FadeIn(FadeInSeconds, 1.f, FMath::FRandRange(0.f, 0.5f));
        ++AudioStartCount;
        bPourAudioRunning = true;
    }
    else
    {
        PourAudioComponent->SetVolumeMultiplier(Volume);
    }
}

void ALiquidContainerActor::StopPourAudio(bool bImmediate)
{
    bPourAudioStopOnNextTick = false;
    bPourAudioRequested = false;
    PourAudioActualFlow = 0.0;
    const bool bWasRunning = bPourAudioRunning;
    bPourAudioRunning = false;
    if (!IsValid(PourAudioComponent.Get())) return;
    if (bImmediate)
    {
        PourAudioComponent->Stop();
    }
    else if (bWasRunning)
    {
        const float FadeOutSeconds = FMath::Clamp(FMath::IsFinite(PourAudioFadeOut) ? PourAudioFadeOut : 0.12f, 0.f, 10.f);
        PourAudioComponent->FadeOut(FadeOutSeconds, 0.f);
    }
}

//=====================================================================
// 倒液 Tick 逻辑
//=====================================================================

void ALiquidContainerActor::UpdatePouring(float DeltaTime)
{
    if (bBroken || !ContainerMesh)
    {
        StopPourAudio(bBroken);
        return;
    }

    // 每帧把 PourFX 跟随到当前"口沿最低点"——这样倾斜姿态改变时，
    // 水柱起点会沿着杯口/瓶口边沿平滑滑动，视觉上就是从最低处流下。
    if (PourFX)
    {
        const FTransform PourTM = GetPourWorldTransform();
        // 见 StartPouring() 中的说明：bSweep=false + TeleportPhysics，
        // 避免碰撞卡回原位、且让 Niagara 的运动插值同步到新位置。
        PourFX->SetWorldLocationAndRotation(PourTM.GetLocation(), PourTM.GetRotation(),
            /*bSweep=*/false, /*OutHit=*/nullptr, ETeleportType::TeleportPhysics);

#if ENABLE_DRAW_DEBUG
        if (bDebugDrawTrace)
        {
            // ============================================================
            // 出液口可视化（用于排查"起点在中心 / 方向不对"两类问题）
            //   — 青色大球 : GetPourWorldTransform() 计算出来的目标位置（口沿最低点）
            //   — 白色小球 : PourFX 组件的实际世界位置（若两者重合说明 SetWorldLocation 生效）
            //   — 红/绿/蓝短线 : PourFX 组件的世界 +X / +Y / +Z 轴（RGB 惯例）
            //     水柱视觉方向若发现"顺着蓝线飞出"，说明 P_Ribbon 沿组件 +Z 发射；
            //     若"顺着红线飞出"，则是沿组件 +X 发射——据此可以反推 GetPourWorldTransform
            //     里到底哪根轴需要指向重力向下。
            // ============================================================
            if (UWorld* W = GetWorld())
            {
                const FVector TargetLoc = PourTM.GetLocation();
                const FVector ActualLoc = PourFX->GetComponentLocation();
                DrawDebugSphere(W, TargetLoc, 1.6f, 12, FColor::Cyan,   false, 0.f, 0, 0.4f);
                DrawDebugSphere(W, ActualLoc, 1.0f, 8,  FColor::White,  false, 0.f, 0, 0.4f);

                const FTransform ActualTM = PourFX->GetComponentTransform();
                const FVector AxX = ActualTM.GetUnitAxis(EAxis::X);
                const FVector AxY = ActualTM.GetUnitAxis(EAxis::Y);
                const FVector AxZ = ActualTM.GetUnitAxis(EAxis::Z);
                const float L = 6.f;
                DrawDebugLine(W, ActualLoc, ActualLoc + AxX * L, FColor::Red,   false, 0.f, 0, 0.4f);
                DrawDebugLine(W, ActualLoc, ActualLoc + AxY * L, FColor::Green, false, 0.f, 0, 0.4f);
                DrawDebugLine(W, ActualLoc, ActualLoc + AxZ * L, FColor::Blue,  false, 0.f, 0, 0.4f);
            }
        }
#endif
    }

    // 更新 Niagara 的实时颜色(液体颜色会因混色变化)
    if (PourFX)
    {
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"), LiquidColor);
        if (bUseRibbonTrajectory && PourFX->GetAsset() && PourFX->GetAsset()->GetFName() == TEXT("P_Ribbon"))
        {
            PourFX->SetNiagaraVariableFloat(TEXT("User.Velocity"), PourRibbonVelocity);
            PourFX->SetNiagaraVariableFloat(TEXT("User.Offset"), PourRibbonOffset);
        }
    }

    // One transaction debits the source and credits a precise in-flight packet.
    double Emitted = 0.0;
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            Emitted += S->EmitPour(this, DeltaTime, GetPourWorldTransform());
    if (!IsValid(this) || IsActorBeingDestroyed()) return;
    if (bIsPouring && bCanPour && !bLiquidFrozen && !bBroken)
    {
        const double ActualRate = FMath::IsFinite(DeltaTime) && DeltaTime > 0.f ? Emitted / DeltaTime : 0.0;
        UpdatePourAudio(ActualRate, GetPourWorldTransform());
    }
    if (GetPreciseVolumeML() <= 0.0)
    {
        // Keep the audio of the final actual emission until the next empty Tick,
        // while preserving the existing immediate end of the Niagara spawn state.
        TGuardValue<bool> HoldLastAudio(bPreserveLastEmissionAudio, Emitted > 0.0 && bIsPouring && bCanPour && !bLiquidFrozen && !bBroken);
        StopPouring();
        bPourAudioStopOnNextTick = bPreserveLastEmissionAudio && bPourAudioRequested;
    }
}

//=====================================================================
// 材质相关：一键换材质 + 从材质读颜色
//=====================================================================

void ALiquidContainerActor::SetLiquidMaterialAsset(UMaterialInterface* NewMaterial, bool bReadColor)
{
    if (bBroken || !NewMaterial)
    {
        return;
    }

    // ==============================================================
    // 平滑过渡策略（方案 A：MID + 颜色参数插值）
    //
    // 现在杯子液体的外观由 LiquidMID + 其 Liquid_Color01 参数共同决定。
    // 大部分"倒酒进杯"场景下，我们不再整块替换材质，只依靠 AddLiquid 的
    // 体积加权混色更新 LiquidColor，然后由 RefreshLiquidFX → ApplyLiquidColorToMID
    // 把颜色平滑写进 MID，避免"第一颗粒子命中就整杯变色"的僵硬感。
    //
    // 两种情形分别处理：
    //   1) 目标杯子几乎为空（FillAmount ≈ 0）——这是"换酒"场景，允许整体切换：
    //      - 更新 LiquidMaterialAsset
    //      - 用新材质派生一个新 MID 并绑到 LiquidFX.User.Material
    //      - 从材质读色一次（bReadColor）
    //      - 由 RefreshLiquidFX 走一遍参数刷新（不做 Reinitialize，Fill=0 时它会
    //        DeactivateImmediate，等下一颗粒子加液再 Activate 自然重启）
    //   2) 目标杯子已经有液体——保持当前 MID 不动，只做"目标色引导"：
    //      - 从新材质读一次目标颜色，但只用来作为"下一次混色的另一头"，不覆盖当前混色
    //        （颜色实际过渡完全由 AddLiquid 的加权公式驱动）
    //      - 不替换 User.Material、不 Reinitialize，Niagara 不会闪一下
    //
    // 注：本函数依然可以被蓝图强行调用来"硬切材质"（例如清空杯子/道具重置），
    //     调用前把 FillAmount 置 0 即可走情形 1。
    // ==============================================================

    const bool bTargetEmpty = (FillAmount <= KINDA_SMALL_NUMBER);

    if (bTargetEmpty)
    {
        // 情形 1：空杯换酒——允许整体切换材质
        LiquidMaterialAsset = NewMaterial;

        if (bReadColor)
        {
            TryReadColorFromMaterial();
        }

        // 用新材质派生 MID 并绑定到 LiquidFX.User.Material
        EnsureLiquidMID(/*bForceRecreate=*/true);
    }
    else
    {
        // 情形 2：杯里已经有液体——只更新"数据源材质"的引用（用于 PourFX/Splash 读色引导），
        //         不动 LiquidMID、不动 User.Material，颜色由 AddLiquid 混色 + RefreshLiquidFX 写 MID 自然过渡。
        LiquidMaterialAsset = NewMaterial;
        // 注意：这里刻意不再调用 TryReadColorFromMaterial()——那会把当前混色结果冲掉，
        //       导致杯子颜色"瞬跳到源色"，正是我们要避免的僵硬感来源。
    }

    // 广播一次事件，便于蓝图侧联动（例如刷新 3D UI 上的酒名）
    OnLiquidChanged.Broadcast(FillAmount, LiquidColor);
}

void ALiquidContainerActor::EnsureLiquidMID(bool bForceRecreate)
{
    if (bBroken || !LiquidFX || !LiquidMaterialAsset)
    {
        return;
    }

    // 已有 MID 且父级仍是当前 LiquidMaterialAsset —— 直接复用
    if (!bForceRecreate && LiquidMID && LiquidMID->Parent == LiquidMaterialAsset)
    {
        return;
    }

    // 用当前 LiquidMaterialAsset 派生一个新 MID
    LiquidMID = UMaterialInstanceDynamic::Create(LiquidMaterialAsset, this);
    if (!LiquidMID)
    {
        return;
    }

    // 立即把当前 LiquidColor 写进 MID，防止第一帧出现"MID 用父级默认色"的闪烁
    LiquidMID->SetVectorParameterValue(LiquidColorParamName, LiquidColor);

    // 绑定到 P_Liquid.User.Material —— 使用与旧 SetLiquidMaterialAsset 一致的 SetVariableMaterial 路径，
    // 它会正确匹配 Niagara 里 User.Material 的类型声明，并触发 PSO 重编译。
    LiquidFX->SetVariableMaterial(FName(TEXT("User.Material")), LiquidMID);

    // 材质换了根，Niagara 需要重启一次让 Mesh Renderer 立刻用新 MID 渲染。
    // 只有在"派生了新 MID"这条路径上才需要重启——常规颜色渐变不再走这里。
    LiquidFX->ReinitializeSystem();
}

void ALiquidContainerActor::ApplyLiquidColorToMID()
{
    if (!LiquidMID || LiquidColorParamName.IsNone())
    {
        return;
    }

    LiquidMID->SetVectorParameterValue(LiquidColorParamName, LiquidColor);
}

void ALiquidContainerActor::GetPourTraceIgnoreActors(TArray<AActor*>& OutActors) const
{
    // 默认实现：什么都不加。子类可 override 追加需要忽略的 Actor。
    // （示例：ABottleActor 会追加它的瓶盖 Actor，避免拧下的盖子挡住倒液射线。）
}

bool ALiquidContainerActor::TryReadColorFromMaterial()
{
    if (!LiquidMaterialAsset || LiquidColorParamName.IsNone())
    {
        return false;
    }

    // UMaterialInterface::GetVectorParameterValue 对 MI / M 都能用；
    // 参数不存在时返回 false，不修改 OutValue。
    FLinearColor Out;
    if (LiquidMaterialAsset->GetVectorParameterValue(FMaterialParameterInfo(LiquidColorParamName), Out))
    {
        LiquidColor = Out;
        LiquidColor.A = 1.f;
        return true;
    }
    return false;
}

// Niagara exports remain connected for asset compatibility, but never own volume.
void ALiquidContainerActor::ReceiveParticleData_Implementation(
    const TArray<FBasicParticleData>& Data,
    UNiagaraSystem* NiagaraSystem,
    const FVector& SimulationPositionOffset)
{
    // @trace LQ-05. Actual reception is settled by LiquidTransferSubsystem.
}
