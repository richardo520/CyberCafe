// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/BottleActor.h"

#include "GrabComponent.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

ABottleActor::ABottleActor()
{
    // 瓶子风格的默认值（覆盖基类默认）
    // - 容量 750mL（一瓶红酒）
    // - 满瓶初始
    // - 倒液角度 60°（瓶身较高，稍微倾斜就出液）
    // - 出液速率 60mL/s，水流强度 1.0
    MaxVolumeML             = 750.f;
    FillAmount              = 1.0f;

    PourAngleThreshold      = 60.f;
    PourRatePerSecond       = 60.f;
    FlowStrength            = 1.f;
    PourTraceDistance       = 60.f;
    PourTraceRadius         = 6.f;
    PourTraceForwardOffset  = 0.f;

    // 瓶子只倒出，不接液
    bCanPour                = true;
    bAcceptLiquidFromOthers = false;

    PourHaptic              = nullptr;
}

void ABottleActor::StartPouring()
{
    // 先执行基类流程（激活 PourFX、写入 User 参数、置 bIsPouring）
    Super::StartPouring();

    // 追加：播放持瓶手柄的触觉反馈（瓶子专属）
    if (PourHaptic && GrabComp && GrabComp->IsHeld())
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            PC->PlayHapticEffect(PourHaptic, GrabComp->GetHeldByHand());
        }
    }
}