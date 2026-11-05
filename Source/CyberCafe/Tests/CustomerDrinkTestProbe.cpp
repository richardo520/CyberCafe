#include "Tests/CustomerDrinkTestProbe.h"
#include "Tutorial/CustomerDrinkComponent.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialStep.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Serving/ServingSpotActor.h"
#include "Engine/World.h"

// UFUNCTION 实现不套 WITH_DEV_AUTOMATION_TESTS，保证所有目标中的 UHT 符号可链接。
void ACustomerDrinkTestProbe::HandleEnded(ALiquidContainerActor* InCup, bool bCompleted, double ConsumedML)
{
    ++EndedCount;
    EndedCup = InCup;
    bEndedCompleted = bCompleted;
    EndedConsumedML = ConsumedML;
    UCustomerDrinkComponent* C = Component.Get();
    bInactiveAtEnded = !C || (!C->IsDrinking() && !C->GetDrinkingCup());
    if (bAttemptRestartOnEnded && C)
        bRestartAccepted = C->BeginDrink(InCup, Spot.Get());
}

void ACustomerDrinkTestProbe::HandleLiquidChanged(float Fill, FLinearColor Color)
{
    ++LiquidChangedCount;
    UCustomerDrinkComponent* C = Component.Get();
    ALiquidContainerActor* Original = Cup.Get();
    ULiquidTransferSubsystem* S = GetWorld() ? GetWorld()->GetSubsystem<ULiquidTransferSubsystem>() : nullptr;
    if (C && Original && S)
    {
        LiquidConsumedML = C->GetConsumedML();
        LiquidRemainingML = Original->GetPreciseVolumeML();
        const FLiquidBalance B = S->GetBalance();
        LiquidRemovedML = B.RemovedML;
        const double HalfRemaining = LiquidRemainingML / 2;
        bCommittedBeforeLiquidCallback = C->IsDrinking() && C->GetDrinkingCup() == Original &&
            LiquidConsumedML > 0 && FMath::Abs(LiquidConsumedML + LiquidRemainingML - 200) < 1.e-6 &&
            FMath::Abs(LiquidRemovedML - LiquidConsumedML) < 1.e-6 && FMath::Abs(B.ErrorML) < 1.e-6 &&
            FMath::Abs(Original->GetIngredientVolumeML(TEXT("Coffee")) - HalfRemaining) < 1.e-6 &&
            FMath::Abs(Original->GetIngredientVolumeML(TEXT("Milk")) - HalfRemaining) < 1.e-6 &&
            FMath::Abs(static_cast<double>(Fill) - LiquidRemainingML / 200) < 1.e-6;
    }
    // 先保存实际观察值；执行后不再访问旧裸指针，销毁走真实 EndPlay/Destroyed。
    if (LiquidChangedCount != 1) return;
    switch (Action)
    {
    case ECustomerDrinkProbeAction::Cancel:
        if (C) C->CancelDrink();
        break;
    case ECustomerDrinkProbeAction::DestroyCup:
        if (Original) Original->Destroy();
        break;
    case ECustomerDrinkProbeAction::DestroyNPC:
        if (ATutorialNPC* N = NPC.Get()) N->Destroy();
        break;
    case ECustomerDrinkProbeAction::ChangeOrder:
        if (AServingSpotActor* Serving = Spot.Get()) Serving->SetSubmissionEnabled(false);
        break;
    default:
        break;
    }
}

void ACustomerDrinkTestProbe::HandleServed(AServingSpotActor* InSpot, ALiquidContainerActor* InCup)
{
    ++ServedCount;
    ServedSpot = InSpot;
    ServedCup = InCup;
}

void ACustomerDrinkTestProbe::HandleStepCompleted(UTutorialStep* InStep)
{
    ++StepCompletedCount;
    CompletedStep = InStep;
}
