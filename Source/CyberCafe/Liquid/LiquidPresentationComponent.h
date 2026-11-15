#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LiquidPresentationComponent.generated.h"
class UMaterialInterface;
UCLASS(ClassGroup=(Liquid))
class CYBERCAFE_API ULiquidPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULiquidPresentationComponent() { PrimaryComponentTick.bCanEverTick = false; }
    void RefreshLiquidFX();
    void UpdateDynamicWaves(float DeltaTime);
    void InitLiquidFX();
    void ResolveLiquidMaterialFromFX();
    void InitPourFX();
    void SetLiquidMaterialAsset(UMaterialInterface* NewMaterial, bool bReadColor);
    void EnsureLiquidMID(bool bForceRecreate);
    void ApplyLiquidColorToMID();
    bool TryReadColorFromMaterial();
    void ResetMotionHistory();
private:
    float CurrentDynamicWaves = 0;
    FVector PrevLocation = FVector::ZeroVector;
    FQuat PrevRotation = FQuat::Identity;
};
