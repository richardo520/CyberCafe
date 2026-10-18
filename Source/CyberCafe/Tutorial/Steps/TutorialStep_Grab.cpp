// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_Grab.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialDirector.h"
#include "GameFramework/Actor.h"

void UTutorialStep_Grab::EnterStep(ATutorialDirector* InDirector)
{
    Super::EnterStep(InDirector);

    // 向全局事件总线订阅"任意物体被抓取"
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnAnyObjectGrabbed.AddDynamic(this, &UTutorialStep_Grab::HandleAnyObjectGrabbed);
    }
}

void UTutorialStep_Grab::ExitStep()
{
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnAnyObjectGrabbed.RemoveDynamic(this, &UTutorialStep_Grab::HandleAnyObjectGrabbed);
    }

    Super::ExitStep();
}

void UTutorialStep_Grab::HandleAnyObjectGrabbed(AActor* Object, EControllerHand /*Hand*/)
{
    // 已完成则忽略（防止 Director 延迟 ExitStep 期间的多次触发）
    if (IsCompleted())
    {
        return;
    }

    // RequiredActor 未指定 → 任意抓取都算完成
    // 已指定 → 必须命中它才算完成
    if (RequiredActor && Object != RequiredActor)
    {
        return;
    }

    MarkCompleted();
}
