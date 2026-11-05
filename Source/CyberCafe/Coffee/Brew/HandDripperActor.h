// @trace CF-02 CF-03 CF-04
#pragma once
#include "CoreMinimal.h"
#include "Liquid/LiquidContainerActor.h"
#include "HandDripperActor.generated.h"
class UTextRenderComponent;class USoundBase;class UInstancedStaticMeshComponent;class UBoxComponent;
UENUM(BlueprintType) enum class ECoffeeBrewState:uint8 { Empty,Dry,Blooming,Brewing,Spent };
UCLASS(Blueprintable)
class CYBERCAFE_API AHandDripperActor:public ALiquidContainerActor
{
 GENERATED_BODY()
public:
 AHandDripperActor();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 virtual void PrepareTransferredLiquid(FLiquidComposition& Portion,FLinearColor& Color) override;
 virtual UMaterialInterface* GetTransferredMaterial(const FLiquidComposition& Portion) const override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> CoffeeLiquidMaterial;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> WaterLiquidMaterial;
 UFUNCTION(BlueprintPure) bool CanReceiveGrounds() const;
 UFUNCTION(BlueprintCallable) float AddGrounds(float Grams,const FVector& WorldPoint);
 UFUNCTION(BlueprintPure) float GetGroundsGrams() const{return GroundsGrams;}
 UFUNCTION(BlueprintPure) float GetCoffeeBudgetML() const{return RemainingCoffeeML;}
 UFUNCTION(BlueprintCallable) void AdvanceBrew(float Dt);
 UFUNCTION(BlueprintCallable) bool ReplacePaper();
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> DropVisual;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> WaterSurface;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBoxComponent> PaperButton;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UTextRenderComponent> PaperButtonLabel;
 UPROPERTY(BlueprintReadOnly) double DiscardedGroundsGrams=0;
 float DropAge=0,PaperHold=0;bool bPaperTouchLatched=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> PaperMesh;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> GroundBed;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UTextRenderComponent> StatusText;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxGroundsGrams=20.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float BloomSeconds=8.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float DrainRateMLPerSecond=12.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float CoffeeYieldMLPerGram=16.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector OutletLocal=FVector(0,0,-1);
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<USoundBase> DripSound;
 UPROPERTY(BlueprintReadOnly) ECoffeeBrewState BrewState=ECoffeeBrewState::Empty;
 UPROPERTY(BlueprintReadOnly) float Wetness=0.f;
 UPROPERTY(BlueprintReadOnly) int32 BrewGeneration=0;
 UPROPERTY(BlueprintReadOnly) double DrippedML=0,CoffeeProducedML=0;
protected:
 UFUNCTION() void OnStorageChanged(float Fill,FLinearColor Color);
private:
 float GroundsGrams=0,RemainingCoffeeML=0,BloomElapsed=0,SoundCooldown=0;
 bool bWaterStarted=false;
 FVector InitialGroundLocation=FVector::ZeroVector,InitialGroundScale=FVector::OneVector;
 void UpdateBrewVisuals();
};
