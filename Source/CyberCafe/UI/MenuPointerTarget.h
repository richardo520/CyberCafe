#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Serving/OrderHologramActor.h"
#include "MenuPointerTarget.generated.h"

// Native world panels opt into the existing VR pointer without owning input mappings.
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMenuPointerTarget : public UInterface
{
    GENERATED_BODY()
};

class CYBERCAFE_API IMenuPointerTarget
{
    GENERATED_BODY()
public:
    virtual bool TracePointer(const FVector& Start, const FVector& Direction, double Limit, FMenuPointerHit& Hit) const = 0;
    virtual bool ActivatePointer(EControllerHand Hand, const FMenuPointerHit& Hit) = 0;
};
