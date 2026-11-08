#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/VRPawn.h"
#include "MotionControllerComponent.h"
#include "GrabComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Serving/DrinkRecipe.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/Steps/TutorialStep_Grab.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Liquid/BottleActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Serving/ServingSpotActor.h"
#include "Engine/StaticMesh.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Coffee/Bean/DirectBeanJarActor.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
AGlassRecoveryActor::AGlassRecoveryActor()
{
 PrimaryActorTick.bCanEverTick=true;
 auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));SetRootComponent(Root);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
 Base=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RecoveryPad"));Base->SetupAttachment(Root);Base->SetStaticMesh(Cube.Object);Base->SetRelativeScale3D(FVector(.16,.10,.012));Base->SetRelativeLocation(FVector(0,0,.6));Base->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 CupButton=CreateDefaultSubobject<UBoxComponent>(TEXT("CupButton"));CupButton->SetupAttachment(Root);CupButton->SetBoxExtent(FVector(2.5,2.8,1.5));CupButton->SetRelativeLocation(FVector(-4,0,2));CupButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 BottleButton=CreateDefaultSubobject<UBoxComponent>(TEXT("BottleButton"));BottleButton->SetupAttachment(Root);BottleButton->SetBoxExtent(FVector(2.5,2.8,1.5));BottleButton->SetRelativeLocation(FVector(4,0,2));BottleButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 for(int32 I=0;I<2;++I){auto* C=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Key%d"),I));C->SetupAttachment(Root);C->SetStaticMesh(Cube.Object);C->SetRelativeScale3D(FVector(.045,.05,.007));C->SetRelativeLocation(FVector(I==0?-4:4,0,1.5));C->SetCollisionEnabled(ECollisionEnabled::NoCollision);Keys.Add(C);auto* T=CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("Label%d"),I));T->SetupAttachment(Root);T->SetRelativeLocation(FVector(I==0?-7:1,-2.5,2));T->SetRelativeRotation(FRotator(90,90,0));T->SetWorldSize(1.1f);T->SetText(FText::FromString(I==0?TEXT("CUP"):TEXT("MILK")));Labels.Add(T);}
}
void AGlassRecoveryActor::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform);ApplyButtonMode();
}
void AGlassRecoveryActor::ApplyButtonMode()
{
 const bool Unified=IsUnifiedRestoreMode();
 CupButton->SetRelativeLocation(FVector(Unified?0:-4,0,2));CupButton->SetBoxExtent(FVector(Unified?6:2.5,2.8,1.5));CupButton->SetActive(true);
 BottleButton->SetRelativeLocation(FVector(4,0,2));BottleButton->SetBoxExtent(FVector(2.5,2.8,1.5));BottleButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);BottleButton->SetActive(!Unified);
 for(int32 I=0;I<2;++I){if(!Keys.IsValidIndex(I)||!Labels.IsValidIndex(I))continue;
  Keys[I]->SetRelativeLocation(FVector(Unified&&I==0?0:(I==0?-4:4),0,1.5));Keys[I]->SetRelativeScale3D(FVector(Unified&&I==0?.12:.045,.05,.007));Keys[I]->SetVisibility(!Unified||I==0);Keys[I]->SetHiddenInGame(Unified&&I==1);
  Labels[I]->SetRelativeLocation(FVector(Unified&&I==0?-7:(I==0?-7:1),-2.5,2));Labels[I]->SetWorldSize(Unified&&I==0?.8f:1.1f);Labels[I]->SetVisibility(!Unified||I==0);Labels[I]->SetHiddenInGame(Unified&&I==1);Labels[I]->SetText(FText::FromString(Unified?(I==0?TEXT("RESTORE SUPPLIES"):TEXT("")):(I==0?TEXT("CUP"):TEXT("MILK"))));
 }
 if(IsValid(StatusLabel.Get())){StatusLabel->SetVisibility(Unified);StatusLabel->SetHiddenInGame(!Unified);}
}
void AGlassRecoveryActor::BindPrototype(ALiquidContainerActor* Cup,ALiquidContainerActor* Bottle,UGlassBreakProfile* CupData,UGlassBreakProfile* BottleData)
{PrototypeCup=Cup;PrototypeBottle=Bottle;CupProfile=CupData;BottleProfile=BottleData;}
void AGlassRecoveryActor::Install(ALiquidContainerActor* A,UGlassBreakProfile* Data)
{
 if(!IsValid(A)||!IsValid(Data)||A->FindComponentByClass<UGlassBreakableComponent>())return;
 auto* Comp=NewObject<UGlassBreakableComponent>(A);Comp->Profile=Data;A->AddInstanceComponent(Comp);Comp->RegisterComponent();
}
// @trace RS-01 保存初始供给，不以恢复时的余液、变形或运行时 MID 作为模板。
void AGlassRecoveryActor::CaptureSlot(int32 I,ALiquidContainerActor* A,UGlassBreakProfile* P)
{
 if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=GetWorld()||!IsValid(A->ContainerMesh.Get()))return;
 auto& S=Slots[I];
 if(auto* Existing=A->FindComponentByClass<UGlassBreakableComponent>())if(IsValid(Existing->Profile.Get()))P=Existing->Profile.Get();
 S.Current=A;S.Profile=P;S.ContainerClass=A->GetClass();S.SpawnTransform=A->GetActorTransform();
 S.InitialFill=A->FillAmount;S.Capacity=A->MaxVolumeML;S.Ingredient=A->InitialIngredientId;S.Color=A->LiquidColor;
 if(auto* Jar=Cast<ADirectBeanJarActor>(A)){
  S.bBeanJar=true;S.InitialFill=0;
  const float Capacity=FMath::IsFinite(Jar->BeanCapacityGrams)?FMath::Max(0.f,Jar->BeanCapacityGrams):0.f;
  S.BeanSupplyG=FMath::IsFinite(Jar->InitialBeanGrams)?FMath::Clamp(Jar->InitialBeanGrams,0.f,Capacity):0.f;
 }
 S.PourEntryRadius=A->PourEntryRadius;
 if(A->PourEntryPoint){S.PourEntryRelativeTransform=A->PourEntryPoint->GetRelativeTransform();S.bEntryAbsoluteLocation=A->PourEntryPoint->IsUsingAbsoluteLocation();S.bEntryAbsoluteRotation=A->PourEntryPoint->IsUsingAbsoluteRotation();S.bEntryAbsoluteScale=A->PourEntryPoint->IsUsingAbsoluteScale();}
 if(!P)P=A->GlassBreakProfile;
 S.Profile=P;
 S.bCanPour=!S.bBeanJar&&A->bCanPour;S.bAcceptLiquid=!S.bBeanJar&&A->bAcceptLiquidFromOthers;
 S.Mesh=A->ContainerMesh->GetStaticMesh();S.MeshRelativeTransform=A->ContainerMesh->GetAttachParent()?A->ContainerMesh->GetRelativeTransform():FTransform::Identity;
 for(int32 M=0;M<A->ContainerMesh->GetNumMaterials();++M){UMaterialInterface* Material=A->ContainerMesh->GetMaterial(M);while(auto* MID=Cast<UMaterialInstanceDynamic>(Material))Material=MID->Parent;S.Materials.Add(Material);}
 if(auto* B=Cast<ABottleActor>(A)){S.CapClass=B->CapClass;S.CapSocket=B->CapSocketName;S.bStartCapped=B->bStartCapped;S.CapturedCap=B->CapActor;}
 if(auto* Ice=A->FindComponentByClass<UCupIceComponent>()){S.bHasCupIce=true;S.IceRadiusCM=Ice->InteriorRadiusCM;S.IceDepthCM=Ice->InteriorDepthCM;S.MaxCupIce=Ice->MaxCupIce;S.ClinkSound=Ice->ClinkSound;}
 else for(TActorIterator<AIceStationActor> It(GetWorld());It;++It)if(It->Cup.Get()==A){S.bHasCupIce=true;S.IceRadiusCM=It->CupInteriorRadiusCM;S.IceDepthCM=It->CupInteriorDepthCM;S.ClinkSound=It->ClinkSound;break;}
 S.bInitialized=true;Install(A,P);
}
void AGlassRecoveryActor::BeginPlay()
{
 // 在本 Actor 的 BeginPlay 回调和物理 Tick 前捕获地图配置；不触发工具液体初始化。
 ToolSlots.Reset();bToolCaptureComplete=bRestoreAllSupplies;bToolCaptureValid=true;
 if(bRestoreAllSupplies)for(const auto& Target:RestoreToolTargets){bool Duplicate=false;for(const auto& S:ToolSlots)if(S.Current==Target){Duplicate=true;break;}if(Duplicate)continue;
  auto& S=ToolSlots.AddDefaulted_GetRef();S.Current=Target;
  auto* A=Target.Get();if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=GetWorld()){bToolCaptureValid=false;continue;}
  S.InitialTransform=A->GetActorTransform();
  if(auto* Liquid=Cast<ALiquidContainerActor>(A)){S.InitialVolumeML=Liquid->FillAmount*Liquid->MaxVolumeML;S.Ingredient=Liquid->InitialIngredientId;if(!FMath::IsFinite(Liquid->FillAmount)||Liquid->FillAmount<0||Liquid->FillAmount>1||!FMath::IsFinite(Liquid->MaxVolumeML)||Liquid->MaxVolumeML<=0)bToolCaptureValid=false;}
 }
 Super::BeginPlay();Slots.SetNum(2);
 CaptureSlot(0,PrototypeCup.Get(),CupProfile.Get());CaptureSlot(1,PrototypeBottle.Get(),BottleProfile.Get());
 for(const auto& Target:RestoreTargets){bool Duplicate=false;for(const auto& S:Slots)if(S.bInitialized&&S.Current==Target){Duplicate=true;break;}if(!Duplicate&&IsValid(Target.Get())){const int32 I=Slots.AddDefaulted();CaptureSlot(I,Target.Get(),nullptr);}}
 if(IsValid(RestoreScoop.Get())&&RestoreScoop->GetWorld()==GetWorld()){CapturedScoop=RestoreScoop;ScoopSpawnTransform=RestoreScoop->GetActorTransform();}
 RestoreStatus=FText::FromString(TEXT("WAIT"));
 ApplyButtonMode();
 if(IsUnifiedRestoreMode()){
  StatusLabel=NewObject<UTextRenderComponent>(this,TEXT("RestoreStatusLabel"));StatusLabel->SetupAttachment(GetRootComponent());StatusLabel->SetRelativeLocation(FVector(-4,1.2,2));StatusLabel->SetRelativeRotation(FRotator(90,90,0));StatusLabel->SetWorldSize(.7f);StatusLabel->SetText(RestoreStatus);AddInstanceComponent(StatusLabel);StatusLabel->RegisterComponent();}
}
ALiquidContainerActor* AGlassRecoveryActor::GetCurrent(int32 I) const{if(!Slots.IsValidIndex(I))return nullptr;auto* A=Slots[I].Current.Get();return IsValid(A)&&!A->IsActorBeingDestroyed()?A:nullptr;}
bool AGlassRecoveryActor::CanRecover(int32 I) const{auto* A=GetCurrent(I);return I<2&&A&&Slots[I].Profile&&A->IsBroken()&&!A->IsLiquidFrozen();}
int32 AGlassRecoveryActor::GetRestoreToolCount() const{return ToolSlots.Num();}
AActor* AGlassRecoveryActor::GetCapturedTool(int32 I) const{if(!ToolSlots.IsValidIndex(I))return nullptr;auto* A=ToolSlots[I].Current.Get();return IsValid(A)&&!A->IsActorBeingDestroyed()?A:nullptr;}
FTransform AGlassRecoveryActor::GetToolInitialTransform(int32 I) const{return ToolSlots.IsValidIndex(I)?ToolSlots[I].InitialTransform:FTransform::Identity;}
bool AGlassRecoveryActor::IsToolLiquid(const ALiquidContainerActor* A) const
{
 if(!A)return false;for(const auto& S:ToolSlots)if(S.Current.Get()==A)return true;return false;
}
bool AGlassRecoveryActor::IsRestoredTool(const AActor* A) const
{
 if(!bRestoreAllSupplies||!IsValid(A)||A->IsActorBeingDestroyed())return false;
 if(const auto* Liquid=Cast<ALiquidContainerActor>(A))if(Liquid->IsLiquidFrozen())return false;
 for(const auto& S:ToolSlots)if(S.Current.Get()==A)return true;return false;
}
bool AGlassRecoveryActor::IsOwnedToolChild(const AActor* A) const
{
 if(!IsValid(A))return false;
 for(const auto& S:ToolSlots)if(const auto* Grinder=Cast<ACoffeeGrinderActor>(S.Current.Get()))if(IsRestoredTool(Grinder)&&(Grinder->HandleRef.Get()==A||Grinder->DrawerRef.Get()==A))return true;
 return false;
}
void AGlassRecoveryActor::StopToolPourAudio(ALiquidContainerActor* A)
{
 // 单次 Prepare 已经调用基类 StopPouring 清掉音频重启门；额外 Stop 去掉 FadeOut 尾音。
 if(!IsValid(A)||A->IsActorBeingDestroyed())return;
 if(auto* Audio=A->GetPourAudioComponent())Audio->Stop();
 if(!IsValid(A)||A->IsActorBeingDestroyed())return;
 if(auto* Kettle=Cast<AHandPourKettleActor>(A)){Kettle->CurrentFlowMLPerSecond=0;if(IsValid(Kettle->WaterJet.Get()))Kettle->WaterJet->ClearInstances();}
}
// @trace RS-02 只预检指定供给；其他手持物不阻断，也不释放。
bool AGlassRecoveryActor::PreflightRestore(TArray<ALiquidContainerActor*>& OldToRestore,TArray<ABottleCapActor*>& CapsToRestore)
{
 auto Fail=[this](const TCHAR* Text){RestoreStatus=FText::FromString(Text);return false;};
 UWorld* World=GetWorld();
 if(bRestoreInProgress||!World)return Fail(TEXT("WAIT"));
 if(bRestoreAllSupplies&&(!bToolCaptureComplete||!bToolCaptureValid))return Fail(TEXT("CHECK SUPPLIES - TOOL CAPTURE"));
 if(GetRestoreSlotCount()==0&&(!bRestoreAllSupplies||ToolSlots.IsEmpty()))return Fail(TEXT("WAIT"));
 auto* Transfer=World->GetSubsystem<ULiquidTransferSubsystem>();
 if(!Transfer||!Transfer->CanPrepareWorkbenchRestore())return Fail(TEXT("WAIT"));
 auto Busy=[](UGrabComponent* G){return IsValid(G)&&(G->IsHeld()||G->IsPulled());};
 if(bRestoreAllSupplies){
  TArray<AActor*> Listed;for(const auto& Target:RestoreToolTargets){auto* A=Target.Get();if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=World)return Fail(TEXT("CHECK SUPPLIES - MISSING TOOL"));Listed.AddUnique(A);}
  if(Listed.Num()!=ToolSlots.Num())return Fail(TEXT("CHECK SUPPLIES - TOOL LIST CHANGED"));
  for(const auto& S:ToolSlots){auto* A=S.Current.Get();
   if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=World||!Listed.Contains(A))return Fail(TEXT("CHECK SUPPLIES - MISSING CAPTURED TOOL"));
   auto* Liquid=Cast<ALiquidContainerActor>(A);auto* Grinder=Cast<ACoffeeGrinderActor>(A);
   if(!Cast<AHandPourKettleActor>(A)&&!Cast<AHandDripperActor>(A)&&!Grinder)return Fail(TEXT("CHECK SUPPLIES - UNSUPPORTED TOOL"));
   for(const auto& Slot:Slots)if(Slot.bInitialized&&Slot.Current.Get()==A)return Fail(TEXT("CHECK SUPPLIES - TOOL IN REPLACEMENT LIST"));
   if(S.InitialTransform.ContainsNaN()||!S.InitialTransform.GetRotation().IsNormalized()||S.InitialTransform.GetScale3D().GetMin()<=KINDA_SMALL_NUMBER)return Fail(TEXT("CHECK TOOL SETUP"));
   if(Liquid){
    if(Liquid->IsBroken())return Fail(TEXT("CHECK TOOL SETUP - BROKEN TOOL"));
    if(!IsValid(Liquid->ContainerMesh.Get())||Liquid->ContainerMesh->GetOwner()!=A||Liquid->GetRootComponent()!=Liquid->ContainerMesh.Get()||!IsValid(Liquid->ContainerMesh->GetStaticMesh())||!IsValid(Liquid->GrabComp.Get())||Liquid->GrabComp->GetOwner()!=A||!FMath::IsFinite(S.InitialVolumeML)||S.InitialVolumeML<0||!FMath::IsFinite(Liquid->MaxVolumeML)||Liquid->MaxVolumeML<=0||S.InitialVolumeML>Liquid->MaxVolumeML||(S.InitialVolumeML>0&&S.Ingredient.IsNone()))return Fail(TEXT("CHECK TOOL SETUP"));
    if(const auto* Kettle=Cast<AHandPourKettleActor>(A))if(!FMath::IsFinite(Kettle->WaterStreamColor.R)||!FMath::IsFinite(Kettle->WaterStreamColor.G)||!FMath::IsFinite(Kettle->WaterStreamColor.B)||!FMath::IsFinite(Kettle->WaterStreamColor.A))return Fail(TEXT("CHECK TOOL SETUP - WATER COLOR"));
    if(Cast<AHandDripperActor>(A)&&S.InitialVolumeML>1.e-6f)return Fail(TEXT("CHECK TOOL SETUP - DRIPPER MUST START EMPTY"));
    if(Liquid->IsLiquidFrozen())continue;
    if(Busy(Liquid->GrabComp.Get()))return Fail(TEXT("PUT DOWN SUPPLIES"));
    OldToRestore.AddUnique(Liquid);
   }else{
    if(Busy(Grinder->GrabComp.Get())||(IsValid(Grinder->HandleRef.Get())&&Busy(Grinder->HandleRef->GrabComp.Get()))||(IsValid(Grinder->DrawerRef.Get())&&Busy(Grinder->DrawerRef->GrabComp.Get())))return Fail(TEXT("PUT DOWN SUPPLIES"));
    if(!Grinder->CanResetForWorkbench()||!IsValid(Grinder->BodyMesh->GetStaticMesh()))return Fail(TEXT("CHECK GRINDER SETUP"));
   }
  }
 }
 for(const auto& S:Slots){if(!S.bInitialized)continue;
  if(!S.ContainerClass||S.ContainerClass->HasAnyClassFlags(CLASS_Abstract)||!IsValid(S.Mesh.Get())||S.SpawnTransform.ContainsNaN()||S.SpawnTransform.GetScale3D().GetAbs().GetMin()<=KINDA_SMALL_NUMBER||!FMath::IsFinite(S.InitialFill)||!FMath::IsFinite(S.Capacity))return Fail(TEXT("CHECK SUPPLY SETUP"));
  if(S.bStartCapped&&(!IsValid(S.CapClass.Get())||S.CapClass->HasAnyClassFlags(CLASS_Abstract)))return Fail(TEXT("CHECK SUPPLY SETUP"));
  auto* A=S.Current.Get();
  if(IsValid(A)&&!A->IsActorBeingDestroyed()){if(A->GetWorld()!=World)return Fail(TEXT("CHECK SUPPLY SETUP"));if(A->IsLiquidFrozen())continue;if(Busy(A->GrabComp.Get()))return Fail(TEXT("PUT DOWN SUPPLIES"));OldToRestore.AddUnique(A);}
  if(auto* Cap=S.CapturedCap.Get())CapsToRestore.AddUnique(Cap);
  if(IsValid(A)&&!A->IsActorBeingDestroyed()){
   if(auto* B=Cast<ABottleActor>(A))if(IsValid(B->CapActor.Get()))CapsToRestore.AddUnique(B->CapActor.Get());
   if(auto* Break=A->FindComponentByClass<UGlassBreakableComponent>())if(auto* Cap=Break->RelatedCap.Get())CapsToRestore.AddUnique(Cap);
  }
 }
 for(TActorIterator<ABottleCapActor> It(World);It;++It)if(!IsToolLiquid(It->OwnerBottle.Get())&&OldToRestore.Contains(It->OwnerBottle.Get()))CapsToRestore.AddUnique(*It);
 for(auto* Cap:CapsToRestore)if(!IsValid(Cap)||Cap->IsActorBeingDestroyed()||Cap->GetWorld()!=World)return Fail(TEXT("CHECK SUPPLY SETUP"));else if(Busy(Cap->GrabComp.Get()))return Fail(TEXT("PUT DOWN SUPPLIES"));
 if(RestoreScoop.Get()){
  if(!IsValid(RestoreScoop.Get())||RestoreScoop->IsActorBeingDestroyed()||RestoreScoop->GetWorld()!=World||CapturedScoop.Get()!=RestoreScoop.Get()||!IsValid(RestoreScoop->Mesh.Get())||ScoopSpawnTransform.ContainsNaN()||ScoopSpawnTransform.GetScale3D().GetAbs().GetMin()<=KINDA_SMALL_NUMBER)return Fail(TEXT("CHECK SCOOP SETUP"));
  if(Busy(RestoreScoop->Grab.Get()))return Fail(TEXT("PUT DOWN SUPPLIES"));
 }
 auto CheckPosition=[&](const FVector& Center){
  if(Center.ContainsNaN())return Fail(TEXT("CHECK SUPPLY SETUP"));
  // 小球只检查供给中心，不把起始贴桌接触当成占位；冻结杯可能已关闭查询碰撞。
  for(TActorIterator<ALiquidContainerActor> It(World);It;++It){auto* A=*It;if(OldToRestore.Contains(A)||!IsValid(A->ContainerMesh.Get())||!IsValid(A->ContainerMesh->GetStaticMesh()))continue;const FBox Bounds=A->ContainerMesh->GetStaticMesh()->GetBoundingBox().TransformBy(A->ContainerMesh->GetComponentTransform());if(Bounds.ExpandBy(1.f).IsInsideOrOn(Center))return Fail(TEXT("SUPPLY POSITION OCCUPIED"));}
  FCollisionQueryParams Query(SCENE_QUERY_STAT(WorkbenchSupply),false);Query.AddIgnoredActor(this);for(auto* A:OldToRestore)Query.AddIgnoredActor(A);for(auto* C:CapsToRestore)Query.AddIgnoredActor(C);if(IsValid(RestoreScoop.Get()))Query.AddIgnoredActor(RestoreScoop.Get());
  if(bRestoreAllSupplies)for(const auto& Tool:ToolSlots){auto* A=Tool.Current.Get();if(!IsRestoredTool(A))continue;Query.AddIgnoredActor(A);if(auto* Grinder=Cast<ACoffeeGrinderActor>(A)){if(IsValid(Grinder->HandleRef.Get()))Query.AddIgnoredActor(Grinder->HandleRef.Get());if(IsValid(Grinder->DrawerRef.Get()))Query.AddIgnoredActor(Grinder->DrawerRef.Get());}}
  TArray<FOverlapResult> Hits;World->OverlapMultiByObjectType(Hits,Center,FQuat::Identity,FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeSphere(1.f),Query);
  for(const auto& Hit:Hits){auto* A=Hit.GetActor();auto* C=Hit.GetComponent();if(!IsValid(A)||Cast<APawn>(A)||IsRestoredTool(A)||IsOwnedToolChild(A))continue;
   if(auto* Debris=Cast<AGlassDebrisActor>(A)){bool Owned=false;for(const auto& Slot:Slots)if(Slot.bInitialized&&Slot.Current.Get()&&Debris->SourceContainer==TWeakObjectPtr<ALiquidContainerActor>(Slot.Current.Get())){auto* Current=Slot.Current.Get();if(!IsValid(Current)||!Current->IsLiquidFrozen())Owned=true;}if(Owned)continue;}
   if(auto* Ice=Cast<AIceCubeActor>(A)){
    const bool Protected=IsValid(Ice->CupIce.Get())&&Ice->CupIce->GetCup()&&Ice->CupIce->GetCup()->IsLiquidFrozen();
    if(!Protected&&((IsValid(Ice->CupIce.Get())&&!IsToolLiquid(Ice->CupIce->GetCup())&&OldToRestore.Contains(Ice->CupIce->GetCup()))||(!IsToolLiquid(Ice->LastCupSource.Get())&&OldToRestore.Contains(Ice->LastCupSource.Get()))||(RestoreScoop&&Ice->LastScoopSource.Get()==RestoreScoop.Get())))continue;
   }
   if(IsValid(C)&&C->Mobility==EComponentMobility::Static&&!Cast<ALiquidContainerActor>(A)&&!Cast<ABottleCapActor>(A)&&!A->FindComponentByClass<UGrabComponent>())continue;
   return Fail(TEXT("SUPPLY POSITION OCCUPIED"));
  }
  return true;
 };
 for(const auto& S:Slots){if(!S.bInitialized)continue;auto* A=S.Current.Get();if(IsValid(A)&&A->IsLiquidFrozen())continue;
  if(!CheckPosition((S.MeshRelativeTransform*S.SpawnTransform).TransformPosition(S.Mesh->GetBoundingBox().GetCenter())))return false;
 }
 if(bRestoreAllSupplies)for(const auto& S:ToolSlots){auto* A=S.Current.Get();if(!IsRestoredTool(A))continue;
  auto* Liquid=Cast<ALiquidContainerActor>(A);auto* Grinder=Cast<ACoffeeGrinderActor>(A);auto* Mesh=Liquid?Liquid->ContainerMesh.Get():Grinder->BodyMesh.Get();
  const FTransform InitialMeshTransform=Mesh->GetComponentTransform().GetRelativeTransform(A->GetActorTransform())*S.InitialTransform;
  if(!CheckPosition(InitialMeshTransform.TransformPosition(Mesh->GetStaticMesh()->GetBoundingBox().GetCenter())))return false;
 }
 return true;
}
int32 AGlassRecoveryActor::GetRestoreSlotCount() const{int32 Count=0;for(const auto& S:Slots)if(S.bInitialized)++Count;return Count;}
float AGlassRecoveryActor::GetRestoreProgress() const{return FMath::Clamp(RestoreHoldElapsed/FMath::Max(.01f,FMath::IsFinite(HoldSeconds)?HoldSeconds:1.f),0.f,1.f);}
// @trace RS-04 成功或失败均锁存到松开，禁止持续接触反复创建供给。
bool AGlassRecoveryActor::AdvanceRestoreHold(float Dt,bool Touching)
{
 if(!Touching){RestoreHoldElapsed=0;bRestoreHoldLatched=false;if(!bRestoreInProgress)RestoreStatus=FText::FromString(TEXT("WAIT"));return false;}
 if(bRestoreHoldLatched||bRestoreInProgress||!FMath::IsFinite(Dt)||Dt<=0)return false;
 const float Threshold=FMath::Max(.01f,FMath::IsFinite(HoldSeconds)?HoldSeconds:1.f);
 RestoreHoldElapsed=FMath::Min(Threshold,RestoreHoldElapsed+FMath::Min(Dt,Threshold));
 RestoreStatus=FText::FromString(FString::Printf(TEXT("HOLD %.1fS"),Threshold-RestoreHoldElapsed));
 if(RestoreHoldElapsed<Threshold)return false;
 bRestoreHoldLatched=true;return RestoreWorkbench();
}
// @trace RS-01 RS-02 RS-03 新供给全部初始化和校验后才发布、销毁旧罐；工具、盖、冰清理及任意回调不承诺强事务回滚。
bool AGlassRecoveryActor::RestoreWorkbench()
{
 if(bRestoreInProgress||!GetWorld()){RestoreStatus=FText::FromString(TEXT("WAIT"));return false;}
 // 每次事务入口读取真实运行时历史；不保存 BeginPlay 快照，也不凭初始供给补造已丢失的账本。
 TArray<FBeanJarLedgerSnapshot> BeanSnapshotsPerSlot;BeanSnapshotsPerSlot.SetNum(Slots.Num());
 TArray<bool> HasValidBeanSnapshot;HasValidBeanSnapshot.Init(false,Slots.Num());
 auto FailBeanSetup=[this](const TCHAR* Text){RestoreStatus=FText::FromString(Text);return false;};
 for(int32 I=0;I<Slots.Num();++I){const auto& S=Slots[I];if(!S.bInitialized||!S.bBeanJar)continue;
  auto* Jar=Cast<ADirectBeanJarActor>(S.Current.Get());
  if(!IsValid(Jar)||Jar->IsActorBeingDestroyed()||Jar->GetWorld()!=GetWorld())return FailBeanSetup(TEXT("CHECK SUPPLY SETUP - MISSING BEAN LEDGER"));
  if(!FMath::IsFinite(S.BeanSupplyG)||S.BeanSupplyG<0)return FailBeanSetup(TEXT("CHECK SUPPLY SETUP - BEAN SUPPLY"));
  const auto Snapshot=Jar->GetBeanLedger();
  for(double Value:{Snapshot.InitialGrams,Snapshot.InjectedGrams,Snapshot.AcceptedGrams,Snapshot.SpilledGrams,Snapshot.DiscardedGrams,Snapshot.CurrentGrams,Snapshot.TotalPouredGrams})
   if(!FMath::IsFinite(Value)||Value<0)return FailBeanSetup(TEXT("CHECK SUPPLY SETUP - INVALID BEAN LEDGER"));
  const double Input=Snapshot.InitialGrams+Snapshot.InjectedGrams;
  const double Output=Snapshot.CurrentGrams+Snapshot.AcceptedGrams+Snapshot.SpilledGrams+Snapshot.DiscardedGrams;
  if(!FMath::IsFinite(Input)||!FMath::IsFinite(Output)||FMath::Abs(Input-Output)>1.e-5)return FailBeanSetup(TEXT("CHECK SUPPLY SETUP - INVALID BEAN LEDGER"));
  BeanSnapshotsPerSlot[I]=Snapshot;HasValidBeanSnapshot[I]=true;
 }
 TArray<ALiquidContainerActor*> OldToRestore;TArray<ABottleCapActor*> CapsToRestore;
 if(!PreflightRestore(OldToRestore,CapsToRestore))return false;
 TStrongObjectPtr<AGlassRecoveryActor> Self(this);bRestoreInProgress=true;
 // 在任何外部回调前固定旧身份和冻结状态，保护槽也必须跟踪，不能在后续阶段动态跳槽。
 TArray<TStrongObjectPtr<ALiquidContainerActor>> OldSlotGuards;OldSlotGuards.SetNum(Slots.Num());
 TArray<bool> OldSlotFrozen;OldSlotFrozen.Init(false,Slots.Num());
 bool bHasProtected=false;
 for(int32 I=0;I<Slots.Num();++I){auto* A=Slots[I].Current.Get();if(!Slots[I].bInitialized||!IsValid(A)||A->IsActorBeingDestroyed())continue;OldSlotGuards[I].Reset(A);OldSlotFrozen[I]=A->IsLiquidFrozen();bHasProtected|=OldSlotFrozen[I];}
 struct FToolPermissions {bool CanPour=false,AcceptLiquid=false,Frozen=false;};
 struct FGrinderIdentity {TStrongObjectPtr<ACoffeeGrinderActor> Grinder;TStrongObjectPtr<AGrinderHandleActor> Handle;TStrongObjectPtr<AGrinderDrawerActor> Drawer;};
 TArray<FToolPermissions> ToolPermissions;ToolPermissions.SetNum(ToolSlots.Num());
 TArray<TStrongObjectPtr<AActor>> ToolGuards;
 TArray<FGrinderIdentity> GrinderGuards;
 if(bRestoreAllSupplies)for(int32 I=0;I<ToolSlots.Num();++I){auto* A=ToolSlots[I].Current.Get();ToolGuards.Emplace(A);
  if(auto* Liquid=Cast<ALiquidContainerActor>(A)){ToolPermissions[I].CanPour=Liquid->bCanPour;ToolPermissions[I].AcceptLiquid=Liquid->bAcceptLiquidFromOthers;ToolPermissions[I].Frozen=Liquid->IsLiquidFrozen();bHasProtected|=ToolPermissions[I].Frozen;}
  if(auto* Grinder=Cast<ACoffeeGrinderActor>(A)){auto& G=GrinderGuards.AddDefaulted_GetRef();G.Grinder.Reset(Grinder);auto* Handle=Grinder->HandleRef.Get();auto* Drawer=Grinder->DrawerRef.Get();if(IsValid(Handle))G.Handle.Reset(Handle);if(IsValid(Drawer))G.Drawer.Reset(Drawer);}
 }
 // 发量门覆盖 Prepare、补液、FinishSpawning 和失败清理回调；强引用不阻止 Destroy。
 struct FEmissionGuard {
  TStrongObjectPtr<ALiquidContainerActor> Actor;bool OldFlag;
  explicit FEmissionGuard(ALiquidContainerActor* A):Actor(A),OldFlag(A->bWorkbenchRestoreEmissionBlocked){A->bWorkbenchRestoreEmissionBlocked=true;}
 };
 TArray<FEmissionGuard> EmissionGuards;for(auto* A:OldToRestore)EmissionGuards.Emplace(A);
 TArray<TStrongObjectPtr<ABottleCapActor>> CapGuards;for(auto* Cap:CapsToRestore)CapGuards.Emplace(Cap);
 TArray<TStrongObjectPtr<ALiquidContainerActor>> Staged;Staged.SetNum(Slots.Num());
 TArray<bool> StagedCommitted;StagedCommitted.Init(false,Slots.Num());
 auto Alive=[&](){
  if(!IsValid(Self.Get())||Self->IsActorBeingDestroyed())return false;
  auto Fail=[&Self](const TCHAR* Text){Self->RestoreStatus=FText::FromString(Text);UE_LOG(LogTemp,Warning,TEXT("Workbench restore aborted: %s"),Text);return false;};
  for(int32 I=0;I<OldSlotGuards.Num();++I){auto* A=OldSlotGuards[I].Get();if(IsValid(A)&&!A->IsActorBeingDestroyed()&&A->IsLiquidFrozen()!=OldSlotFrozen[I])return Fail(TEXT("SUPPLY RESET FAILED - STATE CHANGED"));}
  if(Self->bRestoreAllSupplies)for(int32 I=0;I<ToolGuards.Num();++I){auto* A=ToolGuards[I].Get();if(!IsValid(A)||A->IsActorBeingDestroyed()||Self->ToolSlots[I].Current.Get()!=A)return Fail(TEXT("TOOL DESTROYED - RESTORE FAILED"));if(auto* Liquid=Cast<ALiquidContainerActor>(A))if(Liquid->IsLiquidFrozen()!=ToolPermissions[I].Frozen)return Fail(TEXT("TOOL RESET FAILED - STATE CHANGED"));}
  for(const auto& G:GrinderGuards){auto* Grinder=G.Grinder.Get();
   if(!IsValid(Grinder)||Grinder->IsActorBeingDestroyed())return Fail(TEXT("TOOL DESTROYED - RESTORE FAILED"));
   auto* Handle=G.Handle.Get();auto* Drawer=G.Drawer.Get();
   if(Handle&&(!IsValid(Handle)||Handle->IsActorBeingDestroyed()||Grinder->HandleRef.Get()!=Handle||Handle->OwnerGrinder.Get()!=Grinder))return Fail(TEXT("GRINDER RESET FAILED - STATE CHANGED"));
   if(Drawer&&(!IsValid(Drawer)||Drawer->IsActorBeingDestroyed()||Grinder->DrawerRef.Get()!=Drawer||Drawer->OwnerGrinder.Get()!=Grinder))return Fail(TEXT("GRINDER RESET FAILED - STATE CHANGED"));
  }
  for(const auto& Item:Staged){auto* A=Item.Get();if(A&&(!IsValid(A)||A->IsActorBeingDestroyed()))return Fail(TEXT("SUPPLY SPAWN FAILED"));}
  return true;
 };
 ON_SCOPE_EXIT{
  for(int32 I=0;I<Staged.Num();++I){if(StagedCommitted[I])continue;auto* A=Staged[I].Get();if(!IsValid(A)||A->IsActorBeingDestroyed())continue;if(auto* B=Cast<ABottleActor>(A)){TStrongObjectPtr<ABottleCapActor> Cap(B->CapActor.Get());if(IsValid(Cap.Get())&&!Cap->IsActorBeingDestroyed()){Cap->InvalidateBrokenBottle();if(IsValid(Cap.Get())&&!Cap->IsActorBeingDestroyed())Cap->Destroy();}}if(IsValid(A)&&!A->IsActorBeingDestroyed())A->Destroy();}
  for(auto& Guard:EmissionGuards)if(auto* A=Guard.Actor.Get())A->bWorkbenchRestoreEmissionBlocked=Guard.OldFlag;
  if(IsValid(Self.Get())&&!Self->IsActorBeingDestroyed())Self->bRestoreInProgress=false;
 };
 UWorld* World=GetWorld();
 if(!Alive())return false;
 for(int32 I=0;I<Slots.Num();++I){const auto& S=Slots[I];if(!S.bInitialized||OldSlotFrozen[I])continue;
  auto* A=World->SpawnActorDeferred<ALiquidContainerActor>(S.ContainerClass,S.SpawnTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
  Staged[I].Reset(A);if(IsValid(A)&&!A->IsActorBeingDestroyed())EmissionGuards.Emplace(A);
  if(!Alive())return false;
  if(!IsValid(A)||A->IsActorBeingDestroyed()||!IsValid(A->ContainerMesh.Get())){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->bInitialVolumeIsReplacement=true;A->FillAmount=0;A->ContainerMesh->SetSimulatePhysics(false);A->SetActorEnableCollision(false);A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  if(!Alive())return false;
 }
 // 在停源回调和释放旧杯内冰之前收集身份；不删除已散落、无来源字段的冰。
 TArray<TWeakObjectPtr<AIceCubeActor>> OwnedIce;
 for(TActorIterator<AIceCubeActor> It(World);It;++It){
  const bool Protected=IsValid(It->CupIce.Get())&&It->CupIce->GetCup()&&It->CupIce->GetCup()->IsLiquidFrozen();
  if(Protected)continue;
  bool Owned=It->State==EIceState::InCup&&IsValid(It->CupIce.Get())&&!IsToolLiquid(It->CupIce->GetCup())&&OldToRestore.Contains(It->CupIce->GetCup());
  Owned|=RestoreScoop&&It->LastScoopSource.Get()==RestoreScoop.Get();
  for(const auto& S:Slots)if(S.bInitialized&&IsValid(S.Current.Get())&&!S.Current->IsLiquidFrozen()&&Cast<ACupActor>(S.Current.Get())&&It->LastCupSource==TWeakObjectPtr<ACupActor>(Cast<ACupActor>(S.Current.Get())))Owned=true;
  if(Owned)OwnedIce.Add(*It);
 }
 TArray<TWeakObjectPtr<AGlassDebrisActor>> OwnedDebris;
 for(TActorIterator<AGlassDebrisActor> It(World);It;++It)for(const auto& S:Slots)if(S.bInitialized&&S.Current.Get()&&It->SourceContainer==TWeakObjectPtr<ALiquidContainerActor>(S.Current.Get())){auto* A=S.Current.Get();if(!IsValid(A)||!A->IsLiquidFrozen())OwnedDebris.AddUnique(*It);}
 auto* Transfer=World->GetSubsystem<ULiquidTransferSubsystem>();
 if(!Transfer||!Transfer->PrepareWorkbenchRestore(OldToRestore)){if(Alive())RestoreStatus=FText::FromString(TEXT("WAIT"));return false;}
 const float ReceiveDelay=static_cast<float>(Transfer->GetUnsettledFlightHorizon());
 if(!Alive())return false;
 auto FailTool=[this](const TCHAR* Text){RestoreStatus=FText::FromString(Text);UE_LOG(LogTemp,Warning,TEXT("工作台工具恢复失败：%s"),Text);return false;};
 // Prepare 只清源/接收资格与源在途包；原地工具的库存须显式计入 Removed。
 if(bRestoreAllSupplies)for(const auto& S:ToolSlots){auto* A=Cast<ALiquidContainerActor>(S.Current.Get());if(!A||!OldToRestore.Contains(A))continue;
  if(A->IsLiquidFrozen()||A->IsBroken())return FailTool(TEXT("TOOL RESET FAILED - STATE CHANGED"));
  StopToolPourAudio(A);if(!Alive())return false;
  A->ConsumeLiquid(TNumericLimits<float>::Max());if(!Alive())return false;
  if(A->IsLiquidFrozen()||A->IsBroken()||A->GetPreciseVolumeML()>1.e-6)return FailTool(TEXT("TOOL EMPTY FAILED"));
  A->RestoreReceiveCooldown=ReceiveDelay;
 }
 for(TActorIterator<AServingSpotActor> It(World);It;++It){It->ForgetRestoredContainers(OldToRestore);if(!Alive())return false;}
 for(auto* Cap:CapsToRestore){if(!IsValid(Cap)||Cap->IsActorBeingDestroyed())continue;Cap->InvalidateBrokenBottle();if(!Alive())return false;if(IsValid(Cap)&&!Cap->IsActorBeingDestroyed())Cap->Destroy();if(!Alive())return false;}
 for(auto* A:OldToRestore){if(IsToolLiquid(A)||!IsValid(A)||A->IsActorBeingDestroyed()||A->IsLiquidFrozen())continue;if(IsValid(A->GrabComp.Get()))A->GrabComp->ReleaseForBreak();if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()||A->IsLiquidFrozen())continue;if(auto* Ice=A->FindComponentByClass<UCupIceComponent>())Ice->ReleaseAll();if(!Alive())return false;}
 for(const auto& Item:OwnedIce){if(auto* Ice=Item.Get()){if(Ice->State==EIceState::InCup&&IsValid(Ice->CupIce.Get())&&!OldToRestore.Contains(Ice->CupIce->GetCup()))continue;Ice->Destroy();}if(!Alive())return false;}
 for(const auto& Item:OwnedDebris){if(auto* Debris=Item.Get())Debris->Destroy();if(!Alive())return false;}
 if(!Alive())return false;
 if(IsValid(RestoreScoop.Get())&&!RestoreScoop->ResetForWorkbench(ScoopSpawnTransform)){if(Alive())RestoreStatus=FText::FromString(TEXT("SCOOP RESET FAILED"));return false;}
 if(!Alive())return false;
 if(bRestoreAllSupplies)for(int32 I=0;I<ToolSlots.Num();++I){const auto& S=ToolSlots[I];auto* Tool=S.Current.Get();if(!Alive())return false;
  if(auto* A=Cast<ALiquidContainerActor>(Tool)){
   if(!OldToRestore.Contains(A))continue;
   if(A->IsLiquidFrozen()||A->IsBroken()||!IsValid(A->ContainerMesh.Get())||!IsValid(A->GrabComp.Get())||A->GrabComp->IsHeld()||A->GrabComp->IsPulled())return FailTool(TEXT("TOOL RESET FAILED - STATE CHANGED"));
   const bool Simulating=A->ContainerMesh->IsSimulatingPhysics();
   A->SetActorTransform(S.InitialTransform,false,nullptr,ETeleportType::TeleportPhysics);if(!Alive())return false;
   if(!IsValid(A->ContainerMesh.Get()))return FailTool(TEXT("TOOL RESET FAILED - MISSING MESH"));
   if(Simulating){A->ContainerMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);A->ContainerMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);}
   if(!Alive())return false;
   if(auto* Dripper=Cast<AHandDripperActor>(A)){const bool Reset=Dripper->ResetBrewForWorkbench();if(!Alive())return false;if(!Reset)return FailTool(TEXT("DRIPPER RESET FAILED"));}
   else if(auto* Kettle=Cast<AHandPourKettleActor>(A))if(S.InitialVolumeML>0){const float Injected=A->AddIngredientLiquid(S.InitialVolumeML,S.Ingredient,Kettle->WaterStreamColor);if(!Alive())return false;if(!FMath::IsNearlyEqual(Injected,S.InitialVolumeML,1.e-4f))return FailTool(TEXT("KETTLE REFILL FAILED"));}
   if(!Alive())return false;
   if(A->IsLiquidFrozen()||A->IsBroken())return FailTool(TEXT("TOOL RESET FAILED - STATE CHANGED"));
   if(!FMath::IsFinite(A->GetPreciseVolumeML())||FMath::Abs(A->GetPreciseVolumeML()-S.InitialVolumeML)>1.e-4)return FailTool(TEXT("TOOL REFILL FAILED - INVENTORY CHANGED"));
   A->RestoreReceiveCooldown=ReceiveDelay;A->bCanPour=ToolPermissions[I].CanPour;A->bAcceptLiquidFromOthers=ToolPermissions[I].AcceptLiquid;
  }else if(auto* Grinder=Cast<ACoffeeGrinderActor>(Tool)){const bool Reset=Grinder->ResetForWorkbench(S.InitialTransform);if(!Alive())return false;if(!Reset)return FailTool(TEXT("GRINDER RESET FAILED"));}
  else return FailTool(TEXT("CHECK SUPPLIES - UNSUPPORTED TOOL"));
 }
 // 初始化阶段仍保留旧槽引用和旧罐账本；失败清理只处理未发布的新供给。
 for(int32 I=0;I<Slots.Num();++I){const auto& S=Slots[I];if(!S.bInitialized||OldSlotFrozen[I])continue;auto* A=Staged[I].Get();if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->FillAmount=(S.bBeanJar||Cast<ACupActor>(A))?0:S.InitialFill;A->MaxVolumeML=S.Capacity;A->InitialIngredientId=S.Ingredient;A->LiquidColor=S.Color;A->bCanPour=S.bCanPour;A->bAcceptLiquidFromOthers=S.bAcceptLiquid;
  A->PourEntryRadius=S.PourEntryRadius;
  if(A->PourEntryPoint){A->PourEntryPoint->SetAbsolute(S.bEntryAbsoluteLocation,S.bEntryAbsoluteRotation,S.bEntryAbsoluteScale);A->PourEntryPoint->SetRelativeTransform(S.PourEntryRelativeTransform);}
  A->ContainerMesh->SetStaticMesh(S.Mesh);if(A->ContainerMesh->GetAttachParent())A->ContainerMesh->SetRelativeTransform(S.MeshRelativeTransform);for(int32 M=0;M<S.Materials.Num();++M)A->ContainerMesh->SetMaterial(M,S.Materials[M]);
  if(auto* B=Cast<ABottleActor>(A)){B->CapClass=S.CapClass;B->CapSocketName=S.CapSocket;B->bStartCapped=S.bStartCapped;}
  A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);A->ContainerMesh->SetSimulatePhysics(true);A->SetActorEnableCollision(true);
  if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->RestoreReceiveCooldown=ReceiveDelay;
  A->FinishSpawning(S.SpawnTransform);if(!Alive())return false;
  if(S.bBeanJar){auto* Jar=Cast<ADirectBeanJarActor>(A);
   // BeginPlay 默认初始账本只被覆盖一次；旧 Current 在新历史中仅转为 Discarded。
   if(!Jar||!HasValidBeanSnapshot[I]||!Jar->ApplyRestoredBeanLedger(BeanSnapshotsPerSlot[I],S.BeanSupplyG)){RestoreStatus=FText::FromString(TEXT("BEAN LEDGER RESTORE FAILED"));return false;}
   if(!Alive())return false;
   const double Expected=FMath::IsFinite(Jar->BeanCapacityGrams)?FMath::Clamp(static_cast<double>(S.BeanSupplyG),0.0,FMath::Max(0.0,static_cast<double>(Jar->BeanCapacityGrams))):0.0;
   if(!FMath::IsFinite(Jar->GetBeanGrams())||FMath::Abs(Jar->GetBeanGrams()-Expected)>1.e-5||!FMath::IsFinite(Jar->GetBeanBalanceError())||FMath::Abs(Jar->GetBeanBalanceError())>1.e-5||Jar->GetPreciseVolumeML()!=0||Jar->FillAmount!=0){RestoreStatus=FText::FromString(TEXT("BEAN LEDGER RESTORE FAILED - INVENTORY CHANGED"));return false;}
  }
  if(A->GrabComp)A->GrabComp->Activate();if(!Alive())return false;
  // 构造脚本可能重写视觉配置；初始化后的权威体积不重复灌注。
  A->PourEntryRadius=S.PourEntryRadius;
  if(A->PourEntryPoint){A->PourEntryPoint->SetAbsolute(S.bEntryAbsoluteLocation,S.bEntryAbsoluteRotation,S.bEntryAbsoluteScale);A->PourEntryPoint->SetRelativeTransform(S.PourEntryRelativeTransform);}
  A->ContainerMesh->SetStaticMesh(S.Mesh);for(int32 M=0;M<S.Materials.Num();++M)A->ContainerMesh->SetMaterial(M,S.Materials[M]);A->LiquidColor=S.Color;A->RefreshLiquidFX();if(!Alive())return false;
  Install(A,S.Profile);if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->SetActorTransform(S.SpawnTransform,false,nullptr,ETeleportType::TeleportPhysics);A->ContainerMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);A->ContainerMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
  if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  if(S.bHasCupIce&&Cast<ACupActor>(A)){auto* Ice=A->FindComponentByClass<UCupIceComponent>();if(!Ice){Ice=NewObject<UCupIceComponent>(A);A->AddInstanceComponent(Ice);}Ice->InteriorRadiusCM=S.IceRadiusCM;Ice->InteriorDepthCM=S.IceDepthCM;Ice->MaxCupIce=S.MaxCupIce;Ice->ClinkSound=S.ClinkSound;if(!Ice->IsRegistered())Ice->RegisterComponent();}
  if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
 }
 if(!Alive())return false;
 // 补液通知可能改变源姿态；全部回调后再固定工具姿态，不留下倾斜后立即出流的供给。
 if(bRestoreAllSupplies)for(int32 I=0;I<ToolGuards.Num();++I){auto* Tool=ToolGuards[I].Get();if(!IsRestoredTool(Tool))continue;
  Tool->SetActorTransform(ToolSlots[I].InitialTransform,false,nullptr,ETeleportType::TeleportPhysics);if(!Alive())return false;
  auto* Mesh=Cast<UPrimitiveComponent>(Tool->GetRootComponent());if(IsValid(Mesh)&&Mesh->IsSimulatingPhysics()){Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);}
  if(auto* A=Cast<ALiquidContainerActor>(Tool)){A->StopFlowForWorkbenchRestore();StopToolPourAudio(A);}if(!Alive())return false;
 }
 // 所有构造/补液回调结束后再覆盖一次，包含无关源在回调中新发出的延迟包。
 const float FinalReceiveDelay=static_cast<float>(Transfer->GetUnsettledFlightHorizon());
 for(const auto& Item:Staged)if(auto* A=Item.Get())A->RestoreReceiveCooldown=FMath::Max(A->RestoreReceiveCooldown,FinalReceiveDelay);
 if(bRestoreAllSupplies)for(int32 I=0;I<ToolGuards.Num();++I){auto* A=Cast<ALiquidContainerActor>(ToolGuards[I].Get());if(!A||!OldToRestore.Contains(A))continue;
  if(A->IsBroken())return FailTool(TEXT("TOOL RESET FAILED - STATE CHANGED"));
  const double ActualVolume=A->GetPreciseVolumeML();
  if(!FMath::IsFinite(ActualVolume)||FMath::Abs(ActualVolume-ToolSlots[I].InitialVolumeML)>1.e-4)return FailTool(TEXT("TOOL REFILL FAILED - INVENTORY CHANGED"));
  A->RestoreReceiveCooldown=FMath::Max(A->RestoreReceiveCooldown,FinalReceiveDelay);
 }
 if(!Alive())return false;
 // 最后一个槽或工具回调也可能改变前面已初始化的供给；发布前重读全部真实库存和盖归属。
 for(int32 I=0;I<Slots.Num();++I){const auto& S=Slots[I];if(!S.bInitialized||OldSlotFrozen[I])continue;auto* A=Staged[I].Get();
  if(!IsValid(A)||A->IsActorBeingDestroyed()||A->IsBroken()||A->IsLiquidFrozen()||!IsValid(A->ContainerMesh.Get())){RestoreStatus=FText::FromString(TEXT("SUPPLY RESET FAILED - STATE CHANGED"));return false;}
  if(S.bBeanJar){auto* Jar=Cast<ADirectBeanJarActor>(A);
   if(!Jar){RestoreStatus=FText::FromString(TEXT("BEAN LEDGER RESTORE FAILED"));return false;}
   const double Expected=FMath::IsFinite(Jar->BeanCapacityGrams)?FMath::Clamp(static_cast<double>(S.BeanSupplyG),0.0,FMath::Max(0.0,static_cast<double>(Jar->BeanCapacityGrams))):0.0;
   if(!FMath::IsFinite(Jar->GetBeanGrams())||FMath::Abs(Jar->GetBeanGrams()-Expected)>1.e-5||!FMath::IsFinite(Jar->GetBeanBalanceError())||FMath::Abs(Jar->GetBeanBalanceError())>1.e-5||Jar->GetPreciseVolumeML()!=0||Jar->FillAmount!=0){RestoreStatus=FText::FromString(TEXT("BEAN LEDGER RESTORE FAILED - INVENTORY CHANGED"));return false;}
  }else{
   const double Expected=Cast<ACupActor>(A)?0.0:FMath::Clamp(static_cast<double>(S.InitialFill),0.0,1.0)*FMath::Max(0.f,S.Capacity);
   const double Actual=A->GetPreciseVolumeML();
   if(!FMath::IsFinite(Actual)||FMath::Abs(Actual-Expected)>1.e-4){RestoreStatus=FText::FromString(TEXT("SUPPLY RESET FAILED - INVENTORY CHANGED"));return false;}
  }
  if(S.bStartCapped){auto* B=Cast<ABottleActor>(A);auto* Cap=B?B->CapActor.Get():nullptr;
   if(!IsValid(Cap)||Cap->IsActorBeingDestroyed()||Cap->OwnerBottle.Get()!=B||!Cap->bIsAttached||!B->bIsCapped){RestoreStatus=FText::FromString(TEXT("SUPPLY RESET FAILED - CAP CHANGED"));return false;}
  }
 }
 // 全部新供给与工具校验完成才迁移引用；发布标记先于旧罐 Destroy 回调，失败清理不再销毁已发布的新账本。
 for(int32 I=0;I<Slots.Num();++I){auto& S=Slots[I];if(!S.bInitialized||OldSlotFrozen[I])continue;auto* Old=S.Current.Get();auto* A=Staged[I].Get();
  if(Old){if(PrototypeCup.Get()==Old)PrototypeCup=A;if(PrototypeBottle.Get()==Old)PrototypeBottle=A;for(auto& Target:RestoreTargets)if(Target.Get()==Old)Target=A;
   for(TActorIterator<ATutorialDirector> D(World);D;++D)for(const auto& Step:D->Steps)if(auto* G=Cast<UTutorialStep_Grab>(Step.Get()))if(G->RequiredActor==Old)G->RequiredActor=A;
   for(TActorIterator<AIceStationActor> It(World);It;++It)if(It->Cup.Get()==Old)It->Cup=Cast<ACupActor>(A);
  }S.Current=A;StagedCommitted[I]=true;S.CapturedCap.Reset();if(auto* B=Cast<ABottleActor>(A))S.CapturedCap=B->CapActor;
 }
 for(auto* A:OldToRestore){if(IsToolLiquid(A)||!IsValid(A)||A->IsActorBeingDestroyed()||A->IsLiquidFrozen())continue;A->SetActorEnableCollision(false);if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed())continue;A->SetActorHiddenInGame(true);A->SetActorTickEnabled(false);if(IsValid(A->ContainerMesh.Get())){A->ContainerMesh->SetSimulatePhysics(false);A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);}if(!Alive())return false;if(IsValid(A)&&!A->IsActorBeingDestroyed())A->Destroy();if(!Alive())return false;}
 ++RestoreGeneration;RestoreStatus=FText::FromString(bHasProtected?TEXT("RESTORED - PROTECTED"):TEXT("RESTORED"));return true;
}
bool AGlassRecoveryActor::Recover(int32 I)
{
 if(bRestoreInProgress||!CanRecover(I))return false;auto& S=Slots[I];
 // A manual reclaim adds a new configured supply, not a resurrection or a reset
 // of an in-flight packet. No intact vessel can be duplicated with this button.
 auto* A=GetWorld()->SpawnActorDeferred<ALiquidContainerActor>(S.ContainerClass,S.SpawnTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
 if(!A)return false;
 A->FillAmount=S.InitialFill;A->MaxVolumeML=S.Capacity;A->InitialIngredientId=S.Ingredient;A->LiquidColor=S.Color;A->bInitialVolumeIsReplacement=true;A->ContainerMesh->SetStaticMesh(S.Mesh);for(int32 M=0;M<S.Materials.Num();++M)A->ContainerMesh->SetMaterial(M,S.Materials[M]);A->FinishSpawning(S.SpawnTransform);
 A->InitializeLiquidState();A->ContainerMesh->SetVisibility(true,true);if(A->GrabComp)A->GrabComp->Activate();
 auto* Old=S.Current.Get();S.Current=A;Install(A,S.Profile);
 for(TActorIterator<ATutorialDirector> D(GetWorld());D;++D)for(const auto& Step:D->Steps)if(auto* G=Cast<UTutorialStep_Grab>(Step.Get()))if(G->RequiredActor==Old)G->RequiredActor=A;
 if(Old)Old->Destroy();return true;
}
void AGlassRecoveryActor::Tick(float Dt)
{
 Super::Tick(Dt);
 // @trace RS-04 统一键沿用实体手指触碰；只为局部跟踪的手累计接触时间。
 if(IsUnifiedRestoreMode()){bool Touching=false;
  for(TActorIterator<AVRPawn> It(GetWorld());It;++It){auto* Pawn=*It;if(!Pawn->IsLocallyControlled())continue;
   for(int32 Hand=0;Hand<2;++Hand){auto* Grip=Hand==0?Pawn->MotionControllerLeftGrip.Get():Pawn->MotionControllerRightGrip.Get();auto* Held=Hand==0?Pawn->HeldComponentLeft.Get():Pawn->HeldComponentRight.Get();
    if(!IsValid(Grip)||!Grip->IsTracked()||(IsValid(Held)&&(Held->IsHeld()||Held->IsPulled()))){bTouchSampleReady[Hand]=false;continue;}
    auto* Mesh=Hand==0?Pawn->HandLeft.Get():Pawn->HandRight.Get();const FName Bone=Hand==0?TEXT("index_03_l"):TEXT("index_03_r");
    const FVector P=IsValid(Mesh)&&Mesh->DoesSocketExist(Bone)?Mesh->GetSocketLocation(Bone):Grip->GetComponentLocation();
    const FVector L=CupButton->GetComponentTransform().InverseTransformPosition(P);const FVector E=CupButton->GetUnscaledBoxExtent();
    const FBox TouchBox(-E,E);bool Contact=TouchBox.IsInsideOrOn(L);
    // 实体键的小幅扫掠容差，不将追踪跳变或快速穿过当成一整帧按住。
    if(!Contact&&bTouchSampleReady[Hand]&&FVector::DistSquared(P,PreviousTouch[Hand])<=4.f){const FVector Old=CupButton->GetComponentTransform().InverseTransformPosition(PreviousTouch[Hand]);Contact=FMath::LineBoxIntersection(TouchBox,Old,L,L-Old);}
    PreviousTouch[Hand]=P;bTouchSampleReady[Hand]=!P.ContainsNaN();Touching|=Contact;
   }
  }
  TStrongObjectPtr<AGlassRecoveryActor> Self(this);AdvanceRestoreHold(Dt,Touching);if(!IsValid(Self.Get())||Self->IsActorBeingDestroyed())return;
  const float Progress=FMath::Clamp(GetRestoreProgress(),0.f,1.f);Keys[0]->SetRelativeLocation(FVector(0,0,1.5f-.35f*Progress));Labels[0]->SetTextRenderColor(bRestoreHoldLatched?FColor(255,190,70):FColor(170,220,190));if(IsValid(StatusLabel.Get()))StatusLabel->SetText(RestoreStatus);return;
 }
 // No menu/input remapping. Both buttons only work while that prototype is broken.
 for(int32 I=0;I<2;++I){Labels[I]->SetTextRenderColor(CanRecover(I)?FColor(255,190,70):FColor(95,110,110));UBoxComponent* Box=I==0?CupButton.Get():BottleButton.Get();bool AnyInside=false;
  for(TActorIterator<AVRPawn> It(GetWorld());It;++It){AVRPawn* Pawn=*It;if(!Pawn->IsLocallyControlled())continue;
   for(int32 Hand=0;Hand<2;++Hand){auto* Grip=Hand==0?Pawn->MotionControllerLeftGrip.Get():Pawn->MotionControllerRightGrip.Get();auto* Held=Hand==0?Pawn->HeldComponentLeft.Get():Pawn->HeldComponentRight.Get();if(!Grip||!Grip->IsTracked()||(Held&&Held->IsHeld()))continue;auto* Mesh=Hand==0?Pawn->HandLeft.Get():Pawn->HandRight.Get();FName Bone=Hand==0?TEXT("index_03_l"):TEXT("index_03_r");const FVector P=Mesh&&Mesh->DoesSocketExist(Bone)?Mesh->GetSocketLocation(Bone):Grip->GetComponentLocation();const FVector L=Box->GetComponentTransform().InverseTransformPosition(P);const FVector E=Box->GetUnscaledBoxExtent();if(FMath::Abs(L.X)<E.X+2&&FMath::Abs(L.Y)<E.Y+2&&FMath::Abs(L.Z)<E.Z+2)AnyInside=true;if(FMath::Abs(L.X)<E.X&&FMath::Abs(L.Y)<E.Y&&FMath::Abs(L.Z)<E.Z&&!Slots[I].bLatched){Slots[I].bLatched=true;Recover(I);}}
  }if(!AnyInside)Slots[I].bLatched=false;
 }
}
