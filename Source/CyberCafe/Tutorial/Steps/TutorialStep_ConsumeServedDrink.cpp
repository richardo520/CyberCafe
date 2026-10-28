// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "Tutorial/TutorialDirector.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"

void UTutorialStep_ConsumeServedDrink::EnterStep(ATutorialDirector* InDirector)
{
    Super::EnterStep(InDirector);

    UWorld* World = GetWorld();
    if (!World)
    {
        MarkCompleted();
        return;
    }

    // 没有上一步存下的杯子？直接走完（防御）
    if (!InDirector || !InDirector->LastServedCup)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[TutorialStep_ConsumeServedDrink] Director->LastServedCup is null; skip."));
        World->GetTimerManager().SetTimer(AfterDrinkTimer, this,
            &UTutorialStep_ConsumeServedDrink::HandleStepFinish,
            FMath::Max(0.001f, AfterDrinkDelay), false);
        return;
    }

    // 排队两段延时
    if (PreDrinkDelay > 0.f)
    {
        World->GetTimerManager().SetTimer(PreDrinkTimer, this,
            &UTutorialStep_ConsumeServedDrink::HandleDrinkConsumed, PreDrinkDelay, false);
    }
    else
    {
        // 立刻消失
        HandleDrinkConsumed();
    }
}

void UTutorialStep_ConsumeServedDrink::ExitStep()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PreDrinkTimer);
        World->GetTimerManager().ClearTimer(AfterDrinkTimer);
    }

    Super::ExitStep();
}

void UTutorialStep_ConsumeServedDrink::HandleDrinkConsumed()
{
    AActor* Cup = nullptr;
    if (ATutorialDirector* D = GetDirector())
    {
        Cup = D->LastServedCup;
    }

    if (Cup)
    {
        if (bDestroyCupInsteadOfHide)
        {
            Cup->Destroy();
        }
        else
        {
            Cup->SetActorHiddenInGame(true);
            Cup->SetActorEnableCollision(false);
            Cup->SetActorTickEnabled(false);
        }
    }

    // 排队 AfterDrinkDelay
    if (UWorld* World = GetWorld())
    {
        if (AfterDrinkDelay > 0.f)
        {
            World->GetTimerManager().SetTimer(AfterDrinkTimer, this,
                &UTutorialStep_ConsumeServedDrink::HandleStepFinish, AfterDrinkDelay, false);
        }
        else
        {
            HandleStepFinish();
        }
    }
    else
    {
        MarkCompleted();
    }
}

void UTutorialStep_ConsumeServedDrink::HandleStepFinish()
{
    if (!IsCompleted())
    {
        MarkCompleted();
    }
}
