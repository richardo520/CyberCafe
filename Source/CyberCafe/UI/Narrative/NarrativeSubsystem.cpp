// @trace NAR-04 -- Presentation completion controls reading/auto-hide timers.
#include "UI/Narrative/NarrativeSubsystem.h"
#include "UI/Narrative/NarrativeHUDActor.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

void UNarrativeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UNarrativeSubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(AutoHideTimer);
    if (BoundWidget.IsValid())
    {
        BoundWidget->OnPresentationFinishedNative.Remove(WidgetFinishedHandle);
        BoundWidget->StopTypewriter();
    }
    BoundWidget.Reset();
    VoiceAnchor.Reset();
    HUDActor.Reset();
    HUDWidgetClass = nullptr;
    Super::Deinitialize();
}

void UNarrativeSubsystem::SetHUDWidgetClass(TSubclassOf<UNarrativeWidget> InClass)
{
    if (HUDWidgetClass == InClass) return;
    if (BoundWidget.IsValid())
    {
        BoundWidget->OnPresentationFinishedNative.Remove(WidgetFinishedHandle);
        BoundWidget->StopTypewriter();
        BoundWidget.Reset();
    }
    HUDWidgetClass = InClass;
    if (HUDActor.IsValid() && InClass) HUDActor->SetWidgetClass(InClass);
}

void UNarrativeSubsystem::EnsureHUDSpawned()
{
    if (HUDActor.IsValid()) return;
    UWorld* World = GetWorld();
    if (!World) return;
    TArray<AActor*> Existing;
    UGameplayStatics::GetAllActorsOfClass(World, ANarrativeHUDActor::StaticClass(), Existing);
    if (!Existing.IsEmpty())
    {
        if (ANarrativeHUDActor* FromLevel = Cast<ANarrativeHUDActor>(Existing[0]))
        {
            if (HUDWidgetClass) FromLevel->SetWidgetClass(HUDWidgetClass);
            HUDActor = FromLevel;
            return;
        }
    }
    if (!HUDWidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NarrativeSubsystem] Place a NarrativeHUDActor or configure HUDWidgetClass."));
        return;
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
#if WITH_EDITOR
    Params.ObjectFlags |= RF_Transient;
#endif
    ANarrativeHUDActor* Spawned = World->SpawnActor<ANarrativeHUDActor>(
        ANarrativeHUDActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (Spawned)
    {
        Spawned->SetWidgetClass(HUDWidgetClass);
        HUDActor = Spawned;
    }
}

UNarrativeWidget* UNarrativeSubsystem::PrepareWidget()
{
    EnsureHUDSpawned();
    UNarrativeWidget* W = HUDActor.IsValid() ? HUDActor->GetNarrativeWidget() : nullptr;
    if (!W) return nullptr;
    if (BoundWidget.Get() != W)
    {
        if (BoundWidget.IsValid())
        {
            BoundWidget->OnPresentationFinishedNative.Remove(WidgetFinishedHandle);
            BoundWidget->StopTypewriter();
        }
        BoundWidget = W;
        WidgetFinishedHandle = W->OnPresentationFinishedNative.AddUObject(this, &UNarrativeSubsystem::HandleWidgetFinished);
    }
    W->SetSpeaker(SpeakerName);
    W->SetInputHintEnabled(bInputHintEnabled);
    W->SetVoiceAnchor(VoiceAnchor.Get());
    return W;
}

void UNarrativeSubsystem::StartAutoHide(float Seconds)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AutoHideTimer);
        if (Seconds > 0.f) World->GetTimerManager().SetTimer(AutoHideTimer, this, &UNarrativeSubsystem::HandleAutoHide, Seconds, false);
    }
}

void UNarrativeSubsystem::ShowText(const FText& InText, float AutoHideSeconds)
{
    const uint32 Serial = ++PresentationSerial;
    PendingAutoHideSeconds = 0.f;
    StartAutoHide(0.f);
    if (UNarrativeWidget* W = PrepareWidget())
    {
        HUDActor->ShowPanel();
        W->SetText(InText);
        if (Serial == PresentationSerial) StartAutoHide(AutoHideSeconds);
    }
}

void UNarrativeSubsystem::PlayTypewriter(const FText& InText, float CharsPerSec, float AutoHideSeconds)
{
    ++PresentationSerial;
    PendingAutoHideSeconds = FMath::Max(0.f, AutoHideSeconds);
    StartAutoHide(0.f);
    if (UNarrativeWidget* W = PrepareWidget())
    {
        HUDActor->ShowPanel();
        W->PlayTypewriter(InText, CharsPerSec);
    }
    else
    {
        // Missing widget must not deadlock a step waiting for its line to finish.
        UE_LOG(LogTemp, Warning, TEXT("[NarrativeSubsystem] NarrativeWidget unavailable; completing presentation without rendering."));
        PendingAutoHideSeconds = 0.f;
        const uint32 Serial = PresentationSerial;
        OnTextPresentationFinishedNative.Broadcast(Serial);
        if (Serial == PresentationSerial) OnTextPresentationFinished.Broadcast();
    }
}

void UNarrativeSubsystem::HandleWidgetFinished(uint32 FinishedSerial)
{
    if (!BoundWidget.IsValid() || BoundWidget->GetPresentationSerial() != FinishedSerial) return;
    const uint32 Serial = PresentationSerial;
    const float Delay = PendingAutoHideSeconds;
    PendingAutoHideSeconds = 0.f;
    OnTextPresentationFinishedNative.Broadcast(Serial);
    if (Serial == PresentationSerial) OnTextPresentationFinished.Broadcast();
    // Subscribers may synchronously replace the line. Never schedule the OLD hide timer over a NEW line.
    if (Serial == PresentationSerial) StartAutoHide(Delay);
}

void UNarrativeSubsystem::SetSpeaker(const FText& InSpeaker)
{
    SpeakerName = InSpeaker;
    if (UNarrativeWidget* W = PrepareWidget()) W->SetSpeaker(InSpeaker);
}

void UNarrativeSubsystem::SetInputHintEnabled(bool bEnabled)
{
    bInputHintEnabled = bEnabled;
    if (BoundWidget.IsValid()) BoundWidget->SetInputHintEnabled(bEnabled);
}

void UNarrativeSubsystem::SetVoiceAnchor(USceneComponent* InAnchor)
{
    VoiceAnchor = InAnchor;
    if (BoundWidget.IsValid()) BoundWidget->SetVoiceAnchor(InAnchor);
}

bool UNarrativeSubsystem::IsTyping() const
{
    return BoundWidget.IsValid() && BoundWidget->IsTyping();
}

bool UNarrativeSubsystem::SkipTypewriter()
{
    if (!IsTyping()) return false;
    BoundWidget->SkipTypewriter();
    return true;
}

void UNarrativeSubsystem::CancelPresentation(uint32 OwnedSerial)
{
    if (OwnedSerial == PresentationSerial) HideText();
}

void UNarrativeSubsystem::HideText()
{
    const bool bWasTyping = IsTyping();
    const uint32 CancelledSerial = PresentationSerial;
    const uint32 CancelEpoch = ++PresentationSerial;
    PendingAutoHideSeconds = 0.f;
    StartAutoHide(0.f);
    if (BoundWidget.IsValid()) BoundWidget->StopTypewriter();
    if (HUDActor.IsValid()) HUDActor->HidePanel();
    if (bWasTyping)
    {
        OnTextPresentationCancelledNative.Broadcast(CancelledSerial);
        if (CancelEpoch == PresentationSerial) OnTextPresentationCancelled.Broadcast();
    }
}

bool UNarrativeSubsystem::IsVisible() const
{
    if (!HUDActor.IsValid()) return false;
    const UWidgetComponent* Panel = HUDActor->FindComponentByClass<UWidgetComponent>();
    return !HUDActor->IsHidden() && Panel && Panel->IsVisible() && HUDActor->GetNarrativeWidget();
}

void UNarrativeSubsystem::HandleAutoHide()
{
    HideText();
}
