// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialStep.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATutorialDirector::ATutorialDirector()
{
    PrimaryActorTick.bCanEverTick = false;

    // 给个默认收尾文本（蓝图可覆盖）
    FinishedText = NSLOCTEXT("Tutorial", "DefaultFinished", "教学结束，欢迎来上班！");
}

void ATutorialDirector::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoStart)
    {
        StartTutorial();
    }
}

void ATutorialDirector::StartTutorial()
{
    // 容错：没有 NPC 也允许跑，只是不显示气泡；便于调试阶段只测 Step 判定
    if (!NPCRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TutorialDirector] NPCRef is null — hints will not be shown"));
    }

    if (Steps.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TutorialDirector] Steps is empty, finishing immediately"));
        FinishTutorial();
        return;
    }

    StartStepAt(0);
}

void ATutorialDirector::SkipTutorial()
{
    // 清掉可能存在的 Cheer 延时
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(CheerTimerHandle);
    }

    // 如果有正在进行的 Step，先 ExitStep 解绑订阅
    if (Steps.IsValidIndex(CurrentStepIndex))
    {
        if (UTutorialStep* Step = Steps[CurrentStepIndex])
        {
            Step->OnStepCompleted.RemoveDynamic(this, &ATutorialDirector::HandleStepCompleted);
            Step->ExitStep();
        }
    }

    CurrentStepIndex = -1;
    FinishTutorial();
}

void ATutorialDirector::StartStepAt(int32 Index)
{
    if (!Steps.IsValidIndex(Index))
    {
        FinishTutorial();
        return;
    }

    UTutorialStep* Step = Steps[Index];
    if (!Step)
    {
        // 空槽位：跳过
        StartStepAt(Index + 1);
        return;
    }

    CurrentStepIndex = Index;

    // 订阅完成事件，然后 EnterStep
    Step->OnStepCompleted.AddDynamic(this, &ATutorialDirector::HandleStepCompleted);
    Step->EnterStep(this);

    // 让 NPC 显示提示文本
    if (NPCRef)
    {
        NPCRef->ShowHint(Step->HintText);
    }
}

void ATutorialDirector::HandleStepCompleted(UTutorialStep* CompletedStep)
{
    if (!CompletedStep || !Steps.IsValidIndex(CurrentStepIndex) || Steps[CurrentStepIndex] != CompletedStep)
    {
        // 不是当前 Step 发来的事件：忽略（防御）
        return;
    }

    // 立即解绑并显示"做得好"
    CompletedStep->OnStepCompleted.RemoveDynamic(this, &ATutorialDirector::HandleStepCompleted);

    if (NPCRef)
    {
        // 若 Step 没配 CompletedText，就保留原 HintText，避免气泡闪空
        if (!CompletedStep->CompletedText.IsEmpty())
        {
            NPCRef->ShowHint(CompletedStep->CompletedText);
        }
    }

    // 延迟 CheerDuration 秒后真正退出当前 Step 并推进
    if (UWorld* World = GetWorld())
    {
        if (CheerDuration > 0.f)
        {
            World->GetTimerManager().SetTimer(CheerTimerHandle, this,
                &ATutorialDirector::AdvanceAfterCheer, CheerDuration, false);
        }
        else
        {
            AdvanceAfterCheer();
        }
    }
    else
    {
        AdvanceAfterCheer();
    }
}

void ATutorialDirector::AdvanceAfterCheer()
{
    // 退出当前 Step
    if (Steps.IsValidIndex(CurrentStepIndex))
    {
        if (UTutorialStep* Step = Steps[CurrentStepIndex])
        {
            Step->ExitStep();
        }
    }

    const int32 Next = CurrentStepIndex + 1;
    if (Steps.IsValidIndex(Next))
    {
        StartStepAt(Next);
    }
    else
    {
        FinishTutorial();
    }
}

void ATutorialDirector::FinishTutorial()
{
    CurrentStepIndex = -1;

    if (NPCRef)
    {
        if (!FinishedText.IsEmpty())
        {
            NPCRef->ShowHint(FinishedText);
        }
        else
        {
            NPCRef->HideHint();
        }
    }

    OnTutorialFinished.Broadcast();
}
