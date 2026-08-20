// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/LiquidContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "GrabComponent.h"

ALiquidContainerActor::ALiquidContainerActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // 容器外壳作为 Root（模拟物理 + 抓取目标）
    ContainerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContainerMesh"));
    SetRootComponent(ContainerMesh);
    ContainerMesh->SetSimulatePhysics(true);
    ContainerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));

    // 液体 Mesh：附在容器内部，无碰撞，物理不参与
    LiquidMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LiquidMesh"));
    LiquidMesh->SetupAttachment(ContainerMesh);
    LiquidMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LiquidMesh->SetGenerateOverlapEvents(false);
    LiquidMesh->SetSimulatePhysics(false);

    // 抓取组件（附在 ContainerMesh 上）
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(ContainerMesh);
    GrabComp->GrabType = EGrabType::Snap;
    GrabComp->GrabPriority = 0;

    // 液体状态默认值
    FillAmount    = 0.5f;
    MaxVolumeML   = 750.f;   // 一瓶红酒 = 750mL
    LiquidColor   = FLinearColor(0.35f, 0.05f, 0.08f, 1.f); // 默认红酒色

    // 材质外观默认值
    LiquidOpacity = 0.85f;
    WaveAmplitude = 0.3f;
    WaveFrequency = 2.0f;

    ContainerHeight = 0.f;
}

void ALiquidContainerActor::BeginPlay()
{
    Super::BeginPlay();
    InitLiquidMaterials();
    RefreshLiquidMaterial();
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

    RefreshLiquidMaterial();
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

    RefreshLiquidMaterial();
    OnLiquidChanged.Broadcast(FillAmount, LiquidColor);

    return ConsumedML;
}

void ALiquidContainerActor::RefreshLiquidMaterial()
{
    for (UMaterialInstanceDynamic* MID : LiquidMIDs)
    {
        if (!MID) continue;
        MID->SetScalarParameterValue(TEXT("FillAmount"),      FillAmount);
        MID->SetScalarParameterValue(TEXT("Opacity"),         LiquidOpacity);
        MID->SetScalarParameterValue(TEXT("WaveAmplitude"),   WaveAmplitude);
        MID->SetScalarParameterValue(TEXT("WaveFrequency"),   WaveFrequency);
        MID->SetScalarParameterValue(TEXT("ContainerHeight"), ContainerHeight);
        MID->SetVectorParameterValue(TEXT("LiquidColor"),     LiquidColor);
    }
}

bool ALiquidContainerActor::IsHeld() const
{
    return GrabComp && GrabComp->IsHeld();
}

//=====================================================================
// 内部：初始化 MID
//=====================================================================

void ALiquidContainerActor::InitLiquidMaterials()
{
    LiquidMIDs.Reset();

    if (!LiquidMesh || !LiquidMesh->GetStaticMesh())
    {
        return;
    }

    // 用局部包围盒的 Z 高度作为容器内高度
    const FBox LocalBounds = LiquidMesh->GetStaticMesh()->GetBounds().GetBox();
    ContainerHeight = LocalBounds.GetSize().Z;

    const int32 NumMaterials = LiquidMesh->GetNumMaterials();
    LiquidMIDs.Reserve(NumMaterials);

    for (int32 i = 0; i < NumMaterials; ++i)
    {
        // CreateDynamicMaterialInstance 会自动使用当前 Slot 上的材质作为 Parent
        UMaterialInstanceDynamic* MID = LiquidMesh->CreateDynamicMaterialInstance(i);
        LiquidMIDs.Add(MID);
    }
}
