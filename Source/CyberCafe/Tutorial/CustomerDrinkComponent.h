// @trace BC-02 BC-03 BC-04
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CustomerDrinkComponent.generated.h"
class UCustomerDrinkMotion;class ALiquidContainerActor;class AServingSpotActor;class USkeletalMeshComponent;class UAnimSequence;class USoundBase;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCustomerDrinkEnded,ALiquidContainerActor*,Cup,bool,bCompleted,double,ConsumedML);
/** Opt-in fixed customer performance. Never runs on arbitrary/unaccepted cups. */
UCLASS(ClassGroup=(Customer),meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UCustomerDrinkComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UCustomerDrinkComponent();
 virtual void TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* F) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UFUNCTION(BlueprintCallable) bool BeginDrink(ALiquidContainerActor* Cup,AServingSpotActor* Spot);
 UFUNCTION(BlueprintCallable) void CancelDrink();
 UFUNCTION(BlueprintPure) bool IsDrinking() const{return bActive;}
 UFUNCTION(BlueprintPure) double GetConsumedML() const{return ConsumedML;}
 UFUNCTION(BlueprintPure) float GetElapsedSeconds() const{return Elapsed;}
 UFUNCTION(BlueprintPure) ALiquidContainerActor* GetDrinkingCup() const{return ActiveCup.Get();}
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UCustomerDrinkMotion> Motion;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UAnimSequence> IdleAnimation;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<USoundBase> SipSound;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bUseBlink=true;
 UPROPERTY(BlueprintAssignable) FOnCustomerDrinkEnded OnDrinkEnded;
private:
 UPROPERTY(Transient) TWeakObjectPtr<ALiquidContainerActor> ActiveCup;
 UPROPERTY(Transient) TWeakObjectPtr<AServingSpotActor> ActiveSpot;
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> Mesh;
 TSet<TWeakObjectPtr<ALiquidContainerActor>> UsedCups;
 FTransform OriginalCupTransform,StartMeshTransform;
 FVector StartOffset=FVector::ZeroVector;
 uint32 OrderSerial=0;float Elapsed=0;double ConsumedML=0;bool bActive=false,bSipPlayed=false,bFinishing=false;
 void Finish(bool Completed,bool Broadcast=true);
 bool StillOwnsAcceptedCup() const;
};
