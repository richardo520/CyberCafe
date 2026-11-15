#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Serving/DrinkRecipe.h"
#include "CustomerOrderComponent.generated.h"

class AServingSpotActor;
class ALiquidContainerActor;
class UCustomerDrinkComponent;

UENUM(BlueprintType)
enum class ECustomerOrderState : uint8
{
    Idle, AwaitingDrink, PendingTasting, Tasting, Completed, Cancelled, Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCustomerTastingFinished,
    ALiquidContainerActor*, Cup, bool, bCompleted, double, ConsumedML);

/** Owns one customer's serve-to-taste lifecycle. No tutorial or HUD dependency. */
UCLASS(ClassGroup=(Customer), meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UCustomerOrderComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCustomerOrderComponent();

    // A request is rejected while this component owns an order; cancel it explicitly first.
    UFUNCTION(BlueprintCallable, Category="Customer|Order")
    bool BeginOrder(AServingSpotActor* Spot, UDrinkRecipe* Recipe, float TastingDelay = 1.5f);

    UFUNCTION(BlueprintCallable, Category="Customer|Order")
    void CancelOrder();

    // Release an unserved scoped request and restore its previous station configuration.
    // Used by any temporary requester, including tutorial adapters.
    void RestoreUnservedOrder();

    UFUNCTION(BlueprintPure, Category="Customer|Order")
    ECustomerOrderState GetState() const { return State; }

    UFUNCTION(BlueprintPure, Category="Customer|Order") bool OwnsCup(const ALiquidContainerActor* Cup) const;
    UFUNCTION(BlueprintPure, Category="Customer|Order") bool IsOrderActive() const;

    UPROPERTY(BlueprintAssignable, Category="Customer|Order")
    FOnCustomerTastingFinished OnTastingFinished;

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION() void HandleValidated(AServingSpotActor* Spot, ALiquidContainerActor* Cup, const FDrinkValidationResult& Result);
    UFUNCTION() void HandleDrinkEnded(ALiquidContainerActor* Cup, bool bCompleted, double ConsumedML);
    void StartTasting();
    void Unsubscribe();
    bool IsCurrentOrder() const;

    UPROPERTY(Transient) TWeakObjectPtr<AServingSpotActor> ActiveSpot;
    UPROPERTY(Transient) TWeakObjectPtr<UCustomerDrinkComponent> Drink;
    UPROPERTY(Transient) TWeakObjectPtr<ALiquidContainerActor> AcceptedCup;
    UPROPERTY(Transient) ECustomerOrderState State = ECustomerOrderState::Idle;
    UPROPERTY(Transient) TWeakObjectPtr<UDrinkRecipe> PreviousRecipe;
    bool bPreviousSubmissionEnabled = true;
    uint32 OrderSerial = 0;
    float Delay = 0;
    bool bEndingPlay = false;
    bool bCancelling = false;
    FTimerHandle TastingTimer;
};
