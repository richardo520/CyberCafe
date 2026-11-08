#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AllSuppliesCallbackProbe.generated.h"

class ACupActor;
class AHandPourKettleActor;
class ACoffeeGrinderActor;

UCLASS()
class CYBERCAFE_API UAllSuppliesCallbackProbe : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY() TWeakObjectPtr<ACupActor> Cup;
    UPROPERTY() TWeakObjectPtr<AHandPourKettleActor> Kettle;
    UPROPERTY() TWeakObjectPtr<ACoffeeGrinderActor> Grinder;

    int32 Action = 0;
    int32 Notifications = 0;
    bool Fired = false;
    bool GuardSeen = false;
    double AttemptEmitML = 0;
    double AttemptVolumeML = 0;
    float AttemptTiltDegrees = 0;
    float AttemptSpoutDown = 0;
    float AttemptRateMLPerSecond = 0;
    int32 AttemptPacketsBefore = 0;
    int32 AttemptPacketsAfter = 0;

    UFUNCTION()
    void OnLiquidChange(float Fill, FLinearColor Color);
};
