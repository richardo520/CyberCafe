// @trace GB-04
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GlassRecoveryActor.generated.h"
class ALiquidContainerActor;class UGlassBreakProfile;class UGlassBreakableComponent;class UBoxComponent;class UStaticMeshComponent;class UTextRenderComponent;
USTRUCT()
struct FGlassRecoverySlot
{
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<ALiquidContainerActor> Current;
 UPROPERTY() TObjectPtr<UGlassBreakProfile> Profile;
 UPROPERTY() TSubclassOf<ALiquidContainerActor> ContainerClass;
 FTransform SpawnTransform;
 float InitialFill=0,Capacity=0;
 FName Ingredient;
 FLinearColor Color;
 UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Materials;
 bool bLatched=false;
};
UCLASS()
class CYBERCAFE_API AGlassRecoveryActor:public AActor
{
 GENERATED_BODY()
public:
 AGlassRecoveryActor();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 UPROPERTY(EditAnywhere,Category="Glass Prototype") TObjectPtr<ALiquidContainerActor> PrototypeCup;
 UPROPERTY(EditAnywhere,Category="Glass Prototype") TObjectPtr<ALiquidContainerActor> PrototypeBottle;
 UPROPERTY(EditAnywhere,Category="Glass Prototype") TObjectPtr<UGlassBreakProfile> CupProfile;
 UPROPERTY(EditAnywhere,Category="Glass Prototype") TObjectPtr<UGlassBreakProfile> BottleProfile;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Base;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> CupButton;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> BottleButton;
 UFUNCTION(BlueprintCallable,Category="Glass Recovery") bool Recover(int32 SlotIndex);
 UFUNCTION(BlueprintPure,Category="Glass Recovery") ALiquidContainerActor* GetCurrent(int32 SlotIndex) const;
 UFUNCTION(BlueprintPure,Category="Glass Recovery") bool CanRecover(int32 SlotIndex) const;
 void BindPrototype(ALiquidContainerActor* Cup,ALiquidContainerActor* Bottle,UGlassBreakProfile* CupData,UGlassBreakProfile* BottleData);
private:
 UPROPERTY() TArray<FGlassRecoverySlot> Slots;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Keys;
 UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Labels;
 void Install(ALiquidContainerActor* Container,UGlassBreakProfile* Profile);
};
