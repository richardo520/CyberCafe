// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
    /**
     * 把所有常见的 VR 手柄扳机 FKey 集中放到这里：
     *  - Oculus/Meta Touch: OculusTouch_Left/Right_Trigger_Axis
     *  - 通用 OpenXR / 其它厂商: MotionController_Left/Right_TriggerAxis
     *  - 若将来接入 Vive/Index 的 FKey 名字不一样，这里再补即可。
     * 查询扳机轴值时逐个询问，只要有一个超过阈值就算扣下。
     */
    static const FName GTriggerAxisKeyNames[] =
    {
        TEXT("OculusTouch_Left_Trigger_Axis"),
        TEXT("OculusTouch_Right_Trigger_Axis"),
        TEXT("MotionController_Left_TriggerAxis"),
        TEXT("MotionController_Right_TriggerAxis"),
    };

    /** 查询玩家当前所有扳机轴中的最大值；拿不到 PC 时返回 0 */
    static float GetMaxTriggerValue(const UObject* WorldContext)
    {
        APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContext, 0);
        if (!PC)
        {
            return 0.f;
        }

        float MaxVal = 0.f;
        for (const FName& KeyName : GTriggerAxisKeyNames)
        {
            const FKey Key(KeyName);
            if (!Key.IsValid())
            {
                continue;
            }
            const float V = PC->GetInputAnalogKeyState(Key);
            if (V > MaxVal)
            {
                MaxVal = V;
            }
        }
        return MaxVal;
    }
}

void UTutorialStep_ShowMessage::EnterStep(ATutorialDirector* InDirector)
{
    Super::EnterStep(InDirector);

    UWorld* World = GetWorld();
    if (!World)
    {
        // 没有 World，直接完成，避免 Step 永远卡住
        MarkCompleted();
        return;
    }

    EnterGameTime = World->GetTimeSeconds();

    // 1) 定时器：Duration 秒后自动完成
    if (Duration > 0.f)
    {
        World->GetTimerManager().SetTimer(DurationTimer, this,
            &UTutorialStep_ShowMessage::HandleDurationElapsed, Duration, false);
    }

    // 2) 订阅事件总线的"玩家请求跳过"事件（由 VRPawn 在扣扳机时广播）
    if (bAllowSkipByInput)
    {
        if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
        {
            Sys->OnPlayerSkipRequested.AddDynamic(this, &UTutorialStep_ShowMessage::HandlePlayerSkipRequested);
            UE_LOG(LogTemp, Warning, TEXT("[ShowMessage] Subscribed OnPlayerSkipRequested. Step=%s"), *GetName());
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[ShowMessage] EventSubsystem is NULL, cannot subscribe skip event."));
        }
    }

    // 若 Duration <= 0 且不允许跳过，Step 会卡死：给个日志提醒配置问题
    if (Duration <= 0.f && !bAllowSkipByInput)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[TutorialStep_ShowMessage] Duration<=0 and bAllowSkipByInput=false; step will never complete!"));
    }
}

void UTutorialStep_ShowMessage::ExitStep()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DurationTimer);
    }

    // 解绑事件
    if (bAllowSkipByInput)
    {
        if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
        {
            Sys->OnPlayerSkipRequested.RemoveDynamic(this, &UTutorialStep_ShowMessage::HandlePlayerSkipRequested);
        }
    }

    Super::ExitStep();
}

void UTutorialStep_ShowMessage::HandleDurationElapsed()
{
    if (!IsCompleted())
    {
        MarkCompleted();
    }
}

void UTutorialStep_ShowMessage::HandlePlayerSkipRequested()
{
    UE_LOG(LogTemp, Warning, TEXT("[ShowMessage] HandlePlayerSkipRequested RECEIVED. IsCompleted=%d"),
        IsCompleted() ? 1 : 0);

    if (IsCompleted())
    {
        return;
    }

    // 宽限期内不响应，防止上一个 Step 没松扳机的输入穿透
    if (const UWorld* World = GetWorld())
    {
        const float Elapsed = World->GetTimeSeconds() - EnterGameTime;
        if (Elapsed < InputGracePeriod)
        {
            UE_LOG(LogTemp, Warning, TEXT("[ShowMessage] Skip BLOCKED by grace period (Elapsed=%.2fs < %.2fs)."),
                Elapsed, InputGracePeriod);
            return;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[ShowMessage] Skip ACCEPTED, MarkCompleted."));
    MarkCompleted();
}