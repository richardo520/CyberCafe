#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "RegularCustomerReactions.generated.h"

class AServingSpotActor;
class AOrderHologramActor;
class ACoffeeGrinderActor;
class UCyberCafeEventSubsystem;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FRegularReactionReady, FName, const FString&, AActor*);
DECLARE_MULTICAST_DELEGATE_OneParam(FRegularActivityObserved, AActor*);

/** Bounded, per-visit reactions. One pending line, cooldown, expiry and explicit reading gates. */
UCLASS()
class CYBERCAFE_API URegularCustomerReactions : public UActorComponent
{
    GENERATED_BODY()
public:
    URegularCustomerReactions();
    void BeginPreparation(AServingSpotActor* Spot, AOrderHologramActor* Display);
    void StopReactions();
    void HoldForDialogue(const FString& Text);
    void QueueRejection(FName Reason, const FString& Text);
    bool DidObserveGrinding() const { return bObservedGrinding; }
    int32 GetSpokenCount() const { return SpokenCount; }
    bool IsPlayerReadingMenu() const { return IsReadingMenu(); }
    FRegularReactionReady OnReady;
    FRegularActivityObserved OnActivity;
protected:
    virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool IsReadingMenu() const;
    void Queue(FName Id, const FString& Text, AActor* Focus, int32 Priority);
    UFUNCTION() void HandleGrab(AActor* Object, EControllerHand Hand);
    UFUNCTION() void HandlePour(AActor* From, AActor* To, float ML);
    UFUNCTION() void HandleGrind(float Beans, float Grounds);
    UPROPERTY(Transient) TWeakObjectPtr<AServingSpotActor> Station;
    UPROPERTY(Transient) TWeakObjectPtr<AOrderHologramActor> Menu;
    UPROPERTY(Transient) TWeakObjectPtr<ACoffeeGrinderActor> Grinder;
    UPROPERTY(Transient) TObjectPtr<UCyberCafeEventSubsystem> Events;
    TSet<FName> Seen;
    FName PendingId;
    FString PendingLine;
    TWeakObjectPtr<AActor> PendingFocus;
    int32 PendingPriority = 0, SpokenCount = 0, AmbientCount = 0;
    double PendingUntil = 0, NextSpeech = 0, MenuReadingUntil = 0;
    double PreviousGrounds = 0;
    bool bPreparing = false, bObservedGrinding = false;
    FDelegateHandle MenuHandle;
};
