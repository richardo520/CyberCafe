// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/LiquidContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "MaterialTypes.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraTypes.h"
#include "NiagaraVariant.h"
#include "GrabComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
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

    const float NewTotalML = CurrentML - ConsumedML;
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
    const FNiagaraVariableBase UserMaterialVar(FNiagaraTypeDefinition::GetUObjectDef(), FName(TEXT("User.Material")));
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

    // User.Data = self (Actor)，同官方 BP_Pouring 的做法
    PourFX->SetNiagaraVariableObject(TEXT("User.Data"), this);

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
        // 不销毁，只关闭；下次 Activate 可直接重启
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

    const FTransform PourXform = GetPourWorldTransform();
    const FVector PourLocationWS = PourXform.GetLocation();

    // 更新 Niagara 的实时颜色(液体颜色会因混色变化)
    if (PourFX)
    {
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"), LiquidColor);
    }

    // 本帧应流出的液体量
    const float DesiredML = PourRatePerSecond * DeltaTime;
    const float ActualML  = ConsumeLiquid(DesiredML);
    if (ActualML <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    // 从出液口沿世界 -Z 方向做 SphereTrace（胖射线），兜住水流弧度的落点偏差。
    // 为进一步补偿水流初速度的水平位移，将起点沿 PourFX 局部 +X 方向前推一个可配置量。
    const FVector ForwardWS  = PourXform.GetUnitAxis(EAxis::X);
    const FVector TraceStart = PourLocationWS + ForwardWS * PourTraceForwardOffset;
    const FVector TraceEnd   = TraceStart + FVector(0.f, 0.f, -PourTraceDistance);

    FHitResult Hit;
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LiquidPourTrace), /*bTraceComplex=*/false, this);
    QueryParams.AddIgnoredActor(this);

    // 让子类补充需要忽略的 Actor（例如 ABottleActor 会追加拧下的瓶盖），
    // 避免瓶盖挡在瓶口下方阻塞 Trace 导致液体倒不进目标容器。
    TArray<AActor*> ExtraIgnores;
    GetPourTraceIgnoreActors(ExtraIgnores);
    for (AActor* A : ExtraIgnores)
    {
        if (A)
        {
            QueryParams.AddIgnoredActor(A);
        }
    }

    const bool bHit = GetWorld()->SweepSingleByChannel(
        Hit,
        TraceStart,
        TraceEnd,
        FQuat::Identity,
        ECC_Visibility,
        FCollisionShape::MakeSphere(PourTraceRadius),
        QueryParams);

#if ENABLE_DRAW_DEBUG
    // 编辑器下方便调试，需在 BP 里把 bDebugDrawTrace 打开才会显示
    if (bDebugDrawTrace)
    {
        const FColor LineColor    = bHit ? FColor::Green : FColor::Red;
        const FVector EndPointVis = bHit ? Hit.ImpactPoint : TraceEnd;
        DrawDebugLine(GetWorld(), TraceStart, EndPointVis, LineColor, false, 0.f, 0, 0.2f);
        DrawDebugSphere(GetWorld(), TraceStart, PourTraceRadius, 12, LineColor, false, 0.f, 0, 0.2f);
        DrawDebugSphere(GetWorld(), EndPointVis, PourTraceRadius, 12, LineColor, false, 0.f, 0, 0.2f);
    }
#endif

    if (bHit)
    {
        // 命中任意可接液的 ALiquidContainerActor（不再特判 CupActor 类型）
        if (ALiquidContainerActor* TargetContainer = Cast<ALiquidContainerActor>(Hit.GetActor()))
        {
            if (TargetContainer != this && TargetContainer->bAcceptLiquidFromOthers)
            {
                // 1) 把源容器的液体材质"简单粗暴"地赋给目标容器（含 MI 的颜色一起传递）
                //    只在材质不同的时候才换，避免每帧 ReinitializeSystem。
                if (LiquidMaterialAsset && TargetContainer->LiquidMaterialAsset != LiquidMaterialAsset)
                {
                    TargetContainer->SetLiquidMaterialAsset(LiquidMaterialAsset, /*bReadColor=*/true);
                }

                // 2) 加液到目标容器（走基类混色逻辑）
                TargetContainer->AddLiquid(ActualML, LiquidColor);

                // 3) 在命中点弹出一次水花（P_Splash），同步颜色
                if (bEnableSplash && SplashEffectTemplate)
                {
                    UNiagaraComponent* SplashComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                        GetWorld(),
                        SplashEffectTemplate,
                        Hit.ImpactPoint,
                        Hit.ImpactNormal.Rotation(),
                        FVector(1.f),
                        /*bAutoDestroy=*/true);
                    if (SplashComp)
                    {
                        // 若 P_Splash 支持 User.Color 则会生效；不支持时 Niagara 会静默忽略。
                        SplashComp->SetNiagaraVariableLinearColor(TEXT("User.Color"), LiquidColor);
                    }
                }
            }
            // 命中的容器不接液 → 只当作洒到物体上，扣液已在上面完成
        }
        // 命中非容器（桌面/地面等）→ 液体"洒到地上"，P_Ribbon 内部会自己处理地面 Decal（若 bEnableDecal=true）
    }
    // 未命中 → 液体飞入虚空，扣掉即可
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

    // 同步到 P_Liquid
    if (LiquidFX)
    {
        LiquidFX->SetNiagaraVariableObject(TEXT("User.Material"), LiquidMaterialAsset);
        // 更换 Material 后重启 Niagara，让新材质立即接管渲染
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
