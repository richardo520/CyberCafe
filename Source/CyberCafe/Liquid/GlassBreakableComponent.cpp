#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/BottleActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/IceInteraction.h"
#include "GrabComponent.h"
#include "Character/VRPawn.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "MotionControllerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"
static TAutoConsoleVariable<int32> CVarGlassDebug(TEXT("cybercafe.Glass.Debug"),0,TEXT("Draw glass sweep samples and measured closing speed"));
UGlassBreakableComponent::UGlassBreakableComponent(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostPhysics;}
void UGlassBreakableComponent::BeginPlay()
{
 Super::BeginPlay();Container=Cast<ALiquidContainerActor>(GetOwner());GraceRemaining=StartupGraceSeconds;
 if(auto* A=Container.Get()){
  A->ContainerMesh->SetNotifyRigidBodyCollision(true);
  A->ContainerMesh->SetUseCCD(true);
  A->ContainerMesh->OnComponentHit.AddUniqueDynamic(this,&UGlassBreakableComponent::HandleHit);
 }
}
void UGlassBreakableComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 if(auto* A=Container.Get())A->ContainerMesh->OnComponentHit.RemoveDynamic(this,&UGlassBreakableComponent::HandleHit);
 Super::EndPlay(Reason);
}
float UGlassBreakableComponent::ClosingSpeed(const FVector& Velocity,const FVector& Normal)
{
 if(Velocity.ContainsNaN()||Normal.ContainsNaN()||Normal.IsNearlyZero())return 0;
 return FMath::Max(0.f,static_cast<float>(-FVector::DotProduct(Velocity,Normal.GetSafeNormal())));
}
bool UGlassBreakableComponent::TryImpact(const FVector& Contact,const FVector& Normal,const FVector& Velocity,AActor* Other)
{
 ALiquidContainerActor* A=Container.Get();if(!A)A=Cast<ALiquidContainerActor>(GetOwner());
 if(!A||!Profile||Age<StartupGraceSeconds||GraceRemaining>0||Contact.ContainsNaN()||A->IsBroken()||A->IsLiquidFrozen())return false;
 // @trace IC-04. Ice and the dedicated scoop cannot shatter a glass during normal filling.
 if(Other==A||Cast<AIceCubeActor>(Other)||Cast<AIceScoopActor>(Other)||Cast<APawn>(Other)||Cast<ALiquidContainerActor>(Other)||Cast<ABottleCapActor>(Other)||Cast<AGlassDebrisActor>(Other))return false;
 if(A->GrabComp && A->GrabComp->IsPulled())return false;
 const float Speed=ClosingSpeed(Velocity,Normal);
 if(CVarGlassDebug.GetValueOnGameThread()>0)DrawDebugString(GetWorld(),Contact,FString::Printf(TEXT("Glass %.0f / %.0f cm/s"),Speed,Profile->BreakSpeedCms),nullptr,FColor::Orange,1.f);
 if(!FMath::IsFinite(Profile->BreakSpeedCms)||Profile->BreakSpeedCms<=0||Speed<Profile->BreakSpeedCms)return false;
 const double SpilledVolume=A->GetPreciseVolumeML();const FLinearColor SpilledColor=A->LiquidColor;
 const FTransform Body=A->ContainerMesh->GetComponentTransform();
 UMotionControllerComponent* Hand=A->GrabComp?A->GrabComp->GetHoldingController():nullptr;
 if(!A->BreakContainerForImpact())return false;
 if(auto* CupIce=A->FindComponentByClass<UCupIceComponent>())CupIce->ReleaseAll();
 if(Hand){if(auto* Pawn=Cast<AVRPawn>(Hand->GetOwner()))if(auto* PC=Cast<APlayerController>(Pawn->GetController()))if(Pawn->HoverHapticEffect)PC->PlayHapticEffect(Pawn->HoverHapticEffect,A->GrabComp->GetHeldByHand(),.8f,false);}
 // Disconnect caps before release broadcasts; attached cap goes with broken bottle,
 // detached cap survives without ever snapping back to an invalid container.
 if(auto* Bottle=Cast<ABottleActor>(A)){if(Bottle->CapActor)Bottle->CapActor->InvalidateBrokenBottle();Bottle->CapActor=nullptr;Bottle->bIsCapped=false;}
 if(A->GrabComp)A->GrabComp->ReleaseForBreak();
 for(TActorIterator<AVRPawn> I(GetWorld());I;++I)I->ForgetGrabForBreak(A->GrabComp);
 A->ContainerMesh->SetSimulatePhysics(false);A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 A->ContainerMesh->SetVisibility(false,true);A->SetActorEnableCollision(false);A->SetActorTickEnabled(false);
 if(A->GrabComp)A->GrabComp->Deactivate();
 for(UActorComponent* C:A->GetComponents())if(auto* W=Cast<UWidgetComponent>(C))W->SetVisibility(false);
 if(Profile->BreakSound)UGameplayStatics::PlaySoundAtLocation(this,Profile->BreakSound,Contact,.7f);
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 if(auto* Debris=GetWorld()->SpawnActor<AGlassDebrisActor>(AGlassDebrisActor::StaticClass(),FTransform::Identity,Params))Debris->Initialize(Profile,Body,Contact,Velocity,Normal,SpilledVolume,SpilledColor);
 SetComponentTickEnabled(false);OnBroken.Broadcast(A);return true;
}
void UGlassBreakableComponent::HandleHit(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent* OtherComp,FVector,const FHitResult& Hit)
{
 if(!bSampleReady||GraceRemaining>0)return;
 FVector V=PreviousVelocity;
 if(OtherComp)V-=OtherComp->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint);
 TryImpact(Hit.ImpactPoint,Hit.ImpactNormal,V,Other);
}
void UGlassBreakableComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Fn)
{
 Super::TickComponent(Dt,Tick,Fn);Age+=FMath::Max(0.f,Dt);GraceRemaining=FMath::Max(0.f,GraceRemaining-Dt);
 ALiquidContainerActor* A=Container.Get();if(!A||A->IsBroken())return;
 UStaticMeshComponent* Mesh=A->ContainerMesh;UStaticMesh* SM=Mesh->GetStaticMesh();if(!SM)return;
 const FTransform Body=Mesh->GetComponentTransform();const FBox Box=SM->GetBoundingBox();const FVector Center=Body.TransformPosition(Box.GetCenter());
 const bool Held=A->GrabComp&&A->GrabComp->IsHeld();
 const bool Pull=A->GrabComp&&A->GrabComp->IsPulled();
 const bool Jump=Dt<=0||Dt>.12f||FVector::Dist(Center,PreviousCenter)>MaxTrackingJumpCM;
 if(!bSampleReady||Jump||(Held&&!bWasHeld)||Pull){bSampleReady=true;PreviousCenter=Center;PreviousRotation=Body.GetRotation();PreviousVelocity=FVector::ZeroVector;GraceRemaining=FMath::Max(GraceRemaining,.12f);bWasHeld=Held;return;}
 FVector Velocity=(Center-PreviousCenter)/Dt;
 if(!Held&&Mesh->IsSimulatingPhysics())Velocity=Mesh->GetPhysicsLinearVelocity();
 const FVector Scale=Body.GetScale3D().GetAbs();const float Radius=FMath::Max(.3f,static_cast<float>(FMath::Min(Box.GetExtent().X*Scale.X,Box.GetExtent().Y*Scale.Y)*.80));
 FQuat Delta=Body.GetRotation()*PreviousRotation.Inverse();if(Delta.W<0)Delta=Delta*-1.f;FVector Axis;float Angle;Delta.ToAxisAndAngle(Axis,Angle);const FVector Omega=Axis*Angle/Dt;
 if(GraceRemaining<=0&&!A->IsLiquidFrozen())
 {
  FCollisionQueryParams Params(SCENE_QUERY_STAT(GlassImpact),false);Params.AddIgnoredActor(A);
  if(A->GetInstigator())Params.AddIgnoredActor(A->GetInstigator());
  for(TActorIterator<APawn> I(GetWorld());I;++I)Params.AddIgnoredActor(*I);
  if(auto* B=Cast<ABottleActor>(A))if(B->CapActor)Params.AddIgnoredActor(B->CapActor);
  // Three swept cross sections cover base, body and rim; no enclosing full-height
  // sphere that would hit the table while the vessel is being held safely above it.
  for(float Fraction:{.12f,.5f,.88f})
  {
   const double Margin=FMath::Min(static_cast<double>(Radius/FMath::Max(.001,Scale.Z)+.12),Box.GetExtent().Z*.95);
   const double LocalZ=FMath::Lerp(Box.Min.Z+Margin,Box.Max.Z-Margin,(Fraction-.12f)/.76f);
   const FVector Local=FVector(Box.GetCenter().X,Box.GetCenter().Y,LocalZ);
   const FVector End=Body.TransformPosition(Local);const FVector Offset=End-Center;
   const FVector Start=PreviousCenter+PreviousRotation.RotateVector(Body.GetRotation().Inverse().RotateVector(Offset));
   const FVector PointV=Velocity+FVector::CrossProduct(Omega,Offset);
   FHitResult Hit;
   if(GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(Radius),Params)&&!Hit.bStartPenetrating)
   {
    FVector Relative=PointV;if(Hit.GetComponent())Relative-=Hit.GetComponent()->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint);
    if(TryImpact(Hit.ImpactPoint,Hit.ImpactNormal,Relative,Hit.GetActor()))return;
   }
   if(CVarGlassDebug.GetValueOnGameThread()>0)DrawDebugLine(GetWorld(),Start,End,FColor::Orange,false,.1f);
  }
 }
 PreviousCenter=Center;PreviousRotation=Body.GetRotation();PreviousVelocity=Velocity;bWasHeld=Held;
}
AGlassDebrisActor::AGlassDebrisActor(){auto* R=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));SetRootComponent(R);PrimaryActorTick.bCanEverTick=true;}
void AGlassDebrisActor::Initialize(UGlassBreakProfile* Profile,const FTransform& Body,const FVector& Contact,const FVector& Velocity,const FVector& Normal,double SpilledML,FLinearColor LiquidColor)
{
 if(!Profile)return;
 // Hard scene-wide budget: at most 6 live debris batches, each <=8 simulated pieces.
 TArray<AGlassDebrisActor*> Older;for(TActorIterator<AGlassDebrisActor> I(GetWorld());I;++I)if(*I!=this)Older.Add(*I);
 if(Older.Num()>=6)Older[0]->Destroy();
 SetLifeSpan(FMath::Clamp(Profile->LifetimeSeconds,2.f,20.f));
 if(SpilledML>1&&Profile->SpillMaterial)
 {
  auto* Puddle=NewObject<UStaticMeshComponent>(this);Puddle->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));Puddle->SetupAttachment(GetRootComponent());Puddle->SetWorldLocation(Contact+Normal.GetSafeNormal()*.15f);Puddle->SetWorldRotation(FRotationMatrix::MakeFromZ(Normal).Rotator());const float Size=FMath::Clamp(static_cast<float>(FMath::Sqrt(SpilledML)*.55),3.f,16.f);Puddle->SetWorldScale3D(FVector(Size/50,Size/50,.001));Puddle->SetCollisionEnabled(ECollisionEnabled::NoCollision);Puddle->SetCastShadow(false);Puddle->SetMaterial(0,Profile->SpillMaterial);Puddle->RegisterComponent();if(auto* MID=Puddle->CreateDynamicMaterialInstance(0))MID->SetVectorParameterValue(TEXT("SpillColor"),LiquidColor);Pieces.Add(Puddle);
 }
 const int32 PhysicsBudget=FMath::Clamp(Profile->MaxPhysicsShards,0,8);
 int32 Count=0;
 for(const FGlassShardSpec& S:Profile->Shards)
 {
  if(!S.Mesh||Count>=16)continue;
  UStaticMeshComponent* C=NewObject<UStaticMeshComponent>(this);C->SetStaticMesh(S.Mesh);C->SetupAttachment(GetRootComponent());C->SetWorldTransform(S.LocalTransform*Body);C->SetCastShadow(false);C->SetGenerateOverlapEvents(false);C->SetCollisionResponseToAllChannels(ECR_Ignore);C->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);C->SetCollisionEnabled(Count<PhysicsBudget?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);C->RegisterComponent();Pieces.Add(C);
  if(Count<PhysicsBudget){C->SetSimulatePhysics(true);C->SetUseCCD(true);C->SetLinearDamping(1.f);C->SetAngularDamping(2.f);const FVector Out=(C->GetComponentLocation()-Contact).GetSafeNormal();C->SetPhysicsLinearVelocity(Velocity.GetClampedToMaxSize(800)*.30+Out*90+Normal.GetSafeNormal()*80+FVector(0,0,40));C->SetPhysicsAngularVelocityInDegrees(FVector(120,-70,90)*(Count%2?1:-1));}
  else C->AddWorldOffset(Normal.GetSafeNormal()*(Count%3));
  for(int32 I=0;I<C->GetNumMaterials();++I)if(auto* MID=C->CreateDynamicMaterialInstance(I))MID->SetScalarParameterValue(TEXT("Highlight"),0);
  ++Count;
 }
}
void AGlassDebrisActor::Tick(float Dt)
{
 Super::Tick(Dt);Age+=Dt;
 if(!bSlept&&Age>2.f){for(const auto& C:Pieces){C->SetSimulatePhysics(false);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);}bSlept=true;SetActorTickEnabled(false);}
}
