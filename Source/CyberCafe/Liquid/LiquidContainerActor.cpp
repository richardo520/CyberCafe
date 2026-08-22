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
#include "GrabComponent.h"

ALiquidContainerActor::ALiquidContainerActor()
{
    PrimaryActorTick.bCanEverTick = false;

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

    // 液体状态默认值
    FillAmount    = 0.5f;
    MaxVolumeML   = 750.f;   // 一瓶红酒 = 750mL
    LiquidColor   = FLinearColor(0.35f, 0.05f, 0.08f, 1.f); // 默认红酒色

    // P_Liquid 表现默认值
    LiquidOpacity = 0.85f;
    AddWaves      = 1.0f;
    WavesScale    = 1.0f;
    Viscosity     = 1.0f;

    // 动态波动默认值（静止时完全平静，抓握晚动时漸起波动）
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

    // 默认包围盒（美术会在蓝图里重新填）
    BottleSize    = FVector(10.f, 10.f, 20.f);

    LiquidFXTemplate    = nullptr;
    LiquidMeshAsset     = nullptr;
    LiquidMaterialAsset = nullptr;
    LiquidColorParamName = FName(TEXT("Liquid_Color01"));
}

void ALiquidContainerActor::BeginPlay()
{
    Super::BeginPlay();

    // 启动时先尝试从材质里读出液体颜色（覆盖蓝图里默认的 LiquidColor），
    // 这样 PourFX/Splash 拿到的颜色和瓶内液体自然一致。
    TryReadColorFromMaterial();

    InitLiquidFX();
    RefreshLiquidFX();

    // 初始化上一帧参考位置/旋转
    if (ContainerMesh)
    {
        PrevLocation = ContainerMesh->GetComponentLocation();
        PrevRotation = ContainerMesh->GetComponentQuat();
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

    // 2) 将速度归一化到 0~1，取两者最大作为“运动强度”
    const float LinearNorm  = FMath::Clamp(LinearSpeed  / FMath::Max(LinearVelocityRefCms,  1.f), 0.f, 1.f);
    const float AngularNorm = FMath::Clamp(AngularSpeed / FMath::Max(AngularVelocityRefDegs, 1.f), 0.f, 1.f);
    const float MotionNorm  = FMath::Max(LinearNorm, AngularNorm);

    // 3) 目标波动强度：在 [IdleAddWaves, MaxAddWaves] 之间插值
    const float TargetWaves = FMath::Lerp(IdleAddWaves, MaxAddWaves, MotionNorm);

    // 4) 平滑：上升时快、下降时慢，避免抳动
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

    // 2) 写入静态 User 参数
    if (LiquidMeshAsset)
    {
        // StaticMesh 需用 UNiagaraFunctionLibrary 提供的专用静态方法
        UNiagaraFunctionLibrary::OverrideSystemUserVariableStaticMesh(LiquidFX, TEXT("User.Mesh"), LiquidMeshAsset);
    }
    if (LiquidMaterialAsset)
    {
        // Material 作为 UObject 传入
        LiquidFX->SetNiagaraVariableObject(TEXT("User.Material"), LiquidMaterialAsset);
    }
    LiquidFX->SetNiagaraVariableVec3(TEXT("User.BottleSize"), BottleSize);

    // 3) 重启以让 User 参数生效
    LiquidFX->ReinitializeSystem();
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
