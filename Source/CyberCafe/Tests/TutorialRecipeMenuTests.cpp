// @trace MT-01 MT-02 MT-03. 使用真实 World 和公开手采样路径，不声称头显实测。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tutorial/Steps/TutorialStep_WaitForRecipeMenu.h"
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "Tutorial/TutorialDirector.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "UI/OrderHologramWidget.h"
#include "Components/Image.h"
#include "Components/WidgetComponent.h"
#include "Components/BoxComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "GameFramework/WorldSettings.h"
#include "CoreGlobals.h"

namespace TutorialMenuTests
{
constexpr auto TestOptions = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FWorld
{
    UWorld* World;
    UGameInstance* GI;
    AOrderHologramActor* Screen;
    ATutorialDirector* Director;
    UTutorialStep_WaitForRecipeMenu* Step;

    FWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
        auto* Class = LoadClass<AOrderHologramActor>(nullptr,
            TEXT("/Game/VRTemplate/Blueprints/Serving/BP_OrderHologram.BP_OrderHologram_C"));
        Screen = Class ? World->SpawnActor<AOrderHologramActor>(Class,
            FTransform(FRotator(0, 35, 0), FVector(25, 80, 100))) : nullptr;
        Director = World->SpawnActor<ATutorialDirector>();
        Director->bAutoStart = false;
        Director->CheerDuration = 0;
        Director->FinishedText = FText::GetEmpty();
        Step = NewObject<UTutorialStep_WaitForRecipeMenu>(Director);
        Step->OrderHologram = Screen;
        Step->RequiredPinyinIndex = TEXT("BNT");
        Step->CompletedText = FText::GetEmpty();
        Director->Steps.Add(Step);
    }
    void Begin()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        Director->StartTutorial();
    }
    void Tick(int32 N = 1)
    {
        for (int32 I = 0; I < N; ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, .02); }
    }
    bool TouchPower()
    {
        const FVector Away = Screen->GetActorLocation() + FVector(500, 500, 500);
        Screen->ProcessTouchSample(EControllerHand::Left, Away);
        Screen->ProcessTouchSample(EControllerHand::Right, Away);
        return Screen->ProcessTouchSample(EControllerHand::Left, Screen->ButtonZone->GetComponentLocation());
    }
    ~FWorld()
    {
        Director->SkipTutorial();
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuSaved, "CyberCafe.TutorialMenu.SavedEightStepsAndOriginalOrder", TutorialMenuTests::TestOptions)
bool FTutorialMenuSaved::RunTest(const FString&)
{
    auto* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
    if (!TestNotNull(TEXT("保存的主地图"), Map)) return false;
    ATutorialDirector* D = nullptr;
    for (const auto& A : Map->PersistentLevel->Actors)
        if (auto* Candidate = Cast<ATutorialDirector>(A.Get())) D = Candidate;
    if (!TestNotNull(TEXT("保存的教学导演"), D)) return false;
    TestEqual(TEXT("恰好八步"), D->Steps.Num(), 8);
    if (D->Steps.Num() != 8) return false;
    const TCHAR* Names[] = { TEXT("TutorialStep_ShowMessage_0"), TEXT("TutorialStep_ShowMessage_1"),
        TEXT("TutorialStep_ShowMessage_2"), TEXT("TutorialStep_ShowMessage_3"),
        TEXT("TutorialStep_ShowMessage_Menu_0"), TEXT("TutorialStep_WaitForRecipeMenu_0"),
        TEXT("TutorialStep_WaitForServe_0"), TEXT("TutorialStep_ConsumeServedDrink_0") };
    for (int32 I = 0; I < 8; ++I)
    {
        if (!TestNotNull(TEXT("保存步骤非空"), D->Steps[I].Get())) return false;
        TestEqual(TEXT("逐项身份与顺序不变"), D->Steps[I]->GetName(), FString(Names[I]));
        TestTrue(TEXT("内联 Outer 仍为原导演"), D->Steps[I]->GetOuter() == D);
        TestFalse(TEXT("每步提示可用"), D->Steps[I]->HintText.IsEmpty());
    }
    auto* FirstOrder = Cast<UTutorialStep_ShowMessage>(D->Steps[1]);
    auto* Intro = Cast<UTutorialStep_ShowMessage>(D->Steps[4]);
    auto* Menu = Cast<UTutorialStep_WaitForRecipeMenu>(D->Steps[5]);
    auto* Serve = Cast<UTutorialStep_WaitForServe>(D->Steps[6]);
    if (!TestNotNull(TEXT("首单对话类型"), FirstOrder) || !TestNotNull(TEXT("菜单介绍类型"), Intro)
        || !TestNotNull(TEXT("真实菜单操作步骤"), Menu) || !TestNotNull(TEXT("原交付步骤"), Serve)) return false;
    TestEqual(TEXT("批准的新对话全文"), Intro->HintText.ToString(),
        FString(TEXT("菜单上点一下冰拿铁，就能看怎么做。")));
    TestTrue(TEXT("介绍保留逐字和输入跳过"), Intro->bUseTypewriter && Intro->bAllowSkipByInput);
    TestEqual(TEXT("必选索引为 BNT"), Menu->RequiredPinyinIndex, FName(TEXT("BNT")));
    if (!TestNotNull(TEXT("保存菜单屏"), Menu->OrderHologram.Get())
        || !TestNotNull(TEXT("保存交付屏"), Serve->OrderHologram.Get())
        || !TestNotNull(TEXT("保存交付配方"), Serve->RequiredRecipe.Get())) return false;
    TestTrue(TEXT("仍使用原地图同一屏幕"), Menu->OrderHologram == Serve->OrderHologram
        && Menu->OrderHologram->GetWorld() == Map);
    TestEqual(TEXT("首单对话引用精确匹配原菜单屏"), FirstOrder->FirstOrderMenu.Get(), Menu->OrderHologram.Get());
    TestEqual(TEXT("首单对话引用精确匹配原交付配方"), FirstOrder->FirstOrderRecipe.Get(), Serve->RequiredRecipe.Get());
    for (int32 I : {0, 2, 3, 4})
    {
        auto* Message = Cast<UTutorialStep_ShowMessage>(D->Steps[I]);
        if (!TestNotNull(TEXT("其余对话仍为 ShowMessage"), Message)) return false;
        TestNull(TEXT("其余对话不配置首单菜单引用"), Message->FirstOrderMenu.Get());
        TestNull(TEXT("其余对话不配置首单配方引用"), Message->FirstOrderRecipe.Get());
    }
    auto* Data = Menu->OrderHologram->RecipeMenu.Get();
    if (!TestNotNull(TEXT("保存菜单数据"), Data) || !TestTrue(TEXT("保存菜单有效"), Data->IsValidMenu())) return false;
    const int32 Index = Data->FindPinyinIndex(TEXT("BNT"));
    TestEqual(TEXT("BNT 重排到第零项"), Index, 0);
    if (!Data->Entries.IsValidIndex(Index)) return false;
    const auto& Entry = Data->Entries[Index];
    TestEqual(TEXT("BNT 与原交付引用同一 DA"), Entry.Recipe.Get(), Serve->RequiredRecipe.Get());
    TestEqual(TEXT("交付仍引用原 DA_Latte"), GetPathNameSafe(Serve->RequiredRecipe),
        FString(TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
    TestEqual(TEXT("权威 RecipeId 不变"), Serve->RequiredRecipe->RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("菜单显示冰拿铁"), Entry.DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("配方显示冰拿铁"), Serve->RequiredRecipe->DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("固定保存 Highball 杯类"), GetPathNameSafe(Serve->RequiredRecipe->AcceptedCupClass.Get()),
        FString(TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C")));
    TestEqual(TEXT("旧加冰建议值保留"), Serve->RequiredRecipe->ExpectedIceCount, 3);
    TestEqual(TEXT("原目标仍为 200 mL"), Serve->RequiredRecipe->TargetVolumeML, 200.0);
    TestTrue(TEXT("交付后紧接消费步骤"), Cast<UTutorialStep_ConsumeServedDrink>(D->Steps[7]) != nullptr);
    TestEqual(TEXT("时零身份保留"), D->DialogueSpeakerName.ToString(), FString(TEXT("时零")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuPhysical, "CyberCafe.TutorialMenu.HandInputFindBNTLeavesRecipeOpen", TutorialMenuTests::TestOptions)
bool FTutorialMenuPhysical::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;
    if (!TestNotNull(TEXT("真实保存屏幕"), F.Screen)) return false;
    F.Begin();
    auto* S = F.Screen;
    auto* Step = F.Step;
    if (!TestNotNull(TEXT("真实菜单数据"), S->RecipeMenu.Get())) return false;
    TestFalse(TEXT("无操作不完成"), Step->IsCompleted());
    F.Tick(100);
    TestFalse(TEXT("等待或阅读不完成"), Step->IsCompleted());
    const FVector Power = S->ButtonZone->GetComponentLocation();
    TestFalse(TEXT("持物手不能开电源"), S->ProcessTouchSample(EControllerHand::Left, Power, true, true));
    TestFalse(TEXT("未跟踪手不能开电源"), S->ProcessTouchSample(EControllerHand::Left, Power, false, false));
    TestFalse(TEXT("拒绝的电源输入未开屏"), S->IsPanelOpen());
    TestFalse(TEXT("拒绝的输入无教学进度"), Step->HasOpenedMenu());
    TestFalse(TEXT("关闭时不能点击 BNT"), TutorialMenuTestHelpers::TouchAction(S, 100));
    if (!TestTrue(TEXT("实体电源真实开屏"), F.TouchPower())) return false;
    TestTrue(TEXT("电源打开配方页"), S->IsPanelOpen() && S->IsRecipeMenuMode());
    TestEqual(TEXT("保留的默认选择就是 BNT"), S->GetSelectedMenuIndex(), 0);
    TestFalse(TEXT("只开电源不算菜单动作"), Step->HasOpenedMenu());
    TestFalse(TEXT("默认 BNT 不算主动点选"), Step->HasViewedRequiredRecipe());
    TestFalse(TEXT("只开电源不能完成"), Step->IsCompleted());
    TestFalse(TEXT("未展开动画不能点选"), TutorialMenuTestHelpers::TouchAction(S, 100));
    F.Tick(20);
    const FIntRect Rect = UOrderHologramWidget::GetScreenActionRect(100);
    const FVector Point = S->Panel->GetComponentTransform().TransformPosition(FVector(0,
        600 - (Rect.Min.X + Rect.Max.X) * .5, 375 - (Rect.Min.Y + Rect.Max.Y) * .5));
    TestFalse(TEXT("持物手不能按屏内按钮"), S->ProcessMenuTouchSample(EControllerHand::Left, Point, true, true));
    TestFalse(TEXT("未跟踪手不能按屏内按钮"), S->ProcessMenuTouchSample(EControllerHand::Left, Point, false, false));
    TestFalse(TEXT("屏幕拒绝输入仍无进度"), Step->HasOpenedMenu());
    TestFalse(TEXT("屏幕拒绝输入不能完成"), Step->IsCompleted());
    TestTrue(TEXT("订单页签接受真实输入"), TutorialMenuTestHelpers::TouchAction(S, 0));
    TestFalse(TEXT("订单页签不完成"), Step->IsCompleted());
    TestTrue(TEXT("配方页签接受真实输入"), TutorialMenuTestHelpers::TouchAction(S, 1));
    TestTrue(TEXT("配方页打开被教学记录"), Step->HasOpenedMenu());
    TestFalse(TEXT("页签不能把默认 BNT 当成点选"), Step->HasViewedRequiredRecipe());
    TestFalse(TEXT("页签不完成"), Step->IsCompleted());
    const TArray<int32> Rows = S->GetVisibleMenuIndices();
    const int32 OtherRow = Rows.IndexOfByKey(S->RecipeMenu->FindPinyinIndex(TEXT("CMHK")));
    if (!TestEqual(TEXT("CMHK 是实际可见第 1 行"), OtherRow, 1)) return false;
    TestTrue(TEXT("真实点击另一杯 CMHK"), TutorialMenuTestHelpers::TouchAction(S, 100 + OtherRow));
    TestEqual(TEXT("选中了 CMHK 而非默认 BNT"), S->GetSelectedMenuIndex(), S->RecipeMenu->FindPinyinIndex(TEXT("CMHK")));
    TestFalse(TEXT("其他饮品不算必选配方"), Step->HasViewedRequiredRecipe());
    TestFalse(TEXT("其他饮品不完成"), Step->IsCompleted());
    F.Tick(100);
    TestFalse(TEXT("读其他配方也不完成"), Step->IsCompleted());
    const int32 BNTRow = S->GetVisibleMenuIndices().IndexOfByKey(S->RecipeMenu->FindPinyinIndex(TEXT("BNT")));
    if (!TestEqual(TEXT("BNT 实际可见第 0 行"), BNTRow, 0)
        || !TestTrue(TEXT("真实 Rect 中心显式点击 BNT"), TutorialMenuTestHelpers::TouchAction(S, 100 + BNTRow))) return false;
    TestEqual(TEXT("教学由实际行点击完成"), S->GetLastScreenAction(), 100 + BNTRow);
    TestTrue(TEXT("目标配方已查看"), Step->HasViewedRequiredRecipe());
    TestTrue(TEXT("点 BNT 后立即完成，不需返回"), Step->IsCompleted());
    TestTrue(TEXT("完成时仍打开配方页"), S->IsPanelOpen() && S->IsRecipeMenuMode());
    auto* Widget = S->GetDisplayWidget();
    if (!TestNotNull(TEXT("实际显示控件"), Widget) || !TestNotNull(TEXT("实际控件树"), Widget->WidgetTree.Get())) return false;
    const FString Text = Widget->GetMenuText().ToString();
    TestTrue(TEXT("实际冰拿铁文字与冰量"), Text.Contains(TEXT("BNT")) && Text.Contains(TEXT("冰拿铁"))
        && Text.Contains(TEXT("加冰即可")) && Text.Contains(TEXT("200 mL")));
    TestTrue(TEXT("两种原料实际各显示 100 mL"), Text.Contains(TEXT("咖啡液    100 mL"))
        && Text.Contains(TEXT("牛奶    100 mL")));
    const auto* Entry = S->GetSelectedMenuEntry();
    if (!TestNotNull(TEXT("实际选择的冰拿铁条目"), Entry)) return false;
    TestTrue(TEXT("实际文字含指定杯型"), Text.Contains(Entry->CupName.ToString()));
    auto* CupIcon = Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("MenuCupIcon")));
    auto* IceIcon = Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("MenuIceIcon")));
    if (!TestNotNull(TEXT("真实杯型图控件"), CupIcon) || !TestNotNull(TEXT("真实冰量图控件"), IceIcon)
        || !TestNotNull(TEXT("保存杯型贴图"), Entry->CupIcon.Get())
        || !TestNotNull(TEXT("保存冰块贴图"), S->RecipeMenu->IceIcon.Get())) return false;
    TestTrue(TEXT("杯型图实际绑定保存贴图且可见"), CupIcon->GetBrush().GetResourceObject() == Entry->CupIcon.Get()
        && CupIcon->GetVisibility() == ESlateVisibility::HitTestInvisible);
    TestTrue(TEXT("三块冰图实际绑定保存贴图且可见"), IceIcon->GetBrush().GetResourceObject() == S->RecipeMenu->IceIcon.Get()
        && IceIcon->GetVisibility() == ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("无伪造交付历史"), S->GetDeliveryRecords().Num(), 0);
    TestFalse(TEXT("查配方没有创建订单"), S->IsPending());
    F.Tick(2);
    TestFalse(TEXT("完成后教学订阅已移除"), S->OnMenuInteractionNative.IsBound());
    TestTrue(TEXT("教学收尾不返回、不关配方页"), S->IsPanelOpen() && S->IsRecipeMenuMode());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuRestart, "CyberCafe.TutorialMenu.RestartSkipAndScriptSelectionDoNotFakeInput", TutorialMenuTests::TestOptions)
bool FTutorialMenuRestart::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;
    if (!TestNotNull(TEXT("真实保存屏幕"), F.Screen)) return false;
    F.Begin();
    TestTrue(TEXT("脚本选择 API 仍可用"), F.Screen->SelectRecipeByPinyin(TEXT("BNT")));
    TestTrue(TEXT("脚本切换订单页"), F.Screen->ToggleRecipeMenu());
    TestTrue(TEXT("脚本切换配方页"), F.Screen->ToggleRecipeMenu());
    TestTrue(TEXT("脚本浏览 API 仍可用"), F.Screen->BrowseRecipeMenu(1));
    TestTrue(TEXT("脚本重选 BNT"), F.Screen->SelectRecipeByPinyin(TEXT("BNT")));
    F.Tick(3);
    TestFalse(TEXT("脚本选择和浏览不算查看"), F.Step->HasViewedRequiredRecipe());
    TestFalse(TEXT("脚本开页不算打开"), F.Step->HasOpenedMenu());
    TestFalse(TEXT("脚本调用不完成教学"), F.Step->IsCompleted());
    F.Director->StartTutorial();
    TestFalse(TEXT("重启清空旧查看进度"), F.Step->HasViewedRequiredRecipe());
    TestFalse(TEXT("重启清空旧开页进度"), F.Step->HasOpenedMenu());
    TestTrue(TEXT("实体电源关闭脚本打开的屏"), F.TouchPower());
    TestFalse(TEXT("实体电源已关闭"), F.Screen->IsPanelOpen());
    TestTrue(TEXT("实体电源重新打开"), F.TouchPower());
    F.Tick(20);
    TestFalse(TEXT("重启后只开电源仍不完成"), F.Step->IsCompleted());
    TestTrue(TEXT("真实点击 BNT 完成"), TutorialMenuTestHelpers::FindRecipe(F.Screen, TEXT("BNT")));
    TestTrue(TEXT("已完成"), F.Step->IsCompleted());
    TestTrue(TEXT("helper 不自动返回"), F.Screen->IsPanelOpen() && F.Screen->IsRecipeMenuMode());
    F.Director->StartTutorial();
    TestFalse(TEXT("已完成步骤重启重置"), F.Step->IsCompleted());
    TestFalse(TEXT("保留选择不能充当查看"), F.Step->HasViewedRequiredRecipe());
    TestFalse(TEXT("保留开页不能充当打开"), F.Step->HasOpenedMenu());
    TestTrue(TEXT("再次用电源关闭保留屏幕"), F.TouchPower());
    TestTrue(TEXT("再次用电源真实开屏"), F.TouchPower());
    F.Tick(20);
    TestFalse(TEXT("重启后电源不能代替点击"), F.Step->IsCompleted());
    F.Director->SkipTutorial();
    TestFalse(TEXT("跳过移除旧订阅"), F.Screen->OnMenuInteractionNative.IsBound());
    const bool Before = F.Step->IsCompleted();
    TestTrue(TEXT("取消后真实行点击仍可操作屏幕"), TutorialMenuTestHelpers::FindRecipe(F.Screen, TEXT("BNT")));
    TestEqual(TEXT("取消后的真实事件不能完成旧步骤"), F.Step->IsCompleted(), Before);
    TestFalse(TEXT("取消后没有查看进度"), F.Step->HasViewedRequiredRecipe());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialMenuUnavailable, "CyberCafe.TutorialMenu.UnavailableMenuIsHonestAndRecoverable", TutorialMenuTests::TestOptions)
bool FTutorialMenuUnavailable::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;
    if (!TestNotNull(TEXT("真实保存屏幕"), F.Screen)) return false;
    auto* Data = F.Screen->RecipeMenu.Get();
    if (!TestNotNull(TEXT("恢复用原菜单"), Data) || !TestEqual(TEXT("恢复目标 BNT 在第零项"), Data->FindPinyinIndex(TEXT("BNT")), 0)) return false;
    F.Screen->RecipeMenu = nullptr;
    F.Begin();
    TestTrue(TEXT("缺菜单被识别"), F.Step->WasUnavailable());
    TestTrue(TEXT("缺配置不会卡死"), F.Step->IsCompleted());
    TestTrue(TEXT("诚实跳过而非宣称成功"), F.Step->CompletedText.ToString().Contains(TEXT("暂不可用")));
    F.Director->StartTutorial();
    TestTrue(TEXT("重复缺配置仍诚实跳过"), F.Step->WasUnavailable());
    F.Director->SkipTutorial();
    F.Screen->RecipeMenu = Data;
    F.Director->StartTutorial();
    TestFalse(TEXT("恢复为有效 BNT 配置"), F.Step->WasUnavailable());
    TestTrue(TEXT("恢复原完成文案"), F.Step->CompletedText.IsEmpty());
    TestFalse(TEXT("恢复配置本身不完成"), F.Step->IsCompleted());
    TestTrue(TEXT("恢复后真实电源开屏"), F.TouchPower());
    F.Tick(20);
    TestFalse(TEXT("恢复后只开屏仍不完成"), F.Step->IsCompleted());
    TestTrue(TEXT("恢复后真实 BNT 行点击"), TutorialMenuTestHelpers::FindRecipe(F.Screen, TEXT("BNT")));
    TestTrue(TEXT("恢复后可完成"), F.Step->IsCompleted());
    TestTrue(TEXT("恢复完成后配方仍打开"), F.Screen->IsPanelOpen() && F.Screen->IsRecipeMenuMode());
    TestEqual(TEXT("恢复过程不伪造交付"), F.Screen->GetDeliveryRecords().Num(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialFirstOrderReveal, "CyberCafe.TutorialMenu.FirstSpokenOrderAutoRevealAndDismissRespected", TutorialMenuTests::TestOptions)
bool FTutorialFirstOrderReveal::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;
    if (!TestNotNull(TEXT("真实屏幕"), F.Screen) || !TestNotNull(TEXT("实际菜单数据"), F.Screen->RecipeMenu.Get())) return false;
    auto* Recipe = F.Screen->RecipeMenu->Entries[0].Recipe.Get();
    auto* Intro = NewObject<UTutorialStep_ShowMessage>(F.Director);
    Intro->FirstOrderMenu = F.Screen; Intro->FirstOrderRecipe = Recipe;
    Intro->bUseTypewriter = false; Intro->bAllowSkipByInput = false; Intro->Duration = .1f;
    Intro->HintText = FText::GetEmpty(); F.Director->Steps.Insert(Intro, 0);
    auto* Spot = F.World->SpawnActor<AServingSpotActor>();
    Spot->SetActiveRecipe(Recipe); Spot->SetSubmissionEnabled(true);
    const auto Serial = Spot->GetOrderSerial();
    const FTransform CupTM(FRotator::ZeroRotator, FVector(200,0,150), TutorialMenuTestHelpers::SceneCupScale(Recipe->AcceptedCupClass.Get()));
    auto* Cup = F.World->SpawnActorDeferred<ACupActor>(Recipe->AcceptedCupClass.Get(), CupTM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("实际固定杯类"), Cup)) return false;
    Cup->ContainerMesh->SetSimulatePhysics(false); Cup->GlassBreakProfile=nullptr;
    Cup->LiquidFXTemplate=nullptr; Cup->LiquidFX->SetAsset(nullptr); Cup->PourFX->SetAsset(nullptr);
    Cup->FinishSpawning(CupTM); Cup->bCanPour=false;
    TestFalse(TEXT("首句前屏幕未开"), F.Screen->IsPanelOpen());
    F.Begin();
    TestTrue(TEXT("真实导演进入点单句自动开屏"), F.Screen->IsPanelOpen() && F.Screen->HasRevealedFirstOrderRecipe());
    TestTrue(TEXT("展示原冰拿铁配方页"), F.Screen->IsRecipeMenuMode() && F.Screen->GetSelectedMenuEntry()->Recipe.Get()==Recipe);
    TestFalse(TEXT("自动开屏不冒充玩家操作"), F.Screen->IsPlayerOpened());
    TestFalse(TEXT("提前展示不建立订单"), F.Screen->IsPending());
    TestEqual(TEXT("提前展示不改变服务单序号"), Spot->GetOrderSerial(), Serial);
    TestEqual(TEXT("自动展示不广播触屏动作"), F.Screen->GetLastScreenAction(), INDEX_NONE);
    F.Tick(30);
    TestFalse(TEXT("自动展示不完成配方教学"), F.Step->IsCompleted());
    TestFalse(TEXT("自动选中不算查看"), F.Step->HasViewedRequiredRecipe());
    TestTrue(TEXT("实际电源可收起自动屏"), F.TouchPower());
    TestFalse(TEXT("手动收屏生效"), F.Screen->IsPanelOpen());
    F.Director->StartTutorial(); F.Tick(30);
    TestFalse(TEXT("重启同局教程不再次强弹"), F.Screen->IsPanelOpen());
    TestFalse(TEXT("同局显式重复提示不再次展示"), F.Screen->RevealFirstOrderRecipe(Recipe));
    TestTrue(TEXT("后续实际BeginOrder正常建立"), F.Screen->BeginOrder(Recipe,Spot,nullptr,FText::GetEmpty()));
    TestFalse(TEXT("实际建单尊重已手动收起"), F.Screen->IsPanelOpen());
    F.Tick(100);
    TestFalse(TEXT("等待后也不重复强弹"), F.Screen->IsPanelOpen());
    TestTrue(TEXT("玩家仍可实际电源重开"), F.TouchPower()); F.Tick(20);
    TestTrue(TEXT("真实点饮品才完成教学"), TutorialMenuTestHelpers::FindRecipe(F.Screen,TEXT("BNT")));
    TestTrue(TEXT("真实教学完成"), F.Step->IsCompleted());
    Cup->AddIngredientLiquid(100,TEXT("Coffee"),FLinearColor::White); Cup->AddIngredientLiquid(100,TEXT("Milk"),FLinearColor::White);
    if (!TestTrue(TEXT("实际加入一块冰"), TutorialMenuTestHelpers::AddIce(Cup,1))) return false;
    FTransform ServeTM=Spot->GetActorTransform(); ServeTM.SetScale3D(Cup->GetActorScale3D()); Cup->SetActorTransform(ServeTM);
    if (!TestTrue(TEXT("同杯200液加1冰实际提交成功"), Spot->TrySubmitCup(Cup))) return false;
    TestEqual(TEXT("交付快照实际1冰"), Spot->GetLastValidation().IceCount,1);
    F.Tick(150);
    TestTrue(TEXT("玩家主动阅读不被交付收屏"), F.Screen->IsPanelOpen() && F.Screen->IsPlayerOpened());
    if (!TestEqual(TEXT("只记一次实际历史"), F.Screen->GetDeliveryRecords().Num(),1)) return false;
    TestEqual(TEXT("历史仍实际200mL"), F.Screen->GetDeliveryRecords()[0].VolumeML,200.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTutorialAutoRevealOptional, "CyberCafe.TutorialMenu.AutoRevealOptionalAndNextOrderCompatible", TutorialMenuTests::TestOptions)
bool FTutorialAutoRevealOptional::RunTest(const FString&)
{
    TutorialMenuTests::FWorld F;
    if (!TestNotNull(TEXT("真实屏幕"), F.Screen) || !TestNotNull(TEXT("实际菜单数据"), F.Screen->RecipeMenu.Get())) return false;
    auto* Recipe = F.Screen->RecipeMenu->Entries[0].Recipe.Get();
    F.Begin();
    auto* Data=F.Screen->RecipeMenu.Get(); F.Screen->RecipeMenu=nullptr;
    TestFalse(TEXT("缺菜单不伪称已展示"), F.Screen->RevealFirstOrderRecipe(Recipe));
    TestFalse(TEXT("无配置不设置首次已展示"), F.Screen->HasRevealedFirstOrderRecipe());
    F.Screen->RecipeMenu=Data;
    TestTrue(TEXT("恢复有效配置可首次展示"), F.Screen->RevealFirstOrderRecipe(Recipe)); F.Tick(20);
    TestTrue(TEXT("电源收起预览"), F.TouchPower());
    auto* Other=Data->Entries[1].Recipe.Get(); auto* Spot=F.World->SpawnActor<AServingSpotActor>();
    Spot->SetActiveRecipe(Other); Spot->SetSubmissionEnabled(true);
    TestTrue(TEXT("非首杯其他订单仍可正常建立"), F.Screen->BeginOrder(Other,Spot,nullptr,FText::GetEmpty()));
    TestTrue(TEXT("其他订单保原自动开屏行为"), F.Screen->IsPanelOpen() && F.Screen->HasAutoOpened() && !F.Screen->IsPlayerOpened());
    return true;
}
#endif
