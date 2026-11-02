// @trace LQ-06
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LiquidConservationTestProbe.generated.h"
class ALiquidContainerActor;

/** Transient automation delegate target; never placed or saved in a level. */
UCLASS(Transient, NotBlueprintable)
class ULiquidConservationTestProbe : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TWeakObjectPtr<ALiquidContainerActor> Source;
    int32 Notifications = 0;
    bool bStopOnFirstChange = false;
    bool bDestroyOnFirstChange = false;
    bool bBalancedAtEveryChange = true;
    UFUNCTION() void HandleChanged(float Fill, FLinearColor Color);
};
