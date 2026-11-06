// @trace BAR-F01 BAR-F02. Saved assets + real ledger/VR grab API. Not HMD pixels.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Liquid/BarSampleContainerActors.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/LiquidTransferMath.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "MotionControllerComponent.h"
#include "GrabComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraVariant.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
namespace BarSampleTests
{
constexpr EAutomationTestFlags Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
void Near(FAutomationTestBase& T,const TCHAR* Name,double A,double B,double E=.001){T.TestTrue(FString::Printf(TEXT("%s %.6f == %.6f"),Name,A,B),FMath::IsFinite(A)&&FMath::Abs(A-B)<E);}
struct FWorld
{
 UWorld* W=nullptr;UGameInstance* GI=nullptr;
 FWorld(){UWorld::InitializationValues IV;IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::SM5,&IV);auto& C=GEngine->CreateNewWorldContext(EWorldType::Game);C.SetCurrentWorld(W);GI=NewObject<UGameInstance>(GEngine);C.OwningGameInstance=GI;W->SetGameInstance(GI);GI->Init();}
 ~FWorld(){if(W->HasBegunPlay())W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}
 ULiquidTransferSubsystem* Transfer(){return W->GetSubsystem<ULiquidTransferSubsystem>();}
 void Begin(){W->InitializeActorsForPlay(FURL());W->GetWorldSettings()->NotifyBeginPlay();}
 template<typename T>T* Spawn(const TCHAR* ID,FVector Pos){const FString Path=FString::Printf(TEXT("/Game/VRTemplate/BarSamples/Functional/Blueprints/BP_%s.BP_%s_C"),ID,ID);UClass* Class=LoadClass<T>(nullptr,*Path);if(!Class)return nullptr;auto* A=W->SpawnActorDeferred<T>(Class,FTransform(Pos),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);A->ContainerMesh->SetSimulatePhysics(false);A->GrabComp->bSimulateOnDrop=false;A->LiquidFXTemplate=nullptr;A->LiquidFX->SetAutoActivate(false);A->LiquidFX->SetAsset(nullptr);// The saved P_Ribbon identity selects the real age-curve transfer path.
  // Nulling it would silently select the opposite-direction ballistic fallback.
  A->PourFX->SetAsset(LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/LiquidMaterials_VFXPack/Effects/P_Ribbon.P_Ribbon")));A->PourFX->SetComponentTickEnabled(false);A->FinishSpawning(FTransform(Pos));return A;}
 UMotionControllerComponent* Hand(FVector Pos){auto* Pawn=W->SpawnActor<AActor>();auto* MC=NewObject<UMotionControllerComponent>(Pawn);Pawn->SetRootComponent(MC);Pawn->AddInstanceComponent(MC);MC->RegisterComponent();MC->SetWorldLocation(Pos);return MC;}
 void Save(const TCHAR* Name,const FString& Text){FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("BAR_FUNCTION_RUN_DIR"));if(Root.IsEmpty())return;FFileHelper::SaveStringToFile(Text,*(Root/FString(Name)+TEXT(".json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
};
void Ledger(FAutomationTestBase& T,FWorld& F){auto B=F.Transfer()->GetBalance();Near(T,TEXT("mass error"),B.ErrorML,0);Near(T,TEXT("all volume accounted"),B.InitialML+B.InjectedML,B.ContainerML+B.InFlightML+B.SpilledML+B.RemovedML);}
void AimBottle(ABarSampleBottleActor* B,ABarSampleCupActor* C)
{
 B->SetActorRotation(FRotator(-90,0,0));const FTransform E=B->PourEntryPoint->GetComponentTransform();const FVector N=E.GetUnitAxis(EAxis::Z);FVector D=FVector::DownVector-FVector::DotProduct(FVector::DownVector,N)*N;const FVector Lip=E.GetLocation()+D.GetSafeNormal()*B->PourEntryRadius;const FVector Wanted=C->PourEntryPoint->GetComponentLocation()-CyberCafeLiquid::RibbonDisplacement(.30,N,B->PourRibbonVelocity,B->PourRibbonOffset);B->SetActorLocation(B->GetActorLocation()+Wanted-Lip);
}
}
// Keep helper names scoped: a file-level using-directive leaks into subsequent
// .cpp files under Unity builds and shadows engine template parameters (C4459).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSampleSavedConfiguration,"CyberCafe.BarSamples.SavedCollisionCalibrationAndCaps",BarSampleTests::Flags)
bool FSampleSavedConfiguration::RunTest(const FString&)
{
 using namespace BarSampleTests;
 FWorld F;auto* G=F.Spawn<ABarSampleBottleActor>(TEXT("SampleGin"),FVector(0,0,150));auto* V=F.Spawn<ABarSampleBottleActor>(TEXT("SampleVodka"),FVector(30,0,150));auto* O=F.Spawn<ABarSampleBottleActor>(TEXT("SampleOrangeSyrup"),FVector(60,0,150));auto* C=F.Spawn<ABarSampleCupActor>(TEXT("SampleCoupe"),FVector(90,0,150));if(!G||!V||!O||!C){AddError(TEXT("Saved sample BPs missing"));return false;}F.Begin();Near(*this,TEXT("Gin initial mL"),G->GetPreciseVolumeML(),525);Near(*this,TEXT("Vodka initial mL"),V->GetPreciseVolumeML(),750);Near(*this,TEXT("Orange initial mL"),O->GetPreciseVolumeML(),225);Near(*this,TEXT("Coupe empty"),C->GetPreciseVolumeML(),0);Near(*this,TEXT("Coupe capacity"),C->MaxVolumeML,200);
 for(auto* B:{G,V,O}){TestTrue(TEXT("sealed at BeginPlay"),B->bIsCapped&&!B->bCanPour);TestNotNull(TEXT("real cap spawned"),B->CapActor.Get());if(B->CapActor){const FVector Expected=B->ContainerMesh->GetSocketLocation(B->CapSocketName);TestTrue(TEXT("cap pivot at authored socket"),B->CapActor->GetActorLocation().Equals(Expected,.01));}TestTrue(TEXT("nonlinear fill calibration"),B->SampleLiquid->VolumeToHeight.Num()==65&&B->SampleLiquid->VisualFill>0&&B->SampleLiquid->VisualFill<1);TestEqual(TEXT("free grab preserves pose"),B->GrabComp->GrabType,EGrabType::Free);TestTrue(TEXT("finite convex collision present"),B->ContainerMesh->GetStaticMesh()->GetBodySetup()->AggGeom.ConvexElems.Num()>8);TestTrue(TEXT("no implicit break profile"),B->GlassBreakProfile==nullptr);}
 TestTrue(TEXT("Coupe wide opening"),C->PourEntryRadius>5);TestTrue(TEXT("Coupe cavity collision"),C->ContainerMesh->GetStaticMesh()->GetBodySetup()->AggGeom.ConvexElems.Num()>8);C->AddIngredientLiquid(100,TEXT("Water"),FLinearColor(.72,.84,.9));TestTrue(TEXT("half logical mL not half bowl height"),FMath::Abs(C->SampleLiquid->VisualFill-.5f)>.05f);Near(*this,TEXT("calibration never changes logical fill"),C->FillAmount,.5);Ledger(*this,F);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSampleGrabPourMix,"CyberCafe.BarSamples.GrabOpenAutomaticPourMixAndFullCup",BarSampleTests::Flags)
bool FSampleGrabPourMix::RunTest(const FString&)
{
 using namespace BarSampleTests;
 FWorld F;auto* G=F.Spawn<ABarSampleBottleActor>(TEXT("SampleGin"),FVector(0,0,220));auto* V=F.Spawn<ABarSampleBottleActor>(TEXT("SampleVodka"),FVector(30,0,220));auto* O=F.Spawn<ABarSampleBottleActor>(TEXT("SampleOrangeSyrup"),FVector(60,0,220));auto* C=F.Spawn<ABarSampleCupActor>(TEXT("SampleCoupe"),FVector(0,0,180));if(!G||!V||!O||!C){AddError(TEXT("sample BPs missing"));return false;}F.Begin();auto* BodyHand=F.Hand(G->GetActorLocation());TestTrue(TEXT("real VR bottle grab"),G->GrabComp->TryGrab(BodyHand));TestTrue(TEXT("held registered"),G->IsHeld());TestTrue(TEXT("release bottle"),G->GrabComp->TryRelease());
 for(auto* B:{G,V,O}){
  const double Before=B->GetPreciseVolumeML();AimBottle(B,C);B->Tick(.5);Near(*this,TEXT("capped tilt cannot emit"),B->GetPreciseVolumeML(),Before);
  auto* Cap=B->CapActor.Get();if(!TestNotNull(TEXT("cap"),Cap))return false;auto* H=F.Hand(Cap->GetActorLocation());TestTrue(TEXT("real VR cap grab"),Cap->GrabComp->TryGrab(H));H->SetWorldLocation(H->GetComponentLocation()+FVector(0,0,6));Cap->Tick(.02);TestTrue(TEXT("pull detaches cap and unlocks bottle"),!Cap->bIsAttached&&B->bCanPour);// Move the detached cap away before dropping. Dropping at the original socket
  // intentionally reattaches it (the existing production snap-back contract).
  Cap->SetActorLocation(FVector(400,400,300));Cap->GrabComp->TryRelease();Cap->CapMesh->SetSimulatePhysics(false);TestTrue(TEXT("cap remains off away from socket"),!B->bIsCapped&&B->bCanPour);AimBottle(B,C);B->Tick(.5);B->SetActorRotation(FRotator::ZeroRotator);B->Tick(.01);const FVector FX=B->PourFX->GetComponentLocation();const FVector EP=C->PourEntryPoint->GetComponentLocation();FHitResult Hit;FCollisionQueryParams Params;Params.AddIgnoredActor(B);Params.AddIgnoredActor(C);const bool Block=F.W->LineTraceSingleByChannel(Hit,FX,EP,ECC_Visibility,Params);
  AddInfo(FString::Printf(TEXT("POUR_PROBE src=%s FX=%s cup=%s accept=%d radius=%.3f ribbon=%s blocks=%d hit=%s"),*B->GetName(),*FX.ToString(),*EP.ToString(),C->bAcceptLiquidFromOthers,C->PourEntryRadius,*GetNameSafe(B->PourFX->GetAsset()),Block,*GetNameSafe(Hit.GetActor())));
  F.Transfer()->Simulate(3);AddInfo(FString::Printf(TEXT("POUR_BALANCE cup=%.6f spill=%.6f"),C->GetPreciseVolumeML(),F.Transfer()->GetBalance().SpilledML));TestTrue(TEXT("automatic pour debits bottle"),B->GetPreciseVolumeML()<Before);Cap->ReattachToBottle();TestTrue(TEXT("recap relocks bottle"),B->bIsCapped&&!B->bCanPour);Ledger(*this,F);
 }
 TestTrue(TEXT("cup received Gin"),C->GetIngredientVolumeML(TEXT("Gin"))>1);TestTrue(TEXT("cup received Vodka"),C->GetIngredientVolumeML(TEXT("Vodka"))>1);TestTrue(TEXT("cup received OrangeSyrup"),C->GetIngredientVolumeML(TEXT("OrangeSyrup"))>1);Near(*this,TEXT("three ingredient sum equals cup"),C->GetComposition().Total(),C->GetPreciseVolumeML());
 const double Before=C->GetPreciseVolumeML();C->AddIngredientLiquid(1000,TEXT("Water"),FLinearColor(.72,.84,.9));Near(*this,TEXT("capacity caps mL"),C->GetPreciseVolumeML(),200);Near(*this,TEXT("no further injection when full"),C->AddIngredientLiquid(100,TEXT("Water"),FLinearColor::White),0);C->SetActorRotation(FRotator(-85,0,0));C->Tick(.25);C->SetActorRotation(FRotator::ZeroRotator);C->Tick(.01);F.Transfer()->Simulate(3);TestTrue(TEXT("cup can pour back out"),C->GetPreciseVolumeML()<200);Ledger(*this,F);F.Save(TEXT("sample_pour_mix"),FString::Printf(TEXT("{\"mixed_before_water_ml\":%.6f,\"cup_after_pour_ml\":%.6f,\"error_ml\":%.9f}"),Before,C->GetPreciseVolumeML(),F.Transfer()->GetBalance().ErrorML));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSampleRestore,"CyberCafe.BarSamples.RestoreOnlySamplesAndHeldRejection",BarSampleTests::Flags)
bool FSampleRestore::RunTest(const FString&)
{
 using namespace BarSampleTests;
 FWorld F;auto* G=F.Spawn<ABarSampleBottleActor>(TEXT("SampleGin"),FVector(0,0,150));auto* V=F.Spawn<ABarSampleBottleActor>(TEXT("SampleVodka"),FVector(30,0,150));auto* O=F.Spawn<ABarSampleBottleActor>(TEXT("SampleOrangeSyrup"),FVector(60,0,150));auto* C=F.Spawn<ABarSampleCupActor>(TEXT("SampleCoupe"),FVector(90,0,150));if(!G||!V||!O||!C){AddError(TEXT("sample BPs missing"));return false;}auto* R=F.W->SpawnActorDeferred<AGlassRecoveryActor>(AGlassRecoveryActor::StaticClass(),FTransform(FVector(300,0,150)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);R->bUnifiedRestore=true;R->RestoreTargets={G,V,O,C};R->FinishSpawning(FTransform(FVector(300,0,150)));F.Begin();Near(*this,TEXT("only 4 explicit sample slots"),R->GetRestoreSlotCount(),4);G->ConsumeLiquid(50);C->AddIngredientLiquid(100,TEXT("Gin"),FLinearColor::White);auto* H=F.Hand(G->GetActorLocation());G->GrabComp->TryGrab(H);TestFalse(TEXT("held refuses restore"),R->RestoreWorkbench());G->GrabComp->TryRelease();TestTrue(TEXT("released samples can restore"),R->RestoreWorkbench());Near(*this,TEXT("restore generation once"),R->GetRestoreGeneration(),1);double Total=0;for(int I=0;I<6;++I)if(auto* A=R->GetCurrent(I)){Total+=A->GetPreciseVolumeML();TestFalse(TEXT("new supply not frozen"),A->IsLiquidFrozen());if(auto* B=Cast<ABottleActor>(A))TestTrue(TEXT("new bottles recapped"),B->bIsCapped&&B->CapActor);}Near(*this,TEXT("original sample supply restored"),Total,1500);Ledger(*this,F);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaterSpoutColor,"CyberCafe.BarSamples.KettleActualNozzleAndWaterColor",BarSampleTests::Flags)
bool FWaterSpoutColor::RunTest(const FString&)
{
 using namespace BarSampleTests;
 FWorld F;UClass* Class=LoadClass<AHandPourKettleActor>(nullptr,TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandPourKettle.BP_HandPourKettle_C"));if(!Class){AddError(TEXT("actual kettle BP missing"));return false;}auto* K=F.W->SpawnActorDeferred<AHandPourKettleActor>(Class,FTransform(FVector(0,0,160)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);K->ContainerMesh->SetSimulatePhysics(false);K->LiquidFXTemplate=nullptr;K->LiquidFX->SetAsset(nullptr);K->PourFX->SetAsset(nullptr);K->LiquidColor=FLinearColor(.35,.05,.08);K->FinishSpawning(FTransform(FVector(0,0,160)));F.Begin();Near(*this,TEXT("water not wine red after legacy default"),K->LiquidColor.R,.72);Near(*this,TEXT("water green"),K->LiquidColor.G,.84);Near(*this,TEXT("water blue"),K->LiquidColor.B,.90);
 for(FRotator Rot:{FRotator(-60,0,0),FRotator(-60,90,20),FRotator(-80,210,-15)}){K->SetActorRotation(Rot);const FTransform Expected=K->GetKettleSpoutWorldTransform();const double V=K->GetPreciseVolumeML();K->Tick(.02);TestTrue(TEXT("logic enables stream"),K->bIsPouring);TestTrue(TEXT("rendered origin equals measured mesh mouth"),K->PourFX->GetComponentLocation().Equals(Expected.GetLocation(),.001));TestTrue(TEXT("rendered X is nozzle outward"),K->PourFX->GetForwardVector().Equals(Expected.GetUnitAxis(EAxis::X),.001));TestTrue(TEXT("origin uses 17/0/16 mesh position"),Expected.GetLocation().Equals(K->ContainerMesh->GetComponentTransform().TransformPosition(FVector(17,0,16)),.001));TestTrue(TEXT("source debit"),K->GetPreciseVolumeML()<V);Near(*this,TEXT("offset has no backshift"),K->PourRibbonOffset,0);Near(*this,TEXT("no thickness compensation"),K->FlowStrength,1);}
 F.Transfer()->Simulate(3);Ledger(*this,F);return true;
}
#endif
