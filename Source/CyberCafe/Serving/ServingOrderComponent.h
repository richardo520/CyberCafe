#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Serving/DrinkRecipe.h"
#include "ServingOrderComponent.generated.h"
class ALiquidContainerActor;
/** Owns acceptance transactions and order identity; the spot owns spatial detection/placement. */
UCLASS(ClassGroup=(Serving))
class CYBERCAFE_API UServingOrderComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UServingOrderComponent() { PrimaryComponentTick.bCanEverTick=false; }
    void SetActiveRecipe(UDrinkRecipe* Recipe);
    void SetSubmissionEnabled(bool bEnabled);
    bool TryClaimOrder(UObject* Requester);
    void ReleaseOrder(UObject* Requester);
    bool HasAcceptedCup(const ALiquidContainerActor* Cup, FName RecipeId) const;
    bool TrySubmitCup(ALiquidContainerActor* Cup);
private:
    friend class AServingSpotActor;
    UPROPERTY(Transient) FDrinkValidationResult LastValidation;
    uint32 OrderSerial=0;
    uint32 AcceptedOrderSerial=0;
    UPROPERTY(Transient) TWeakObjectPtr<ALiquidContainerActor> CommittingCup;
    UPROPERTY(Transient) TWeakObjectPtr<UObject> OrderOwner;
    bool bCommitting=false;
};
