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
#include "UObject/UObjectGlobals.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuFixedCupIce,"CyberCafe.MenuPreparation.AllTenRequireSpecificCupAndIcePresence",MenuPreparationTests::TestOptions)
bool FMenuFixedCupIce::RunTest(const FString&)
{
 auto* Menu=MenuPreparationTests::Menu();if(!TestNotNull(TEXT("saved menu"),Menu))return false;
 TestEqual(TEXT("覆盖全部十种菜单饮品"),Menu->Entries.Num(),10);
 for(const auto& Entry:Menu->Entries)
 {
  MenuPreparationTests::FWorld F;auto* Recipe=Entry.Recipe.Get();auto* C=F.Cup(Recipe->AcceptedCupClass.Get());auto* Wrong=F.Cup(ACupActor::StaticClass(),FVector(100,0,0));F.Begin();
  for(const auto& P:Entry.Ingredients){C->AddIngredientLiquid(P.VolumeML,P.IngredientId,FLinearColor::White);Wrong->AddIngredientLiquid(P.VolumeML,P.IngredientId,FLinearColor::White);}
  TestEqual(TEXT("other native cup rejected"),Recipe->Evaluate(Wrong).Reason,EDrinkValidationReason::WrongCup);
  const int32 LiquidRevision=C->GetLiquidRevision();const double Liquid=C->GetPreciseVolumeML();
  TestEqual(TEXT("原杯物理冰块上限仍为6"),C->CupIce->MaxCupIce,6);
  const auto NoIce=Recipe->Evaluate(C);TestEqual(TEXT("无冰快照为0"),NoIce.IceCount,0);TestEqual(TEXT("无冰快照保留冰版本"),NoIce.IceRevision,C->CupIce->GetPreparationRevision());
  if(Recipe->ExpectedIceCount>0)
  {
   TestEqual(TEXT("冰饮旧资产建议量3不迁移"),Recipe->ExpectedIceCount,3);
   TestFalse(TEXT("正确液体但无冰不验收"),NoIce.bAccepted);TestEqual(TEXT("缺冰拒绝原因"),NoIce.Reason,EDrinkValidationReason::WrongIce);
   for(const int32 Count:{1,2,4,6})
   {
    const int32 IceRevision=C->CupIce->GetPreparationRevision();
    if(!TestTrue(FString::Printf(TEXT("%s 真实加冰至%d块"),*Entry.PinyinIndex.ToString(),Count),TutorialMenuTestHelpers::AddIce(C,Count-C->CupIce->GetIceCount())))return false;
    const auto Result=Recipe->Evaluate(C);
    TestEqual(TEXT("杯内实际冰块数"),C->CupIce->GetIceCount(),Count);TestTrue(TEXT("固定杯与配比有冰即可验收"),Result.bAccepted);TestEqual(TEXT("验收原因"),Result.Reason,EDrinkValidationReason::Accepted);
    TestEqual(TEXT("快照保留实际冰块数"),Result.IceCount,Count);TestEqual(TEXT("快照保留当前冰版本"),Result.IceRevision,C->CupIce->GetPreparationRevision());TestTrue(TEXT("真实补冰更新冰版本"),Result.IceRevision!=IceRevision);
    TestEqual(TEXT("冰不改变液体版本"),C->GetLiquidRevision(),LiquidRevision);TestEqual(TEXT("冰不增加液体mL"),C->GetPreciseVolumeML(),Liquid);TestEqual(TEXT("快照液体版本不变"),Result.LiquidRevision,LiquidRevision);TestEqual(TEXT("快照液体mL不变"),Result.TotalML,Liquid);
   }
  }
  else
  {
   TestEqual(TEXT("no ice requirement exact0"),Recipe->ExpectedIceCount,0);TestTrue(TEXT("no ice correct mixture accepted"),NoIce.bAccepted);
   if(!TestTrue(FString::Printf(TEXT("insert one actual ice %s"),*Entry.PinyinIndex.ToString()),TutorialMenuTestHelpers::AddIce(C,1)))return false;
   const auto Result=Recipe->Evaluate(C);TestFalse(TEXT("无冰饮品加冰不验收"),Result.bAccepted);TestEqual(TEXT("ice rejects no-ice drink"),Result.Reason,EDrinkValidationReason::WrongIce);
   TestEqual(TEXT("拒绝快照实际有1块冰"),Result.IceCount,1);TestEqual(TEXT("拒绝快照保留当前冰版本"),Result.IceRevision,C->CupIce->GetPreparationRevision());TestEqual(TEXT("加冰不改变液体版本"),C->GetLiquidRevision(),LiquidRevision);TestEqual(TEXT("加冰不增加液体mL"),C->GetPreciseVolumeML(),Liquid);TestEqual(TEXT("拒绝快照液体版本不变"),Result.LiquidRevision,LiquidRevision);TestEqual(TEXT("拒绝快照液体mL不变"),Result.TotalML,Liquid);
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
 if(!TestTrue(TEXT("拒绝后真实补入1块冰"),TutorialMenuTestHelpers::AddIce(C,1)))return false;F.Tick(60);
 TestEqual(TEXT("liquid revision remains unchanged"),C->GetLiquidRevision(),LiquidRevision);TestEqual(TEXT("order serial remains unchanged"),Spot->GetOrderSerial(),Order);
 TestTrue(TEXT("stable Tick automatically retries and serves"),Spot->HasAcceptedCup(C,TEXT("Latte")));TestTrue(TEXT("same cup frozen"),C->IsLiquidFrozen());TestEqual(TEXT("快照实际1块冰"),Spot->GetLastValidation().IceCount,1);TestEqual(TEXT("快照保留冰版本"),Spot->GetLastValidation().IceRevision,C->CupIce->GetPreparationRevision());TestEqual(TEXT("快照保留液体版本"),Spot->GetLastValidation().LiquidRevision,LiquidRevision);TestEqual(TEXT("exact200 liquid"),Spot->GetLastValidation().TotalML,200.0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuIceCompatibility,"CyberCafe.MenuPreparation.ExpectedIceCountPresenceCompatibility",MenuPreparationTests::TestOptions)
bool FMenuIceCompatibility::RunTest(const FString&)
{
 auto* Menu=MenuPreparationTests::Menu();if(!TestNotNull(TEXT("兼容测试读取保存菜单"),Menu))return false;
 const int32 Index=Menu->FindPinyinIndex(TEXT("BNT"));if(!TestTrue(TEXT("冰拿铁条目存在"),Menu->Entries.IsValidIndex(Index)))return false;
 auto* SourceRecipe=Menu->Entries[Index].Recipe.Get();if(!TestNotNull(TEXT("源配方存在"),SourceRecipe))return false;
 auto* Recipe=DuplicateObject<UDrinkRecipe>(SourceRecipe,GetTransientPackage());if(!TestNotNull(TEXT("独立瞬态配方副本"),Recipe))return false;
 // This case tests unconfigured ice-policy feedback, independent of authored dialogue.
 // Only the transient copy opts out; the saved ShiLing recipe is never modified.
 Recipe->AcceptedFeedback=FText::GetEmpty();
 TestTrue(TEXT("副本不共享源资产身份"),Recipe!=SourceRecipe);TestEqual(TEXT("默认标记仍为-1"),GetDefault<UDrinkRecipe>()->ExpectedIceCount,-1);TestEqual(TEXT("源冰饮资产仍为建议量3"),SourceRecipe->ExpectedIceCount,3);
 MenuPreparationTests::FWorld F;auto* C=F.Cup(Recipe->AcceptedCupClass.Get());F.Begin();
 for(const auto& P:Menu->Entries[Index].Ingredients)C->AddIngredientLiquid(P.VolumeML,P.IngredientId,FLinearColor::White);
 const int32 LiquidRevision=C->GetLiquidRevision();const double Liquid=C->GetPreciseVolumeML();
 Recipe->ExpectedIceCount=-1;const auto GenericNoIce=Recipe->Evaluate(C);TestTrue(TEXT("旧通用-1无冰可验收"),GenericNoIce.bAccepted);TestEqual(TEXT("旧通用无冰快照"),GenericNoIce.IceCount,0);TestEqual(TEXT("旧通用无冰版本快照"),GenericNoIce.IceRevision,C->CupIce->GetPreparationRevision());
 Recipe->ExpectedIceCount=0;TestTrue(TEXT("0标记无冰可验收"),Recipe->Evaluate(C).bAccepted);
 for(const int32 Flag:{1,2,3,6})
 {
  Recipe->ExpectedIceCount=Flag;const auto Result=Recipe->Evaluate(C);
  TestFalse(FString::Printf(TEXT("正数标记%d无冰不可验收"),Flag),Result.bAccepted);TestEqual(TEXT("正数缺冰原因"),Result.Reason,EDrinkValidationReason::WrongIce);TestEqual(TEXT("缺冰反馈不限块数"),Result.Feedback.ToString(),FString(TEXT("这杯需要加冰。有冰即可，不限块数；补一些冰再送来。")));
 }
 for(const int32 Flag:{-2,-3,MIN_int32})
 {
  Recipe->ExpectedIceCount=Flag;const auto Result=Recipe->Evaluate(C);TestFalse(TEXT("小于-1不可验收"),Result.bAccepted);TestEqual(TEXT("小于-1配置无效"),Result.Reason,EDrinkValidationReason::InvalidRecipe);
 }
 for(const int32 Count:{1,6})
 {
  if(!TestTrue(TEXT("兼容测试使用真实加冰"),TutorialMenuTestHelpers::AddIce(C,Count-C->CupIce->GetIceCount())))return false;
  TestEqual(TEXT("兼容测试实际冰块数"),C->CupIce->GetIceCount(),Count);
  Recipe->ExpectedIceCount=-1;const auto GenericIce=Recipe->Evaluate(C);TestTrue(TEXT("旧通用-1有冰也可验收"),GenericIce.bAccepted);TestEqual(TEXT("旧通用快照仍记录实际冰"),GenericIce.IceCount,Count);TestEqual(TEXT("旧通用快照仍记录冰版本"),GenericIce.IceRevision,C->CupIce->GetPreparationRevision());
  Recipe->ExpectedIceCount=0;const auto NoIce=Recipe->Evaluate(C);TestFalse(TEXT("0标记有冰不可验收"),NoIce.bAccepted);TestEqual(TEXT("0标记有冰拒绝原因"),NoIce.Reason,EDrinkValidationReason::WrongIce);TestEqual(TEXT("0标记拒绝保留实际冰快照"),NoIce.IceCount,Count);TestEqual(TEXT("0标记拒绝保留冰版本"),NoIce.IceRevision,C->CupIce->GetPreparationRevision());
  for(const int32 Flag:{1,2,3,6})
  {
   Recipe->ExpectedIceCount=Flag;const auto Result=Recipe->Evaluate(C);
   TestTrue(FString::Printf(TEXT("正数标记%d接受实际%d块冰"),Flag,Count),Result.bAccepted);TestEqual(TEXT("正数验收原因"),Result.Reason,EDrinkValidationReason::Accepted);TestEqual(TEXT("正数仅检查有冰的验收反馈"),Result.Feedback.ToString(),FString(TEXT("饮品份量、配比和冰要求符合订单。")));
   TestEqual(TEXT("正数验收实际冰快照"),Result.IceCount,Count);TestEqual(TEXT("正数验收冰版本快照"),Result.IceRevision,C->CupIce->GetPreparationRevision());TestEqual(TEXT("兼容验收液体版本不变"),Result.LiquidRevision,LiquidRevision);TestEqual(TEXT("兼容验收液体mL不变"),Result.TotalML,Liquid);
  }
 }
 TestEqual(TEXT("兼容测试不改变源资产"),SourceRecipe->ExpectedIceCount,3);TestEqual(TEXT("兼容测试不改变杯中液体版本"),C->GetLiquidRevision(),LiquidRevision);TestEqual(TEXT("兼容测试不改变杯中液体mL"),C->GetPreciseVolumeML(),Liquid);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuIceRemovedBeforeSubmit,"CyberCafe.MenuPreparation.IceRemovedBeforeStableSubmitRejects",MenuPreparationTests::TestOptions)
bool FMenuIceRemovedBeforeSubmit::RunTest(const FString&)
{
 MenuPreparationTests::FWorld F;auto* Menu=MenuPreparationTests::Menu();if(!Menu)return false;auto* Recipe=Menu->Entries[Menu->FindPinyinIndex(TEXT("BNT"))].Recipe.Get();
 auto* Spot=F.World->SpawnActor<AServingSpotActor>();Spot->SetActiveRecipe(Recipe);Spot->AcceptedCupClass=ACupActor::StaticClass();
 auto* C=F.Cup(Recipe->AcceptedCupClass.Get());C->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);C->ContainerMesh->SetGenerateOverlapEvents(true);C->ContainerMesh->SetCollisionResponseToAllChannels(ECR_Overlap);F.Begin();
 C->AddIngredientLiquid(100,TEXT("Coffee"),FLinearColor::White);C->AddIngredientLiquid(100,TEXT("Milk"),FLinearColor::White);
 const int32 LiquidRevision=C->GetLiquidRevision();const auto Order=Spot->GetOrderSerial();
 TestFalse(TEXT("200mL无冰提交拒绝"),Spot->TrySubmitCup(C));TestEqual(TEXT("首次拒绝因为缺冰"),Spot->GetLastValidation().Reason,EDrinkValidationReason::WrongIce);
 if(!TestTrue(TEXT("首次真实补入1块冰"),TutorialMenuTestHelpers::AddIce(C,1)))return false;
 TestTrue(TEXT("有1块冰配方已符合"),Recipe->Evaluate(C).bAccepted);F.Tick(5);
 TestFalse(TEXT("稳定窗口未到尚未验收"),Spot->HasBeenServed());TestFalse(TEXT("尚未冻结杯子"),C->IsLiquidFrozen());const int32 AddedIceRevision=C->CupIce->GetPreparationRevision();
 C->CupIce->ReleaseAll();TestEqual(TEXT("稳定提交前冰已全部移除"),C->CupIce->GetIceCount(),0);TestTrue(TEXT("移除冰更新版本"),C->CupIce->GetPreparationRevision()!=AddedIceRevision);F.Tick(60);
 TestFalse(TEXT("旧补冰状态不能导致验收"),Spot->HasBeenServed());TestFalse(TEXT("同杯仍未验收"),Spot->HasAcceptedCup(C,TEXT("Latte")));TestFalse(TEXT("移除冰后杯子未冻结"),C->IsLiquidFrozen());
 const auto Removed=Spot->GetLastValidation();TestFalse(TEXT("稳定重试结果仍拒绝"),Removed.bAccepted);TestEqual(TEXT("移除后稳定重试缺冰"),Removed.Reason,EDrinkValidationReason::WrongIce);TestEqual(TEXT("移除后快照真实无冰"),Removed.IceCount,0);TestEqual(TEXT("移除后快照保留新冰版本"),Removed.IceRevision,C->CupIce->GetPreparationRevision());TestEqual(TEXT("移除后快照液体版本不变"),Removed.LiquidRevision,LiquidRevision);TestEqual(TEXT("移除后仍为200mL"),Removed.TotalML,200.0);TestEqual(TEXT("移除冰不改变订单"),Spot->GetOrderSerial(),Order);
 if(!TestTrue(TEXT("同杯再次真实补入1块冰"),TutorialMenuTestHelpers::AddIce(C,1)))return false;F.Tick(60);
 TestTrue(TEXT("再补冰后自动稳定验收同杯"),Spot->HasAcceptedCup(C,TEXT("Latte")));TestTrue(TEXT("验收后仍是原杯"),Spot->GetServedCup()==C);TestTrue(TEXT("同杯成功冻结"),C->IsLiquidFrozen());TestTrue(TEXT("配方身份不变"),Spot->ActiveRecipe.Get()==Recipe);
 const auto Accepted=Spot->GetLastValidation();TestEqual(TEXT("成功快照实际1块冰"),Accepted.IceCount,1);TestEqual(TEXT("成功快照当前冰版本"),Accepted.IceRevision,C->CupIce->GetPreparationRevision());TestEqual(TEXT("成功快照液体版本不变"),Accepted.LiquidRevision,LiquidRevision);TestEqual(TEXT("杯中液体版本未变"),C->GetLiquidRevision(),LiquidRevision);TestEqual(TEXT("杯中仍为200mL"),C->GetPreciseVolumeML(),200.0);TestEqual(TEXT("成功快照仍为200mL"),Accepted.TotalML,200.0);TestEqual(TEXT("咖啡仍为100mL"),Accepted.Composition.Get(TEXT("Coffee")),100.0);TestEqual(TEXT("牛奶仍为100mL"),Accepted.Composition.Get(TEXT("Milk")),100.0);TestEqual(TEXT("订单未变"),Spot->GetOrderSerial(),Order);TestEqual(TEXT("验收订单仍为原订单"),Spot->GetAcceptedOrderSerial(),Order);
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
