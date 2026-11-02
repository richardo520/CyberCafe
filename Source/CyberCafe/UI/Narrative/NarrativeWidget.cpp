// @trace NAR-01 NAR-03 -- Cadenced, cancelable reveal with bounded spatial audio.
#include "UI/Narrative/NarrativeWidget.h"
#include "UI/Narrative/NarrativeTextPolicy.h"
#include "Components/TextBlock.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UNarrativeWidget::SetText(const FText& InText)
{
    StopTypewriter();
    FullText = InText.ToString();
    VisibleCharCount = FullText.Len();
    ApplyBodyText(InText);
    RefreshInputHint();
}

void UNarrativeWidget::SetSpeaker(const FText& InSpeaker)
{
    if (Text_Speaker)
    {
        Text_Speaker->SetText(InSpeaker);
        Text_Speaker->SetVisibility(InSpeaker.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
}

void UNarrativeWidget::SetInputHintEnabled(bool bEnabled)
{
    bInputHintEnabled = bEnabled;
    RefreshInputHint();
}

void UNarrativeWidget::RefreshInputHint()
{
    if (!Text_ContinueHint) return;
    Text_ContinueHint->SetVisibility(bInputHintEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    Text_ContinueHint->SetText(bTyping
        ? NSLOCTEXT("Narrative", "RevealHint", "扳机 · 显示完整文字")
        : NSLOCTEXT("Narrative", "ContinueHint", "扳机 · 跳过当前消息"));
}

void UNarrativeWidget::SetVoiceAnchor(USceneComponent* InAnchor)
{
    if (VoiceAnchor.Get() != InAnchor) StopVoice();
    VoiceAnchor = InAnchor;
}

void UNarrativeWidget::PlayTypewriter(const FText& InText, float CharsPerSec)
{
    StopTypewriter();
    FullText = InText.ToString();
    VisibleCharCount = 0;
    VoiceCharacters = 0;
    LastVoiceTime = -1000.f;
    const uint32 Serial = PresentationSerial;
    if (CharsPerSec <= 0.f || FullText.IsEmpty() || !GetWorld())
    {
        VisibleCharCount = FullText.Len();
        ApplyBodyText(InText);
        RefreshInputHint();
        if (Serial == PresentationSerial)
        {
            OnPresentationFinishedNative.Broadcast(Serial);
            if (Serial == PresentationSerial) OnTypewriterFinished.Broadcast();
        }
        return;
    }
    TypewriterInterval = 1.f / FMath::Clamp(CharsPerSec, 1.f, 120.f);
    bTyping = true;
    RefreshInputHint();
    ApplyBodyText(FText::GetEmpty());
    // A Blueprint text hook may replace or cancel the line synchronously.
    if (Serial != PresentationSerial || !bTyping) return;
    GetWorld()->GetTimerManager().SetTimer(TypewriterTimer, this, &UNarrativeWidget::TickTypewriter, TypewriterInterval, false);
}

void UNarrativeWidget::TickTypewriter()
{
    if (!bTyping) return;
    const uint32 Serial = PresentationSerial;
    const uint32 Character = CyberCafeNarrative::ScalarAt(FullText, VisibleCharCount).Value;
    VisibleCharCount = CyberCafeNarrative::NextBoundary(FullText, VisibleCharCount);
    ApplyBodyText(FText::FromString(FullText.Left(VisibleCharCount)));
    if (Serial != PresentationSerial || !bTyping) return;
    PlayVoiceTick(Character);
    if (VisibleCharCount >= FullText.Len())
    {
        CompleteTypewriter();
        return;
    }
    const float Delay = TypewriterInterval + CyberCafeNarrative::PauseAfter(Character, CommaPauseSeconds, SentencePauseSeconds);
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(TypewriterTimer, this, &UNarrativeWidget::TickTypewriter, Delay, false);
    }
}

void UNarrativeWidget::SkipTypewriter()
{
    if (!bTyping) return;
    const uint32 Serial = PresentationSerial;
    VisibleCharCount = FullText.Len();
    ApplyBodyText(FText::FromString(FullText));
    if (Serial == PresentationSerial && bTyping) CompleteTypewriter(true);
}

void UNarrativeWidget::CompleteTypewriter(bool bStopTailSound)
{
    if (!bTyping) return;
    bTyping = false;
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(TypewriterTimer);
    const uint32 Serial = PresentationSerial;
    if (bStopTailSound) StopVoice(); // natural completion lets the final bounded grain finish
    RefreshInputHint();
    OnPresentationFinishedNative.Broadcast(Serial);
    if (Serial == PresentationSerial) OnTypewriterFinished.Broadcast();
}

void UNarrativeWidget::StopTypewriter()
{
    ++PresentationSerial;
    bTyping = false;
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(TypewriterTimer);
    StopVoice();
    RefreshInputHint();
}

void UNarrativeWidget::PlayVoiceTick(uint32 Character)
{
    if (!CyberCafeNarrative::IsVoiceCharacter(Character)) return;
    ++VoiceCharacters;
    if (!bEnableVoice || !VoiceTickSound || !VoiceAnchor.IsValid() || !GetWorld()) return;
    if (VoiceCharacters % FMath::Max(VoiceEveryCharacters, 1) != 0) return;
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now - LastVoiceTime < FMath::Max(0.03f, MinVoiceIntervalSeconds)) return;
    LastVoiceTime = Now;
    const float Pitch = FMath::FRandRange(FMath::Min(VoicePitchMin, VoicePitchMax), FMath::Max(VoicePitchMin, VoicePitchMax));
    // One component at a time prevents stacking dozens of overlapping voice grains.
    StopVoice();
    VoiceAudio = UGameplayStatics::SpawnSoundAttached(VoiceTickSound, VoiceAnchor.Get(), NAME_None,
        FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true,
        FMath::Clamp(VoiceVolume, 0.f, 1.f), Pitch, 0.f, VoiceAttenuation, nullptr, true);
}

void UNarrativeWidget::StopVoice()
{
    if (IsValid(VoiceAudio)) VoiceAudio->Stop();
    VoiceAudio = nullptr;
}

void UNarrativeWidget::NativeDestruct()
{
    StopTypewriter();
    Super::NativeDestruct();
}

void UNarrativeWidget::ApplyBodyText(const FText& InText)
{
    if (Text_Body) Text_Body->SetText(InText);
    ReceiveTextUpdated(InText);
}
