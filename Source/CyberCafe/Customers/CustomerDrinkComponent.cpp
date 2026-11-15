#include "Customers/CustomerDrinkComponent.h"
#include "Customers/CustomerDrinkMotion.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidConsumptionComponent.h"
#include "Liquid/CupActor.h"
#include "Serving/ServingSpotActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "GrabComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
UCustomerDrinkComponent::UCustomerDrinkComponent()
{
 PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;PrimaryComponentTick.TickGroup=TG_PostPhysics;
}
bool UCustomerDrinkComponent::StillOwnsAcceptedCup() const
{
 auto* C=ActiveCup.Get();auto* S=ActiveSpot.Get();
 return IsValid(C)&&!C->IsActorBeingDestroyed()&&!C->IsBroken()&&C->IsLiquidFrozen()&&IsValid(S)&&!S->IsActorBeingDestroyed()&&S->GetWorld()==GetWorld()&&S->GetServedCup()==C&&S->HasBeenServed()&&S->GetOrderSerial()==OrderSerial&&S->GetAcceptedOrderSerial()==OrderSerial&&S->GetLastValidation().bAccepted;
}
bool UCustomerDrinkComponent::BeginDrink(ALiquidContainerActor* Cup,AServingSpotActor* Spot)
{
 LastStartFailureReason.Reset();
 const auto Reject=[this,Cup,Spot](const FString& Reason)
 {
  LastStartFailureReason=Reason;
  UE_LOG(LogTemp,Log,TEXT("[CustomerDrink] Start rejected: %s; owner=%s cup=%s spot=%s"),*Reason,*GetNameSafe(GetOwner()),*GetNameSafe(Cup),*GetNameSafe(Spot));
  return false;
 };
 if(!bEnabled)return Reject(TEXT("customer-disabled"));
 if(bActive||bFinishing)return Reject(TEXT("customer-busy"));
 if(!IsValid(Motion)||!Motion->IsValidMotion())return Reject(TEXT("invalid-motion"));
 if(!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()||!IsValid(Cup)||!IsValid(Spot)||Cup->IsActorBeingDestroyed()||Spot->IsActorBeingDestroyed())return Reject(TEXT("missing-or-destroyed-actor"));
 if(Cup->GetWorld()!=GetWorld()||Spot->GetWorld()!=GetWorld())return Reject(TEXT("wrong-world"));
 if(!Cast<ACupActor>(Cup)||Cup->IsHeld()||(Cup->GrabComp&&Cup->GrabComp->IsPulled())||Cup->IsBroken()||!Cup->IsLiquidFrozen())return Reject(TEXT("cup-not-ready"));
 if(Cup->Consumption->IsClaimed()||UsedCups.Contains(Cup))return Reject(TEXT("cup-already-claimed"));
 if(Spot->GetServedCup()!=Cup||!Spot->HasBeenServed()||!Spot->GetLastValidation().bAccepted||Spot->GetOrderSerial()!=Spot->GetAcceptedOrderSerial())return Reject(TEXT("unaccepted-or-stale-order"));
 auto* M=GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
 if(!M||!M->GetSkeletalMeshAsset()||M->GetSkeletalMeshAsset()->GetSkeleton()!=Motion->Animation->GetSkeleton()||!Cup->ContainerMesh||Cup->ContainerMesh->GetStaticMesh()!=Motion->SupportedCupMesh)return Reject(TEXT("unsupported-mesh-or-skeleton"));
 const FTransform TM=M->GetComponentTransform();
 if(TM.ContainsNaN()||!TM.GetScale3D().Equals(FVector::OneVector,.01)||!Cup->GetActorScale3D().Equals(Motion->SupportedCupScale,.01))return Reject(TEXT("unsupported-scale"));
 const FVector Expected=(Motion->Sample(0)*TM).GetLocation();const FVector Offset=Cup->GetActorLocation()-Expected;
 if(Offset.Size()>Motion->MaxStartOffsetCM)return Reject(FString::Printf(TEXT("cup-start-offset: %.3fcm > %.3fcm"),Offset.Size(),Motion->MaxStartOffsetCM));
 UsedCups.Remove(TWeakObjectPtr<ALiquidContainerActor>());UsedCups.Add(Cup);
 ActiveCup=Cup;ActiveSpot=Spot;Mesh=M;OrderSerial=Spot->GetOrderSerial();OriginalCupTransform=Cup->GetActorTransform();StartMeshTransform=TM;StartOffset=Offset;Elapsed=0;ConsumedML=0;bSipPlayed=false;bActive=true;
 if(!Cup->Consumption->TryAcquire(this,Motion->SipVolumeML)){bActive=false;ActiveCup.Reset();ActiveSpot.Reset();Mesh.Reset();return Reject(TEXT("cup-already-claimed"));}
 M->SetAnimationMode(EAnimationMode::AnimationSingleNode);M->SetAnimation(Motion->Animation);
 if(auto* Single=M->GetSingleNodeInstance())Single->SetPlaying(false);
 M->SetPosition(0,false);
 if(Cup->GrabComp){Cup->GrabComp->GrabType=EGrabType::None;Cup->GrabComp->SetActive(false);}Cup->ContainerMesh->SetSimulatePhysics(false);Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cup->StopFlow();
 SetComponentTickEnabled(true);
 UE_LOG(LogTemp,Log,TEXT("[CustomerDrink] Started: owner=%s cup=%s order=%u duration=%.3f animation=%s"),*GetNameSafe(GetOwner()),*GetNameSafe(Cup),OrderSerial,Motion->Duration(),*GetPathNameSafe(Motion->Animation.Get()));
 return true;
}
void UCustomerDrinkComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* F)
{
 Super::TickComponent(Dt,Tick,F);
 if(!bActive||!FMath::IsFinite(Dt)||Dt<=0)return;
 if(!bEnabled||!StillOwnsAcceptedCup()||!Mesh.IsValid()||!IsValid(Motion)||!Motion->IsValidMotion()||!GetOwner()||GetOwner()->IsActorBeingDestroyed()){Finish(false);return;}
 auto* C=ActiveCup.Get();auto* M=Mesh.Get();if(!M->GetComponentTransform().Equals(StartMeshTransform,.05)){Finish(false);return;}
 const float Previous=Elapsed;Elapsed=FMath::Min(Elapsed+Dt,Motion->Duration());M->SetPosition(Elapsed,false);
 if(bUseBlink&&!Motion->BlinkMorph.IsNone())M->SetMorphTarget(Motion->BlinkMorph,Motion->BlinkWeight(Elapsed));
 if(Elapsed>=Motion->GraspTime&&Previous<Motion->ReleaseTime)
 {
  // Move original cup only once the hand reaches it; no duplicate prop and no
  // world-space attach offset snaps. The whole motion is mesh-relative.
  C->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
  FTransform T=Motion->Sample(Elapsed)*StartMeshTransform;
  const float Blend=Motion->GripBlend(Elapsed);
  T.AddToTranslation(StartOffset*(1.f-Blend));
  const FQuat OriginalDelta=OriginalCupTransform.GetRotation()*(Motion->Sample(0)*StartMeshTransform).GetRotation().Inverse();
  T.SetRotation((FQuat::Slerp(OriginalDelta,FQuat::Identity,Blend)*T.GetRotation()).GetNormalized());
  T.SetScale3D(OriginalCupTransform.GetScale3D());C->SetActorTransform(T,false,nullptr,ETeleportType::TeleportPhysics);
 }
 if(Elapsed>=Motion->ReleaseTime&&IsValid(C))
 {
  C->SetActorTransform(OriginalCupTransform,false,nullptr,ETeleportType::TeleportPhysics);
  if(auto* S=ActiveSpot.Get())C->AttachToComponent(S->SceneRoot,FAttachmentTransformRules::KeepWorldTransform);
 }
 const double Overlap=FMath::Max(0.f,FMath::Min(Elapsed,Motion->SipEnd)-FMath::Max(Previous,Motion->SipStart));
 if(Overlap>0&&StillOwnsAcceptedCup())
 {
  const double Requested=Motion->SipVolumeML*Overlap/(Motion->SipEnd-Motion->SipStart);
  const double Actual=C->Consumption->Consume(this,Requested);ConsumedML+=Actual;
  if(Actual>0&&!bSipPlayed){bSipPlayed=true;if(SipSound)UGameplayStatics::PlaySoundAtLocation(this,SipSound,M->GetSocketLocation(Motion->VoiceSocket),Motion->SipSoundVolume);}
  if(Actual>0)C->PublishLiquidState();
 }
 // Liquid-change callbacks can cancel, destroy actors or start a new order.
 if(!bActive)return;if(!StillOwnsAcceptedCup()){Finish(false);return;}
 if(Elapsed>=Motion->Duration())Finish(true);
}
void UCustomerDrinkComponent::Finish(bool Completed,bool Broadcast)
{
 if(!bActive||bFinishing)return;TGuardValue<bool> Guard(bFinishing,true);
 auto* C=ActiveCup.Get();auto* S=ActiveSpot.Get();const bool Owns=StillOwnsAcceptedCup();const double Removed=ConsumedML;
 bActive=false;SetComponentTickEnabled(false);
 if(IsValid(C)&&!C->IsActorBeingDestroyed())
 {
  // Even if the order was cancelled, put the frozen cup back to its known
  // original presentation position. Never unfreeze it or destroy the remainder.
  C->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);C->SetActorTransform(OriginalCupTransform,false,nullptr,ETeleportType::TeleportPhysics);
  if(IsValid(S)&&!S->IsActorBeingDestroyed())C->AttachToComponent(S->SceneRoot,FAttachmentTransformRules::KeepWorldTransform);
  C->Consumption->Release(this);
 }
 if(auto* M=Mesh.Get()){if(Motion&&!Motion->BlinkMorph.IsNone())M->SetMorphTarget(Motion->BlinkMorph,0);if(IdleAnimation){M->SetAnimationMode(EAnimationMode::AnimationSingleNode);M->PlayAnimation(IdleAnimation,true);}else M->SetPosition(0,false);}
 UE_LOG(LogTemp,Log,TEXT("[CustomerDrink] Ended: owner=%s cup=%s completed=%d accepted=%d elapsed=%.3f consumedML=%.6f"),*GetNameSafe(GetOwner()),*GetNameSafe(C),Completed?1:0,Owns?1:0,Elapsed,Removed);
 ActiveCup.Reset();ActiveSpot.Reset();Mesh.Reset();
 if(Broadcast)OnDrinkEnded.Broadcast(C,Completed&&Owns,Removed);
}
void UCustomerDrinkComponent::CancelDrink(){Finish(false);}
void UCustomerDrinkComponent::EndPlay(const EEndPlayReason::Type Reason){Finish(false,true);Super::EndPlay(Reason);}
