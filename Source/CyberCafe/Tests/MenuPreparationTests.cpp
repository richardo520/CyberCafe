#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/ServingSpotActor.h"
#include "Serving/OrderHologramActor.h"
#include "Tutorial/CustomerDrinkMotion.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "Liquid/GlassRecoveryActor.h"
#include "GrabComponent.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include "CoreGlobals.h"
namespace MenuPreparationTests
{
constexpr auto TestOptions=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
struct FWorld
{
 UWorld* World;UGameInstance* GI;
 FWorld(){UWorld::InitializationValues IV;IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::SM5,&IV);auto& C=GEngine->CreateNewWorldContext(EWorldType::Game);C.SetCurrentWorld(World);GI=NewObject<UGameInstance>(GEngine);C.OwningGameInstance=GI;World->SetGameInstance(GI);GI->Init();}
 ~FWorld(){if(World->HasBegunPlay())World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
 void Begin(){World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();}
 void Tick(int N=1){for(int I=0;I<N;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.02);}}
 ACupActor* Cup(UClass* Class,FVector Position=FVector::ZeroVector)
 {
  const FTransform Spawn(FRotator::ZeroRotator,Position,TutorialMenuTestHelpers::SceneCupScale(Class));
  auto* A=World->SpawnActorDeferred<ACupActor>(Class,Spawn,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  A->ContainerMesh->SetSimulatePhysics(false);A->GlassBreakProfile=nullptr;A->LiquidFXTemplate=nullptr;A->LiquidFX->SetAsset(nullptr);A->PourFX->SetAsset(nullptr);A->FinishSpawning(Spawn);A->bCanPour=false;return A;
 }
};
UDrinkMenuData* Menu(){return LoadObject<UDrinkMenuData>(nullptr,TEXT("/Game/VRTemplate/MenuIngredients/Menu/DA_RecipeMenu.DA_RecipeMenu"));}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuFixedCupIce,"CyberCafe.MenuPreparation.AllTenRequireExactCupAndIce",MenuPreparationTests::TestOptions)
bool FMenuFixedCupIce::RunTest(const FString&)
{
 auto* Menu=MenuPreparationTests::Menu();if(!TestNotNull(TEXT("saved menu"),Menu))return false;
 for(const auto& Entry:Menu->Entries)
 {
  MenuPreparationTests::FWorld F;auto* Recipe=Entry.Recipe.Get();auto* C=F.Cup(Recipe->AcceptedCupClass.Get());auto* Wrong=F.Cup(ACupActor::StaticClass(),FVector(100,0,0));F.Begin();
  for(const auto& P:Entry.Ingredients){C->AddIngredientLiquid(P.VolumeML,P.IngredientId,FLinearColor::White);Wrong->AddIngredientLiquid(P.VolumeML,P.IngredientId,FLinearColor::White);}
  TestEqual(TEXT("other native cup rejected"),Recipe->Evaluate(Wrong).Reason,EDrinkValidationReason::WrongCup);
  if(Recipe->ExpectedIceCount==3)
  {
   TestEqual(TEXT("right liquid without ice rejected"),Recipe->Evaluate(C).Reason,EDrinkValidationReason::WrongIce);
   const int32 LiquidRevision=C->GetLiquidRevision();const double Liquid=C->GetPreciseVolumeML();
   if(!TestTrue(TEXT("3 actual ice cross mouth"),TutorialMenuTestHelpers::AddIce(C,3)))return false;
   TestEqual(TEXT("ice never mutates liquid revision"),C->GetLiquidRevision(),LiquidRevision);TestEqual(TEXT("ice not added to liquid mL"),C->GetPreciseVolumeML(),Liquid);
   TestTrue(TEXT("fixed cup mixture and 3 ice accepted"),Recipe->Evaluate(C).bAccepted);
   if(!TestTrue(TEXT("one extra physical ice accepted by vessel"),TutorialMenuTestHelpers::AddIce(C,1)))return false;
   TestEqual(TEXT("4 ice rejects recipe"),Recipe->Evaluate(C).Reason,EDrinkValidationReason::WrongIce);
  }
  else
  {
   TestEqual(TEXT("no ice requirement exact0"),Recipe->ExpectedIceCount,0);TestTrue(TEXT("no ice correct mixture accepted"),Recipe->Evaluate(C).bAccepted);
   UE_LOG(LogTemp,Log,TEXT("[MenuIceProbe] code=%s cup=%s scale=%s depth=%.4f radius=%.4f entryRadius=%.4f"),*Entry.PinyinIndex.ToString(),*C->GetClass()->GetName(),*C->GetActorScale3D().ToString(),C->CupIce->InteriorDepthCM,C->CupIce->InteriorRadiusCM,C->PourEntryRadius);
   if(!TestTrue(FString::Printf(TEXT("insert one actual ice %s"),*Entry.PinyinIndex.ToString()),TutorialMenuTestHelpers::AddIce(C,1)))return false;
   TestEqual(TEXT("ice rejects no-ice drink"),Recipe->Evaluate(C).Reason,EDrinkValidationReason::WrongIce);
  }
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuIceResubmit,"CyberCafe.MenuPreparation.FillIceRearmsStableSubmitWithoutRegrab",MenuPreparationTests::TestOptions)
bool FMenuIceResubmit::RunTest(const FString&)
{
 MenuPreparationTests::FWorld F;auto* Menu=MenuPreparationTests::Menu();if(!Menu)return false;auto* Recipe=Menu->Entries[Menu->FindPinyinIndex(TEXT("BNT"))].Recipe.Get();
 auto* Spot=F.World->SpawnActor<AServingSpotActor>();Spot->SetActiveRecipe(Recipe);Spot->AcceptedCupClass=ACupActor::StaticClass();
 auto* C=F.Cup(Recipe->AcceptedCupClass.Get());C->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);C->ContainerMesh->SetGenerateOverlapEvents(true);C->ContainerMesh->SetCollisionResponseToAllChannels(ECR_Overlap);F.Begin();
 C->AddIngredientLiquid(100,TEXT("Coffee"),FLinearColor::White);C->AddIngredientLiquid(100,TEXT("Milk"),FLinearColor::White);
 TestFalse(TEXT("empty ice submission rejects"),Spot->TrySubmitCup(C));TestEqual(TEXT("reason missing ice"),Spot->GetLastValidation().Reason,EDrinkValidationReason::WrongIce);
 const int32 LiquidRevision=C->GetLiquidRevision();const auto Order=Spot->GetOrderSerial();
 TestTrue(TEXT("3 actual ice added after rejection"),TutorialMenuTestHelpers::AddIce(C,3));F.Tick(60);
 TestEqual(TEXT("liquid revision remains unchanged"),C->GetLiquidRevision(),LiquidRevision);TestEqual(TEXT("order serial remains unchanged"),Spot->GetOrderSerial(),Order);
 TestTrue(TEXT("stable Tick automatically retries and serves"),Spot->HasAcceptedCup(C,TEXT("Latte")));TestTrue(TEXT("same cup frozen"),C->IsLiquidFrozen());TestEqual(TEXT("snapshot actual3 ice"),Spot->GetLastValidation().IceCount,3);TestEqual(TEXT("exact200 liquid"),Spot->GetLastValidation().TotalML,200.0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuIceRevision,"CyberCafe.MenuPreparation.SameCountReplacementChangesIceRevision",MenuPreparationTests::TestOptions)
bool FMenuIceRevision::RunTest(const FString&)
{
 MenuPreparationTests::FWorld F;auto* Menu=MenuPreparationTests::Menu();if(!Menu)return false;auto* C=F.Cup(Menu->Entries[0].Recipe->AcceptedCupClass.Get());F.Begin();
 TestTrue(TEXT("ice inserted"),TutorialMenuTestHelpers::AddIce(C,3));auto* Ice=C->FindComponentByClass<UCupIceComponent>();const int32 Before=Ice->GetPreparationRevision();F.Tick(10);TestEqual(TEXT("normal tick is not a membership change"),Ice->GetPreparationRevision(),Before);
 Ice->ReleaseAll();TestEqual(TEXT("all released"),Ice->GetIceCount(),0);const int32 Removed=Ice->GetPreparationRevision();TestTrue(TEXT("released version differs"),Removed!=Before);
 TestTrue(TEXT("new3 actual ice"),TutorialMenuTestHelpers::AddIce(C,3));TestTrue(TEXT("same final count has a new version"),Ice->GetIceCount()==3&&Ice->GetPreparationRevision()!=Before);
 C->FreezeLiquidForServing();C->SetActorRotation(FRotator(85,0,0));F.Tick(20);TestEqual(TEXT("frozen ice remains with cup during motion"),Ice->GetIceCount(),3);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuHighballMotion,"CyberCafe.MenuPreparation.NewHighballMotionKeepsOriginalAssets",MenuPreparationTests::TestOptions)
bool FMenuHighballMotion::RunTest(const FString&)
{
 auto* New=LoadObject<UCustomerDrinkMotion>(nullptr,TEXT("/Game/Characters/BarGirl/FirstCustomer/Highball/DA_BarGirlHighballDrinkMotion.DA_BarGirlHighballDrinkMotion"));auto* Old=LoadObject<UCustomerDrinkMotion>(nullptr,TEXT("/Game/Characters/BarGirl/FirstCustomer/DA_BarGirlDrinkMotion.DA_BarGirlDrinkMotion"));
 if(!TestNotNull(TEXT("authored new motion"),New)||!TestNotNull(TEXT("original retained motion"),Old))return false;
 TestTrue(TEXT("new motion valid"),New->IsValidMotion());TestTrue(TEXT("different new mesh and animation"),New->Animation!=Old->Animation&&New->SupportedCupMesh!=Old->SupportedCupMesh);TestTrue(TEXT("new exactscale1.1"),New->SupportedCupScale.Equals(FVector(1.1),.001));TestTrue(TEXT("legacy exactscale1 unchanged"),Old->SupportedCupScale.Equals(FVector::OneVector,.001));TestEqual(TEXT("7 second duration"),New->Duration(),7.f);TestEqual(TEXT("20mL budget"),New->SipVolumeML,20.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuLostTracking,"CyberCafe.MenuPreparation.TrackingLossReleasesOnlyLostHandLatch",MenuPreparationTests::TestOptions)
bool FMenuLostTracking::RunTest(const FString&)
{
 MenuPreparationTests::FWorld F;auto* Class=LoadClass<AOrderHologramActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Serving/BP_OrderHologram.BP_OrderHologram_C"));auto* S=F.World->SpawnActor<AOrderHologramActor>(Class);F.Begin();
 const FVector Power=S->ButtonZone->GetComponentLocation();
 TestTrue(TEXT("left hand opens power"),S->ProcessTouchSample(EControllerHand::Left,Power));
 TestFalse(TEXT("lost hand never triggers power"),S->ProcessTouchSample(EControllerHand::Left,Power,false));
 TestTrue(TEXT("right hand can close after left loses tracking"),S->ProcessTouchSample(EControllerHand::Right,Power));
 S->ProcessTouchSample(EControllerHand::Right,Power,false);S->PressButton();F.Tick(20);
 TestTrue(TEXT("left menu filter touch"),TutorialMenuTestHelpers::TouchAction(S,11));
 const FIntRect R=UOrderHologramWidget::GetScreenActionRect(100);const FVector Point=S->Panel->GetComponentTransform().TransformPosition(FVector(0,600-(R.Min.X+R.Max.X)*.5,375-(R.Min.Y+R.Max.Y)*.5));
 TestFalse(TEXT("lost left hand no action"),S->ProcessMenuTouchSample(EControllerHand::Left,Point,false));
 TestTrue(TEXT("right hand can select after lost left contact"),S->ProcessMenuTouchSample(EControllerHand::Right,Point));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuIceRestore,"CyberCafe.MenuPreparation.RestoreHighballRetainsIceGeometryPhysicsAndProfile",MenuPreparationTests::TestOptions)
bool FMenuIceRestore::RunTest(const FString&)
{
 MenuPreparationTests::FWorld F;auto* Menu=MenuPreparationTests::Menu();if(!Menu)return false;auto* Class=Menu->Entries[Menu->FindPinyinIndex(TEXT("BNT"))].Recipe->AcceptedCupClass.Get();
 auto* C=F.Cup(Class,FVector(0,0,120));C->GlassBreakProfile=Class->GetDefaultObject<ACupActor>()->GlassBreakProfile;C->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 auto* Manager=F.World->SpawnActor<AGlassRecoveryActor>();Manager->bUnifiedRestore=true;Manager->RestoreTargets={C};Manager->PrototypeCup=C;Manager->CupProfile=C->GlassBreakProfile;F.Begin();
 C->AddIngredientLiquid(100,TEXT("Coffee"),FLinearColor::White);C->AddIngredientLiquid(100,TEXT("Milk"),FLinearColor::White);TestTrue(TEXT("actual3 ice inserted before reset"),TutorialMenuTestHelpers::AddIce(C,3));
 const auto Profile=C->GlassBreakProfile;const auto Scale=C->GetActorScale3D();const float Depth=C->CupIce->InteriorDepthCM,Radius=C->CupIce->InteriorRadiusCM;
 TestEqual(TEXT("test restore only one explicit vessel"),Manager->GetRestoreSlotCount(),1);if(!TestTrue(TEXT("recovery transaction succeeds"),Manager->RestoreWorkbench()))return false;
 auto* New=Cast<ACupActor>(Manager->PrototypeCup);if(!TestNotNull(TEXT("new explicit cup published"),New))return false;
 TestTrue(TEXT("new identity uses same class"),New!=C&&New->GetClass()==Class);TestTrue(TEXT("same scale"),New->GetActorScale3D().Equals(Scale,.001));TestEqual(TEXT("300mL cap"),New->MaxVolumeML,300.f);TestEqual(TEXT("empty new cup"),New->GetPreciseVolumeML(),0.0);
 TestEqual(TEXT("reset cleans owned ice"),New->CupIce->GetIceCount(),0);TestEqual(TEXT("saved ice depth"),New->CupIce->InteriorDepthCM,Depth);TestEqual(TEXT("saved ice radius"),New->CupIce->InteriorRadiusCM,Radius);
 TestEqual(TEXT("same matched break profile"),New->GlassBreakProfile.Get(),Profile.Get());TestEqual(TEXT("PhysicsHandle retained"),New->GrabComp->PhysicsMode,EGrabPhysicsMode::PhysicsHandle);return true;
}
#endif
