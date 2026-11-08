// @trace NAR-01 NAR-04 NAR-05 -- Dialogue presentation is separate from task completion.
#include "Tutorial/TutorialDirector.h"
#include "CafeSceneMusicComponent.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialStep.h"
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "Tutorial/CustomerDrinkComponent.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATutorialDirector::ATutorialDirector()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneMusic = CreateDefaultSubobject<UCafeSceneMusicComponent>(TEXT("SceneMusic"));
    FinishedText = NSLOCTEXT("Tutorial", "DefaultFinished", "教学结束，欢迎来上班！");
    DialogueSpeakerName = NSLOCTEXT("Tutorial", "DefaultSpeaker", "咖啡馆向导");
}

void ATutorialDirector::BeginPlay()
{
    Super::BeginPlay();
    if (UWorld* World = GetWorld())
    {
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
        {
            if (NarrativeHUDWidgetClass) N->SetHUDWidgetClass(NarrativeHUDWidgetClass);
        }
        if (bAutoStart)
            World->GetTimerManager().SetTimer(StartTimerHandle, this, &ATutorialDirector::StartTutorial, .001f, false);
    }
    else if (bAutoStart) StartTutorial();
}

void ATutorialDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearActiveStep();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StartTimerHandle);
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>()) N->HideText();
    }
    Super::EndPlay(EndPlayReason);
}

void ATutorialDirector::RemoveFeedbackSubscription()
{
    if (UWorld* World = GetWorld())
    {
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
        {
            N->OnTextPresentationFinishedNative.Remove(FeedbackFinishedHandle);
            N->OnTextPresentationCancelledNative.Remove(FeedbackCancelledHandle);
        }
    }
    bWaitingForFeedback = false;
    OwnedFeedbackSerial = 0;
}

void ATutorialDirector::ClearActiveStep()
{
    RemoveFeedbackSubscription();
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(CheerTimerHandle);
    if (Steps.IsValidIndex(CurrentStepIndex) && Steps[CurrentStepIndex])
    {
        Steps[CurrentStepIndex]->OnStepCompleted.RemoveDynamic(this, &ATutorialDirector::HandleStepCompleted);
        Steps[CurrentStepIndex]->ExitStep();
    }
    CurrentStepIndex = -1;
}

void ATutorialDirector::StartTutorial()
{
    ClearActiveStep();
    if (Steps.IsEmpty()) { FinishTutorial(); return; }
    StartStepAt(0);
}

void ATutorialDirector::SkipTutorial()
{
    ClearActiveStep();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(StartTimerHandle);
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>()) N->HideText();
    }
    FinishTutorial();
}

void ATutorialDirector::StartStepAt(int32 Index)
{
    while (Steps.IsValidIndex(Index) && !Steps[Index]) ++Index;
    if (!Steps.IsValidIndex(Index)) { FinishTutorial(); return; }
    UTutorialStep* Step = Steps[Index];
    CurrentStepIndex = Index;
    Step->OnStepCompleted.AddUniqueDynamic(this, &ATutorialDirector::HandleStepCompleted);
    Step->EnterStep(this);
    // Synchronous completion during Enter must not paint old HintText over feedback/next step.
    if (CurrentStepIndex != Index || Step->IsCompleted()) return;
    if (UTutorialStep_ShowMessage* Message = Cast<UTutorialStep_ShowMessage>(Step))
    {
        if (UWorld* World = GetWorld())
        {
            if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
                N->SetInputHintEnabled(Message->bAllowSkipByInput);
        }
        if (Message->bUseTypewriter)
            ShowDialogue(Step->HintText, bDialogueTypewriter ? Message->CharsPerSecond : 0.f);
        else ShowNarrative(Step->HintText);
    }
    else
    {
        // Grab/serve/consume status is readable immediately and does not speak.
        if (UWorld* World = GetWorld())
        {
            if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>()) N->SetInputHintEnabled(false);
        }
        ShowNarrative(Step->HintText);
    }
}

void ATutorialDirector::HandleStepCompleted(UTutorialStep* CompletedStep)
{
    if (!CompletedStep || !Steps.IsValidIndex(CurrentStepIndex) || Steps[CurrentStepIndex] != CompletedStep) return;
    CompletedStep->OnStepCompleted.RemoveDynamic(this, &ATutorialDirector::HandleStepCompleted);
    UNarrativeSubsystem* N = GetWorld() ? GetWorld()->GetSubsystem<UNarrativeSubsystem>() : nullptr;
    if (N) N->SetInputHintEnabled(false);
    // @trace BC-04: a validated first-customer serve is a gameplay transition,
    // not a text-presentation gate. A replacement HUD line can suppress the old
    // typewriter completion forever; it must never suppress the drink action.
    const int32 NextIndex = CurrentStepIndex + 1;
    const bool bAutomaticCustomerDrink = Cast<UTutorialStep_WaitForServe>(CompletedStep) &&
        Steps.IsValidIndex(NextIndex) && Cast<UTutorialStep_ConsumeServedDrink>(Steps[NextIndex]) &&
        IsValid(NPCRef) && NPCRef->CustomerDrink && NPCRef->CustomerDrink->bEnabled;
    if (bAutomaticCustomerDrink)
    {
        const int32 CompletedIndex = CurrentStepIndex;
        UE_LOG(LogTemp, Log, TEXT("[CustomerDrink] Accepted serve schedules automatic tasting; director=%s cup=%s delay=%.3f"),
            *GetName(), *GetNameSafe(LastServedCup.Get()), CheerDuration);
        if (N && !CompletedStep->CompletedText.IsEmpty()) ShowNarrative(CompletedStep->CompletedText);
        // A Blueprint text hook may synchronously skip or restart this director.
        if (CurrentStepIndex == CompletedIndex && Steps.IsValidIndex(CurrentStepIndex) &&
            Steps[CurrentStepIndex] == CompletedStep && CompletedStep->IsCompleted()) ScheduleAfterCheer();
        return;
    }
    if (!CompletedStep->CompletedText.IsEmpty() && N)
    {
        bWaitingForFeedback = true;
        OwnedFeedbackSerial = N->GetPresentationSerial() + 1;
        FeedbackFinishedHandle = N->OnTextPresentationFinishedNative.AddUObject(this, &ATutorialDirector::HandleOwnedFeedbackFinished);
        FeedbackCancelledHandle = N->OnTextPresentationCancelledNative.AddUObject(this, &ATutorialDirector::HandleOwnedFeedbackFinished);
        ShowDialogue(CompletedStep->CompletedText, bDialogueTypewriter ? DialogueCharsPerSecond : 0.f);
        return;
    }
    ScheduleAfterCheer();
}

void ATutorialDirector::HandleOwnedFeedbackFinished(uint32 FinishedSerial)
{
    if (FinishedSerial == OwnedFeedbackSerial) HandleFeedbackPresentationFinished();
}

void ATutorialDirector::HandleFeedbackPresentationFinished()
{
    if (!bWaitingForFeedback) return;
    RemoveFeedbackSubscription();
    ScheduleAfterCheer();
}

void ATutorialDirector::ScheduleAfterCheer()
{
    if (UWorld* World = GetWorld())
    {
        if (CheerDuration > 0.f)
            World->GetTimerManager().SetTimer(CheerTimerHandle, this, &ATutorialDirector::AdvanceAfterCheer, CheerDuration, false);
        else CheerTimerHandle = World->GetTimerManager().SetTimerForNextTick(this, &ATutorialDirector::AdvanceAfterCheer);
    }
    else AdvanceAfterCheer();
}

void ATutorialDirector::AdvanceAfterCheer()
{
    if (Steps.IsValidIndex(CurrentStepIndex) && Steps[CurrentStepIndex]) Steps[CurrentStepIndex]->ExitStep();
    const int32 Next = CurrentStepIndex + 1;
    if (Steps.IsValidIndex(Next)) StartStepAt(Next);
    else FinishTutorial();
}

void ATutorialDirector::FinishTutorial()
{
    RemoveFeedbackSubscription();
    CurrentStepIndex = -1;
    if (UWorld* World = GetWorld())
    {
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
        {
            N->SetInputHintEnabled(false);
            if (!FinishedText.IsEmpty()) ShowDialogue(FinishedText, bDialogueTypewriter ? DialogueCharsPerSecond : 0.f, FinishedTextAutoHideSeconds);
            else N->HideText();
        }
    }
    OnTutorialFinished.Broadcast();
}

void ATutorialDirector::ShowNarrative(const FText& InText, float AutoHideSeconds)
{
    if (UWorld* World = GetWorld())
    {
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
        {
            N->SetSpeaker(FText::GetEmpty());
            N->SetVoiceAnchor(nullptr);
            N->ShowText(InText, AutoHideSeconds);
        }
    }
}

void ATutorialDirector::ShowDialogue(const FText& InText, float Speed, float AutoHideSeconds)
{
    if (UWorld* World = GetWorld())
    {
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
        {
            N->SetSpeaker(DialogueSpeakerName);
            USceneComponent* VoiceSource = nullptr;
            if (bDialogueVoice && IsValid(NPCRef))
            {
                VoiceSource = Cast<USceneComponent>(NPCRef->GetDefaultSubobjectByName(TEXT("HeadAnchor")));
                if (!VoiceSource) VoiceSource = NPCRef->GetRootComponent();
            }
            N->SetVoiceAnchor(VoiceSource);
            N->PlayTypewriter(InText, Speed, AutoHideSeconds);
        }
    }
}
