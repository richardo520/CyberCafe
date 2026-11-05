// @trace GB-01 GB-02 GB-03 GB-04
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GlassBreakableComponent.generated.h"
class ALiquidContainerActor;class ABottleCapActor;
class UStaticMesh;class USoundBase;class UPrimitiveComponent;class AGlassRecoveryActor;
USTRUCT(BlueprintType)
struct FGlassShardSpec
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform LocalTransform;
};
UCLASS(BlueprintType)
class CYBERCAFE_API UGlassBreakProfile:public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FGlassShardSpec> Shards;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<USoundBase> BreakSound;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> SpillMaterial;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0")) float BreakSpeedCms=260.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float LifetimeSeconds=10.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxPhysicsShards=8;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGlassBroken,ALiquidContainerActor*,Container);
UCLASS(ClassGroup=(CyberCafe),meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UGlassBreakableComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UGlassBreakableComponent();
 virtual void BeginPlay() override;
 virtual void TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Fn) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UGlassBreakProfile> Profile;
 UPROPERTY(BlueprintAssignable) FOnGlassBroken OnBroken;
 // @trace GBX-01 保留断开归属的盖子身份，恢复时只清理本容器的配套物。
 UPROPERTY() TWeakObjectPtr<ABottleCapActor> RelatedCap;
 UFUNCTION(BlueprintCallable) bool TryImpact(const FVector& Contact,const FVector& Normal,const FVector& PreImpactVelocity,AActor* OtherActor=nullptr);
 static float ClosingSpeed(const FVector& Velocity,const FVector& Normal);
 UPROPERTY(EditAnywhere) float StartupGraceSeconds=.6f;
 UPROPERTY(EditAnywhere) float MaxTrackingJumpCM=80.f;
private:
 UFUNCTION() void HandleHit(UPrimitiveComponent* HitComp,AActor* Other,UPrimitiveComponent* OtherComp,FVector Impulse,const FHitResult& Hit);
 TWeakObjectPtr<ALiquidContainerActor> Container;
 FVector PreviousCenter=FVector::ZeroVector,PreviousVelocity=FVector::ZeroVector;
 FQuat PreviousRotation=FQuat::Identity;
 float Age=0,GraceRemaining=0;
 bool bSampleReady=false,bWasHeld=false;
};
UCLASS()
class CYBERCAFE_API AGlassDebrisActor:public AActor
{
 GENERATED_BODY()
public:
 AGlassDebrisActor();
 // @trace GBX-01 弱身份可在原容器 Destroy 后用于限定碎片清理。
 UPROPERTY() TWeakObjectPtr<ALiquidContainerActor> SourceContainer;
 void Initialize(UGlassBreakProfile* Profile,const FTransform& Body,const FVector& Contact,const FVector& Velocity,const FVector& Normal,double SpilledML=0,FLinearColor LiquidColor=FLinearColor::White);
 virtual void Tick(float Dt) override;
private:
 UPROPERTY() TArray<TObjectPtr<UPrimitiveComponent>> Pieces;
 float Age=0;
 bool bSlept=false;
};
