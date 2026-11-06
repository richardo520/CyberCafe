#include "Coffee/Brew/HandPourKettleActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "GrabComponent.h"
AHandPourKettleActor::AHandPourKettleActor()
{
 MaxVolumeML=500;FillAmount=1;InitialIngredientId=TEXT("Water");bStartCapped=false;CapClass=nullptr;bCanPour=false;bAcceptLiquidFromOthers=false;bUseRibbonTrajectory=false;
 // P_Ribbon has a -User.Offset spawn displacement and a thickness compensation
 // proportional to (1-FlowStrength). Zero both for a nozzle-anchored thin jet.
 FlowStrength=1.f;PourRibbonOffset=0.f;LiquidColor=WaterStreamColor;bNoSplashes=true;bNoList=true;bEnableDecal=false;
 PourFX->SetRelativeLocation(KettleSpoutLocal);PourFX->SetRelativeRotation(FRotator::ZeroRotator);PourFX->SetRelativeScale3D(FVector::OneVector);
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Ribbon(TEXT("/Game/LiquidMaterials_VFXPack/Effects/P_Ribbon.P_Ribbon"));
 PourFX->SetAsset(Ribbon.Object);
 GrabComp->PhysicsMode=EGrabPhysicsMode::Kinematic;
 // 保留旧 native 子对象以兼容地图/BP 序列化，不再用实例球渲染水流。
 WaterJet=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WaterJet"));WaterJet->SetupAttachment(ContainerMesh);WaterJet->SetCollisionEnabled(ECollisionEnabled::NoCollision);WaterJet->SetCastShadow(false);
 WaterJet->SetHiddenInGame(true);WaterJet->SetVisibility(false);WaterJet->PrimaryComponentTick.bCanEverTick=false;WaterJet->SetComponentTickEnabled(false);
}
void AHandPourKettleActor::BeginPlay()
{
 // Saved pre-fix BP/map instances still contain the inherited red-wine default.
 LiquidColor=WaterStreamColor;
 Super::BeginPlay();if(WaterLiquidMaterial)SetLiquidMaterialAsset(WaterLiquidMaterial,false);
 LiquidColor=WaterStreamColor;RefreshLiquidFX();
 if(PourFX)PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),WaterStreamColor);
 StopPouring();
}
void AHandPourKettleActor::Tick(float Dt)
{
 if(!FMath::IsFinite(Dt)||Dt<=0){StopPouring();return;}
 bCanPour=false;
 // bIsPouring 在壶中只标记视觉出流；临时隐藏它，避免基类因 bCanPour=false 每帧停 Ribbon，且只由 PourWater 发量。
 const bool WasPouring=bIsPouring;bIsPouring=false;
 Super::Tick(Dt);
 bIsPouring=WasPouring;
 // 冻结/破碎仍走基类 StopPouring；恢复标记后由 PourWater 再检查并停止，不能继续发量。
 PourWater(Dt);
}
float AHandPourKettleActor::PourWater(float Dt)
{
 if(WaterJet)WaterJet->ClearInstances();CurrentFlowMLPerSecond=0;
 if(!FMath::IsFinite(Dt)||Dt<=0||IsBroken()||IsLiquidFrozen()||GetPreciseVolumeML()<=0||!PourFX){StopPouring();return 0;}
 const float Tilt=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(GetActorUpVector().Z,-1.f,1.f)));
 const FTransform SpoutTM=GetKettleSpoutWorldTransform();
 const float Down=FVector::DotProduct(SpoutTM.GetUnitAxis(EAxis::X),FVector::DownVector);
 const float Threshold=FMath::Clamp(FMath::IsFinite(PourTiltDegrees)?PourTiltDegrees:40.f,10.f,80.f);
 if(Tilt<=Threshold||Down<=.3){StopPouring();return 0;}
 const float Rate=FMath::Max(0.f,FMath::IsFinite(MaxFlowMLPerSecond)?MaxFlowMLPerSecond:0.f)*FMath::Clamp((Tilt-Threshold)/40.f,0.f,1.f);
 if(Rate<=0){StopPouring();return 0;}
 auto* S=GetWorld()?GetWorld()->GetSubsystem<ULiquidTransferSubsystem>():nullptr;if(!S){StopPouring();return 0;}
 LiquidColor=WaterStreamColor;
 const FVector Origin=SpoutTM.GetLocation();const FVector Velocity=SpoutTM.GetUnitAxis(EAxis::X)*35.f;double Emitted=0;
 const int32 N=FMath::Clamp(FMath::CeilToInt(Dt*120),1,240);const double Slice=Dt/N;
 for(int I=0;I<N;++I){if(IsBroken()||IsLiquidFrozen()||GetPreciseVolumeML()<=0)break;Emitted+=S->EmitVolume(this,Rate*Slice,Origin,Velocity,FVector(0,0,-980),(I+.5)*Slice);}
 CurrentFlowMLPerSecond=Emitted/Dt;
 // 最后一份液量发完的当帧保留水柱；空壶在下一帧停止，粒子回调不参与计量。
 UpdateKettleRibbon(Emitted>0);
 return Emitted;
}
void AHandPourKettleActor::UpdateKettleRibbon(bool bEmissionPositive)
{
 if(!bEmissionPositive||!PourFX||IsBroken()||IsLiquidFrozen()){StopPouring();return;}
 const FTransform SpoutTM=GetKettleSpoutWorldTransform();
 // Logic and rendered jet share the measured mouth centre and outward axis.
 PourFX->SetWorldLocationAndRotation(SpoutTM.GetLocation(),SpoutTM.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
 PourFX->SetRelativeScale3D(FVector::OneVector);
 PourFX->SetNiagaraVariableFloat(TEXT("User.Thickness"),KettleRibbonThicknessCM);
 PourFX->SetNiagaraVariableFloat(TEXT("User.FlowStrength"),FlowStrength);
 // 原 P_Ribbon 是年龄曲线位移，无重力参数；20/3 是视觉曲线参数，不是业务弹道的 35 cm/s。
 PourFX->SetNiagaraVariableFloat(TEXT("User.Velocity"),PourRibbonVelocity);
 PourFX->SetNiagaraVariableFloat(TEXT("User.Offset"),PourRibbonOffset);
 PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),WaterStreamColor);
 PourFX->SetNiagaraVariableBool(TEXT("User.NoSplashes"),true);
 PourFX->SetNiagaraVariableBool(TEXT("User.NoList"),true);
 PourFX->SetNiagaraVariableBool(TEXT("User.Decal"),false);
 // 不依赖 IsActive（NullRHI/空资产可为 false），仅在出流开始时激活，不逐帧 Reset 已发粒子。
 if(!bIsPouring){bIsPouring=true;PourFX->Activate(false);}
}
FTransform AHandPourKettleActor::GetKettleSpoutWorldTransform() const
{
 const FTransform MeshTM=ContainerMesh?ContainerMesh->GetComponentTransform():GetActorTransform();
 FVector X=MeshTM.TransformVectorNoScale(KettleSpoutForwardLocal).GetSafeNormal();
 if(X.IsNearlyZero())X=MeshTM.GetUnitAxis(EAxis::X);
 const FVector Z=MeshTM.GetUnitAxis(EAxis::Z);
 return FTransform(FRotationMatrix::MakeFromXZ(X,Z).ToQuat(),MeshTM.TransformPosition(KettleSpoutLocal),MeshTM.GetScale3D());
}
void AHandPourKettleActor::StopPouring()
{
 Super::StopPouring();CurrentFlowMLPerSecond=0;
 if(WaterJet){WaterJet->ClearInstances();WaterJet->SetHiddenInGame(true);WaterJet->SetVisibility(false);WaterJet->PrimaryComponentTick.bCanEverTick=false;WaterJet->SetComponentTickEnabled(false);}
}
