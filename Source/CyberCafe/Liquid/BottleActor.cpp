// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/BottleActor.h"
#include "Liquid/CupActor.h"

#include "Components/StaticMeshComponent.h"
#include "MotionControllerComponent.h"
#include "GrabComponent.h"

#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#include "Haptics/HapticFeedbackEffect_Base.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

ABottleActor::ABottleActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 默认值
    PourOffset          = FVector(0.f, 0.f, 15.f); // 假设瓶口在瓶身局部 +Z 15cm 处
    PourSocketName      = NAME_None;
    PourAngleThreshold  = 60.f;
    PourRatePerSecond   = 60.f;    // 每秒 60mL
    PourTraceDistance   = 60.f;    // 60cm，足够从桌面高度倒到杯子
    PourEffectTemplate  = nullptr;
    PourHaptic          = nullptr;

    bIsPouring   = false;
    ActivePourFX = nullptr;

    // 酒瓶默认容量：750mL(一瓶红酒)
    MaxVolumeML = 750.f;
    FillAmount  = 1.0f;
}

void ABottleActor::BeginPlay()
{
    Super::BeginPlay();
    // 抓取事件不需要绑定：Tick 里根据倾角/剩余量自行判定，
    // 松手后瓶子若仍倾斜也会继续出液(方案确认)。
}

void ABottleActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

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

//=====================================================================
// 瓶口变换 / 倾角
//=====================================================================

FTransform ABottleActor::GetPourWorldTransform() const
{
    if (!ContainerMesh)
    {
        return GetActorTransform();
    }

    if (PourSocketName != NAME_None && ContainerMesh->DoesSocketExist(PourSocketName))
    {
        return ContainerMesh->GetSocketTransform(PourSocketName, RTS_World);
    }

    // 用 PourOffset 从容器局部变换到世界
    const FTransform CompXform = ContainerMesh->GetComponentTransform();
    FTransform PourXform;
    PourXform.SetLocation(CompXform.TransformPosition(PourOffset));
    PourXform.SetRotation(CompXform.GetRotation());
    return PourXform;
}

float ABottleActor::GetTiltAngleDegrees() const
{
    if (!ContainerMesh)
    {
        return 0.f;
    }
    // 瓶身局部 +Z 在世界空间的方向
    const FVector BottleUpWS = ContainerMesh->GetUpVector();
    const float Dot = FVector::DotProduct(BottleUpWS.GetSafeNormal(), FVector::UpVector);
    const float AngleRad = FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f));
    return FMath::RadiansToDegrees(AngleRad);
}

//=====================================================================
// 倒酒开始 / 结束
//=====================================================================

void ABottleActor::StartPouring()
{
    bIsPouring = true;

    if (PourEffectTemplate && !ActivePourFX)
    {
        const FTransform PourXform = GetPourWorldTransform();

        ActivePourFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
            PourEffectTemplate,
            ContainerMesh,
            PourSocketName,                        // 若无 socket 也会 fallback 到 Component 原点
            PourXform.GetLocation(),               // 用于 KeepWorld 位置
            PourXform.Rotator(),
            EAttachLocation::KeepWorldPosition,
            /*bAutoDestroy=*/false);

        if (ActivePourFX)
        {
            // 传递液体颜色 & 速率
            ActivePourFX->SetVariableLinearColor(TEXT("User.LiquidColor"), LiquidColor);
            ActivePourFX->SetVariableFloat(TEXT("User.PourRate"), PourRatePerSecond);
        }
    }

    // 播放持瓶手柄的触觉反馈(可选)
    if (PourHaptic && GrabComp && GrabComp->IsHeld())
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            PC->PlayHapticEffect(PourHaptic, GrabComp->GetHeldByHand());
        }
    }
}

void ABottleActor::StopPouring()
{
    bIsPouring = false;

    if (ActivePourFX)
    {
        ActivePourFX->Deactivate();     // 让粒子自然消散
        ActivePourFX->DestroyComponent();
        ActivePourFX = nullptr;
    }
}

//=====================================================================
// 倒酒 Tick 逻辑
//=====================================================================

void ABottleActor::UpdatePouring(float DeltaTime)
{
    if (!ContainerMesh)
    {
        return;
    }

    const FTransform PourXform = GetPourWorldTransform();
    const FVector PourLocationWS = PourXform.GetLocation();

    // 更新 Niagara 的实时颜色(液体颜色会因混色变化)
    if (ActivePourFX)
    {
        ActivePourFX->SetVariableLinearColor(TEXT("User.LiquidColor"), LiquidColor);
    }

    // 本帧应流出的液体量
    const float DesiredML = PourRatePerSecond * DeltaTime;
    const float ActualML  = ConsumeLiquid(DesiredML);
    if (ActualML <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    // 从瓶口沿世界 -Z 做 LineTrace，判断下方是否有酒杯
    const FVector TraceStart = PourLocationWS;
    const FVector TraceEnd   = PourLocationWS + FVector(0.f, 0.f, -PourTraceDistance);

    FHitResult Hit;
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BottlePourTrace), /*bTraceComplex=*/false, this);
    QueryParams.AddIgnoredActor(this);

    const bool bHit = GetWorld()->LineTraceSingleByChannel(
        Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

#if ENABLE_DRAW_DEBUG
    // 编辑器下方便调试，Shipping 会自动去除
    DrawDebugLine(GetWorld(), TraceStart, bHit ? Hit.ImpactPoint : TraceEnd,
                  bHit ? FColor::Green : FColor::Red, false, 0.f, 0, 0.2f);
#endif

    if (bHit)
    {
        if (ACupActor* Cup = Cast<ACupActor>(Hit.GetActor()))
        {
            Cup->AddLiquid(ActualML, LiquidColor);
        }
        // 命中非杯子 → 液体"洒到地上"，就单纯扣掉不入杯(首版不做溢出/地面湿迹)
    }
    // 未命中 → 液体飞入虚空，扣掉即可
}
