#include "Liquid/LiquidContainerActor.h"
// Fill out your copyright notice in the Description page of Project Settings.

// @trace LQ-01 LQ-02 LQ-05
#include "Liquid/LiquidConsumptionComponent.h"
#include "Liquid/LiquidPresentationComponent.h"
#include "Liquid/LiquidAudioComponent.h"
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
#include "Core/Events/CyberCafeEventSubsystem.h"


ALiquidContainerActor::ALiquidContainerActor()
{
    // 打开 Tick：基类统一驱动"动态波动 + 倒液"两条主流程，子类无需重复
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    Inventory = CreateDefaultSubobject<ULiquidInventoryComponent>(TEXT("LiquidInventory"));
    Consumption = CreateDefaultSubobject<ULiquidConsumptionComponent>(TEXT("LiquidConsumption"));
    Presentation = CreateDefaultSubobject<ULiquidPresentationComponent>(TEXT("LiquidPresentation"));
    Audio = CreateDefaultSubobject<ULiquidAudioComponent>(TEXT("LiquidAudio"));


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
        TEXT("/Game/CyberCafe/Gameplay/Drinks/Audio/S_PourWaterLoop.S_PourWaterLoop"));
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
    Presentation->ResetMotionHistory();
}

void ALiquidContainerActor::EndPlay(const EEndPlayReason::Type Reason)
{
    StopPouring();
    StopPourAudio(true);
    if (Inventory->bInitialized && GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            S->UnregisterContainer(this, Inventory->VolumeML);
    Super::EndPlay(Reason);
}

void ALiquidContainerActor::Destroyed()
{
    StopPourAudio(true);
    if (Inventory->bInitialized && GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            S->UnregisterContainer(this, Inventory->VolumeML);
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
        else if (!bShouldPour && (bIsPouring || Audio->IsRequested()))
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
    if (Inventory->bInitialized) return;
    MaxVolumeML = FMath::IsFinite(MaxVolumeML) ? FMath::Max(0.f, MaxVolumeML) : 0.f;
    const double InitialFill = FMath::IsFinite(FillAmount) ? FMath::Clamp(static_cast<double>(FillAmount), 0.0, 1.0) : 0.0;
    Inventory->VolumeML = SupportsLiquidInventory() ? InitialFill * MaxVolumeML : 0.0;
    Inventory->Contents = FLiquidComposition::Pure(InitialIngredientId, Inventory->VolumeML);
    Inventory->bInitialized = true;
    SyncInventorySnapshot();
    if (GetWorld())
        if (ULiquidTransferSubsystem* S = GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            S->RegisterContainer(this, Inventory->VolumeML);
}

double ALiquidContainerActor::GetPreciseVolumeML() const
{
    if (!SupportsLiquidInventory()) return 0.0;
    if (Inventory->bInitialized) return Inventory->VolumeML;
    return FMath::IsFinite(FillAmount) && FMath::IsFinite(MaxVolumeML)
        ? FMath::Clamp(static_cast<double>(FillAmount), 0.0, 1.0) * FMath::Max(0.f, MaxVolumeML) : 0.0;
}

FLiquidComposition ALiquidContainerActor::GetComposition() const
{
    return Inventory->bInitialized ? Inventory->Contents : FLiquidComposition::Pure(InitialIngredientId, GetPreciseVolumeML());
}

bool ALiquidContainerActor::BreakContainerForImpact()
{
    if (bLiquidFrozen || bBroken) return false;
    // 先锁定破碎状态，结算完成后才通知外部，防止回调重入重复记账。
    bBroken = true;
    InitializeLiquidState();
    const double RemainingML = Inventory->VolumeML;
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


double ALiquidContainerActor::DebitVolumeInternal(double RequestedML, FLiquidComposition* OutComposition)
{
    if (!SupportsLiquidInventory()) return 0;
    InitializeLiquidState();
    const double Actual=Inventory->Debit(RequestedML, OutComposition);
    SyncInventorySnapshot();
    return Actual;
}

double ALiquidContainerActor::CreditVolumeInternal(double RequestedML, const FLinearColor& InColor, const FLiquidComposition* InComposition)
{
    if (!SupportsLiquidInventory() || bLiquidFrozen || bBroken) return 0;
    InitializeLiquidState();
    const double Actual=Inventory->Credit(RequestedML, MaxVolumeML, LiquidColor, InColor, InComposition);
    SyncInventorySnapshot();
    return Actual;
}

void ALiquidContainerActor::SyncInventorySnapshot()
{
    CurrentVolumeML=Inventory->GetVolumeML();
    Composition=Inventory->GetComposition();
}

void ALiquidContainerActor::NotifyLiquidState()
{
    RefreshLiquidFX();
    OnLiquidChanged.Broadcast(FillAmount, LiquidColor);
}

void ALiquidContainerActor::SetCapacityML(float NewCapacityML)
{
    if (bLiquidFrozen || bBroken || !FMath::IsFinite(NewCapacityML) || NewCapacityML < 0) return;
    if (!Inventory->bInitialized)
    {
        // Construction/default setup changes capacity while retaining saved initial
        // fill. Do not lock the old capacity into runtime mL before BeginPlay.
        MaxVolumeML = NewCapacityML;
        RefreshLiquidFX();
        return;
    }
    MaxVolumeML = NewCapacityML;
    const double Excess = DebitVolumeInternal(FMath::Max(0.0, Inventory->VolumeML - MaxVolumeML));
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
    Presentation->RefreshLiquidFX();
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
    Presentation->UpdateDynamicWaves(DeltaTime);
}

//=====================================================================
// 内部：初始化 LiquidFX（P_Liquid User 参数）
//=====================================================================

void ALiquidContainerActor::InitLiquidFX()
{
    Presentation->InitLiquidFX();
}

void ALiquidContainerActor::ResolveLiquidMaterialFromFX()
{
    Presentation->ResolveLiquidMaterialFromFX();
}

//=====================================================================
// 内部：初始化 PourFX（P_Ribbon User 参数）
//=====================================================================

void ALiquidContainerActor::InitPourFX()
{
    Presentation->InitPourFX();
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
    return Audio->GetLoopSoundComponent();
}

void ALiquidContainerActor::UpdatePourAudio(double ActualFlowMLPerSecond, const FTransform& SourceTransform)
{
    Audio->UpdatePourAudio(ActualFlowMLPerSecond, SourceTransform);
}

void ALiquidContainerActor::StopPourAudio(bool bImmediate)
{
    Audio->StopPourAudio(bImmediate);
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
        bPourAudioStopOnNextTick = bPreserveLastEmissionAudio && Audio->IsRequested();
    }
}

//=====================================================================
// 材质相关：一键换材质 + 从材质读颜色
//=====================================================================

void ALiquidContainerActor::SetLiquidMaterialAsset(UMaterialInterface* NewMaterial, bool bReadColor)
{
    Presentation->SetLiquidMaterialAsset(NewMaterial, bReadColor);
}

void ALiquidContainerActor::EnsureLiquidMID(bool bForceRecreate)
{
    Presentation->EnsureLiquidMID(bForceRecreate);
}

void ALiquidContainerActor::ApplyLiquidColorToMID()
{
    Presentation->ApplyLiquidColorToMID();
}

void ALiquidContainerActor::GetPourTraceIgnoreActors(TArray<AActor*>& OutActors) const
{
    // 默认实现：什么都不加。子类可 override 追加需要忽略的 Actor。
    // （示例：ABottleActor 会追加它的瓶盖 Actor，避免拧下的盖子挡住倒液射线。）
}

bool ALiquidContainerActor::TryReadColorFromMaterial()
{
    return Presentation->TryReadColorFromMaterial();
}

// Niagara exports remain connected for asset compatibility, but never own volume.
void ALiquidContainerActor::ReceiveParticleData_Implementation(
    const TArray<FBasicParticleData>& Data,
    UNiagaraSystem* NiagaraSystem,
    const FVector& SimulationPositionOffset)
{
    // @trace LQ-05. Actual reception is settled by LiquidTransferSubsystem.
}

bool ALiquidContainerActor::IsPourAudioRequested() const
{
    return Audio->IsRequested();
}
