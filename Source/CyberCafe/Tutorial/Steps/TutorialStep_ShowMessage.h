#pragma once
#include "CoreMinimal.h"
#include "Tutorial/TutorialStep.h"
#include "TutorialStep_ShowMessage.generated.h"

class AOrderHologramActor;
class UDrinkRecipe;

/** NPC line: reveal first, then start reading duration; first press reveals, next press advances. */
UCLASS(DisplayName = "Step: Show Message")
class CYBERCAFE_API UTutorialStep_ShowMessage : public UTutorialStep
{
    GENERATED_BODY()
public:
    /** Reading time AFTER complete reveal. <=0 waits for permitted input. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage", meta = (ClampMin = "0.0"))
    float Duration = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage")
    bool bAllowSkipByInput = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage", meta = (ClampMin = "0.0"))
    float InputGracePeriod = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage")
    bool bUseTypewriter = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage", meta = (ClampMin = "1.0", ClampMax = "120.0"))
    float CharsPerSecond = 30.f;

    // Optional discovery cue on the first spoken order; all other dialogue steps stay unchanged.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage|Menu")
    TObjectPtr<AOrderHologramActor> FirstOrderMenu;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage|Menu")
    TObjectPtr<UDrinkRecipe> FirstOrderRecipe;

    virtual void EnterStep(ATutorialDirector* InDirector) override;
    virtual void ExitStep() override;

protected:
    void HandleDurationElapsed();
    UFUNCTION()
    void HandlePlayerSkipRequested();
    UFUNCTION()
    void HandlePresentationFinished();
    void HandleOwnedPresentationFinished(uint32 FinishedSerial);
    FDelegateHandle PresentationFinishedHandle;
    FDelegateHandle PresentationCancelledHandle;

private:
    friend class FNarrativeFlowTest;
    void RemoveSubscriptions();
    void QueueAdvance();
    FTimerHandle DurationTimer;
    FTimerHandle AdvanceTimer;
    float EnterGameTime = 0.f;
    float LastRevealGameTime = -1000.f;
    bool bAwaitingPresentation = false;
    bool bAdvanceQueued = false;
    bool bActive = false;
    uint32 OwnedPresentationSerial = 0;
};
