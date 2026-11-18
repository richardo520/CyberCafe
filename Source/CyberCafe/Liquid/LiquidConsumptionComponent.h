#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LiquidConsumptionComponent.generated.h"

/** One-shot consumption lease on a frozen served container; knows no customer class. */
UCLASS(ClassGroup=(Liquid))
class CYBERCAFE_API ULiquidConsumptionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULiquidConsumptionComponent() { PrimaryComponentTick.bCanEverTick = false; }
    bool TryAcquire(UObject* Requester, double BudgetML);
    double Consume(UObject* Requester, double RequestedML);
    void Release(UObject* Requester);
    bool IsClaimed() const { return bClaimed; }
    bool HasActiveConsumer() const { return Consumer.IsValid(); }
    bool IsOwnedBy(const UObject* Requester) const { return Requester && Consumer.Get() == Requester; }
private:
    UPROPERTY(Transient) TWeakObjectPtr<UObject> Consumer;
    double RemainingBudgetML = 0;
    bool bClaimed = false;
};
