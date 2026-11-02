// @trace NAR-02 NAR-04 -- Input consumed once; reading duration starts only after complete reveal.
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "UI/Narrative/NarrativeTextPolicy.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UTutorialStep_ShowMessage::EnterStep(ATutorialDirector* InDirector)
{
    if (bActive) ExitStep();
    Super::EnterStep(InDirector);
    bActive = true;
    OwnedPresentationSerial = 0;
    UWorld* World = GetWorld();
    if (!World) { MarkCompleted(); return; }
    EnterGameTime = World->GetTimeSeconds();
    LastRevealGameTime = -1000.f;
    bAdvanceQueued = false;
    bAwaitingPresentation = bUseTypewriter && !HintText.IsEmpty();
    if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
    {
        if (bAwaitingPresentation)
        {
            OwnedPresentationSerial = N->GetPresentationSerial() + 1;
            PresentationFinishedHandle = N->OnTextPresentationFinishedNative.AddUObject(this, &UTutorialStep_ShowMessage::HandleOwnedPresentationFinished);
            PresentationCancelledHandle = N->OnTextPresentationCancelledNative.AddUObject(this, &UTutorialStep_ShowMessage::HandleOwnedPresentationFinished);
        }
    }
    else bAwaitingPresentation = false;
    if (!bAwaitingPresentation) HandlePresentationFinished();
    if (bAllowSkipByInput)
    {
        if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
            Sys->OnPlayerSkipRequested.AddUniqueDynamic(this, &UTutorialStep_ShowMessage::HandlePlayerSkipRequested);
    }
    if (Duration <= 0.f && !bAllowSkipByInput)
        UE_LOG(LogTemp, Warning, TEXT("[TutorialStep_ShowMessage] Duration<=0 and input disabled; no completion condition configured."));
}

void UTutorialStep_ShowMessage::HandlePresentationFinished()
{
    if (!bActive) return;
    bAwaitingPresentation = false;
    if (UWorld* World = GetWorld())
    {
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
        {
            N->OnTextPresentationFinishedNative.Remove(PresentationFinishedHandle);
            N->OnTextPresentationCancelledNative.Remove(PresentationCancelledHandle);
        }
        if (!IsCompleted() && !bAdvanceQueued && Duration > 0.f)
            World->GetTimerManager().SetTimer(DurationTimer, this, &UTutorialStep_ShowMessage::HandleDurationElapsed, Duration, false);
    }
}

void UTutorialStep_ShowMessage::RemoveSubscriptions()
{
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
        Sys->OnPlayerSkipRequested.RemoveDynamic(this, &UTutorialStep_ShowMessage::HandlePlayerSkipRequested);
    if (UWorld* World = GetWorld())
    {
        if (UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>())
        {
            N->OnTextPresentationFinishedNative.Remove(PresentationFinishedHandle);
            N->OnTextPresentationCancelledNative.Remove(PresentationCancelledHandle);
        }
    }
}

void UTutorialStep_ShowMessage::HandleOwnedPresentationFinished(uint32 FinishedSerial)
{
    if (bActive && FinishedSerial == OwnedPresentationSerial) HandlePresentationFinished();
}

void UTutorialStep_ShowMessage::ExitStep()
{
    bActive = false;
    RemoveSubscriptions();
    if (bAwaitingPresentation && OwnedPresentationSerial != 0 && GetWorld())
        if (UNarrativeSubsystem* N = GetWorld()->GetSubsystem<UNarrativeSubsystem>())
            N->CancelPresentation(OwnedPresentationSerial);
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DurationTimer);
        World->GetTimerManager().ClearTimer(AdvanceTimer);
    }
    bAwaitingPresentation = false;
    bAdvanceQueued = false;
    Super::ExitStep();
}

void UTutorialStep_ShowMessage::HandleDurationElapsed()
{
    RemoveSubscriptions();
    bAdvanceQueued = false;
    if (bActive && !IsCompleted()) MarkCompleted();
}

void UTutorialStep_ShowMessage::QueueAdvance()
{
    if (bAdvanceQueued) return;
    RemoveSubscriptions();
    bAdvanceQueued = true;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DurationTimer);
        // Never create a new input subscription inside the same multicast input broadcast.
        AdvanceTimer = World->GetTimerManager().SetTimerForNextTick(this, &UTutorialStep_ShowMessage::HandleDurationElapsed);
    }
    else HandleDurationElapsed();
}

void UTutorialStep_ShowMessage::HandlePlayerSkipRequested()
{
    if (!bActive || !GetDirector() || IsCompleted() || bAdvanceQueued || !bAllowSkipByInput) return;
    UWorld* World = GetWorld();
    if (!World) { QueueAdvance(); return; }
    const float Now = World->GetTimeSeconds();
    if (Now - LastRevealGameTime < 0.15f) return; // duplicate hands / same-frame delivery guard
    UNarrativeSubsystem* N = World->GetSubsystem<UNarrativeSubsystem>();
    const auto Action = CyberCafeNarrative::ResolveSkip(N && N->IsTyping(), bAllowSkipByInput,
        Now - EnterGameTime < FMath::Max(0.f, InputGracePeriod));
    if (Action == CyberCafeNarrative::ESkipAction::Ignore) return;
    if (Action == CyberCafeNarrative::ESkipAction::Reveal)
    {
        LastRevealGameTime = Now;
        N->SkipTypewriter();
        return;
    }
    QueueAdvance();
}
