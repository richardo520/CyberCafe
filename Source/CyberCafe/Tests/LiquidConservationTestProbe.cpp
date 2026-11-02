#include "Tests/LiquidConservationTestProbe.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Engine/World.h"

void ULiquidConservationTestProbe::HandleChanged(float, FLinearColor)
{
#if WITH_DEV_AUTOMATION_TESTS
    ++Notifications;
    if(ALiquidContainerActor* A=Source.Get())
    {
        if(ULiquidTransferSubsystem* S=A->GetWorld()->GetSubsystem<ULiquidTransferSubsystem>())
            bBalancedAtEveryChange &= FMath::Abs(S->GetBalance().ErrorML)<.00001;
        if(Notifications==1)
        {
            if(bStopOnFirstChange) A->bCanPour=false;
            if(bDestroyOnFirstChange) A->Destroy();
        }
    }
#endif
}
