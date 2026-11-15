#include "Liquid/LiquidConsumptionComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Engine/World.h"

bool ULiquidConsumptionComponent::TryAcquire(UObject* Requester, double BudgetML)
{
    auto* Container = Cast<ALiquidContainerActor>(GetOwner());
    if (!IsValid(Requester) || Requester->GetWorld() != GetWorld() || bClaimed || !IsValid(Container) ||
        Container->IsActorBeingDestroyed() || !Container->IsLiquidFrozen() || Container->IsBroken() ||
        !FMath::IsFinite(BudgetML) || BudgetML <= 0) return false;
    bClaimed = true;
    Consumer = Requester;
    RemainingBudgetML = FMath::Min(BudgetML, Container->GetPreciseVolumeML());
    return true;
}

double ULiquidConsumptionComponent::Consume(UObject* Requester, double RequestedML)
{
    auto* Container = Cast<ALiquidContainerActor>(GetOwner());
    auto* Ledger = GetWorld() ? GetWorld()->GetSubsystem<ULiquidTransferSubsystem>() : nullptr;
    if (!IsValid(Requester) || !IsOwnedBy(Requester) || !IsValid(Container) || Container->IsActorBeingDestroyed() ||
        !Container->IsLiquidFrozen() || Container->IsBroken() || !Ledger || !FMath::IsFinite(RequestedML) || RequestedML <= 0) return 0;
    const double Actual = Container->DebitVolumeInternal(FMath::Min(RequestedML, RemainingBudgetML));
    RemainingBudgetML -= Actual;
    Ledger->RecordRemoved(Actual);
    // Caller commits action counters before publishing liquid-change callbacks.
    return Actual;
}

void ULiquidConsumptionComponent::Release(UObject* Requester)
{
    if (!IsOwnedBy(Requester)) return;
    Consumer.Reset();
    RemainingBudgetML = 0;
    // The one-shot claim remains set: a served cup cannot be tasted twice.
}
