// @trace MN-01 MN-02 MN-03. Saved menu and real game-world interaction, not HMD screenshots.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "UI/OrderHologramWidget.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "NiagaraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "CoreGlobals.h"
namespace RecipeMenuTest
{
constexpr auto MenuTestFlags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
UDrinkMenuData* Data(){return LoadObject<UDrinkMenuData>(nullptr,TEXT("/Game/VRTemplate/MenuIngredients/Menu/DA_RecipeMenu.DA_RecipeMenu"));}
struct World
{
 UWorld* W;UGameInstance* GI;AOrderHologramActor* Screen;
 World(){UWorld::InitializationValues IV;IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::SM5,&IV);auto& C=GEngine->CreateNewWorldContext(EWorldType::Game);C.SetCurrentWorld(W);GI=NewObject<UGameInstance>(GEngine);C.OwningGameInstance=GI;W->SetGameInstance(GI);GI->Init();auto* Class=LoadClass<AOrderHologramActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Serving/BP_OrderHologram.BP_OrderHologram_C"));Screen=Class?W->SpawnActor<AOrderHologramActor>(Class,FTransform(FRotator(0,32,0),FVector(30,50,100))):nullptr;}
 ~World(){if(W->HasBegunPlay())W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}
 void Begin(){W->InitializeActorsForPlay(FURL());W->GetWorldSettings()->NotifyBeginPlay();}
 void Tick(int N=1){for(int I=0;I<N;++I){++GFrameCounter;W->Tick(LEVELTICK_All,.02);}}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuSavedData,"CyberCafe.RecipeMenu.SavedTenPinyinIndicesAndML",RecipeMenuTest::MenuTestFlags)
bool FMenuSavedData::RunTest(const FString&)
{
 auto* D=RecipeMenuTest::Data();if(!TestNotNull(TEXT("saved menu dataset"),D))return false;TestTrue(TEXT("menu validation"),D->IsValidMenu());TestEqual(TEXT("10 drinks"),D->Entries.Num(),10);const TCHAR* Codes[]={TEXT("CMHK"),TEXT("GJHY"),TEXT("JDMS"),TEXT("KFYH"),TEXT("MSXH"),TEXT("MYNT"),TEXT("SQJG"),TEXT("YGNT"),TEXT("YWNT"),TEXT("YYBL")};for(int I=0;I<10;++I){TestEqual(TEXT("sorted explicit pinyin"),D->Entries[I].PinyinIndex,FName(Codes[I]));TestEqual(TEXT("exact index selection"),D->FindPinyinIndex(FName(Codes[I])),I);double ML=0;for(const auto& P:D->Entries[I].Ingredients)ML+=P.VolumeML;TestTrue(TEXT("exact target mL"),FMath::IsNearlyEqual(ML,D->Entries[I].Recipe->TargetVolumeML,.001));}TestEqual(TEXT("unknown code no fallback"),D->FindPinyinIndex(TEXT("NONE")),INDEX_NONE);auto* Duplicate=NewObject<UDrinkMenuData>();Duplicate->Entries=D->Entries;Duplicate->Entries[1].PinyinIndex=Duplicate->Entries[0].PinyinIndex;TestFalse(TEXT("duplicate rejects ambiguous menu"),Duplicate->IsValidMenu());Duplicate->Entries=D->Entries;Duplicate->Entries[0].Ingredients[0].VolumeML+=20;TestFalse(TEXT("wrong amounts rejects stale menu"),Duplicate->IsValidMenu());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuOrderUnchanged,"CyberCafe.RecipeMenu.BrowseDoesNotChangeActiveOrderOrAutoClose",RecipeMenuTest::MenuTestFlags)
bool FMenuOrderUnchanged::RunTest(const FString&)
{
 RecipeMenuTest::World F;if(!TestNotNull(TEXT("saved screen"),F.Screen))return false;auto* R=LoadObject<UDrinkRecipe>(nullptr,TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));auto* Spot=F.W->SpawnActor<AServingSpotActor>();Spot->SetActiveRecipe(R);F.Begin();TestTrue(TEXT("order starts"),F.Screen->BeginOrder(R,Spot,nullptr,FText::GetEmpty()));const auto Serial=Spot->GetOrderSerial();TestFalse(TEXT("default order view"),F.Screen->IsRecipeMenuMode());TestTrue(TEXT("menu toggles"),F.Screen->ToggleRecipeMenu());TestTrue(TEXT("player-open menu prevents auto close"),F.Screen->IsPlayerOpened());for(int I=0;I<23;++I)TestTrue(TEXT("next"),F.Screen->BrowseRecipeMenu(1));TestTrue(TEXT("select MYNT"),F.Screen->SelectRecipeByPinyin(TEXT("MYNT")));TestTrue(TEXT("order pointer unchanged"),F.Screen->GetCurrentRecipe()==R&&Spot->ActiveRecipe==R);TestEqual(TEXT("order serial unchanged"),Spot->GetOrderSerial(),Serial);TestTrue(TEXT("pending unchanged"),F.Screen->IsPending());TestFalse(TEXT("bad code rejected"),F.Screen->SelectRecipeByPinyin(TEXT("UNKNOWN")));F.Tick(150);TestTrue(TEXT("menu remains open while reading"),F.Screen->IsPanelOpen());TestTrue(TEXT("return order"),F.Screen->ToggleRecipeMenu());TestFalse(TEXT("order view restored"),F.Screen->IsRecipeMenuMode());TestTrue(TEXT("current order still same"),F.Screen->GetCurrentRecipe()==R);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuTouch,"CyberCafe.RecipeMenu.PhysicalKeysScreenRowsAndDebounce",RecipeMenuTest::MenuTestFlags)
bool FMenuTouch::RunTest(const FString&)
{
 RecipeMenuTest::World F;if(!F.Screen)return false;F.Begin();auto* S=F.Screen;const FVector Mode=S->MenuModeZone->GetComponentLocation();TestFalse(TEXT("holding hand cannot menu touch"),S->ProcessMenuTouchSample(EControllerHand::Left,Mode,true,true));TestTrue(TEXT("touch MENU"),S->ProcessMenuTouchSample(EControllerHand::Left,Mode));TestTrue(TEXT("opens menu"),S->IsRecipeMenuMode());TestFalse(TEXT("continuous contact not repeating"),S->ProcessMenuTouchSample(EControllerHand::Left,Mode));const FVector Away=Mode+FVector(100,100,100);S->ProcessMenuTouchSample(EControllerHand::Left,Away);S->ProcessMenuTouchSample(EControllerHand::Right,Away);F.Tick(20);const int Before=S->GetSelectedMenuIndex();TestTrue(TEXT("NEXT original hand geometry"),S->ProcessMenuTouchSample(EControllerHand::Right,S->MenuNextZone->GetComponentLocation()));TestEqual(TEXT("advanced once"),S->GetSelectedMenuIndex(),(Before+1)%10);S->ProcessMenuTouchSample(EControllerHand::Right,Away);S->ProcessMenuTouchSample(EControllerHand::Left,Away);
 for(int I=0;I<10;++I){const FVector Local(0,600-180,375-(134+I*48+24));const FVector Point=S->Panel->GetComponentTransform().TransformPosition(Local);TestTrue(TEXT("actual panel index row hit"),S->ProcessMenuTouchSample(EControllerHand::Left,Point));TestEqual(TEXT("selected visual index row"),S->GetSelectedMenuIndex(),I);TestFalse(TEXT("second hand same contact cannot double select"),S->ProcessMenuTouchSample(EControllerHand::Right,Point));S->ProcessMenuTouchSample(EControllerHand::Left,Away);S->ProcessMenuTouchSample(EControllerHand::Right,Away);}S->PressButton();TestFalse(TEXT("original power closes"),S->IsPanelOpen());TestFalse(TEXT("cannot page when closed"),S->BrowseRecipeMenu(1));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPresentation,"CyberCafe.RecipeMenu.WidgetTwoPagesExactRecipeLines",RecipeMenuTest::MenuTestFlags)
bool FMenuPresentation::RunTest(const FString&)
{
 RecipeMenuTest::World F;if(!F.Screen)return false;F.Begin();auto* S=F.Screen;auto* Widget=S->GetDisplayWidget();if(!TestNotNull(TEXT("real saved WBP instance"),Widget))return false;auto* Switch=Cast<UWidgetSwitcher>(Widget->WidgetTree->FindWidget(TEXT("OrderMenuSwitcher")));if(!TestNotNull(TEXT("real UMG page switcher"),Switch))return false;TestEqual(TEXT("exact 2 pages"),Switch->GetChildrenCount(),2);
 for(const auto& E:S->RecipeMenu->Entries){TestTrue(TEXT("index select"),S->SelectRecipeByPinyin(E.PinyinIndex));TestEqual(TEXT("menu UMG shown"),Switch->GetActiveWidgetIndex(),1);const FString Text=Widget->GetMenuText().ToString();TestTrue(TEXT("index + name"),Text.Contains(E.PinyinIndex.ToString())&&Text.Contains(E.DisplayName.ToString()));TestTrue(TEXT("cup name"),Text.Contains(E.CupName.ToString()));for(const auto& P:E.Ingredients)TestTrue(TEXT("exact ingredient amount displayed"),Text.Contains(UDrinkMenuData::IngredientDisplayName(P.IngredientId).ToString())&&Text.Contains(FString::Printf(TEXT("%.0f mL"),P.VolumeML)));}
 S->ToggleRecipeMenu();TestEqual(TEXT("original order UMG shown"),Switch->GetActiveWidgetIndex(),0);TestTrue(TEXT("old order text remains usable"),!Widget->GetOrderText().IsEmpty());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuRecipeEvaluate,"CyberCafe.RecipeMenu.AllDisplayedVolumesPassExistingRecipes",RecipeMenuTest::MenuTestFlags)
bool FMenuRecipeEvaluate::RunTest(const FString&)
{
 auto* D=RecipeMenuTest::Data();if(!D)return false;for(const auto& E:D->Entries){RecipeMenuTest::World F;auto* R=E.Recipe.Get();UClass* CupClass=R->AcceptedCupClass?R->AcceptedCupClass.Get():LoadClass<ACupActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup01.BP_Cup01_C"));auto* C=F.W->SpawnActorDeferred<ALiquidContainerActor>(CupClass,FTransform(FVector(0,0,200)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);C->ContainerMesh->SetSimulatePhysics(false);C->LiquidFXTemplate=nullptr;C->LiquidFX->SetAsset(nullptr);C->PourFX->SetAsset(nullptr);C->GlassBreakProfile=nullptr;C->FinishSpawning(FTransform(FVector(0,0,200)));F.Begin();C->bCanPour=false;for(const auto& I:E.Ingredients)C->AddIngredientLiquid(I.VolumeML,I.IngredientId,FLinearColor::White);TestTrue(FString::Printf(TEXT("%s menu recipe accepts actual mixture"),*E.PinyinIndex.ToString()),R->Evaluate(C).bAccepted);C->ConsumeLiquid(5);C->AddIngredientLiquid(5,TEXT("ForbiddenIngredient"),FLinearColor::White);TestFalse(TEXT("extra ingredient still rejects"),R->Evaluate(C).bAccepted);}return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuOptIn,"CyberCafe.RecipeMenu.UnconfiguredScreenPreservesOldPower",RecipeMenuTest::MenuTestFlags)
bool FMenuOptIn::RunTest(const FString&)
{
 RecipeMenuTest::World F;if(!F.Screen)return false;F.Screen->RecipeMenu=nullptr;F.Begin();TestFalse(TEXT("no menu no toggle"),F.Screen->ToggleRecipeMenu());TestFalse(TEXT("no menu no index"),F.Screen->SelectRecipeByPinyin(TEXT("MYNT")));F.Screen->PressButton();TestTrue(TEXT("old power still opens"),F.Screen->IsPanelOpen());F.Screen->PressButton();TestFalse(TEXT("old power still closes"),F.Screen->IsPanelOpen());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuAcceptedOrder,"CyberCafe.RecipeMenu.AcceptedOrderWhileBrowsingKeepsMenuAndHistory",RecipeMenuTest::MenuTestFlags)
bool FMenuAcceptedOrder::RunTest(const FString&)
{
 RecipeMenuTest::World F;if(!F.Screen)return false;auto* Recipe=LoadObject<UDrinkRecipe>(nullptr,TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));auto* Spot=F.W->SpawnActor<AServingSpotActor>();Spot->SetActiveRecipe(Recipe);Spot->SetSubmissionEnabled(false);auto* CupClass=LoadClass<ACupActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup01.BP_Cup01_C"));auto* Cup=F.W->SpawnActorDeferred<ACupActor>(CupClass,FTransform(Spot->GetActorLocation()),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Cup->ContainerMesh->SetSimulatePhysics(false);Cup->LiquidFXTemplate=nullptr;Cup->LiquidFX->SetAsset(nullptr);Cup->PourFX->SetAsset(nullptr);Cup->GlassBreakProfile=nullptr;Cup->FinishSpawning(FTransform(Spot->GetActorLocation()));F.Begin();Cup->bCanPour=false;Cup->AddIngredientLiquid(100,TEXT("Coffee"),FLinearColor::White);Cup->AddIngredientLiquid(100,TEXT("Milk"),FLinearColor::White);Spot->SetSubmissionEnabled(true);TestTrue(TEXT("original order setup"),F.Screen->BeginOrder(Recipe,Spot,nullptr,FText::GetEmpty()));TestTrue(TEXT("reading MYNT recipe"),F.Screen->SelectRecipeByPinyin(TEXT("MYNT")));TestTrue(TEXT("original latte submitted"),Spot->TrySubmitCup(Cup));TestTrue(TEXT("delivery event still reaches screen"),F.Screen->IsDelivered());TestEqual(TEXT("one real delivery record"),F.Screen->GetDeliveryRecords().Num(),1);F.Tick(150);TestTrue(TEXT("player reading menu not auto closed"),F.Screen->IsPanelOpen()&&F.Screen->IsRecipeMenuMode());TestEqual(TEXT("selected index retained through delivery"),F.Screen->GetSelectedMenuEntry()->PinyinIndex,FName(TEXT("MYNT")));TestTrue(TEXT("return to delivered original order"),F.Screen->ToggleRecipeMenu());TestTrue(TEXT("same served cup stays frozen"),Cup->IsLiquidFrozen());TestEqual(TEXT("history not duplicated by browsing"),F.Screen->GetDeliveryRecords().Num(),1);return true;
}
#endif
