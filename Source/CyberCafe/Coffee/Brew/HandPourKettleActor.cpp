#include "Coffee/Brew/HandPourKettleActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "GrabComponent.h"
AHandPourKettleActor::AHandPourKettleActor()
{
 MaxVolumeML=500;FillAmount=1;InitialIngredientId=TEXT("Water");bStartCapped=false;CapClass=nullptr;bCanPour=false;bAcceptLiquidFromOthers=false;bUseRibbonTrajectory=false;
 PourFX->SetRelativeLocation(FVector(16.8,0,13));
 GrabComp->PhysicsMode=EGrabPhysicsMode::Kinematic;
 WaterJet=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WaterJet"));WaterJet->SetupAttachment(ContainerMesh);WaterJet->SetCollisionEnabled(ECollisionEnabled::NoCollision);WaterJet->SetCastShadow(false);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));WaterJet->SetStaticMesh(Sphere.Object);
}
void AHandPourKettleActor::BeginPlay()
{
 Super::BeginPlay();if(WaterLiquidMaterial)SetLiquidMaterialAsset(WaterLiquidMaterial,false);
}
void AHandPourKettleActor::Tick(float Dt)
{
 if(!FMath::IsFinite(Dt)||Dt<=0)return;
 bCanPour=false;Super::Tick(Dt);PourWater(Dt);
}
float AHandPourKettleActor::PourWater(float Dt)
{
 if(WaterJet)WaterJet->ClearInstances();CurrentFlowMLPerSecond=0;
 if(!FMath::IsFinite(Dt)||Dt<=0||IsBroken()||IsLiquidFrozen()||GetPreciseVolumeML()<=0||!PourFX)return 0;
 const float Tilt=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(GetActorUpVector().Z,-1.f,1.f)));
 const float Down=FVector::DotProduct(GetActorForwardVector(),FVector::DownVector);
 const float Threshold=FMath::Clamp(FMath::IsFinite(PourTiltDegrees)?PourTiltDegrees:40.f,10.f,80.f);
 if(Tilt<=Threshold||Down<=.3)return 0;
 const float Rate=FMath::Max(0.f,FMath::IsFinite(MaxFlowMLPerSecond)?MaxFlowMLPerSecond:0.f)*FMath::Clamp((Tilt-Threshold)/40.f,0.f,1.f);
 auto* S=GetWorld()?GetWorld()->GetSubsystem<ULiquidTransferSubsystem>():nullptr;if(!S)return 0;
 const FVector Origin=PourFX->GetComponentLocation();const FVector Velocity=GetActorForwardVector()*35.f;double Emitted=0;
 const int32 N=FMath::Clamp(FMath::CeilToInt(Dt*120),1,240);const double Slice=Dt/N;
 for(int I=0;I<N;++I){if(IsBroken()||IsLiquidFrozen()||GetPreciseVolumeML()<=0)break;Emitted+=S->EmitVolume(this,Rate*Slice,Origin,Velocity,FVector(0,0,-980),(I+.5)*Slice);}
 CurrentFlowMLPerSecond=Emitted/Dt;FlowAge+=Dt;
 if(WaterJet&&Emitted>0){for(int I=0;I<20;++I){const float T=FMath::Frac(FlowAge*4+I/20.f)*.22f;const FVector P=Origin+Velocity*T+FVector(0,0,-490*T*T);WaterJet->AddInstance(FTransform(FQuat::Identity,P,FVector(.002,.002,.0035)),true);}}
 return Emitted;
}
