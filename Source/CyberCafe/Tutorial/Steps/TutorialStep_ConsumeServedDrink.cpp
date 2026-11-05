// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "Tutorial/TutorialDirector.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/CustomerDrinkComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Serving/ServingSpotActor.h"
#include "EngineUtils.h"

void UTutorialStep_ConsumeServedDrink::EnterStep(ATutorialDirector* InDirector)
{
    Super::EnterStep(InDirector);

    UWorld* World = GetWorld();
    if (!World)
    {
        MarkCompleted();
        return;
    }

    // @trace BC-04. Opt-in customer uses actual completion, never the legacy
    // timer-driven hide/destroy fallback, including unsupported cups/failures.
    bActiveCustomerStep=true;bUsesCustomerPerformance=false;OwnedDrinkCup.Reset();DrinkingComponent.Reset();
    auto* NPC=InDirector?InDirector->NPCRef.Get():nullptr;
    if(IsValid(NPC)&&NPC->CustomerDrink&&NPC->CustomerDrink->bEnabled)
    {
        bUsesCustomerPerformance=true;auto* Component=NPC->CustomerDrink.Get();
        auto* Cup=Cast<ALiquidContainerActor>(InDirector->LastServedCup.Get());
        AServingSpotActor* Spot=nullptr;
        for(TActorIterator<AServingSpotActor> It(World);It;++It)if(It->GetServedCup()==Cup&&It->HasBeenServed()){Spot=*It;break;}
        OwnedDrinkCup=Cup;DrinkingComponent=Component;
        Component->OnDrinkEnded.AddUniqueDynamic(this,&UTutorialStep_ConsumeServedDrink::HandleCustomerDrinkEnded);
        if(!Component->BeginDrink(Cup,Spot))
        {
            Component->OnDrinkEnded.RemoveDynamic(this,&UTutorialStep_ConsumeServedDrink::HandleCustomerDrinkEnded);
            // Keep the accepted cup visible if animation can't be started.
            CompletedText=FText::FromString(TEXT("这次先把杯子留在这里吧。"));
            World->GetTimerManager().SetTimer(AfterDrinkTimer,this,&UTutorialStep_ConsumeServedDrink::HandleStepFinish,FMath::Max(.001f,AfterDrinkDelay),false);
        }
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
    bActiveCustomerStep=false;
    if(auto* C=DrinkingComponent.Get())
    {
        C->OnDrinkEnded.RemoveDynamic(this,&UTutorialStep_ConsumeServedDrink::HandleCustomerDrinkEnded);
        if(C->GetDrinkingCup()==OwnedDrinkCup.Get()&&OwnedDrinkCup.IsValid())C->CancelDrink();
    }
    DrinkingComponent.Reset();OwnedDrinkCup.Reset();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PreDrinkTimer);
        World->GetTimerManager().ClearTimer(AfterDrinkTimer);
    }

    Super::ExitStep();
}

void UTutorialStep_ConsumeServedDrink::HandleCustomerDrinkEnded(ALiquidContainerActor* Cup,bool Completed,double ConsumedML)
{
    if(!bActiveCustomerStep||!bUsesCustomerPerformance||IsCompleted())return;
    // Destroyed cups are nullptr; only the component subscribed by this step
    // can invoke this owned callback. Exit removes it before cancellation.
    if(Cup&&Cup!=OwnedDrinkCup.Get())return;
    if(auto* C=DrinkingComponent.Get())C->OnDrinkEnded.RemoveDynamic(this,&UTutorialStep_ConsumeServedDrink::HandleCustomerDrinkEnded);
    DrinkingComponent.Reset();OwnedDrinkCup.Reset();
    if(!Completed)CompletedText=FText::FromString(TEXT("先把杯子放回这里吧。"));
    if(auto* W=GetWorld())W->GetTimerManager().SetTimer(AfterDrinkTimer,this,&UTutorialStep_ConsumeServedDrink::HandleStepFinish,FMath::Max(.001f,AfterDrinkDelay),false);
    else MarkCompleted();
}
void UTutorialStep_ConsumeServedDrink::HandleDrinkConsumed()
{
    if(bUsesCustomerPerformance)return;
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
