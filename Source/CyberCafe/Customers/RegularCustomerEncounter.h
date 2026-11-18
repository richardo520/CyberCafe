#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/MenuPointerTarget.h"
#include "Serving/DrinkRecipe.h"
#include "Customers/RegularCustomerMemory.h"
#include "Dialogue/RegularDialogueDiagnostics.h"
#include "RegularCustomerEncounter.generated.h"

class ATutorialNPC;
class AServingSpotActor;
class UWidgetComponent;
class URegularCustomerMemorySubsystem;
class URegularCustomerWidget;
class UCyberCafeEventSubsystem;
class ALiquidContainerActor;
class URegularCustomerReactions;
class URegularCustomerAttention;
class UAnimInstance;
class URegularDialogueClient;
class UAudioComponent;

UENUM(BlueprintType)
enum class ERegularCustomerStage : uint8 { Greeting, TopicChoice, Preparing, Tasting, Completed, Interrupted, Unavailable, Chatting, ReorderOffer };

/** An opt-in regular-customer encounter, independent of the tutorial director. */
UCLASS()
class CYBERCAFE_API ARegularCustomerEncounter : public AActor, public IMenuPointerTarget
{
    GENERATED_BODY()
public:
    ARegularCustomerEncounter();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<ATutorialNPC> Customer;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<AServingSpotActor> ServingSpot;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<UDrinkRecipe> Recipe;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TArray<TObjectPtr<UDrinkRecipe>> FollowupRecipes;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<AOrderHologramActor> OrderDisplay;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") FName CustomerId = TEXT("Regular_NightShift_01");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") FText CustomerName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") bool bAutoStart = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer|Attention") TSubclassOf<UAnimInstance> AttentionLayer;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<URegularCustomerReactions> Reactions;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Regular Customer|AI") TObjectPtr<URegularDialogueClient> DialogueClient;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Regular Customer|Panel") bool bFacePlayer = true;
    /** Uniform UI size per pixel; actor Y scale controls width without stretching glyphs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Regular Customer|Panel", meta=(ClampMin="0.01", ClampMax="0.3")) float PanelScale = .08f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Regular Customer|Audio") TObjectPtr<UAudioComponent> DialogueNoticeAudio;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Regular Customer|Audio") bool bEnableDialogueNotice = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Regular Customer|Panel", meta=(ClampMin="4", ClampMax="40")) float CharactersPerSecond = 18.f;
    bool UpdatePanelFacing(const FVector& ViewLocation);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<UWidgetComponent> Panel;
    UFUNCTION(BlueprintCallable, Category="Regular Customer") bool StartEncounter();
    UFUNCTION(BlueprintCallable, Category="Regular Customer") bool Choose(int32 Index);
    UFUNCTION(BlueprintCallable, Category="Regular Customer") void CancelEncounter();
    UFUNCTION(BlueprintPure, Category="Regular Customer") ERegularCustomerStage GetStage() const { return Stage; }
    UFUNCTION(BlueprintPure, Category="Regular Customer") FText GetLine() const { return Line; }
    UFUNCTION(BlueprintPure, Category="Regular Customer") TArray<FText> GetChoices() const { return Choices; }
    const FDrinkValidationResult& GetAcceptedSnapshot() const { return Accepted; }
    virtual bool TracePointer(const FVector& Start, const FVector& Direction, double Limit, FMenuPointerHit& Hit) const override;
    virtual bool ActivatePointer(EControllerHand Hand, const FMenuPointerHit& Hit) override;
    int32 ActionAt(const FVector& Point) const;
    FRegularDialogueLinkView GetLinkView() const;
    bool AreLinkDetailsExpanded() const { return bLinkDetailsExpanded; }
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FRegularPresentationFeedback;
    friend class FRegularContinuousConversation;
    friend class FRegularLinkProvenance;
#endif
    bool BeginDrinkOrder();
    bool BeginNextDrink();
    void ContinueConversation(int32 Choice);
    void OfferNextDrink();
    FString CurrentRecipeName() const;
    void Present(const FString& Text, FName Cue = NAME_None);
    void CommitLine(const FString& Text, ERegularDialogueSource Source = ERegularDialogueSource::Local, const FString& Reason = TEXT("authored_line"));
    void RefreshPanel();
    void RefreshLink();
    void ToggleLinkDetails();
    void FaceLocalPlayer();
    void UpdatePanelScale();
    void StopDialogueNotice();
    void Unbind();
    UFUNCTION() void HandleValidation(AServingSpotActor* Spot, ALiquidContainerActor* Cup, const FDrinkValidationResult& Result);
    UFUNCTION() void HandleFinished(ALiquidContainerActor* Cup, bool bCompleted, double ConsumedML);
    void HandleReaction(FName Cue, const FString& Text, AActor* Focus);
    UPROPERTY(Transient) TObjectPtr<URegularCustomerAttention> Attention;
    FRegularCustomerMemory PreviousVisit;
    UPROPERTY(Transient) TObjectPtr<URegularCustomerMemorySubsystem> Memory;
    UPROPERTY(Transient) TObjectPtr<UCyberCafeEventSubsystem> Events;
    UPROPERTY(Transient) TWeakObjectPtr<ALiquidContainerActor> AcceptedCup;
    UPROPERTY(Transient) FDrinkValidationResult Accepted;
    UPROPERTY(Transient) ERegularCustomerStage Stage = ERegularCustomerStage::Unavailable;
    UPROPERTY(Transient) FText Line;
    UPROPERTY(Transient) TArray<FText> Choices;
    FName Topic;
    FName LoungeTopic = TEXT("NightShift");
    FGuid Visit;
    FGuid DrinkOrderId;
    int32 ConversationTurns = 0;
    int32 TurnsSinceOffer = 0;
    int32 DrinksThisVisit = 0;
    UPROPERTY(Transient) TObjectPtr<UDrinkRecipe> NextRecipe;
    UPROPERTY(Transient) TObjectPtr<UDrinkRecipe> InitialRecipe;
    FString LastPlayerReply;
    FString RecentDialogue;
    uint64 LastClickFrame = MAX_uint64;
    bool bEnding = false;
    uint32 OwnedOrderSerial = 0;
    uint64 PresentationSerial = 0;
    FString DeferredCloudLine;
    double DeferredCloudUntil = 0;
    ERegularDialogueSource DeferredSource = ERegularDialogueSource::Local;
    FString DeferredReason;
    ERegularDialogueSource LineSource = ERegularDialogueSource::Local;
    FString LineReason = TEXT("authored_line");
    bool bWaitingForCloud = false;
    bool bLinePlaceholder = false;
    bool bLinkDetailsExpanded = false;
    float LinkRefreshElapsed = 0;
};
