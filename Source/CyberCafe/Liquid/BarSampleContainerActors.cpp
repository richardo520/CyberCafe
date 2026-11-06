#include "Liquid/BarSampleContainerActors.h"
#include "NiagaraComponent.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

UBarSampleLiquidCalibrationComponent::UBarSampleLiquidCalibrationComponent()
{
 PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UBarSampleLiquidCalibrationComponent::BeginPlay()
{
 Super::BeginPlay();
 if(auto* Vessel=Cast<ALiquidContainerActor>(GetOwner()))
 {
  AddTickPrerequisiteActor(Vessel);
  Vessel->OnLiquidChanged.AddUniqueDynamic(this,&UBarSampleLiquidCalibrationComponent::HandleLiquidChanged);
 }
}
float UBarSampleLiquidCalibrationComponent::GetCalibratedFill(double VolumeML) const
{
 if(!FMath::IsFinite(VolumeML)||VolumeML<=0)return 0;
 if(VolumeToHeight.Num()<2)return 0;
 for(int32 I=1;I<VolumeToHeight.Num();++I)
 {
  const auto& A=VolumeToHeight[I-1];const auto& B=VolumeToHeight[I];
  if(!FMath::IsFinite(A.X)||!FMath::IsFinite(A.Y)||!FMath::IsFinite(B.X)||!FMath::IsFinite(B.Y)||B.X<=A.X)return 0;
  if(VolumeML<=B.X)return FMath::Clamp(static_cast<float>(FMath::Lerp(A.Y,B.Y,FMath::Clamp((VolumeML-A.X)/(B.X-A.X),0.,1.))),0.f,1.f);
 }
 return FMath::Clamp(static_cast<float>(VolumeToHeight.Last().Y),0.f,1.f);
}
void UBarSampleLiquidCalibrationComponent::HandleLiquidChanged(float,FLinearColor){SyncVisual();}
void UBarSampleLiquidCalibrationComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction)
{
 Super::TickComponent(Dt,Type,TickFunction);SyncVisual();
}
void UBarSampleLiquidCalibrationComponent::SyncVisual()
{
 auto* Vessel=Cast<ALiquidContainerActor>(GetOwner());if(!Vessel||!Vessel->LiquidFX)return;
 VisualFill=Vessel->IsBroken()?0.f:GetCalibratedFill(Vessel->GetPreciseVolumeML());
 // Parent keeps FillAmount=mL/capacity for rules/UI. Niagara gets calibrated
 // geometric height because the shallow bowl and pear bottle are not cylinders.
 Vessel->LiquidFX->SetNiagaraVariableFloat(TEXT("User.Fill"),VisualFill);
 if(Vessel->LiquidMID)
  for(const FName Name:{FName(TEXT("Liquid_Color01")),FName(TEXT("Liquid_Color02")),FName(TEXT("Liquid_Color01Bottom")),FName(TEXT("Liquid_Color02Bottom")),FName(TEXT("Surface_Color01")),FName(TEXT("Surface_Color02"))})
   Vessel->LiquidMID->SetVectorParameterValue(Name,Vessel->LiquidColor);
}
ABarSampleBottleActor::ABarSampleBottleActor()
{
 SampleLiquid=CreateDefaultSubobject<UBarSampleLiquidCalibrationComponent>(TEXT("SampleLiquid"));
 GrabComp->GrabType=EGrabType::Free;GrabComp->PhysicsMode=EGrabPhysicsMode::Kinematic;
 bStartCapped=true;bAcceptLiquidFromOthers=false;FillAmount=.75f;
 bNoSplashes=true;bNoList=true;bEnableDecal=false;
}
void ABarSampleBottleActor::BeginPlay()
{
 Super::BeginPlay();if(SampleLiquid->InitialLiquidMaterial)SetLiquidMaterialAsset(SampleLiquid->InitialLiquidMaterial,false);SampleLiquid->SyncVisual();
}
ABarSampleCupActor::ABarSampleCupActor()
{
 SampleLiquid=CreateDefaultSubobject<UBarSampleLiquidCalibrationComponent>(TEXT("SampleLiquid"));
 GrabComp->GrabType=EGrabType::Free;GrabComp->PhysicsMode=EGrabPhysicsMode::Kinematic;
 FillAmount=0;MaxVolumeML=200;bAcceptLiquidFromOthers=true;bEditorPreviewWidget=false;
 bNoSplashes=true;bNoList=true;bEnableDecal=false;
}
void ABarSampleCupActor::BeginPlay()
{
 Super::BeginPlay();if(SampleLiquid->InitialLiquidMaterial)SetLiquidMaterialAsset(SampleLiquid->InitialLiquidMaterial,false);SampleLiquid->SyncVisual();
}
