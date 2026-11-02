#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NarrativeWidget.generated.h"

class UTextBlock;
class USceneComponent;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNarrativeTypewriterFinishedSignature);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnNarrativePresentationFinishedNative, uint32);

/** World-space dialogue presentation. Existing Text_Body / Text_Speaker bindings are preserved. */
UCLASS(BlueprintType, Blueprintable)
class CYBERCAFE_API UNarrativeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetText(const FText& InText);

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetSpeaker(const FText& InSpeaker);

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void PlayTypewriter(const FText& InText, float CharsPerSec);

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SkipTypewriter();

    /** Cancel pending reveal/audio without signaling successful completion. */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void StopTypewriter();

    UFUNCTION(BlueprintPure, Category = "Narrative")
    bool IsTyping() const { return bTyping; }

    UFUNCTION(BlueprintPure, Category = "Narrative")
    FString GetVisibleText() const { return FullText.Left(VisibleCharCount); }

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetInputHintEnabled(bool bEnabled);

    /** Audio comes from this NPC component, never the UI panel. Null means silent. */
    UFUNCTION(BlueprintCallable, Category = "Narrative|Voice")
    void SetVoiceAnchor(USceneComponent* InAnchor);

    UPROPERTY(BlueprintAssignable, Category = "Narrative")
    FOnNarrativeTypewriterFinishedSignature OnTypewriterFinished;

    // Serial-bearing internal signal prevents an old completion from completing a replacement line.
    FOnNarrativePresentationFinishedNative OnPresentationFinishedNative;
    uint32 GetPresentationSerial() const { return PresentationSerial; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Typewriter", meta = (ClampMin = "0.0"))
    float CommaPauseSeconds = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Typewriter", meta = (ClampMin = "0.0"))
    float SentencePauseSeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice")
    bool bEnableVoice = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice")
    TObjectPtr<USoundBase> VoiceTickSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice")
    TObjectPtr<USoundAttenuation> VoiceAttenuation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float VoiceVolume = 0.22f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice", meta = (ClampMin = "1", ClampMax = "8"))
    int32 VoiceEveryCharacters = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice", meta = (ClampMin = "0.03"))
    float MinVoiceIntervalSeconds = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice", meta = (ClampMin = "0.5", ClampMax = "2.0"))
    float VoicePitchMin = 0.94f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|Voice", meta = (ClampMin = "0.5", ClampMax = "2.0"))
    float VoicePitchMax = 1.06f;

    /** Existing hook fires on each reveal. Do not restart panel entrance animation per character. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Narrative", meta = (DisplayName = "On Text Updated"))
    void ReceiveTextUpdated(const FText& NewText);

protected:
    virtual void NativeDestruct() override;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
    TObjectPtr<UTextBlock> Text_Body;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
    TObjectPtr<UTextBlock> Text_Speaker;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
    TObjectPtr<UTextBlock> Text_ContinueHint;

private:
    friend class FNarrativeWidgetTest;
    FTimerHandle TypewriterTimer;
    FString FullText;
    int32 VisibleCharCount = 0;
    float TypewriterInterval = 0.f;
    bool bTyping = false;
    bool bInputHintEnabled = false;
    int32 VoiceCharacters = 0;
    float LastVoiceTime = -1000.f;
    uint32 PresentationSerial = 0;

    UPROPERTY(Transient)
    TWeakObjectPtr<USceneComponent> VoiceAnchor;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> VoiceAudio;

    void ApplyBodyText(const FText& InText);
    void TickTypewriter();
    void CompleteTypewriter(bool bStopTailSound = false);
    void RefreshInputHint();
    void PlayVoiceTick(uint32 Character);
    void StopVoice();
};
