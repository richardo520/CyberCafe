// @trace GB-04
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GlassRecoveryActor.generated.h"
class ALiquidContainerActor;class ABottleCapActor;class AIceScoopActor;class USoundBase;class UGlassBreakProfile;class UGlassBreakableComponent;class UBoxComponent;class UStaticMeshComponent;class UTextRenderComponent;
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
 UPROPERTY() TSubclassOf<ABottleCapActor> CapClass;
 UPROPERTY() TWeakObjectPtr<ABottleCapActor> CapturedCap;
 FName CapSocket;
 bool bStartCapped=false,bCanPour=true,bAcceptLiquid=false,bInitialized=false;
 bool bHasCupIce=false;
 float IceRadiusCM=3.3f,IceDepthCM=8.f;
 int32 MaxCupIce=6;
 UPROPERTY() TObjectPtr<USoundBase> ClinkSound;
 FTransform MeshRelativeTransform,PourEntryRelativeTransform;
 float PourEntryRadius=0;
 bool bEntryAbsoluteLocation=false,bEntryAbsoluteRotation=false,bEntryAbsoluteScale=false;
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
 // @trace RS-01 RS-02 RS-04 默认关闭以保留旧双键契约。
 UPROPERTY(EditAnywhere,Category="Workbench Restore") bool bUnifiedRestore=false;
 UPROPERTY(EditAnywhere,Category="Workbench Restore") TArray<TObjectPtr<ALiquidContainerActor>> RestoreTargets;
 UPROPERTY(EditAnywhere,Category="Workbench Restore") TObjectPtr<AIceScoopActor> RestoreScoop;
 UPROPERTY(EditAnywhere,Category="Workbench Restore",meta=(ClampMin="0.01")) float HoldSeconds=1.0f;
 UPROPERTY(BlueprintReadOnly,Category="Workbench Restore") FText RestoreStatus;
 UFUNCTION(BlueprintCallable,Category="Workbench Restore") bool RestoreWorkbench();
 UFUNCTION(BlueprintCallable,Category="Workbench Restore") bool AdvanceRestoreHold(float Dt,bool Touching);
 UFUNCTION(BlueprintPure,Category="Workbench Restore") float GetRestoreProgress() const;
 UFUNCTION(BlueprintPure,Category="Workbench Restore") int32 GetRestoreGeneration() const { return RestoreGeneration; }
 UFUNCTION(BlueprintPure,Category="Workbench Restore") int32 GetRestoreSlotCount() const;
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
 UPROPERTY() TObjectPtr<UTextRenderComponent> StatusLabel;
 FTransform ScoopSpawnTransform;
 TWeakObjectPtr<AIceScoopActor> CapturedScoop;
 float RestoreHoldElapsed=0;
 int32 RestoreGeneration=0;
 bool bRestoreInProgress=false,bRestoreHoldLatched=false;
 FVector PreviousTouch[2]={FVector::ZeroVector,FVector::ZeroVector};
 bool bTouchSampleReady[2]={false,false};
 void CaptureSlot(int32 Index,ALiquidContainerActor* Container,UGlassBreakProfile* Profile);
 bool PreflightRestore(TArray<ALiquidContainerActor*>& OldToRestore,TArray<ABottleCapActor*>& CapsToRestore);
 void Install(ALiquidContainerActor* Container,UGlassBreakProfile* Profile);
};
