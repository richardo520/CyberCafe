// @trace CF-03
#pragma once
#include "CoreMinimal.h"
#include "Liquid/BottleActor.h"
#include "HandPourKettleActor.generated.h"
class UInstancedStaticMeshComponent;
UCLASS(Blueprintable)
class CYBERCAFE_API AHandPourKettleActor:public ABottleActor
{
 GENERATED_BODY()
public:
 AHandPourKettleActor();
 virtual void Tick(float Dt) override;
 virtual void BeginPlay() override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> WaterLiquidMaterial;
 UFUNCTION(BlueprintCallable) float PourWater(float Dt);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> WaterJet;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float PourTiltDegrees=40;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxFlowMLPerSecond=28;
 UPROPERTY(BlueprintReadOnly) float CurrentFlowMLPerSecond=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Kettle|Ribbon",meta=(ClampMin="0.0",Units="cm")) float KettleRibbonThicknessCM=.25f;
 // Coordinates are authored in the actual ContainerMesh frame, not the FX frame.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Kettle|Spout") FVector KettleSpoutLocal=FVector(17.f,0.f,16.f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Kettle|Spout") FVector KettleSpoutForwardLocal=FVector(.9486833f,0.f,.3162278f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Kettle|Water") FLinearColor WaterStreamColor=FLinearColor(.72f,.84f,.90f,1.f);
 UFUNCTION(BlueprintPure,Category="Kettle|Spout") FTransform GetKettleSpoutWorldTransform() const;
protected:
 void UpdateKettleRibbon(bool bEmissionPositive);
 virtual void StopPouring() override;
};
