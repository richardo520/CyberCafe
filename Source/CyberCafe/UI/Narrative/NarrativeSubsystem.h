#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "NarrativeSubsystem.generated.h"

class ANarrativeHUDActor;
class USceneComponent;

/** World-scoped narrative service; retains existing Blueprint entry points. */
UCLASS()
class CYBERCAFE_API UNarrativeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetHUDWidgetClass(TSubclassOf<UNarrativeWidget> InClass);

    UFUNCTION(BlueprintCallable, Category = "Narrative", meta = (AutoCreateRefTerm = "InText"))
    void ShowText(const FText& InText, float AutoHideSeconds = 0.f);

    /** AutoHideSeconds is reading time AFTER reveal finishes, including manual full reveal. */
    UFUNCTION(BlueprintCallable, Category = "Narrative", meta = (AutoCreateRefTerm = "InText"))
    void PlayTypewriter(const FText& InText, float CharsPerSec = 30.f, float AutoHideSeconds = 0.f);

    UFUNCTION(BlueprintCallable, Category = "Narrative", meta = (AutoCreateRefTerm = "InSpeaker"))
    void SetSpeaker(const FText& InSpeaker);

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetInputHintEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Narrative|Voice")
    void SetVoiceAnchor(USceneComponent* InAnchor);

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    bool SkipTypewriter();

    UFUNCTION(BlueprintPure, Category = "Narrative")
    bool IsTyping() const;

    uint32 GetPresentationSerial() const { return PresentationSerial; }
    void CancelPresentation(uint32 OwnedSerial);

    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void HideText();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Narrative")
    bool IsVisible() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Narrative")
    ANarrativeHUDActor* GetHUDActor() const { return HUDActor.Get(); }

    UPROPERTY(BlueprintAssignable, Category = "Narrative")
    FOnNarrativeTypewriterFinishedSignature OnTextPresentationFinished;

    /** Cancellation is distinct from finishing; waiting owners can safely release their timers. */
    UPROPERTY(BlueprintAssignable, Category = "Narrative")
    FOnNarrativeTypewriterFinishedSignature OnTextPresentationCancelled;

    FOnNarrativePresentationFinishedNative OnTextPresentationFinishedNative;
    FOnNarrativePresentationFinishedNative OnTextPresentationCancelledNative;

private:
    friend class FNarrativeFlowTest;
    void EnsureHUDSpawned();
    UNarrativeWidget* PrepareWidget();
    void StartAutoHide(float Seconds);

    void HandleWidgetFinished(uint32 FinishedSerial);
    FDelegateHandle WidgetFinishedHandle;
    void HandleAutoHide();

    UPROPERTY(Transient)
    TWeakObjectPtr<ANarrativeHUDActor> HUDActor;
    UPROPERTY(Transient)
    TWeakObjectPtr<UNarrativeWidget> BoundWidget;
    UPROPERTY(Transient)
    TWeakObjectPtr<USceneComponent> VoiceAnchor;
    UPROPERTY(Transient)
    TSubclassOf<UNarrativeWidget> HUDWidgetClass;

    FText SpeakerName;
    bool bInputHintEnabled = false;
    float PendingAutoHideSeconds = 0.f;
    FTimerHandle AutoHideTimer;
    uint32 PresentationSerial = 0;
};
