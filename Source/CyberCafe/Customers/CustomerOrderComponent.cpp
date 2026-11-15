#include "Customers/CustomerOrderComponent.h"
#include "Customers/CustomerDrinkComponent.h"
#include "Serving/ServingSpotActor.h"
#include "Liquid/LiquidContainerActor.h"
#include "Engine/World.h"
#include "TimerManager.h"

UCustomerOrderComponent::UCustomerOrderComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UCustomerOrderComponent::IsOrderActive() const
{
    return State == ECustomerOrderState::AwaitingDrink || State == ECustomerOrderState::PendingTasting || State == ECustomerOrderState::Tasting;
}

bool UCustomerOrderComponent::OwnsCup(const ALiquidContainerActor* Cup) const
{
    return IsValid(Cup) && AcceptedCup.Get() == Cup;
}

bool UCustomerOrderComponent::IsCurrentOrder() const
{
    const AServingSpotActor* Spot = ActiveSpot.Get();
    return IsValid(Spot) && !Spot->IsActorBeingDestroyed() && Spot->GetWorld() == GetWorld() && Spot->GetOrderSerial() == OrderSerial;
}

bool UCustomerOrderComponent::BeginOrder(AServingSpotActor* Spot, UDrinkRecipe* Recipe, float TastingDelay)
{
    UCustomerDrinkComponent* Component = GetOwner() ? GetOwner()->FindComponentByClass<UCustomerDrinkComponent>() : nullptr;
    if (bEndingPlay || bCancelling || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || IsOrderActive() || !GetWorld() || !IsValid(Spot) || Spot->IsActorBeingDestroyed() || Spot->GetWorld() != GetWorld() ||
        !IsValid(Recipe) || !IsValid(Component) || !Component->bEnabled || Component->IsDrinking() || Spot->HasBeenServed() || !FMath::IsFinite(TastingDelay)) return false;

    Unsubscribe();
    if (!Spot->TryClaimOrder(this)) return false;
    ActiveSpot = Spot;
    Drink = Component;
    AcceptedCup.Reset();
    Delay = FMath::Max(0.f, TastingDelay);
    PreviousRecipe = Spot->ActiveRecipe;
    bPreviousSubmissionEnabled = Spot->bAcceptSubmissions;
    Spot->SetActiveRecipe(Recipe);
    Spot->SetSubmissionEnabled(true);
    OrderSerial = Spot->GetOrderSerial();
    State = ECustomerOrderState::AwaitingDrink;
    Spot->OnDrinkValidated.AddUniqueDynamic(this, &UCustomerOrderComponent::HandleValidated);
    return true;
}

void UCustomerOrderComponent::HandleValidated(AServingSpotActor* Spot, ALiquidContainerActor* Cup, const FDrinkValidationResult& Result)
{
    if (State != ECustomerOrderState::AwaitingDrink || Spot != ActiveSpot.Get() || !IsCurrentOrder() || !Result.bAccepted ||
        !IsValid(Cup) || Spot->GetServedCup() != Cup || Spot->GetAcceptedOrderSerial() != OrderSerial) return;

    AcceptedCup = Cup;
    State = ECustomerOrderState::PendingTasting;
    // Always defer: ServingSpot must finish committing and broadcasting the serve first.
    if (Delay > 0)
        GetWorld()->GetTimerManager().SetTimer(TastingTimer, this, &UCustomerOrderComponent::StartTasting, Delay, false);
    else
        TastingTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UCustomerOrderComponent::StartTasting);
}

void UCustomerOrderComponent::StartTasting()
{
    if (State != ECustomerOrderState::PendingTasting) return;
    UCustomerDrinkComponent* Component = Drink.Get();
    if (!IsCurrentOrder() || !IsValid(Component) || !AcceptedCup.IsValid())
    {
        State = ECustomerOrderState::Failed;
        Unsubscribe();
        OnTastingFinished.Broadcast(AcceptedCup.Get(), false, 0);
        return;
    }
    State = ECustomerOrderState::Tasting;
    Component->OnDrinkEnded.AddUniqueDynamic(this, &UCustomerOrderComponent::HandleDrinkEnded);
    if (!Component->BeginDrink(AcceptedCup.Get(), ActiveSpot.Get()) && State == ECustomerOrderState::Tasting)
    {
        State = ECustomerOrderState::Failed;
        Unsubscribe();
        OnTastingFinished.Broadcast(AcceptedCup.Get(), false, 0);
    }
}

void UCustomerOrderComponent::HandleDrinkEnded(ALiquidContainerActor* Cup, bool bCompleted, double ConsumedML)
{
    if (State != ECustomerOrderState::Tasting || (Cup && Cup != AcceptedCup.Get())) return;
    State = bCompleted && IsCurrentOrder() ? ECustomerOrderState::Completed : ECustomerOrderState::Cancelled;
    const bool bSuccess = State == ECustomerOrderState::Completed;
    Unsubscribe();
    OnTastingFinished.Broadcast(Cup, bSuccess, ConsumedML);
}

void UCustomerOrderComponent::Unsubscribe()
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TastingTimer);
    if (AServingSpotActor* Spot = ActiveSpot.Get())
    {
        Spot->OnDrinkValidated.RemoveDynamic(this, &UCustomerOrderComponent::HandleValidated);
        Spot->ReleaseOrder(this);
    }
    if (UCustomerDrinkComponent* Component = Drink.Get()) Component->OnDrinkEnded.RemoveDynamic(this, &UCustomerOrderComponent::HandleDrinkEnded);
}

void UCustomerOrderComponent::CancelOrder()
{
    if (!IsOrderActive()) return;
    const bool bWasTasting = State == ECustomerOrderState::Tasting;
    const TWeakObjectPtr<ALiquidContainerActor> Cup = AcceptedCup;
    double ConsumedML = 0;
    // A cancelled unserved order must stop accepting drinks. Never change a newer order.
    if (IsCurrentOrder() && !ActiveSpot->HasBeenServed()) ActiveSpot->SetSubmissionEnabled(false);
    State = ECustomerOrderState::Cancelled;
    Unsubscribe();
    {
        TGuardValue<bool> Guard(bCancelling, true);
        if (UCustomerDrinkComponent* Component = Drink.Get(); bWasTasting && Component && Component->GetDrinkingCup() == Cup.Get())
        {
            Component->CancelDrink();
            ConsumedML = Component->GetConsumedML();
        }
    }
    // Cancellation is explicit and synchronous; subscribers may safely start another order afterwards.
    OnTastingFinished.Broadcast(Cup.Get(), false, ConsumedML);
}

void UCustomerOrderComponent::RestoreUnservedOrder()
{
    if (State != ECustomerOrderState::AwaitingDrink || !IsCurrentOrder() || ActiveSpot->HasBeenServed()) return;
    const TWeakObjectPtr<AServingSpotActor> Spot=ActiveSpot;
    // Commit terminal state and restore before calling external listeners.
    State=ECustomerOrderState::Cancelled;
    Unsubscribe();
    Spot->SetActiveRecipe(PreviousRecipe.Get());
    Spot->SetSubmissionEnabled(bPreviousSubmissionEnabled);
    OnTastingFinished.Broadcast(nullptr,false,0);
}

void UCustomerOrderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay = true;
    CancelOrder();
    Unsubscribe();
    Super::EndPlay(Reason);
}
