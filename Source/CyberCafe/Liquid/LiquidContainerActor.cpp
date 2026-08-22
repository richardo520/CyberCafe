// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/LiquidContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
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

    // 默认包围盒（美术会在蓝图里重新填）
    BottleSize    = FVector(10.f, 10.f, 20.f);

    LiquidFXTemplate    = nullptr;
    LiquidMeshAsset     = nullptr;
    LiquidMaterialAsset = nullptr;
}

void ALiquidContainerActor::BeginPlay()
{
    Super::BeginPlay();
    InitLiquidFX();
    RefreshLiquidFX();
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
