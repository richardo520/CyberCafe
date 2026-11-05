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
 S.PourEntryRadius=A->PourEntryRadius;
 if(A->PourEntryPoint){S.PourEntryRelativeTransform=A->PourEntryPoint->GetRelativeTransform();S.bEntryAbsoluteLocation=A->PourEntryPoint->IsUsingAbsoluteLocation();S.bEntryAbsoluteRotation=A->PourEntryPoint->IsUsingAbsoluteRotation();S.bEntryAbsoluteScale=A->PourEntryPoint->IsUsingAbsoluteScale();}
 if(!P)P=A->GlassBreakProfile;
 S.Profile=P;
 S.bCanPour=A->bCanPour;S.bAcceptLiquid=A->bAcceptLiquidFromOthers;
 S.Mesh=A->ContainerMesh->GetStaticMesh();S.MeshRelativeTransform=A->ContainerMesh->GetAttachParent()?A->ContainerMesh->GetRelativeTransform():FTransform::Identity;
 for(int32 M=0;M<A->ContainerMesh->GetNumMaterials();++M){UMaterialInterface* Material=A->ContainerMesh->GetMaterial(M);while(auto* MID=Cast<UMaterialInstanceDynamic>(Material))Material=MID->Parent;S.Materials.Add(Material);}
 if(auto* B=Cast<ABottleActor>(A)){S.CapClass=B->CapClass;S.CapSocket=B->CapSocketName;S.bStartCapped=B->bStartCapped;S.CapturedCap=B->CapActor;}
 if(auto* Ice=A->FindComponentByClass<UCupIceComponent>()){S.bHasCupIce=true;S.IceRadiusCM=Ice->InteriorRadiusCM;S.IceDepthCM=Ice->InteriorDepthCM;S.MaxCupIce=Ice->MaxCupIce;S.ClinkSound=Ice->ClinkSound;}
 else for(TActorIterator<AIceStationActor> It(GetWorld());It;++It)if(It->Cup.Get()==A){S.bHasCupIce=true;S.IceRadiusCM=It->CupInteriorRadiusCM;S.IceDepthCM=It->CupInteriorDepthCM;S.ClinkSound=It->ClinkSound;break;}
 S.bInitialized=true;Install(A,P);
}
void AGlassRecoveryActor::BeginPlay()
{
 Super::BeginPlay();Slots.SetNum(2);
 CaptureSlot(0,PrototypeCup.Get(),CupProfile.Get());CaptureSlot(1,PrototypeBottle.Get(),BottleProfile.Get());
 for(const auto& Target:RestoreTargets){bool Duplicate=false;for(const auto& S:Slots)if(S.bInitialized&&S.Current==Target){Duplicate=true;break;}if(!Duplicate&&IsValid(Target.Get())){const int32 I=Slots.AddDefaulted();CaptureSlot(I,Target.Get(),nullptr);}}
 if(IsValid(RestoreScoop.Get())&&RestoreScoop->GetWorld()==GetWorld()){CapturedScoop=RestoreScoop;ScoopSpawnTransform=RestoreScoop->GetActorTransform();}
 RestoreStatus=FText::FromString(TEXT("WAIT"));
 if(bUnifiedRestore){CupButton->SetRelativeLocation(FVector(0,0,2));CupButton->SetBoxExtent(FVector(6,2.8,1.5));BottleButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);BottleButton->SetActive(false);Keys[0]->SetRelativeLocation(FVector(0,0,1.5));Keys[0]->SetRelativeScale3D(FVector(.12,.05,.007));Keys[1]->SetVisibility(false);Labels[1]->SetVisibility(false);Labels[0]->SetRelativeLocation(FVector(-7,-2.5,2));Labels[0]->SetWorldSize(.8f);Labels[0]->SetText(FText::FromString(TEXT("RESET SUPPLIES")));
  StatusLabel=NewObject<UTextRenderComponent>(this,TEXT("RestoreStatusLabel"));StatusLabel->SetupAttachment(GetRootComponent());StatusLabel->SetRelativeLocation(FVector(-4,1.2,2));StatusLabel->SetRelativeRotation(FRotator(90,90,0));StatusLabel->SetWorldSize(.7f);StatusLabel->SetText(RestoreStatus);AddInstanceComponent(StatusLabel);StatusLabel->RegisterComponent();}
}
ALiquidContainerActor* AGlassRecoveryActor::GetCurrent(int32 I) const{if(!Slots.IsValidIndex(I))return nullptr;auto* A=Slots[I].Current.Get();return IsValid(A)&&!A->IsActorBeingDestroyed()?A:nullptr;}
bool AGlassRecoveryActor::CanRecover(int32 I) const{auto* A=GetCurrent(I);return I<2&&A&&Slots[I].Profile&&A->IsBroken();}
// @trace RS-02 只预检指定供给；其他手持物不阻断，也不释放。
bool AGlassRecoveryActor::PreflightRestore(TArray<ALiquidContainerActor*>& OldToRestore,TArray<ABottleCapActor*>& CapsToRestore)
{
 auto Fail=[this](const TCHAR* Text){RestoreStatus=FText::FromString(Text);return false;};
 UWorld* World=GetWorld();
 if(bRestoreInProgress||!World||GetRestoreSlotCount()==0)return Fail(TEXT("WAIT"));
 auto* Transfer=World->GetSubsystem<ULiquidTransferSubsystem>();
 if(!Transfer||!Transfer->CanPrepareWorkbenchRestore())return Fail(TEXT("WAIT"));
 auto Busy=[](UGrabComponent* G){return IsValid(G)&&(G->IsHeld()||G->IsPulled());};
 for(const auto& S:Slots){if(!S.bInitialized)continue;
  if(!S.ContainerClass||S.ContainerClass->HasAnyClassFlags(CLASS_Abstract)||!IsValid(S.Mesh.Get())||S.SpawnTransform.ContainsNaN()||S.SpawnTransform.GetScale3D().GetAbs().GetMin()<=KINDA_SMALL_NUMBER||!FMath::IsFinite(S.InitialFill)||!FMath::IsFinite(S.Capacity))return Fail(TEXT("CHECK SUPPLY SETUP"));
  if(S.bStartCapped&&S.CapClass&&S.CapClass->HasAnyClassFlags(CLASS_Abstract))return Fail(TEXT("CHECK SUPPLY SETUP"));
  auto* A=S.Current.Get();
  if(IsValid(A)&&!A->IsActorBeingDestroyed()){if(A->GetWorld()!=World)return Fail(TEXT("CHECK SUPPLY SETUP"));if(A->IsLiquidFrozen())continue;if(Busy(A->GrabComp.Get()))return Fail(TEXT("PUT DOWN SUPPLIES"));OldToRestore.AddUnique(A);}
  if(auto* Cap=S.CapturedCap.Get())CapsToRestore.AddUnique(Cap);
  if(IsValid(A)&&!A->IsActorBeingDestroyed()){
   if(auto* B=Cast<ABottleActor>(A))if(IsValid(B->CapActor.Get()))CapsToRestore.AddUnique(B->CapActor.Get());
   if(auto* Break=A->FindComponentByClass<UGlassBreakableComponent>())if(auto* Cap=Break->RelatedCap.Get())CapsToRestore.AddUnique(Cap);
  }
 }
 for(TActorIterator<ABottleCapActor> It(World);It;++It)if(OldToRestore.Contains(It->OwnerBottle.Get()))CapsToRestore.AddUnique(*It);
 for(auto* Cap:CapsToRestore)if(!IsValid(Cap)||Cap->IsActorBeingDestroyed()||Cap->GetWorld()!=World)return Fail(TEXT("CHECK SUPPLY SETUP"));else if(Busy(Cap->GrabComp.Get()))return Fail(TEXT("PUT DOWN SUPPLIES"));
 if(RestoreScoop.Get()){
  if(!IsValid(RestoreScoop.Get())||RestoreScoop->IsActorBeingDestroyed()||RestoreScoop->GetWorld()!=World||CapturedScoop.Get()!=RestoreScoop.Get()||!IsValid(RestoreScoop->Mesh.Get())||ScoopSpawnTransform.ContainsNaN()||ScoopSpawnTransform.GetScale3D().GetAbs().GetMin()<=KINDA_SMALL_NUMBER)return Fail(TEXT("CHECK SCOOP SETUP"));
  if(Busy(RestoreScoop->Grab.Get()))return Fail(TEXT("PUT DOWN SUPPLIES"));
 }
 for(const auto& S:Slots){if(!S.bInitialized)continue;
  const FVector Center=(S.MeshRelativeTransform*S.SpawnTransform).TransformPosition(S.Mesh->GetBoundingBox().GetCenter());
  // 小球只检查供给中心，不把起始贴桌接触当成占位；冻结杯可能已关闭查询碰撞。
  for(TActorIterator<ALiquidContainerActor> It(World);It;++It){auto* A=*It;if(OldToRestore.Contains(A)||!IsValid(A->ContainerMesh.Get())||!IsValid(A->ContainerMesh->GetStaticMesh()))continue;const FBox Bounds=A->ContainerMesh->GetStaticMesh()->GetBoundingBox().TransformBy(A->ContainerMesh->GetComponentTransform());if(Bounds.ExpandBy(1.f).IsInsideOrOn(Center))return Fail(TEXT("SUPPLY POSITION OCCUPIED"));}
  FCollisionQueryParams Query(SCENE_QUERY_STAT(WorkbenchSupply),false);Query.AddIgnoredActor(this);for(auto* A:OldToRestore)Query.AddIgnoredActor(A);for(auto* C:CapsToRestore)Query.AddIgnoredActor(C);if(IsValid(RestoreScoop.Get()))Query.AddIgnoredActor(RestoreScoop.Get());
  TArray<FOverlapResult> Hits;World->OverlapMultiByObjectType(Hits,Center,FQuat::Identity,FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeSphere(1.f),Query);
  for(const auto& Hit:Hits){auto* A=Hit.GetActor();auto* C=Hit.GetComponent();if(!IsValid(A)||Cast<APawn>(A))continue;
   if(auto* Debris=Cast<AGlassDebrisActor>(A)){bool Owned=false;for(const auto& Slot:Slots)if(Slot.bInitialized&&Slot.Current.Get()&&Debris->SourceContainer==TWeakObjectPtr<ALiquidContainerActor>(Slot.Current.Get())){auto* Current=Slot.Current.Get();if(!IsValid(Current)||!Current->IsLiquidFrozen())Owned=true;}if(Owned)continue;}
   if(auto* Ice=Cast<AIceCubeActor>(A)){
    const bool Protected=IsValid(Ice->CupIce.Get())&&Ice->CupIce->GetCup()&&Ice->CupIce->GetCup()->IsLiquidFrozen();
    if(!Protected&&((IsValid(Ice->CupIce.Get())&&OldToRestore.Contains(Ice->CupIce->GetCup()))||OldToRestore.Contains(Ice->LastCupSource.Get())||(RestoreScoop&&Ice->LastScoopSource.Get()==RestoreScoop.Get())))continue;
   }
   if(IsValid(C)&&C->Mobility==EComponentMobility::Static&&!Cast<ALiquidContainerActor>(A)&&!Cast<ABottleCapActor>(A))continue;
   return Fail(TEXT("SUPPLY POSITION OCCUPIED"));
  }
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
// @trace RS-01 RS-02 RS-03 分阶段提交：Deferred 预建失败不碰旧物；FinishSpawning 的蓝图回调不承诺强事务回滚。
bool AGlassRecoveryActor::RestoreWorkbench()
{
 TArray<ALiquidContainerActor*> OldToRestore;TArray<ABottleCapActor*> CapsToRestore;
 if(!PreflightRestore(OldToRestore,CapsToRestore))return false;
 TStrongObjectPtr<AGlassRecoveryActor> Self(this);bRestoreInProgress=true;
 // 强引用只阻止 GC，不阻止 Destroy；每次外部回调后仍须校验存活。
 TArray<TStrongObjectPtr<ALiquidContainerActor>> OldGuards;for(auto* A:OldToRestore)OldGuards.Emplace(A);
 TArray<TStrongObjectPtr<ABottleCapActor>> CapGuards;for(auto* Cap:CapsToRestore)CapGuards.Emplace(Cap);
 TArray<TStrongObjectPtr<ALiquidContainerActor>> Staged;Staged.SetNum(Slots.Num());
 bool Published=false;
 auto Alive=[&Self](){return IsValid(Self.Get())&&!Self->IsActorBeingDestroyed();};
 ON_SCOPE_EXIT{if(!Published)for(auto& Item:Staged){auto* A=Item.Get();if(!IsValid(A)||A->IsActorBeingDestroyed())continue;if(auto* B=Cast<ABottleActor>(A)){TStrongObjectPtr<ABottleCapActor> Cap(B->CapActor.Get());if(IsValid(Cap.Get())&&!Cap->IsActorBeingDestroyed()){Cap->InvalidateBrokenBottle();if(IsValid(Cap.Get())&&!Cap->IsActorBeingDestroyed())Cap->Destroy();}}if(IsValid(A)&&!A->IsActorBeingDestroyed())A->Destroy();}if(Alive())Self->bRestoreInProgress=false;};
 UWorld* World=GetWorld();
 for(int32 I=0;I<Slots.Num();++I){const auto& S=Slots[I];if(!S.bInitialized)continue;
  auto* A=World->SpawnActorDeferred<ALiquidContainerActor>(S.ContainerClass,S.SpawnTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
  Staged[I].Reset(A);if(!IsValid(A)||A->IsActorBeingDestroyed()||!IsValid(A->ContainerMesh.Get())){if(Alive())RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->bInitialVolumeIsReplacement=true;A->FillAmount=0;A->ContainerMesh->SetSimulatePhysics(false);A->SetActorEnableCollision(false);A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  if(!Alive())return false;
 }
 // 在停源回调和释放旧杯内冰之前收集身份；不删除已散落、无来源字段的冰。
 TArray<TWeakObjectPtr<AIceCubeActor>> OwnedIce;
 for(TActorIterator<AIceCubeActor> It(World);It;++It){
  const bool Protected=IsValid(It->CupIce.Get())&&It->CupIce->GetCup()&&It->CupIce->GetCup()->IsLiquidFrozen();
  if(Protected)continue;
  bool Owned=It->State==EIceState::InCup&&IsValid(It->CupIce.Get())&&OldToRestore.Contains(It->CupIce->GetCup());
  Owned|=RestoreScoop&&It->LastScoopSource.Get()==RestoreScoop.Get();
  for(const auto& S:Slots)if(S.bInitialized&&Cast<ACupActor>(S.Current.Get())&&It->LastCupSource==TWeakObjectPtr<ACupActor>(Cast<ACupActor>(S.Current.Get())))Owned=true;
  if(Owned)OwnedIce.Add(*It);
 }
 TArray<TWeakObjectPtr<AGlassDebrisActor>> OwnedDebris;
 for(TActorIterator<AGlassDebrisActor> It(World);It;++It)for(const auto& S:Slots)if(S.bInitialized&&S.Current.Get()&&It->SourceContainer==TWeakObjectPtr<ALiquidContainerActor>(S.Current.Get())){auto* A=S.Current.Get();if(!IsValid(A)||!A->IsLiquidFrozen())OwnedDebris.AddUnique(*It);}
 auto* Transfer=World->GetSubsystem<ULiquidTransferSubsystem>();
 if(!Transfer||!Transfer->PrepareWorkbenchRestore(OldToRestore)){if(Alive())RestoreStatus=FText::FromString(TEXT("WAIT"));return false;}
 const float ReceiveDelay=static_cast<float>(Transfer->GetUnsettledFlightHorizon());
 if(!Alive())return false;
 for(TActorIterator<AServingSpotActor> It(World);It;++It){It->ForgetRestoredContainers(OldToRestore);if(!Alive())return false;}
 for(auto* Cap:CapsToRestore){if(!IsValid(Cap)||Cap->IsActorBeingDestroyed())continue;Cap->InvalidateBrokenBottle();if(!Alive())return false;if(IsValid(Cap)&&!Cap->IsActorBeingDestroyed())Cap->Destroy();if(!Alive())return false;}
 for(auto* A:OldToRestore){if(!IsValid(A)||A->IsActorBeingDestroyed())continue;if(IsValid(A->GrabComp.Get()))A->GrabComp->ReleaseForBreak();if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed())continue;if(auto* Ice=A->FindComponentByClass<UCupIceComponent>())Ice->ReleaseAll();if(!Alive())return false;}
 for(const auto& Item:OwnedIce){if(auto* Ice=Item.Get()){if(Ice->State==EIceState::InCup&&IsValid(Ice->CupIce.Get())&&!OldToRestore.Contains(Ice->CupIce->GetCup()))continue;Ice->Destroy();}if(!Alive())return false;}
 for(const auto& Item:OwnedDebris){if(auto* Debris=Item.Get())Debris->Destroy();if(!Alive())return false;}
 if(!Alive())return false;
 if(IsValid(RestoreScoop.Get())&&!RestoreScoop->ResetForWorkbench(ScoopSpawnTransform)){if(Alive())RestoreStatus=FText::FromString(TEXT("SCOOP RESET FAILED"));return false;}
 if(!Alive())return false;
 // 旧引用先迁移到全部有效的新身份，订单、ServedCup、配方和全息图不改。
 for(int32 I=0;I<Slots.Num();++I){auto& S=Slots[I];if(!S.bInitialized)continue;auto* Old=S.Current.Get();auto* A=Staged[I].Get();
  if(Old){if(PrototypeCup.Get()==Old)PrototypeCup=A;if(PrototypeBottle.Get()==Old)PrototypeBottle=A;for(auto& Target:RestoreTargets)if(Target.Get()==Old)Target=A;
   for(TActorIterator<ATutorialDirector> D(World);D;++D)for(const auto& Step:D->Steps)if(auto* G=Cast<UTutorialStep_Grab>(Step.Get()))if(G->RequiredActor==Old)G->RequiredActor=A;
   for(TActorIterator<AIceStationActor> It(World);It;++It)if(It->Cup.Get()==Old)It->Cup=Cast<ACupActor>(A);
  }S.Current=A;S.CapturedCap.Reset();
 }
 for(auto* A:OldToRestore){if(!IsValid(A)||A->IsActorBeingDestroyed())continue;A->SetActorEnableCollision(false);if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed())continue;A->SetActorHiddenInGame(true);A->SetActorTickEnabled(false);if(IsValid(A->ContainerMesh.Get())){A->ContainerMesh->SetSimulatePhysics(false);A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);}if(!Alive())return false;if(IsValid(A)&&!A->IsActorBeingDestroyed())A->Destroy();if(!Alive())return false;}
 for(int32 I=0;I<Slots.Num();++I){auto& S=Slots[I];if(!S.bInitialized)continue;auto* A=Staged[I].Get();if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->FillAmount=Cast<ACupActor>(A)?0:S.InitialFill;A->MaxVolumeML=S.Capacity;A->InitialIngredientId=S.Ingredient;A->LiquidColor=S.Color;A->bCanPour=S.bCanPour;A->bAcceptLiquidFromOthers=S.bAcceptLiquid;
  A->PourEntryRadius=S.PourEntryRadius;
  if(A->PourEntryPoint){A->PourEntryPoint->SetAbsolute(S.bEntryAbsoluteLocation,S.bEntryAbsoluteRotation,S.bEntryAbsoluteScale);A->PourEntryPoint->SetRelativeTransform(S.PourEntryRelativeTransform);}
  A->ContainerMesh->SetStaticMesh(S.Mesh);if(A->ContainerMesh->GetAttachParent())A->ContainerMesh->SetRelativeTransform(S.MeshRelativeTransform);for(int32 M=0;M<S.Materials.Num();++M)A->ContainerMesh->SetMaterial(M,S.Materials[M]);
  if(auto* B=Cast<ABottleActor>(A)){B->CapClass=S.CapClass;B->CapSocketName=S.CapSocket;B->bStartCapped=S.bStartCapped;}
  A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);A->ContainerMesh->SetSimulatePhysics(true);A->SetActorEnableCollision(true);
  if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->RestoreReceiveCooldown=ReceiveDelay;
  A->FinishSpawning(S.SpawnTransform);if(A->GrabComp)A->GrabComp->Activate();if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  // 构造脚本可能重写视觉配置；初始化后的权威体积不重复灌注。
  A->PourEntryRadius=S.PourEntryRadius;
  if(A->PourEntryPoint){A->PourEntryPoint->SetAbsolute(S.bEntryAbsoluteLocation,S.bEntryAbsoluteRotation,S.bEntryAbsoluteScale);A->PourEntryPoint->SetRelativeTransform(S.PourEntryRelativeTransform);}
  A->ContainerMesh->SetStaticMesh(S.Mesh);for(int32 M=0;M<S.Materials.Num();++M)A->ContainerMesh->SetMaterial(M,S.Materials[M]);A->LiquidColor=S.Color;A->RefreshLiquidFX();if(!Alive())return false;
  Install(A,S.Profile);if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  A->SetActorTransform(S.SpawnTransform,false,nullptr,ETeleportType::TeleportPhysics);A->ContainerMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);A->ContainerMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
  if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  if(S.bHasCupIce&&Cast<ACupActor>(A)){auto* Ice=A->FindComponentByClass<UCupIceComponent>();if(!Ice){Ice=NewObject<UCupIceComponent>(A);A->AddInstanceComponent(Ice);}Ice->InteriorRadiusCM=S.IceRadiusCM;Ice->InteriorDepthCM=S.IceDepthCM;Ice->MaxCupIce=S.MaxCupIce;Ice->ClinkSound=S.ClinkSound;if(!Ice->IsRegistered())Ice->RegisterComponent();}
  if(!Alive())return false;if(!IsValid(A)||A->IsActorBeingDestroyed()){RestoreStatus=FText::FromString(TEXT("SUPPLY SPAWN FAILED"));return false;}
  if(auto* B=Cast<ABottleActor>(A))S.CapturedCap=B->CapActor;
 }
 Published=true;++RestoreGeneration;RestoreStatus=FText::FromString(TEXT("RESTORED"));return true;
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
 if(bUnifiedRestore){bool Touching=false;
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
