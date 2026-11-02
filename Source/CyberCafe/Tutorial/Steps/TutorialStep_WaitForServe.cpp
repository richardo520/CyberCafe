// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Serving/ServingSpotActor.h"
// @trace RC-04
#include "Liquid/LiquidContainerActor.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "Tutorial/TutorialNPC.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

void UTutorialStep_WaitForServe::EnterStep(ATutorialDirector* InDirector)
{
    Super::EnterStep(InDirector);
    bActive = true;
    if (RequiredSpot && RequiredRecipe)
    {
        PreviousRecipe = RequiredSpot->ActiveRecipe; bPreviousEnabled = RequiredSpot->bAcceptSubmissions;
        RequiredSpot->SetActiveRecipe(RequiredRecipe);
        RequiredSpot->SetSubmissionEnabled(true);
        OwnedOrderSerial = RequiredSpot->GetOrderSerial();
        RequiredSpot->OnDrinkValidated.AddUniqueDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkValidated);
    }
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnDrinkServed.RemoveDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkServed);
        Sys->OnDrinkServed.AddDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkServed);
    }
}

void UTutorialStep_WaitForServe::ExitStep()
{
    bActive = false;
    if (RequiredSpot && RequiredRecipe)
    {
        RequiredSpot->OnDrinkValidated.RemoveDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkValidated);
        if (!RequiredSpot->HasBeenServed() && RequiredSpot->ActiveRecipe == RequiredRecipe && RequiredSpot->GetOrderSerial() == OwnedOrderSerial)
        {
            RequiredSpot->SetActiveRecipe(PreviousRecipe.Get());
            RequiredSpot->SetSubmissionEnabled(bPreviousEnabled);
        }
    }
    if (FeedbackSerial && GetWorld())
        if (UNarrativeSubsystem* N = GetWorld()->GetSubsystem<UNarrativeSubsystem>()) N->CancelPresentation(FeedbackSerial);
    FeedbackSerial = 0;
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnDrinkServed.RemoveDynamic(this, &UTutorialStep_WaitForServe::HandleDrinkServed);
    }

    Super::ExitStep();
}

void UTutorialStep_WaitForServe::HandleDrinkServed(AServingSpotActor* ServingSpot, ALiquidContainerActor* Cup)
{
    if (!bActive || IsCompleted() || !IsValid(Cup))
    {
        return;
    }

    // 若配置了 RequiredSpot，只响应该 Spot
    if (RequiredSpot && ServingSpot != RequiredSpot)
    {
        return;
    }

    if (RequiredRecipe && (!ServingSpot || (!ServingSpot->HasAcceptedCup(Cup, RequiredRecipe->RecipeId) || ServingSpot->GetAcceptedOrderSerial() != OwnedOrderSerial))) return;
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

void UTutorialStep_WaitForServe::HandleDrinkValidated(AServingSpotActor* Spot, ALiquidContainerActor* Cup, const FDrinkValidationResult& Result)
{
    if (!bActive || IsCompleted() || Spot != RequiredSpot || !RequiredRecipe || Spot->GetOrderSerial() != OwnedOrderSerial || Result.RecipeId != RequiredRecipe->RecipeId || Result.bAccepted || !IsValid(Cup)) return;
    if (UNarrativeSubsystem* N = GetWorld()->GetSubsystem<UNarrativeSubsystem>())
    {
        ATutorialDirector* D = GetDirector();
        N->SetInputHintEnabled(false);
        if (D)
        {
            N->SetSpeaker(D->DialogueSpeakerName);
            USceneComponent* Anchor = D->bDialogueVoice && IsValid(D->NPCRef) ? Cast<USceneComponent>(D->NPCRef->GetDefaultSubobjectByName(TEXT("HeadAnchor"))) : nullptr;
            N->SetVoiceAnchor(Anchor);
        }
        const uint32 ExpectedSerial = N->GetPresentationSerial() + 1;
        N->PlayTypewriter(Result.Feedback, D ? D->DialogueCharsPerSecond : 30.f, 0.f);
        FeedbackSerial = bActive && N->GetPresentationSerial() == ExpectedSerial ? ExpectedSerial : 0;
    }
}
