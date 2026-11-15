#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Liquid/LiquidComposition.h"
#include "LiquidInventoryComponent.generated.h"

/** Authoritative local contents. Rendering, customers and world accounting live elsewhere. */
UCLASS(ClassGroup=(Liquid))
class CYBERCAFE_API ULiquidInventoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULiquidInventoryComponent() { PrimaryComponentTick.bCanEverTick = false; }
    double Debit(double RequestedML, FLiquidComposition* OutComposition = nullptr);
    double Credit(double RequestedML, double CapacityML, FLinearColor& Color,
        const FLinearColor& InColor, const FLiquidComposition* InComposition);
    double GetVolumeML() const { return VolumeML; }
    int32 GetRevision() const { return Revision; }
    bool IsInitialized() const { return bInitialized; }
    const FLiquidComposition& GetComposition() const { return Contents; }
private:
    friend class ALiquidContainerActor;
    UPROPERTY(Transient) double VolumeML = 0;
    UPROPERTY(Transient) FLiquidComposition Contents;
    UPROPERTY(Transient) int32 Revision = 0;
    bool bInitialized = false;
};
