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
 float FlowAge=0;
};
