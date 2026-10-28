// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Serving/ServingSpotActor.h"
#include "Liquid/LiquidContainerActor.h"

void UTutorialStep_WaitForServe::EnterStep(ATutorialDirector* InDirector)
{
    Super::EnterStep(InDirector);

    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnDrinkServed.RemoveDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkServed);
        Sys->OnDrinkServed.AddDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkServed);
    }
}

void UTutorialStep_WaitForServe::ExitStep()
{
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnDrinkServed.RemoveDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkServed);
    }

    Super::ExitStep();
}

void UTutorialStep_WaitForServe::HandleDrinkServed(AServingSpotActor* ServingSpot, ALiquidContainerActor* Cup)
{
    if (IsCompleted())
    {
        return;
    }

    // 若配置了 RequiredSpot，只响应该 Spot
    if (RequiredSpot && ServingSpot != RequiredSpot)
    {
        return;
    }

    // 把 Cup 存到 Director，供后续 Step 使用
    if (ATutorialDirector* D = GetDirector())
    {
        D->LastServedCup = Cup;
    }

    // 立即解绑，防止一个事件被多次响应（Director 延迟 ExitStep 的窗口期）
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnDrinkServed.RemoveDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkServed);
    }

    MarkCompleted();
}
