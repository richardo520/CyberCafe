#include "Tutorial/CustomerDrinkComponent.h"
#include "Tutorial/CustomerDrinkMotion.h"
#include "Liquid/LiquidContainerActor.h"
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
 if(!bEnabled||bActive||bFinishing||!IsValid(Motion)||!Motion->IsValidMotion()||!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()||!IsValid(Cup)||!IsValid(Spot)||Cup->IsActorBeingDestroyed()||Spot->IsActorBeingDestroyed()||Cup->GetWorld()!=GetWorld()||Spot->GetWorld()!=GetWorld()||!Cast<ACupActor>(Cup)||Cup->IsHeld()||(Cup->GrabComp&&Cup->GrabComp->IsPulled())||Cup->IsBroken()||!Cup->IsLiquidFrozen()||Cup->CustomerConsumer.IsValid()||Cup->bCustomerPerformanceClaimed||UsedCups.Contains(Cup))return false;
 if(Spot->GetServedCup()!=Cup||!Spot->HasBeenServed()||!Spot->GetLastValidation().bAccepted||Spot->GetOrderSerial()!=Spot->GetAcceptedOrderSerial())return false;
 auto* M=GetOwner()->FindComponentByClass<USkeletalMeshComponent>();if(!M||!M->GetSkeletalMeshAsset()||M->GetSkeletalMeshAsset()->GetSkeleton()!=Motion->Animation->GetSkeleton()||Cup->ContainerMesh->GetStaticMesh()!=Motion->SupportedCupMesh)return false;
 const FTransform TM=M->GetComponentTransform();if(TM.ContainsNaN()||!TM.GetScale3D().Equals(FVector::OneVector,.01)||!Cup->GetActorScale3D().Equals(FVector::OneVector,.01))return false;
 const FVector Expected=(Motion->Sample(0)*TM).GetLocation();const FVector Offset=Cup->GetActorLocation()-Expected;if(Offset.Size()>Motion->MaxStartOffsetCM)return false;
 UsedCups.Remove(TWeakObjectPtr<ALiquidContainerActor>());UsedCups.Add(Cup);
 ActiveCup=Cup;ActiveSpot=Spot;Mesh=M;OrderSerial=Spot->GetOrderSerial();OriginalCupTransform=Cup->GetActorTransform();StartMeshTransform=TM;StartOffset=Offset;Elapsed=0;ConsumedML=0;bSipPlayed=false;bActive=true;
 Cup->bCustomerPerformanceClaimed=true;Cup->CustomerConsumer=this;Cup->CustomerSipBudgetML=FMath::Min(static_cast<double>(Motion->SipVolumeML),Cup->GetPreciseVolumeML());
 M->SetAnimationMode(EAnimationMode::AnimationSingleNode);M->SetAnimation(Motion->Animation);
 if(auto* Single=M->GetSingleNodeInstance())Single->SetPlaying(false);
 M->SetPosition(0,false);
 if(Cup->GrabComp){Cup->GrabComp->GrabType=EGrabType::None;Cup->GrabComp->SetActive(false);}Cup->ContainerMesh->SetSimulatePhysics(false);Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cup->StopPouring();
 SetComponentTickEnabled(true);return true;
}
void UCustomerDrinkComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* F)
{
 Super::TickComponent(Dt,Tick,F);
 if(!bActive||!FMath::IsFinite(Dt)||Dt<=0)return;
 if(!bEnabled||!StillOwnsAcceptedCup()||!Mesh.IsValid()||!IsValid(Motion)||!Motion->IsValidMotion()||!GetOwner()||GetOwner()->IsActorBeingDestroyed()){Finish(false);return;}
 auto* C=ActiveCup.Get();auto* M=Mesh.Get();if(!M->GetComponentTransform().Equals(StartMeshTransform,.05)){Finish(false);return;}
 const float Previous=Elapsed;Elapsed=FMath::Min(Elapsed+Dt,Motion->Duration());M->SetPosition(Elapsed,false);
 if(bUseBlink){const float T=Elapsed;const float A=FMath::Clamp(1.f-FMath::Abs(T-3.12f)/.12f,0.f,1.f);const float B=FMath::Clamp(1.f-FMath::Abs(T-6.02f)/.12f,0.f,1.f);M->SetMorphTarget(TEXT("Blink"),FMath::Max(A,B));}
 if(Elapsed>=Motion->GraspTime&&Previous<Motion->ReleaseTime)
 {
  // Move original cup only once the hand reaches it; no duplicate prop and no
  // world-space attach offset snaps. The whole motion is mesh-relative.
  C->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
  FTransform T=Motion->Sample(Elapsed)*StartMeshTransform;
  const float Lift=FMath::SmoothStep(Motion->GraspTime,2.5f,Elapsed);const float Return=1.f-FMath::SmoothStep(4.0f,Motion->ReleaseTime,Elapsed);const float Blend=FMath::Min(Lift,Return);
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
  const double Actual=C->ConsumeForCustomer(this,Requested);ConsumedML+=Actual;
  if(Actual>0&&!bSipPlayed){bSipPlayed=true;if(SipSound)UGameplayStatics::PlaySoundAtLocation(this,SipSound,M->GetSocketLocation(TEXT("head")),.4f);}
  if(Actual>0)C->NotifyLiquidState();
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
  if(C->CustomerConsumer.Get()==this){C->CustomerConsumer.Reset();C->CustomerSipBudgetML=0;}
 }
 if(auto* M=Mesh.Get()){M->SetMorphTarget(TEXT("Blink"),0);if(IdleAnimation){M->SetAnimationMode(EAnimationMode::AnimationSingleNode);M->PlayAnimation(IdleAnimation,true);}else M->SetPosition(0,false);}
 ActiveCup.Reset();ActiveSpot.Reset();Mesh.Reset();
 if(Broadcast)OnDrinkEnded.Broadcast(C,Completed&&Owns,Removed);
}
void UCustomerDrinkComponent::CancelDrink(){Finish(false);}
void UCustomerDrinkComponent::EndPlay(const EEndPlayReason::Type Reason){Finish(false,true);Super::EndPlay(Reason);}
