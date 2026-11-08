#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PourAudioTestProbe.generated.h"
class AHandPourKettleActor;
// Transient callback receiver for cancellation during actual emitted-volume notifications.
UCLASS(Transient,NotBlueprintable)
class UPourAudioTestProbe : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TWeakObjectPtr<AHandPourKettleActor> Kettle;
    int32 Notifications=0;
    UFUNCTION() void CloseDuringEmission(float Fill,FLinearColor Color);
};
