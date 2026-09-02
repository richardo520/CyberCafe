// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/LiquidContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
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

ALiquidContainerActor::ALiquidContainerActor()
{
    // 打开 Tick：基类统一驱动"动态波动 + 倒液"两条主流程，子类无需重复
    PrimaryActorTick.bCanEverTick = true;

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

    // 杯口/接液口锚点：美术在蓝图里拖到杯子开口中心，搭配 PourTargetRadius 使用
    PourTargetPoint = CreateDefaultSubobject<USceneComponent>(TEXT("PourTargetPoint"));
    PourTargetPoint->SetupAttachment(ContainerMesh);

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
    PourAngleThreshold      = 60.f;
    PourRatePerSecond       = 60.f;    // 每秒 60mL
    FlowStrength            = 1.f;
    PourTraceDistance       = 60.f;    // 60cm，足够从桌面高度倒到杯子
    PourTraceRadius         = 6.f;     // 6cm "胖射线"，兜住水流弧度的落点偏差
    PourTraceForwardOffset  = 0.f;
    bDebugDrawTrace         = false;

    // 杯口判定默认关闭（对瓶子无影响）；
    // 杯子子类或蓝图中将其设成大于 0 的值即可启用。
    PourTargetRadius        = 0.f;

    SplashEffectTemplate    = nullptr;
    bEnableSplash           = true;
    bEnableDecal            = true;
    bNoSplashes             = false;
    bNoList                 = true;

    bIsPouring              = false;
}

void ALiquidContainerActor::BeginPlay()
{
    Super::BeginPlay();

    // 从 LiquidFX 组件上反查美术配置的液体材质 Override，回填到 C++ 缓存，
    // 作为后续“倒液传材质 / 读取颜色”的唯一数据源。
    ResolveLiquidMaterialFromFX();

    // 启动时尝试从材质里读出液体颜色（覆盖蓝图里默认的 LiquidColor），
    // 这样 PourFX/Splash 拿到的颜色和瓶内液体自然一致。
    TryReadColorFromMaterial();

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

void ALiquidContainerActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1) 根据容器运动状态实时调节液面波动
    UpdateDynamicWaves(DeltaTime);

    // 2) 倒液流程：允许倒液 + 有液体 + 超阈值 → StartPouring；否则 StopPouring
    if (bCanPour)
    {
        const bool bShouldPour =
            (FillAmount > KINDA_SMALL_NUMBER) &&
            (GetTiltAngleDegrees() >= PourAngleThreshold);

        if (bShouldPour && !bIsPouring)
        {
            StartPouring();
        }
        else if (!bShouldPour && bIsPouring)
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

float ALiquidContainerActor::AddLiquid(float DeltaML, FLinearColor InColor)
{
    if (DeltaML <= 0.f || MaxVolumeML <= 0.f)
    {
        return 0.f;
    }

    const float CurrentML   = FillAmount * MaxVolumeML;
    const float RemainingML = FMath::Max(0.f, MaxVolumeML - CurrentML);
    const float AcceptedML  = FMath::Min(DeltaML, RemainingML);

    if (AcceptedML <= KINDA_SMALL_NUMBER)
    {
        return 0.f;
    }

    const float NewTotalML = CurrentML + AcceptedML;

    // 按体积加权混色
    if (NewTotalML > KINDA_SMALL_NUMBER)
    {
        LiquidColor = (LiquidColor * CurrentML + InColor * AcceptedML) / NewTotalML;
        LiquidColor.A = 1.f;
    }

    FillAmount = FMath::Clamp(NewTotalML / MaxVolumeML, 0.f, 1.f);

    RefreshLiquidFX();
    OnLiquidChanged.Broadcast(FillAmount, LiquidColor);

    return AcceptedML;
}

float ALiquidContainerActor::ConsumeLiquid(float DeltaML)
{
    if (DeltaML <= 0.f || MaxVolumeML <= 0.f)
    {
        return 0.f;
    }

    const float CurrentML  = FillAmount * MaxVolumeML;
    const float ConsumedML = FMath::Min(DeltaML, CurrentML);

    if (ConsumedML <= KINDA_SMALL_NUMBER)
    {
        return 0.f;
    }

    float NewTotalML = CurrentML - ConsumedML;

    // “尾油”阅值：剩下不到 0.5mL 一律归零，
    // 避免 Fill 浮点残留（如 0.00001）导致瓶底总看到一小层液体没法排干。
    if (NewTotalML < 0.5f)
    {
        NewTotalML = 0.f;
    }

    FillAmount = FMath::Clamp(NewTotalML / MaxVolumeML, 0.f, 1.f);

    RefreshLiquidFX();
    OnLiquidChanged.Broadcast(FillAmount, LiquidColor);

    return ConsumedML;
}

void ALiquidContainerActor::RefreshLiquidFX()
{
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
    // 颜色在1行内由 Material 决定（模式 A），因此不在此处写。
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"),       FillAmount);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.Opacity"),    LiquidOpacity);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.AddWaves"),   AddWaves);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.WavesScale"), WavesScale);
    LiquidFX->SetNiagaraVariableFloat(TEXT("User.Viscosity"),  Viscosity);
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
    if (!LiquidFX)
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
    // CallbackHandler——Niagara 每帧会把碰撞死亡的水流粒子数据回调
    // 到 ReceiveParticleData_Implementation。
    // 注意：不同的 Niagara 资产可能把 Handler Parameter 绑定到不同名字上，
    // 这里一次写入三个最常见命名。Niagara 对不存在的 User 参数会静默忽略，
    // 不会报错，也不会重复回调（回调取决于 Export DI 实际绑定的那个）。
    PourFX->SetNiagaraVariableObject(TEXT("User.Data"),            this);
    PourFX->SetNiagaraVariableObject(TEXT("User.Handler"),         this);
    PourFX->SetNiagaraVariableObject(TEXT("User.CallbackHandler"), this);

    // 默认关闭，要倒液时才 Activate
    PourFX->Deactivate();
}

//=====================================================================
// 出液口变换 / 倾角
//=====================================================================

FTransform ALiquidContainerActor::GetPourWorldTransform() const
{
    // 直接使用 PourFX 的世界 Transform——美术在蓝图里拖动的位置就是出液口。
    // 保证水流发射点与 SphereTrace 起点一致，避免两者不同步。
    if (PourFX)
    {
        return PourFX->GetComponentTransform();
    }
    // fallback：PourFX 意外不存在时退回 Root
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

//=====================================================================
// 倒液开始 / 结束
//=====================================================================

void ALiquidContainerActor::StartPouring()
{
    bIsPouring = true;

    if (PourFX)
    {
        // 重新同步一次颜色/强度/开关，防止编辑器运行时改变后未生效
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),        LiquidColor);
        PourFX->SetNiagaraVariableFloat      (TEXT("User.FlowStrength"), FlowStrength);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoSplashes"),   bNoSplashes);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoList"),       bNoList);
        PourFX->SetNiagaraVariableBool       (TEXT("User.Decal"),        bEnableDecal);
        PourFX->SetNiagaraVariableObject     (TEXT("User.Data"),         this);

        PourFX->Activate(/*bReset=*/true);
    }
}

void ALiquidContainerActor::StopPouring()
{
    bIsPouring = false;

    if (PourFX)
    {
        // 不销毁，只停止 Spawn；已在飞的 Ribbon 粒子按 lifetime 自然消散，
        // 视觉上更贴近真实的"水流断尾"过渡。下次 Activate 可直接重启。
        PourFX->Deactivate();
    }
}

//=====================================================================
// 倒液 Tick 逻辑
//=====================================================================

void ALiquidContainerActor::UpdatePouring(float DeltaTime)
{
    if (!ContainerMesh)
    {
        return;
    }

    // 更新 Niagara 的实时颜色(液体颜色会因混色变化)
    if (PourFX)
    {
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"), LiquidColor);
    }

    // 本帧应流出的液体量——只负责"从源容器扣掉"，
    // 至于"液体是否倒进了某个目标容器/在哪落地/是否需要 Splash"，
    // 全部交给 ReceiveParticleData_Implementation 处理：
    // Niagara 内部的 Export Particle Data DI 会把水流粒子真正的碰撞死亡位置
    // 每帧回调回来——那才是"水柱物理上真实的落点"，与视觉完全对齐。
    const float DesiredML = PourRatePerSecond * DeltaTime;
    ConsumeLiquid(DesiredML);
}

//=====================================================================
// 材质相关：一键换材质 + 从材质读颜色
//=====================================================================

void ALiquidContainerActor::SetLiquidMaterialAsset(UMaterialInterface* NewMaterial, bool bReadColor)
{
    if (!NewMaterial)
    {
        return;
    }

    LiquidMaterialAsset = NewMaterial;

    // 顺便读取颜色，让 PourFX/Splash 保持一致
    if (bReadColor)
    {
        TryReadColorFromMaterial();
    }

    // 同步到 P_Liquid：
    // 必须使用 SetVariableMaterial（专为 Niagara Mesh Renderer 材质 Override 设计的 API），
    // 它做了几件通用 API 做不到的事：
    //   1) 使用精确的 GetUMaterialDef() 类型，能正确匹配 P_Liquid 里 User.Material 的类型声明；
    //   2) SystemInstanceController->SetVariable_Deferred 让运行中的 Niagara 实例即时感知；
    //   3) bRecachePSOs = true 触发材质切换后的 PSO 重编译，避免渲染管线延后一帧或不刷新；
    //   4) #if WITH_EDITOR 分支里内部会调用 SetParameterOverride，同步 InstanceParameterOverrides，
    //      让编辑器里美术后续再改材质时行为一致。
    // 之前用 SetNiagaraVariableObject / SetParameterOverride 都无法生效，是因为它们要么绕过
    // 了 PSO 重建，要么类型匹配不上，导致 Mesh Renderer 实际用的还是杯子蓝图里美术手填的旧材质。
    if (LiquidFX)
    {
        LiquidFX->SetVariableMaterial(FName(TEXT("User.Material")), LiquidMaterialAsset);
        // 保险起见重启一次 Niagara，让 Mesh Renderer 立刻用新材质渲染
        LiquidFX->ReinitializeSystem();
    }

    // 混色权威变了，广播一次事件，便于蓝图侧联动
    OnLiquidChanged.Broadcast(FillAmount, LiquidColor);
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

//=====================================================================
// Niagara 粒子回调：接收 P_Ribbon 里 "Export Particle Data to Blueprint"
// 导出的水流粒子实时数据（每颗满足条件的粒子的 Position/Size/Velocity）。
//
// P_Ribbon 里美术设置的条件通常是"粒子碰撞死亡时导出"——也就是说，
// 这里拿到的每颗 Particle.Position 就是【水柱真正命中场景的落点】，
// 与视觉水花的生成位置完全对齐。
//
// 我们据此做接液判定：
//   - 遍历所有可接液容器
//   - 若粒子落点到该容器 PourTargetPoint 的水平距离 <= PourTargetRadius
//     → 认为水滴进了这个杯子，触发 AddLiquid + 材质传递 + Splash
//=====================================================================

void ALiquidContainerActor::ReceiveParticleData_Implementation(
    const TArray<FBasicParticleData>& Data,
    UNiagaraSystem* NiagaraSystem,
    const FVector& SimulationPositionOffset)
{
    if (Data.Num() == 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // 预先收集场景中所有"可接液"的容器（排除自己），一帧内共享，避免每颗粒子重复迭代。
    TArray<ALiquidContainerActor*> Candidates;
    for (TActorIterator<ALiquidContainerActor> It(World); It; ++It)
    {
        ALiquidContainerActor* Cand = *It;
        if (!Cand || Cand == this)                       continue;
        if (!Cand->bAcceptLiquidFromOthers)              continue;
        if (Cand->PourTargetRadius <= 0.f)               continue; // 未启用杯口判定的容器直接跳过
        if (!Cand->PourTargetPoint)                      continue;
        Candidates.Add(Cand);
    }

    if (Candidates.Num() == 0)
    {
        // 场景里没有匹配的可接液容器：粒子落到了地板/桌面等——不接液，
        // 视觉上由 P_Ribbon 内部的 Splash Emitter 自己生成水花，C++ 无需干预。
        return;
    }

    // 每帧一次的粒子回调可能一次带回多颗粒子——为了避免"同一杯子同一帧被判定多次
    // 触发 SetLiquidMaterialAsset/ReinitializeSystem"，做一个"已处理集合"。
    TSet<ALiquidContainerActor*> HitTargets;

    for (const FBasicParticleData& Particle : Data)
    {
        const FVector ParticleWS = Particle.Position + SimulationPositionOffset;

        // 找出这颗粒子落点属于哪个杯子（水平 XY 距离最近且在半径内的一个）
        ALiquidContainerActor* Best = nullptr;
        float BestDistSq = TNumericLimits<float>::Max();
        for (ALiquidContainerActor* Cand : Candidates)
        {
            const FVector MouthWS = Cand->PourTargetPoint->GetComponentLocation();
            const FVector Delta   = ParticleWS - MouthWS;
            const float DistSqXY  = Delta.X * Delta.X + Delta.Y * Delta.Y;
            const float RSq       = Cand->PourTargetRadius * Cand->PourTargetRadius;
            if (DistSqXY <= RSq && DistSqXY < BestDistSq)
            {
                BestDistSq = DistSqXY;
                Best = Cand;
            }
        }

        if (!Best)
        {
            continue;
        }

        // 1) 材质传递（只需一次；重复调用会导致 ReinitializeSystem 频繁重启）
        if (!HitTargets.Contains(Best))
        {
            HitTargets.Add(Best);

            if (LiquidMaterialAsset && Best->LiquidMaterialAsset != LiquidMaterialAsset)
            {
                Best->SetLiquidMaterialAsset(LiquidMaterialAsset, /*bReadColor=*/true);
            }
        }

        // 2) 加液：每颗粒子代表一个"液滴事件"。
        //    使用 PourRatePerSecond * DeltaTime / 预计粒子数 会更精确，但复杂度太高；
        //    这里按"每颗粒子固定加一小口"处理——量的多少由 Niagara 侧粒子生成频率决定，
        //    如果视觉水柱粒子密度合适，感觉就是自然的。
        //    量的具体值可通过 PourRatePerSecond 换算：每颗粒子 ~ PourRatePerSecond / 60
        //    但因回调频率与粒子数难以预测，暂用简单常量兜底。
        const float MLPerParticle = FMath::Max(PourRatePerSecond * 0.016f / FMath::Max(Data.Num(), 1), 0.05f);
        Best->AddLiquid(MLPerParticle, LiquidColor);
    }

#if ENABLE_DRAW_DEBUG
    if (bDebugDrawTrace)
    {
        // 可视化：把本帧所有粒子的落点绘制成小球（黄），
        // 命中的目标 PourTargetPoint 绘制成绿色圆盘。
        for (const FBasicParticleData& Particle : Data)
        {
            const FVector ParticleWS = Particle.Position + SimulationPositionOffset;
            DrawDebugSphere(World, ParticleWS, 1.5f, 8, FColor::Yellow, false, 0.f, 0, 0.2f);
        }
        for (ALiquidContainerActor* Hit : HitTargets)
        {
            DrawDebugCircle(World,
                Hit->PourTargetPoint->GetComponentLocation(),
                Hit->PourTargetRadius,
                32, FColor::Green, false, 0.f, 0, 0.5f,
                FVector(1, 0, 0), FVector(0, 1, 0), false);
        }
    }
#endif
}
