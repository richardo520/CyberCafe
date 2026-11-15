#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LiquidAudioComponent.generated.h"
class UMaterialInterface;
class UAudioComponent;
UCLASS(ClassGroup=(Liquid))
class CYBERCAFE_API ULiquidAudioComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULiquidAudioComponent() { PrimaryComponentTick.bCanEverTick = false; }
    void UpdatePourAudio(double ActualFlowMLPerSecond, const FTransform& SourceTransform);
    void StopPourAudio(bool bImmediate);
    bool IsRequested() const { return bPourAudioRequested; }
    UAudioComponent* GetLoopSoundComponent() const { return PourAudioComponent.Get(); }
private:
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> PourAudioComponent;
    bool bPourAudioRequested = false;
    bool bPourAudioRunning = false;
};
