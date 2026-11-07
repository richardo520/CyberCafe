// @trace IC-01 IC-02 IC-03 IC-04
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "IceInteraction.generated.h"
class UStaticMeshComponent;class UInstancedStaticMeshComponent;class UGrabComponent;class UStaticMesh;class USoundBase;class AIceCubeActor;class AIceScoopActor;class AIceBucketActor;class ACupActor;
UENUM(BlueprintType) enum class EIceState:uint8 { Free,InScoop,InCup };
UCLASS()
class CYBERCAFE_API UIceWorldSubsystem:public UWorldSubsystem
{
 GENERATED_BODY()
public:
 virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
 bool CanAllocate(int32 Count);
 void RegisterIce(AIceCubeActor* Ice);
 void ForgetIce(AIceCubeActor* Ice);
 UFUNCTION(BlueprintPure) int32 GetLiveCount() const;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxLiveIce=48;
private:
 TArray<TWeakObjectPtr<AIceCubeActor>> Live;
};
UCLASS(ClassGroup=(CyberCafe),meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UCupIceComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UCupIceComponent();
 virtual void BeginPlay() override;
 virtual void TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Fn) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ice") float InteriorRadiusCM=3.3f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ice") float InteriorDepthCM=8.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ice") int32 MaxCupIce=6;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ice") TObjectPtr<USoundBase> ClinkSound;
 UFUNCTION(BlueprintPure) int32 GetIceCount() const;
 UFUNCTION(BlueprintPure) int32 GetPreparationRevision() const;
 bool TryAccept(AIceCubeActor* Ice,const FVector& Previous,const FVector& Current);
 void ReleaseAll();
 void RemoveIce(AIceCubeActor* Ice);
 UFUNCTION(BlueprintPure) ACupActor* GetCup() const;
private:
 UPROPERTY() TArray<TObjectPtr<AIceCubeActor>> IceCubes;
 UPROPERTY(Transient) int32 PreparationRevision=0;
 FVector PreviousCupPosition=FVector::ZeroVector;
 float Age=0,SoundCooldown=0;
};
UCLASS(Blueprintable)
class CYBERCAFE_API AIceCubeActor:public AActor
{
 GENERATED_BODY()
public:
 AIceCubeActor();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
 UPROPERTY(BlueprintReadOnly) EIceState State=EIceState::Free;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<UCupIceComponent> CupIce;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AIceScoopActor> Scoop;
 // @trace RS-02. Retained origin for selective cleanup; never erase unrelated ice.
 UPROPERTY(Transient) TWeakObjectPtr<ACupActor> LastCupSource;
 UPROPERTY(Transient) TWeakObjectPtr<AIceScoopActor> LastScoopSource;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float RadiusCM=1.25f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float FreeLifetimeSeconds=12.f;
 FVector LocalVelocity=FVector::ZeroVector;
 UFUNCTION(BlueprintCallable) void ReleaseToWorld(FVector Velocity);
 void PlaceInScoop(AIceScoopActor* OwnerScoop,FVector LocalPosition);
private:
 FVector PreviousPosition=FVector::ZeroVector;
 bool bPreviousReady=false;
 float FreeAge=0;
};
UCLASS(Blueprintable)
class CYBERCAFE_API AIceScoopActor:public AActor
{
 GENERATED_BODY()
public:
 AIceScoopActor();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UGrabComponent> Grab;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<AIceBucketActor> Bucket;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TSubclassOf<AIceCubeActor> IceClass;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",ClampMax="3")) int32 IcePerScoop=3;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float PourTiltDegrees=55.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float ReleaseIntervalSeconds=.16f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector BowlCenter=FVector(10,0,0);
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector ReleaseLip=FVector(16,0,0);
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<USoundBase> ScoopSound;
 UFUNCTION(BlueprintPure) int32 GetLoadedCount() const;
 UFUNCTION(BlueprintCallable) bool TryLoadFromBucket();
 UFUNCTION(BlueprintCallable) bool ReleaseOne();
 // @trace RS-03 只复位本铲，不改桶、抓取组件或音效引用。
 UFUNCTION(BlueprintCallable) bool ResetForWorkbench(FTransform SavedPose);
private:
 UPROPERTY() TArray<TObjectPtr<AIceCubeActor>> Loaded;
 bool bBucketArmed=true;
 float ReleaseCountdown=0;
 FVector PreviousPosition=FVector::ZeroVector,FrameVelocity=FVector::ZeroVector;
};
UCLASS(Blueprintable)
class CYBERCAFE_API AIceBucketActor:public AActor
{
 GENERATED_BODY()
public:
 AIceBucketActor();
 virtual void BeginPlay() override;
 virtual void OnConstruction(const FTransform& Transform) override;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UInstancedStaticMeshComponent> IcePile;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> IceMesh;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float LoadRadiusCM=9.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float LoadMinZCM=8.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float LoadMaxZCM=20.f;
 bool ContainsBowl(const FVector& Point,float Margin=0) const;
};
UCLASS()
class CYBERCAFE_API AIceStationActor:public AActor
{
 GENERATED_BODY()
public:
 AIceStationActor();virtual void BeginPlay() override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<AIceBucketActor> Bucket;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<AIceScoopActor> Scoop;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<ACupActor> Cup;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<USoundBase> ClinkSound;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float CupInteriorDepthCM=8.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float CupInteriorRadiusCM=3.3f;
};
