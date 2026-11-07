// @trace MT-01 MT-02 MT-03. Real hand input and saved tutorial, no HMD claim.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tutorial/Steps/TutorialStep_WaitForRecipeMenu.h"
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "Tutorial/TutorialDirector.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "UI/OrderHologramWidget.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "Components/WidgetComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "GameFramework/WorldSettings.h"
#include "CoreGlobals.h"
namespace TutorialMenuTests
{
constexpr auto TestOptions=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
struct FWorld
{
    UWorld* World;UGameInstance* GI;AOrderHologramActor* Screen;ATutorialDirector* Director;UTutorialStep_WaitForRecipeMenu* Step;
    FWorld()
    {
        UWorld::InitializationValues IV;IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::SM5,&IV);
        auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
        GI=NewObject<UGameInstance>(GEngine);Context.OwningGameInstance=GI;World->SetGameInstance(GI);GI->Init();
        auto* Class=LoadClass<AOrderHologramActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Serving/BP_OrderHologram.BP_OrderHologram_C"));
        Screen=Class?World->SpawnActor<AOrderHologramActor>(Class,FTransform(FRotator(0,35,0),FVector(25,80,100))):nullptr;
        Director=World->SpawnActor<ATutorialDirector>();Director->bAutoStart=false;Director->CheerDuration=0;Director->FinishedText=FText::GetEmpty();
        Step=NewObject<UTutorialStep_WaitForRecipeMenu>(Director);Step->OrderHologram=Screen;Step->RequiredPinyinIndex=TEXT("YWNT");Step->CompletedText=FText::GetEmpty();Director->Steps.Add(Step);
    }
    void Begin(){World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();Director->StartTutorial();}
    void Tick(int32 N=1){for(int32 I=0;I<N;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.02);}}
    ~FWorld(){Director->SkipTutorial();if(World->HasBegunPlay())World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuSaved,"CyberCafe.TutorialMenu.SavedEightStepsAndOriginalOrder",TutorialMenuTests::TestOptions)
bool FTutorialMenuSaved::RunTest(const FString&)
{
    auto* Map=LoadObject<UWorld>(nullptr,TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));if(!TestNotNull(TEXT("saved map"),Map))return false;
    ATutorialDirector* D=nullptr;for(const auto& A:Map->PersistentLevel->Actors)if(auto* Candidate=Cast<ATutorialDirector>(A.Get()))D=Candidate;
    if(!TestNotNull(TEXT("saved director"),D))return false;TestEqual(TEXT("8 steps"),D->Steps.Num(),8);if(D->Steps.Num()!=8)return false;
    const TCHAR* Names[]={TEXT("TutorialStep_ShowMessage_0"),TEXT("TutorialStep_ShowMessage_1"),TEXT("TutorialStep_ShowMessage_2"),TEXT("TutorialStep_ShowMessage_3"),TEXT("TutorialStep_ShowMessage_Menu_0"),TEXT("TutorialStep_WaitForRecipeMenu_0"),TEXT("TutorialStep_WaitForServe_0"),TEXT("TutorialStep_ConsumeServedDrink_0")};
    for(int32 I=0;I<8;++I)
    {
        if(!TestNotNull(TEXT("saved step is not null"),D->Steps[I].Get()))return false;
        TestEqual(TEXT("exact step identity/order"),D->Steps[I]->GetName(),FString(Names[I]));
        TestTrue(TEXT("inline outer retained"),D->Steps[I]->GetOuter()==D);TestFalse(TEXT("all hints available"),D->Steps[I]->HintText.IsEmpty());
    }
    auto* Intro=Cast<UTutorialStep_ShowMessage>(D->Steps[4]);auto* Menu=Cast<UTutorialStep_WaitForRecipeMenu>(D->Steps[5]);auto* Serve=Cast<UTutorialStep_WaitForServe>(D->Steps[6]);
    if(!TestNotNull(TEXT("menu intro type"),Intro)||!TestNotNull(TEXT("real menu action step"),Menu)||!TestNotNull(TEXT("original serve"),Serve))return false;
    TestEqual(TEXT("approved dialogue"),Intro->HintText.ToString(),FString(TEXT("配方记不住就看菜单。像原味拿铁，按名字的拼音首字母找 YWNT 就行。\n先找出来看看，我等你。")));
    TestTrue(TEXT("menu intro typewriter and input"),Intro->bUseTypewriter&&Intro->bAllowSkipByInput);
    TestEqual(TEXT("required full-name index"),Menu->RequiredPinyinIndex,FName(TEXT("YWNT")));
    if(!TestNotNull(TEXT("saved menu screen"),Menu->OrderHologram.Get())||!TestNotNull(TEXT("saved serving screen"),Serve->OrderHologram.Get()))return false;
    TestTrue(TEXT("same original screen"),Menu->OrderHologram==Serve->OrderHologram&&Menu->OrderHologram->GetWorld()==Map);
    auto* Entry=Menu->OrderHologram->RecipeMenu.Get();
    if(!TestNotNull(TEXT("saved menu"),Entry)||!TestTrue(TEXT("valid YWNT index"),Entry->Entries.IsValidIndex(Entry->FindPinyinIndex(TEXT("YWNT")))))return false;
    TestEqual(TEXT("index remains YWNT->Latte"),Entry->Entries[Entry->FindPinyinIndex(TEXT("YWNT"))].Recipe.Get(),Serve->RequiredRecipe.Get());
    TestEqual(TEXT("order remains original Latte"),GetPathNameSafe(Serve->RequiredRecipe),FString(TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
    TestTrue(TEXT("consume follows immediately"),Cast<UTutorialStep_ConsumeServedDrink>(D->Steps[7])!=nullptr);
    TestEqual(TEXT("identity remains ShiLing"),D->DialogueSpeakerName.ToString(),FString(TEXT("时零")));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuPhysical,"CyberCafe.TutorialMenu.HandInputFindYWNTAndReturn",TutorialMenuTests::TestOptions)
bool FTutorialMenuPhysical::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;if(!TestNotNull(TEXT("actual saved screen"),F.Screen))return false;F.Begin();auto* S=F.Screen;auto* Step=F.Step;
    TestFalse(TEXT("no action no completion"),Step->IsCompleted());F.Tick(100);TestFalse(TEXT("not completed by elapsed/reading"),Step->IsCompleted());
    TestFalse(TEXT("holding rejects touch"),S->ProcessMenuTouchSample(EControllerHand::Left,S->MenuModeZone->GetComponentLocation(),true,true));TestFalse(TEXT("holding no stage"),Step->HasOpenedMenu());
    TestTrue(TEXT("actual MENU opens"),TutorialMenuTestHelpers::TouchKey(S,S->MenuModeZone));TestTrue(TEXT("opened tracked"),Step->HasOpenedMenu());TestFalse(TEXT("opening other default drink not target"),Step->HasViewedRequiredRecipe());
    TestTrue(TEXT("early return"),TutorialMenuTestHelpers::TouchKey(S,S->MenuModeZone));TestFalse(TEXT("early return does not complete"),Step->IsCompleted());
    TestTrue(TEXT("open again"),TutorialMenuTestHelpers::TouchKey(S,S->MenuModeZone));F.Tick(20);
    const int32 Row=S->RecipeMenu->FindPinyinIndex(TEXT("YWNT"));const FVector Away=S->GetActorLocation()+FVector(500,500,500);S->ProcessMenuTouchSample(EControllerHand::Left,Away);S->ProcessMenuTouchSample(EControllerHand::Right,Away);
    const FVector Point=S->Panel->GetComponentTransform().TransformPosition(FVector(0,420,375-(134+48*Row+24)));
    TestTrue(TEXT("real YWNT panel row touch"),S->ProcessMenuTouchSample(EControllerHand::Left,Point));TestTrue(TEXT("target viewed"),Step->HasViewedRequiredRecipe());TestFalse(TEXT("still must return to order"),Step->IsCompleted());
    const FString Text=S->GetDisplayWidget()->GetMenuText().ToString();TestTrue(TEXT("actual recipe text shown"),Text.Contains(TEXT("YWNT"))&&Text.Contains(TEXT("原味拿铁"))&&Text.Contains(TEXT("100 mL")));
    TestTrue(TEXT("MENU returns"),TutorialMenuTestHelpers::TouchKey(S,S->MenuModeZone));TestTrue(TEXT("completed only after full sequence"),Step->IsCompleted());TestFalse(TEXT("returned to order page"),S->IsRecipeMenuMode());TestEqual(TEXT("no fake history"),S->GetDeliveryRecords().Num(),0);TestFalse(TEXT("menu has not created an order"),S->IsPending());
    F.Tick(2);TestFalse(TEXT("subscription removed"),S->OnMenuInteractionNative.IsBound());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuRestart,"CyberCafe.TutorialMenu.RestartSkipAndScriptSelectionDoNotFakeInput",TutorialMenuTests::TestOptions)
bool FTutorialMenuRestart::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;if(!F.Screen)return false;F.Begin();
    F.Screen->SelectRecipeByPinyin(TEXT("YWNT"));F.Tick(3);TestFalse(TEXT("script selection no progress"),F.Step->HasViewedRequiredRecipe());TestFalse(TEXT("script opening no progress"),F.Step->HasOpenedMenu());
    F.Director->StartTutorial();TestFalse(TEXT("restart clears old progress"),F.Step->HasViewedRequiredRecipe());
    TestTrue(TEXT("physical full tutorial"),TutorialMenuTestHelpers::FindAndReturn(F.Screen,TEXT("YWNT")));TestTrue(TEXT("completed"),F.Step->IsCompleted());
    F.Director->StartTutorial();TestFalse(TEXT("restart completed step resets"),F.Step->IsCompleted());TestFalse(TEXT("retained selection alone not a view"),F.Step->HasViewedRequiredRecipe());
    F.Director->SkipTutorial();TestFalse(TEXT("skip removes old listener"),F.Screen->OnMenuInteractionNative.IsBound());const bool Before=F.Step->IsCompleted();TutorialMenuTestHelpers::FindAndReturn(F.Screen,TEXT("YWNT"));TestEqual(TEXT("late events cannot finish cancelled step"),F.Step->IsCompleted(),Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuUnavailable,"CyberCafe.TutorialMenu.UnavailableMenuIsHonestAndRecoverable",TutorialMenuTests::TestOptions)
bool FTutorialMenuUnavailable::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;if(!F.Screen)return false;auto* Data=F.Screen->RecipeMenu.Get();F.Screen->RecipeMenu=nullptr;F.Begin();
    TestTrue(TEXT("missing menu recognized"),F.Step->WasUnavailable());TestTrue(TEXT("does not deadlock"),F.Step->IsCompleted());TestTrue(TEXT("honest skip not success"),F.Step->CompletedText.ToString().Contains(TEXT("暂不可用")));
    F.Director->StartTutorial();TestTrue(TEXT("repeated unavailable honest"),F.Step->WasUnavailable());F.Director->SkipTutorial();F.Screen->RecipeMenu=Data;F.Director->StartTutorial();TestFalse(TEXT("recovered valid configuration"),F.Step->WasUnavailable());TestTrue(TEXT("original completion text restored"),F.Step->CompletedText.IsEmpty());
    TestTrue(TEXT("real actions after recovery"),TutorialMenuTestHelpers::FindAndReturn(F.Screen,TEXT("YWNT")));TestTrue(TEXT("recovery completion"),F.Step->IsCompleted());return true;
}
#endif
