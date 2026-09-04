// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/LiquidContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
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
    PourAngleThreshold      = 60.f;
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
    // ============================================================
    // 动态出液环：在 PourEntryPoint 的口沿圆环上，找世界 Z 最低点
    // ============================================================
    //
    // 【问题】：如果直接返回 PourFX 组件自身的 Transform，出液起点会固定在
    //   美术在蓝图里配置的那个局部坐标上。这样容器无论倾斜角度多大、往哪个
    //   方向倾斜，水柱都从同一个局部点喷出——看起来像凭空喷水而不是"顺着
    //   杯口/瓶口最低边沿自然流下"。
    //
    // 【方案】：把 PourEntryPoint（+PourEntryRadius）视为一个"口沿圆环"，
    //   每帧在这个圆环上找世界 Z 最低的一点 P，作为水柱起点：
    //     ① 取世界 -Z 方向在 PourEntryPoint 局部 XY 平面上的投影方向 D
    //        （D 就是"沿口沿哪个方向重力最先把液面拉下去"的方向）
    //     ② P = C + D * PourEntryRadius，其中 C 是 PourEntryPoint 世界位置
    //     ③ 输出 Transform 的 +X 轴对齐 +N（杯身正方向）。P_Ribbon 内部
    //        粒子实际沿组件 -X 发射，所以组件 +X = +N 时，粒子最终沿 -N
    //        方向飞出，视觉上"与杯壁保持平行 / 沿杯身轴反向抛出"。
    //        用 D 与 N 的叉积构造 +Y，保证正交系稳定。
    //
    // 【回退】：当 PourEntryPoint 未设置 / 半径 <= 0 / 容器几乎正立或倒置
    //   （投影方向长度趋近于 0，无法定义唯一最低点）时，退回旧行为，使用
    //   PourFX 组件自身的 Transform。
    // ============================================================

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

            // ============================================================
            // 出液 Transform 的朝向构造 —— 方案 A：+X = +N（沿杯身正方向）
            // ============================================================
            // 【设计目标】水柱应"贴着杯壁 / 平行于杯壁"从杯口最低点抛出，
            //   而不是"垂直于杯壁"顶出来。
            //
            // 【几何观察】
            //   - 杯身轴向 = 口沿平面法线 N（PourEntryPoint 局部 +Z 的世界方向，
            //     从杯底指向杯口）
            //   - "沿杯壁朝外"的方向 = -N（从杯口沿杯身反向继续延伸出去）
            //
            // 【为什么 AxisX = +N 而不是 -N】
            //   经实测，P_Ribbon 内部粒子的发射朝向是沿组件 +X 的【反向】
            //   （粒子朝 -X 方向飞）。因此为了让粒子最终朝 -N 方向抛出（沿
            //   杯壁往外流），组件的 +X 需要设为 +N，这样组件 -X（粒子实际
            //   飞行方向）= -N，正好指向"从杯口往外沿杯身反向"。
            //
            //   ⚠️ 如果将来更换 P_Ribbon 资产、粒子改为沿组件 +X 发射，只需
            //   把这里的 AxisX 换回 -N 即可。
            //
            // 【正交系构造】
            //   AxisX = +N                     —— 组件 +X（粒子实际朝 -X = -N 飞）
            //   AxisY = normalize(N × D)       —— 与 AxisX 正交的横切向
            //                                    （D 与 N 严格正交，故 |N×D|=1）
            //   AxisZ = AxisX × AxisY          —— 落在 (N, D) 平面内、朝向 -D 侧
            //   此 3 轴构成合法右手系。
            //
            // 【失效场景与回退】
            //   - 当容器几乎正立/倒置时，D 长度 < 0.05 已被外层挡掉，走
            //     PourFX 固定 Transform 的回退分支，本段代码不会执行。
            //   - 当 |N × D| 意外过小（理论上不会发生，因为 D ⊥ N 是构造出来
            //     的），保底取一个与 N 正交的向量作为 AxisY。
            // ============================================================

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

//=====================================================================
// 倒液开始 / 结束
//=====================================================================

void ALiquidContainerActor::StartPouring()
{
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
    if (!LiquidFX || !LiquidMaterialAsset)
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

//=====================================================================
// Niagara 粒子回调：接收 P_Ribbon 里 "Export Particle Data to Blueprint"
// 导出的水流粒子实时数据（每颗满足条件的粒子的 Position/Size/Velocity）。
//
// P_Ribbon 里美术设置的条件通常是"粒子碰撞死亡时导出"——也就是说，
// 这里拿到的每颗 Particle.Position 就是【水柱真正命中场景的落点】，
// 与视觉水花的生成位置完全对齐。
//
// 我们据此做接液判定（双锚点入口圆柱方案）：
//   - 遍历所有可接液容器（PourEntryRadius > 0 视为启用）
//   - 将粒子位置转到 PourEntryPoint 局部空间，若粒子处于以 PourEntryPoint
//     为中心、半径 = PourEntryRadius、沿局部 -Z 延伸的圆柱内
//     → 认为水滴进了这个杯子，触发 AddLiquid + 材质传递
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
        if (Cand->PourEntryRadius <= 0.f)                continue; // 未启用接液的容器直接跳过
        if (!Cand->PourEntryPoint)                       continue;
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

    // 粒子刚穿过入口平面时的 Z 容差（cm）——判定循环与调试可视化共用
    constexpr float EntryZTolerance = 1.f;

    for (const FBasicParticleData& Particle : Data)
    {
        const FVector ParticleWS = Particle.Position + SimulationPositionOffset;

        // 找出这颗粒子属于哪个杯子——双锚点入口圆柱判定：
        // 粒子必须处于以 PourEntryPoint 为中心、半径 = PourEntryRadius、
        // 沿其局部 -Z 延伸的圆柱体内。在 PourEntryPoint 局部坐标下：
        //   — XY 距离 ≤ PourEntryRadius（粒子在杯口圆盘正下方）
        //   — Z ≤ EntryZTolerance（粒子已穿越杯口平面进入内腽）
        // 杯身外壁掠过的粒子——局部 XY 超出杯口半径 → 直接淘汰。
        ALiquidContainerActor* Best = nullptr;
        float BestDistSq = TNumericLimits<float>::Max();
        for (ALiquidContainerActor* Cand : Candidates)
        {
            const FTransform& EntryTM = Cand->PourEntryPoint->GetComponentTransform();
            const FVector DeltaLS = EntryTM.InverseTransformPositionNoScale(ParticleWS);

            // 1) 粒子必须已穿越杯口平面（沿局部 -Z 方向进入杯内），允许少量容差
            if (DeltaLS.Z > EntryZTolerance) continue;

            // 2) 局部 XY 必须在杯口圆盘内
            const float DistSqXY = DeltaLS.X * DeltaLS.X + DeltaLS.Y * DeltaLS.Y;
            const float RSq      = Cand->PourEntryRadius * Cand->PourEntryRadius;
            if (DistSqXY > RSq) continue;

            if (DistSqXY < BestDistSq)
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
        // ======================================================================
        // 调试可视化（双锚点入口圆柱）：
        //   — 洋红大球  = PourEntryPoint 世界位置（杯口锚点）
        //   — 洋红小球  = PourTargetPoint 世界位置（杯底锚点，仅参考）
        //   — 绿色圆盘  = 杯口圆盘（贴在 PourEntryPoint 平面上）
        //   — 浅蓝圆柱  = 杯口向下延伸的入口圆柱（盞盘 + 4 条侧壁连线近似）
        //   — 黄色小球  = 每颗粒子的落点
        //   — 绿/红连线 = 该粒子是否通过入口圆柱判定
        // ======================================================================

        for (ALiquidContainerActor* Cand : Candidates)
        {
            const FTransform& EntryTM = Cand->PourEntryPoint->GetComponentTransform();
            const FVector EntryWS = EntryTM.GetLocation();

            // 杯口锚点（洋红大球）
            DrawDebugSphere(World, EntryWS, 2.5f, 12, FColor::Magenta, false, 0.f, 0, 0.4f);

            // 杯底锚点（洋红小球，仅作视觉参考）
            if (Cand->PourTargetPoint)
            {
                DrawDebugSphere(World, Cand->PourTargetPoint->GetComponentLocation(),
                    1.5f, 8, FColor(200, 100, 200), false, 0.f, 0, 0.3f);
            }

            // 杯口圆盘（绿色，贴在 PourEntryPoint 平面上）
            //
            // 注意：DrawDebugCircle(FMatrix) 内部在传入矩阵的 "YZ 平面" 画圆
            // （法线 = 矩阵的 X 轴）。我们希望圆盘贴在 PourEntryPoint 的 XY 平面
            // 上（法线 = 杯口局部 +Z），因此需要"轴换位"：
            //   矩阵 X 轴 ← 杯口 +Z（作为法线）
            //   矩阵 Y 轴 ← 杯口 +X（作为圆盘平面基向量）
            //   矩阵 Z 轴 ← 杯口 +Y（作为圆盘平面基向量）
            const FVector EntryX = EntryTM.GetUnitAxis(EAxis::X);
            const FVector EntryY = EntryTM.GetUnitAxis(EAxis::Y);
            const FVector EntryZ = EntryTM.GetUnitAxis(EAxis::Z);
            const FMatrix DiskTM(EntryZ, EntryX, EntryY, EntryWS);
            DrawDebugCircle(World,
                DiskTM,
                Cand->PourEntryRadius,
                32, FColor::Green, false, 0.f, 0, 0.5f,
                /*bDrawAxis=*/false);

            // 入口圆柱（浅蓝，沿杯口局部 -Z 向内延伸）
            const FVector EntryDown = -EntryZ;
            const float CylHeight = FMath::Max(Cand->PourEntryRadius * 4.f, 15.f);
            const FVector CylBottomWS = EntryWS + EntryDown * CylHeight;

            // 圆柱底盘（下方）——同样按上文"轴换位"规则构造矩阵
            const FMatrix CylBottomTM(EntryZ, EntryX, EntryY, CylBottomWS);
            DrawDebugCircle(World,
                CylBottomTM,
                Cand->PourEntryRadius,
                32, FColor(80, 160, 255), false, 0.f, 0, 0.3f,
                /*bDrawAxis=*/false);

            // 圆柱侧壁 4 条连线
            const FVector AxisX = EntryX * Cand->PourEntryRadius;
            const FVector AxisY = EntryY * Cand->PourEntryRadius;
            for (int32 i = 0; i < 4; ++i)
            {
                const float Ang = i * PI * 0.5f;
                const FVector Off = AxisX * FMath::Cos(Ang) + AxisY * FMath::Sin(Ang);
                DrawDebugLine(World, EntryWS + Off, CylBottomWS + Off,
                    FColor(80, 160, 255), false, 0.f, 0, 0.3f);
            }
        }

        // 每颗粒子 + 到判定锚点的连线（绿=通过, 红=淘汰）
        for (const FBasicParticleData& Particle : Data)
        {
            const FVector ParticleWS = Particle.Position + SimulationPositionOffset;
            DrawDebugSphere(World, ParticleWS, 1.5f, 8, FColor::Yellow, false, 0.f, 0, 0.2f);

            // 找到局部 XY 最近的候选杯子做可视化
            ALiquidContainerActor* NearestCand = nullptr;
            float NearestXYSq = TNumericLimits<float>::Max();
            FVector NearestDeltaLS = FVector::ZeroVector;
            for (ALiquidContainerActor* Cand : Candidates)
            {
                const FTransform& EntryTM = Cand->PourEntryPoint->GetComponentTransform();
                const FVector DeltaLS = EntryTM.InverseTransformPositionNoScale(ParticleWS);
                const float DSq = DeltaLS.X * DeltaLS.X + DeltaLS.Y * DeltaLS.Y;
                if (DSq < NearestXYSq)
                {
                    NearestXYSq = DSq;
                    NearestCand = Cand;
                    NearestDeltaLS = DeltaLS;
                }
            }
            if (NearestCand)
            {
                const FVector EntryWS = NearestCand->PourEntryPoint->GetComponentLocation();
                const float RSq = NearestCand->PourEntryRadius * NearestCand->PourEntryRadius;
                const bool bPass = (NearestDeltaLS.Z <= EntryZTolerance) && (NearestXYSq <= RSq);

                DrawDebugLine(World, ParticleWS, EntryWS,
                    bPass ? FColor::Green : FColor::Red,
                    false, 0.f, 0, 0.3f);
            }
        }
    }
#endif
}
