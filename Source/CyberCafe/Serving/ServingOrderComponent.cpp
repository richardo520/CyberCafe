#include "Serving/ServingOrderComponent.h"
// Fill out your copyright notice in the Description page of Project Settings.

#include "Serving/ServingSpotActor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
// @trace RC-03 RC-05
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "GrabComponent.h"
#include "Core/Events/CyberCafeEventSubsystem.h"
#include "Engine/World.h"

namespace
{
    int32 GetOrderCupIcePreparationRevision(const ALiquidContainerActor* Cup)
    {
        if (!IsValid(Cup)) return 0;
        const UCupIceComponent* Ice = Cup->FindComponentByClass<UCupIceComponent>();
        return Ice ? Ice->GetPreparationRevision() : 0;
    }
}

void UServingOrderComponent::SetActiveRecipe(UDrinkRecipe* Recipe)
{
    auto& Spot=*CastChecked<AServingSpotActor>(GetOwner());
    if (bCommitting) return;
    ++OrderSerial; Spot.ActiveRecipe = Recipe;
    Spot.LastAttemptCup.Reset(); Spot.LastAttemptRevision = -1; Spot.LastAttemptIceRevision = -1; Spot.ObservedRevision = -1; Spot.ObservedIceRevision = -1; Spot.StableElapsed = 0;
    if (!Spot.bHasBeenServed) LastValidation = FDrinkValidationResult();
}

void UServingOrderComponent::SetSubmissionEnabled(bool bEnabled)
{
    auto& Spot=*CastChecked<AServingSpotActor>(GetOwner());
    if (bCommitting) return;
    Spot.bAcceptSubmissions = bEnabled;
    Spot.LastAttemptCup.Reset(); Spot.LastAttemptRevision = -1; Spot.LastAttemptIceRevision = -1;
    ++OrderSerial; Spot.StableElapsed = 0; Spot.ObservedRevision = -1; Spot.ObservedIceRevision = -1;
}

bool UServingOrderComponent::TryClaimOrder(UObject* Requester)
{
    auto& Spot=*CastChecked<AServingSpotActor>(GetOwner());
    if (bCommitting || Spot.bHasBeenServed || !IsValid(Requester) || Requester->GetWorld() != Spot.GetWorld() ||
        (OrderOwner.IsValid() && OrderOwner.Get() != Requester)) return false;
    OrderOwner = Requester;
    return true;
}

void UServingOrderComponent::ReleaseOrder(UObject* Requester)
{
    auto& Spot=*CastChecked<AServingSpotActor>(GetOwner());
    if (OrderOwner.Get() == Requester) OrderOwner.Reset();
}

bool UServingOrderComponent::HasAcceptedCup(const ALiquidContainerActor* Cup, FName RecipeId) const
{
    auto& Spot=*CastChecked<AServingSpotActor>(GetOwner());
    return IsValid(Cup) && Spot.bHasBeenServed && Spot.ServedCup.Get() == Cup && LastValidation.bAccepted && LastValidation.RecipeId == RecipeId;
}

bool UServingOrderComponent::TrySubmitCup(ALiquidContainerActor* Cup)
{
    auto& Spot=*CastChecked<AServingSpotActor>(GetOwner());
    if (!Spot.bAcceptSubmissions || bCommitting || (Spot.bHasBeenServed && Spot.bOneShot) || !Spot.IsCupAcceptable(Cup) || !Spot.IsInServingArea(Cup) || Cup->IsHeld() || Cup->IsLiquidFrozen() || Cup->bIsPouring) return false;
    TGuardValue<bool> Guard(bCommitting, true);
    const TWeakObjectPtr<ALiquidContainerActor> SubmissionCup(Cup);
    TGuardValue<TWeakObjectPtr<ALiquidContainerActor>> CupGuard(CommittingCup, SubmissionCup);
    if (!Spot.ActiveRecipe) { Spot.PerformServe(Cup); return SubmissionCup.IsValid() && Spot.bHasBeenServed && Spot.ServedCup == SubmissionCup; }
    const uint32 Serial = OrderSerial;
    UDrinkRecipe* Recipe = Spot.ActiveRecipe;
    const FDrinkValidationResult Result = Recipe->Evaluate(Cup);
    Cup = SubmissionCup.Get();
    if (!IsValid(&Spot) || !Cup || CommittingCup != SubmissionCup) return false;
    const bool bNewAttempt = Spot.LastAttemptCup.Get() != Cup || Spot.LastAttemptRevision != Result.LiquidRevision || Spot.LastAttemptIceRevision != Result.IceRevision;
    Spot.LastAttemptCup = Cup; Spot.LastAttemptRevision = Result.LiquidRevision; Spot.LastAttemptIceRevision = Result.IceRevision;
    LastValidation = Result;
    if (!Result.bAccepted)
    {
        if (bNewAttempt) Spot.OnDrinkValidated.Broadcast(&Spot, Cup, Result);
        return false;
    }
    // 冻结前复核结果快照：旧计时器或回调中的补冰/换冰不能锁定已改变的杯子。
    if (Serial != OrderSerial || Recipe != Spot.ActiveRecipe || !Spot.bAcceptSubmissions || Cup->IsBroken() || Cup->IsHeld() || Cup->IsLiquidFrozen() || Cup->bIsPouring || !Spot.IsInServingArea(Cup) || Cup->GetLiquidRevision() != Result.LiquidRevision || GetOrderCupIcePreparationRevision(Cup) != Result.IceRevision) return false;
    Cup->FreezeLiquidForServing();
    Cup = SubmissionCup.Get();
    if (!IsValid(&Spot) || !Cup || CommittingCup != SubmissionCup) return false;
    AcceptedOrderSerial = Serial;
    Spot.PerformServe(Cup);
    return SubmissionCup.IsValid() && Spot.bHasBeenServed && Spot.ServedCup == SubmissionCup;
}
