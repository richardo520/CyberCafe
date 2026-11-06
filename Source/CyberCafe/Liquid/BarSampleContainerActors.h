// @trace BAR-F01. Opt-in first sample vessels; existing bottle/cup defaults remain untouched.
#pragma once
#include "CoreMinimal.h"
#include "Liquid/BottleActor.h"
#include "Liquid/CupActor.h"
#include "Components/ActorComponent.h"
#include "BarSampleContainerActors.generated.h"

// Visual mL -> mesh-height calibration only. Never changes authority/composition.
UCLASS(ClassGroup=(Liquid),meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UBarSampleLiquidCalibrationComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UBarSampleLiquidCalibrationComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sample|Liquid") TArray<FVector2D> VolumeToHeight;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sample|Liquid") TObjectPtr<UMaterialInterface> InitialLiquidMaterial;
 UPROPERTY(BlueprintReadOnly,Category="Sample|Liquid") float VisualFill=0.f;
 virtual void BeginPlay() override;
 virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction) override;
 UFUNCTION(BlueprintCallable,Category="Sample|Liquid") void SyncVisual();
 UFUNCTION(BlueprintPure,Category="Sample|Liquid") float GetCalibratedFill(double VolumeML) const;
private:
 UFUNCTION() void HandleLiquidChanged(float Fill,FLinearColor Color);
};
UCLASS(Blueprintable)
class CYBERCAFE_API ABarSampleBottleActor:public ABottleActor
{
 GENERATED_BODY()
public:
 ABarSampleBottleActor();
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBarSampleLiquidCalibrationComponent> SampleLiquid;
 virtual void BeginPlay() override;
};
UCLASS(Blueprintable)
class CYBERCAFE_API ABarSampleCupActor:public ACupActor
{
 GENERATED_BODY()
public:
 ABarSampleCupActor();
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBarSampleLiquidCalibrationComponent> SampleLiquid;
 virtual void BeginPlay() override;
};
