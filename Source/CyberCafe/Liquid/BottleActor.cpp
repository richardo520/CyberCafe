// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/BottleActor.h"

#include "GrabComponent.h"
#include "Liquid/BottleCapActor.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

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

    // 瓶盖默认参数（美术可在蓝图里覆盖）
    CapClass                = nullptr;
    CapSocketName           = TEXT("CapSocket");
    bStartCapped            = true;
    CapActor                = nullptr;
    bIsCapped               = false;
}

void ABottleActor::BeginPlay()
{
    Super::BeginPlay();

    // 若指定了瓶盖类，Spawn 一个盖子并绑定
    if (CapClass && bStartCapped)
    {
        UWorld* World = GetWorld();
        if (World && ContainerMesh)
        {
            FActorSpawnParameters SpawnParams;
            SpawnParams.Owner  = this;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            // 用瓶口 Socket 的 Transform 作为初始位置（Attach 时会再对齐一次）
            const FTransform SocketXform = ContainerMesh->GetSocketTransform(CapSocketName, RTS_World);
            CapActor = World->SpawnActor<ABottleCapActor>(CapClass, SocketXform, SpawnParams);

            if (CapActor)
            {
                CapActor->AttachToBottle(this, CapSocketName);
                // 初始上锁：盖着不能倒
                OnCapAttached();
            }
        }
    }
}

void ABottleActor::OnCapAttached()
{
    bIsCapped = true;
    // 盖着不能倒
    bCanPour = false;
    // 如果此刻正在倒液（比如玩家把盖子塞回时瓶子刚好在倾斜），立即停下
    if (bIsPouring)
    {
        StopPouring();
    }
}

void ABottleActor::OnCapDetached()
{
    bIsCapped = false;
    // 解锁倒液（基类 Tick 会自动判定倾角）
    bCanPour = true;
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

void ABottleActor::GetPourTraceIgnoreActors(TArray<AActor*>& OutActors) const
{
    // 拧下的瓶盖可能挂在手上并悬在瓶口下方——需要在倒液 Trace 里忽略它，
    // 否则盖子会挡住 SphereTrace，液体倒不进下方的杯子。
    if (CapActor)
    {
        OutActors.Add(CapActor);
    }
}