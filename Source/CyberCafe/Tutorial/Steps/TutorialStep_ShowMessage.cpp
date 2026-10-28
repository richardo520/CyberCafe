// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

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

    // 2) 订阅事件总线的"玩家请求跳过"事件（由 VRPawn 在扣扶机时广播）
    //    先 RemoveDynamic 再 AddDynamic：防止同一个 Step 对象被重复 Enter（极端情况）
    //    导致订阅两份，收到广播时触发两次 MarkCompleted。
    if (bAllowSkipByInput)
    {
        if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
        {
            Sys->OnPlayerSkipRequested.RemoveDynamic(this, &UTutorialStep_ShowMessage::HandlePlayerSkipRequested);
            Sys->OnPlayerSkipRequested.AddDynamic(this, &UTutorialStep_ShowMessage::HandlePlayerSkipRequested);
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
    if (IsCompleted())
    {
        // 已完成的 Step 不该再响应（防御：Director 延迟 ExitStep 时的残留订阅）
        return;
    }

    // 宽限期内不响应，防止上一个 Step 没松扶机的输入穿透
    if (const UWorld* World = GetWorld())
    {
        const float Elapsed = World->GetTimeSeconds() - EnterGameTime;
        if (Elapsed < InputGracePeriod)
        {
            return;
        }
    }

    // 关键：立即解绑自己！
    // Director 在 Step 完成后会延迟 CheerDuration 秒再 ExitStep，这段窗口内本 Step
    // 仍然在事件总线订阅列表里。如果玩家在这段窗口再扣一次扶机，就会触发已完成的
    // Step 又收到广播；更糟的是，当前这次广播还在遍历订阅列表，下一个 Step 若已
    // Enter（罕见但可能），也会被同一次广播触发。立即解绑可以从根上杜绝这些问题。
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnPlayerSkipRequested.RemoveDynamic(this, &UTutorialStep_ShowMessage::HandlePlayerSkipRequested);
    }

    MarkCompleted();
}