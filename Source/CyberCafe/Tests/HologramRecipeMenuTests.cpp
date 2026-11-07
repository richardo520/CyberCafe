// @trace MN-01 MN-02 MN-03. 使用保存菜单与真实交互路径，不声称头显实测。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "UI/OrderHologramWidget.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "Liquid/CupActor.h"
#include "NiagaraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "CoreGlobals.h"

namespace RecipeMenuTest
{
constexpr auto MenuTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
UDrinkMenuData* Data()
{
    return LoadObject<UDrinkMenuData>(nullptr, TEXT("/Game/VRTemplate/MenuIngredients/Menu/DA_RecipeMenu.DA_RecipeMenu"));
}
struct World
{
    UWorld* W;
    UGameInstance* GI;
    AOrderHologramActor* Screen;
    World()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        W = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        auto& C = GEngine->CreateNewWorldContext(EWorldType::Game);
        C.SetCurrentWorld(W);
        GI = NewObject<UGameInstance>(GEngine);
        C.OwningGameInstance = GI;
        W->SetGameInstance(GI);
        GI->Init();
        auto* Class = LoadClass<AOrderHologramActor>(nullptr,
            TEXT("/Game/VRTemplate/Blueprints/Serving/BP_OrderHologram.BP_OrderHologram_C"));
        Screen = Class ? W->SpawnActor<AOrderHologramActor>(Class,
            FTransform(FRotator(0, 32, 0), FVector(30, 50, 100))) : nullptr;
    }
    ~World()
    {
        if (W->HasBegunPlay()) W->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown();
        GEngine->DestroyWorldContext(W);
        W->DestroyWorld(false);
    }
    void Begin() { W->InitializeActorsForPlay(FURL()); W->GetWorldSettings()->NotifyBeginPlay(); }
    void Tick(int N = 1)
    {
        for (int I = 0; I < N; ++I) { ++GFrameCounter; W->Tick(LEVELTICK_All, .02); }
    }
    bool TouchPower()
    {
        const FVector Away = Screen->GetActorLocation() + FVector(500, 500, 500);
        Screen->ProcessTouchSample(EControllerHand::Left, Away);
        Screen->ProcessTouchSample(EControllerHand::Right, Away);
        return Screen->ProcessTouchSample(EControllerHand::Left, Screen->ButtonZone->GetComponentLocation());
    }
    ACupActor* SpawnAcceptedCup(UDrinkRecipe* Recipe, const FVector& Position)
    {
        UClass* ActualAcceptedCupClass = Recipe->AcceptedCupClass.Get();
        if (!ActualAcceptedCupClass || !ActualAcceptedCupClass->IsChildOf(ACupActor::StaticClass())) return nullptr;
        const FTransform Transform(FRotator::ZeroRotator, Position, TutorialMenuTestHelpers::SceneCupScale(ActualAcceptedCupClass));
        auto* Cup = W->SpawnActorDeferred<ACupActor>(ActualAcceptedCupClass, Transform,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Cup) return nullptr;
        // 仅逻辑夹具禁用倒出、物理和特效；不改保存杯类、容量、冰量或 BP 根缩放。
        Cup->bCanPour = false;
        Cup->bEnableSplash = false;
        Cup->LiquidFXTemplate = nullptr;
        Cup->SplashEffectTemplate = nullptr;
        Cup->LiquidFX->SetAutoActivate(false);
        Cup->LiquidFX->SetAsset(nullptr);
        Cup->PourFX->SetAutoActivate(false);
        Cup->PourFX->SetAsset(nullptr);
        Cup->GlassBreakProfile = nullptr;
        Cup->ContainerMesh->SetSimulatePhysics(false);
        Cup->FinishSpawning(Transform);
        Cup->ContainerMesh->SetSimulatePhysics(false);
        Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return Cup;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuSavedData, "CyberCafe.RecipeMenu.SavedTenPinyinIndicesAndML", RecipeMenuTest::MenuTestFlags)
bool FMenuSavedData::RunTest(const FString&)
{
    auto* D = RecipeMenuTest::Data();
    if (!TestNotNull(TEXT("保存菜单数据"), D)) return false;
    TestTrue(TEXT("菜单完整校验"), D->IsValidMenu());
    TestEqual(TEXT("恰好十款"), D->Entries.Num(), 10);
    if (D->Entries.Num() != 10) return false;
    const TCHAR* Codes[] = { TEXT("BNT"), TEXT("CMHK"), TEXT("GJHY"), TEXT("JDMS"), TEXT("KFYH"),
        TEXT("MSXH"), TEXT("MYNT"), TEXT("SQJG"), TEXT("YGNT"), TEXT("YYBL") };
    int32 WithoutIce = 0, WithIce = 0;
    for (int32 I = 0; I < 10; ++I)
    {
        const auto& E = D->Entries[I];
        TestEqual(TEXT("精确排序的拼音索引"), E.PinyinIndex, FName(Codes[I]));
        TestEqual(TEXT("精确索引检索"), D->FindPinyinIndex(FName(Codes[I])), I);
        if (!TestNotNull(TEXT("保存条目权威配方"), E.Recipe.Get())) return false;
        double ML = 0;
        for (const auto& P : E.Ingredients) ML += P.VolumeML;
        TestTrue(TEXT("原料用量精确等于目标 mL"), FMath::IsNearlyEqual(ML, E.Recipe->TargetVolumeML, .001));
        UClass* CupClass = E.Recipe->AcceptedCupClass.Get();
        if (!TestNotNull(TEXT("固定 AcceptedCupBP，不允许默认杯兜底"), CupClass)) return false;
        TestTrue(TEXT("指定杯派生自 ACupActor"), CupClass->IsChildOf(ACupActor::StaticClass()));
        TestTrue(TEXT("指定的是保存菜单杯 BP"), CupClass->GetPathName().StartsWith(TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_"))
            && CupClass->GetPathName().EndsWith(TEXT("_C")));
        TestTrue(TEXT("冰量只能明确为 0 或 3"), E.Recipe->ExpectedIceCount == 0 || E.Recipe->ExpectedIceCount == 3);
        WithoutIce += E.Recipe->ExpectedIceCount == 0 ? 1 : 0;
        WithIce += E.Recipe->ExpectedIceCount == 3 ? 1 : 0;
    }
    TestTrue(TEXT("菜单覆盖无冰饮品"), WithoutIce > 0);
    TestTrue(TEXT("菜单覆盖三冰饮品"), WithIce > 0);
    const auto& BNT = D->Entries[0];
    TestEqual(TEXT("BNT 仍引用原 DA_Latte"), BNT.Recipe.Get(), LoadObject<UDrinkRecipe>(nullptr,
        TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
    TestEqual(TEXT("RecipeId 仍为 Latte"), BNT.Recipe->RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("菜单显示冰拿铁"), BNT.DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("权威配方显示冰拿铁"), BNT.Recipe->DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("冰拿铁要求三块冰"), BNT.Recipe->ExpectedIceCount, 3);
    TestEqual(TEXT("冰拿铁固定 Highball BP"), BNT.Recipe->AcceptedCupClass->GetPathName(),
        FString(TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C")));
    TestEqual(TEXT("未知索引不兜底"), D->FindPinyinIndex(TEXT("NONE")), INDEX_NONE);
    auto* Duplicate = NewObject<UDrinkMenuData>();
    Duplicate->Entries = D->Entries;
    Duplicate->Entries[1].PinyinIndex = Duplicate->Entries[0].PinyinIndex;
    TestFalse(TEXT("重复索引拒绝歧义菜单"), Duplicate->IsValidMenu());
    Duplicate->Entries = D->Entries;
    if (!TestTrue(TEXT("首杯原料非空"), !Duplicate->Entries[0].Ingredients.IsEmpty())) return false;
    Duplicate->Entries[0].Ingredients[0].VolumeML += 20;
    TestFalse(TEXT("陈旧错误用量仍被拒绝"), Duplicate->IsValidMenu());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuOrderUnchanged, "CyberCafe.RecipeMenu.BrowseDoesNotChangeActiveOrderOrAutoClose", RecipeMenuTest::MenuTestFlags)
bool FMenuOrderUnchanged::RunTest(const FString&)
{
    RecipeMenuTest::World F;
    if (!TestNotNull(TEXT("保存屏幕"), F.Screen)) return false;
    auto* R = LoadObject<UDrinkRecipe>(nullptr, TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));
    if (!TestNotNull(TEXT("原拿铁配方"), R)) return false;
    auto* Spot = F.W->SpawnActor<AServingSpotActor>();
    Spot->SetActiveRecipe(R);
    F.Begin();
    TestTrue(TEXT("原订单开始"), F.Screen->BeginOrder(R, Spot, nullptr, FText::GetEmpty()));
    const auto Serial = Spot->GetOrderSerial();
    TestFalse(TEXT("订单默认仍为订单页"), F.Screen->IsRecipeMenuMode());
    TestTrue(TEXT("脚本切菜单 API 保留"), F.Screen->ToggleRecipeMenu());
    TestTrue(TEXT("玩家主动开菜单阻止自动关闭"), F.Screen->IsPlayerOpened());
    for (int32 I = 0; I < 23; ++I) TestTrue(TEXT("原浏览 API 仍可向后翻"), F.Screen->BrowseRecipeMenu(1));
    TestTrue(TEXT("选 MYNT 仍有效"), F.Screen->SelectRecipeByPinyin(TEXT("MYNT")));
    const auto* Selected = F.Screen->GetSelectedMenuEntry();
    if (!TestNotNull(TEXT("真实 MYNT 条目"), Selected)) return false;
    TestEqual(TEXT("MYNT 索引保留"), Selected->PinyinIndex, FName(TEXT("MYNT")));
    TestTrue(TEXT("订单指针不被浏览替换"), F.Screen->GetCurrentRecipe() == R && Spot->ActiveRecipe == R);
    TestEqual(TEXT("订单序号不变"), Spot->GetOrderSerial(), Serial);
    TestTrue(TEXT("待交付状态不变"), F.Screen->IsPending());
    TestFalse(TEXT("错误索引仍拒绝"), F.Screen->SelectRecipeByPinyin(TEXT("UNKNOWN")));
    F.Tick(150);
    TestTrue(TEXT("阅读菜单时仍开屏"), F.Screen->IsPanelOpen());
    TestTrue(TEXT("原切换 API 可回订单页"), F.Screen->ToggleRecipeMenu());
    TestFalse(TEXT("订单页恢复"), F.Screen->IsRecipeMenuMode());
    TestTrue(TEXT("当前仍是原订单"), F.Screen->GetCurrentRecipe() == R);
    TestEqual(TEXT("浏览不伪造账本"), F.Screen->GetDeliveryRecords().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuTouch, "CyberCafe.RecipeMenu.PhysicalKeysScreenRowsAndDebounce", RecipeMenuTest::MenuTestFlags)
bool FMenuTouch::RunTest(const FString&)
{
    RecipeMenuTest::World F;
    if (!TestNotNull(TEXT("保存屏幕"), F.Screen)) return false;
    auto* S = F.Screen;
    auto* Recipe = LoadObject<UDrinkRecipe>(nullptr, TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));
    if (!TestNotNull(TEXT("当前订单配方"), Recipe) || !TestNotNull(TEXT("保存菜单"), S->RecipeMenu.Get())) return false;
    auto* Spot = F.W->SpawnActor<AServingSpotActor>();
    Spot->SetActiveRecipe(Recipe);
    F.Begin();
    const FVector Away = S->GetActorLocation() + FVector(500, 500, 500);
    const FVector Power = S->ButtonZone->GetComponentLocation();
    auto PowerAway = [&]()
    {
        S->ProcessTouchSample(EControllerHand::Left, Away);
        S->ProcessTouchSample(EControllerHand::Right, Away);
    };
    auto ScreenAway = [&]()
    {
        S->ProcessMenuTouchSample(EControllerHand::Left, Away);
        S->ProcessMenuTouchSample(EControllerHand::Right, Away);
    };
    TestFalse(TEXT("持物手不能按唯一电源"), S->ProcessTouchSample(EControllerHand::Left, Power, true, true));
    TestFalse(TEXT("未跟踪手不能按电源"), S->ProcessTouchSample(EControllerHand::Left, Power, false, false));
    TestFalse(TEXT("拒绝电源输入仍关闭"), S->IsPanelOpen());
    TestFalse(TEXT("关闭时屏内按钮无动作"), TutorialMenuTestHelpers::TouchAction(S, 100));
    TestFalse(TEXT("关闭时屏幕动作 API 拒绝"), S->ActivateScreenAction(1));
    for (auto* Zone : { S->MenuModeZone.Get(), S->MenuPreviousZone.Get(), S->MenuNextZone.Get() })
    {
        if (!TestNotNull(TEXT("旧输入区仅为序列化残留"), Zone)) return false;
        TestTrue(TEXT("旧三键无碰撞"), Zone->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        TestFalse(TEXT("旧三键不生成重叠"), Zone->GetGenerateOverlapEvents());
        TestFalse(TEXT("关闭时旧 MENU/PREV/NEXT 区无菜单动作"), S->ProcessMenuTouchSample(EControllerHand::Left, Zone->GetComponentLocation()));
        TestFalse(TEXT("旧三键不能代替电源"), S->ProcessTouchSample(EControllerHand::Left, Zone->GetComponentLocation()));
    }
    PowerAway();
    TestTrue(TEXT("真实电源手采样开屏"), S->ProcessTouchSample(EControllerHand::Left, Power));
    TestTrue(TEXT("电源默认开配方页"), S->IsPanelOpen() && S->IsRecipeMenuMode() && S->IsPlayerOpened());
    TestFalse(TEXT("电源连续接触不重复"), S->ProcessTouchSample(EControllerHand::Left, Power));
    TestFalse(TEXT("第二只手不能重复开关"), S->ProcessTouchSample(EControllerHand::Right, Power));
    S->ProcessTouchSample(EControllerHand::Left, Away);
    TestFalse(TEXT("只有左手离开不能解除右手连续锁"), S->ProcessTouchSample(EControllerHand::Right, Power));
    S->ProcessTouchSample(EControllerHand::Right, Away);
    TestTrue(TEXT("两手离开后电源重新武装"), S->ProcessTouchSample(EControllerHand::Right, Power));
    TestFalse(TEXT("重新武装后的右手真实关屏"), S->IsPanelOpen());
    TestFalse(TEXT("右手接触中左手不能重复开屏"), S->ProcessTouchSample(EControllerHand::Left, Power));
    PowerAway();
    TestTrue(TEXT("两手再次离开后左手可开屏"), S->ProcessTouchSample(EControllerHand::Left, Power));
    TestFalse(TEXT("投影未展开时不能点行"), TutorialMenuTestHelpers::TouchAction(S, 100));
    F.Tick(20);
    const FIntRect Rect = UOrderHologramWidget::GetScreenActionRect(100);
    const FVector Point = S->Panel->GetComponentTransform().TransformPosition(FVector(0,
        600 - (Rect.Min.X + Rect.Max.X) * .5, 375 - (Rect.Min.Y + Rect.Max.Y) * .5));
    TestFalse(TEXT("持物手不能点真实屏内行"), S->ProcessMenuTouchSample(EControllerHand::Left, Point, true, true));
    TestFalse(TEXT("未跟踪手不能点真实屏内行"), S->ProcessMenuTouchSample(EControllerHand::Left, Point, false, false));
    TestEqual(TEXT("拒绝输入没有屏幕动作"), S->GetLastScreenAction(), INDEX_NONE);
    ScreenAway();
    TestTrue(TEXT("真实屏幕 Rect 中心点击"), S->ProcessMenuTouchSample(EControllerHand::Left, Point));
    TestEqual(TEXT("实际屏幕动作是第一行"), S->GetLastScreenAction(), 100);
    TestFalse(TEXT("屏幕连续接触不重复"), S->ProcessMenuTouchSample(EControllerHand::Left, Point));
    TestFalse(TEXT("第二只手接触屏幕不重复"), S->ProcessMenuTouchSample(EControllerHand::Right, Point));
    S->ProcessMenuTouchSample(EControllerHand::Left, Away);
    TestFalse(TEXT("右手未离开不能重按"), S->ProcessMenuTouchSample(EControllerHand::Right, Point));
    const FVector Near = S->Panel->GetComponentTransform().TransformPosition(FVector(0,
        600 - (Rect.Min.X - 9), 375 - (Rect.Min.Y + Rect.Max.Y) * .5));
    TestFalse(TEXT("离开按钮但仍在重武装边距内"), S->ProcessMenuTouchSample(EControllerHand::Right, Near));
    TestFalse(TEXT("边距内回来仍为连续接触"), S->ProcessMenuTouchSample(EControllerHand::Right, Point));
    S->ProcessMenuTouchSample(EControllerHand::Right, Away);
    TestTrue(TEXT("两手真正离开后右手可重按屏幕"), S->ProcessMenuTouchSample(EControllerHand::Right, Point));
    ScreenAway();
    TestTrue(TEXT("全部筛选真实点击"), TutorialMenuTestHelpers::TouchAction(S, 10));
    TestEqual(TEXT("全部筛选状态"), S->GetMenuFilter(), 0);
    const TArray<int32> AllRows = S->GetVisibleMenuIndices();
    if (!TestEqual(TEXT("全部十行可见"), AllRows.Num(), 10)) return false;
    for (int32 Row = 0; Row < AllRows.Num(); ++Row)
    {
        TestEqual(TEXT("全部行映射精确"), AllRows[Row], Row);
        TestTrue(TEXT("逐行使用实际 Rect 中心手采样"), TutorialMenuTestHelpers::TouchAction(S, 100 + Row));
        TestEqual(TEXT("选择的是可见行的真实条目"), S->GetSelectedMenuIndex(), AllRows[Row]);
        TestEqual(TEXT("最后屏幕动作匹配行"), S->GetLastScreenAction(), 100 + Row);
    }
    TSet<int32> FilteredIndices;
    for (int32 Filter = 1; Filter <= 2; ++Filter)
    {
        TestTrue(TEXT("真实点击咖啡或鸡尾酒筛选"), TutorialMenuTestHelpers::TouchAction(S, 10 + Filter));
        TestEqual(TEXT("筛选状态更新"), S->GetMenuFilter(), Filter);
        const TArray<int32> Rows = S->GetVisibleMenuIndices();
        int32 ExpectedRows = 0;
        for (const auto& E : S->RecipeMenu->Entries)
        {
            const bool Coffee = E.Category.ToString().Contains(TEXT("咖啡"));
            ExpectedRows += (Filter == 1 ? Coffee : !Coffee) ? 1 : 0;
        }
        TestEqual(TEXT("筛选没有漏行"), Rows.Num(), ExpectedRows);
        if (!TestTrue(TEXT("两类筛选均非空且少于十行"), Rows.Num() > 0 && Rows.Num() < 10)) return false;
        for (int32 Row = 0; Row < Rows.Num(); ++Row)
        {
            const bool Coffee = S->RecipeMenu->Entries[Rows[Row]].Category.ToString().Contains(TEXT("咖啡"));
            TestTrue(TEXT("可见行分类正确"), Filter == 1 ? Coffee : !Coffee);
            TestFalse(TEXT("分类分区不重复"), FilteredIndices.Contains(Rows[Row]));
            FilteredIndices.Add(Rows[Row]);
            TestTrue(TEXT("筛选后逐行真实点击"), TutorialMenuTestHelpers::TouchAction(S, 100 + Row));
            TestEqual(TEXT("筛选后可见行映射至原条目"), S->GetSelectedMenuIndex(), Rows[Row]);
        }
        const int32 Before = S->GetSelectedMenuIndex();
        TestFalse(TEXT("筛选隐藏行不能通过手采样选择"), TutorialMenuTestHelpers::TouchAction(S, 100 + Rows.Num()));
        TestFalse(TEXT("筛选越界行动作 API 拒绝"), S->ActivateScreenAction(100 + Rows.Num()));
        TestEqual(TEXT("隐藏行输入不改变选择"), S->GetSelectedMenuIndex(), Before);
    }
    TestEqual(TEXT("两个筛选完整覆盖十款"), FilteredIndices.Num(), 10);
    TestTrue(TEXT("切回全部"), TutorialMenuTestHelpers::TouchAction(S, 10));
    TestTrue(TEXT("设置原订单"), S->BeginOrder(Recipe, Spot, nullptr, FText::GetEmpty()));
    const auto Serial = Spot->GetOrderSerial();
    TestTrue(TEXT("订单页签真实点击"), TutorialMenuTestHelpers::TouchAction(S, 0));
    TestFalse(TEXT("已切订单页"), S->IsRecipeMenuMode());
    TestFalse(TEXT("订单页不接受配方筛选"), S->ActivateScreenAction(11));
    TestTrue(TEXT("当前订单配方按钮真实点击"), TutorialMenuTestHelpers::TouchAction(S, 2));
    TestEqual(TEXT("当前订单配方动作"), S->GetLastScreenAction(), 2);
    TestTrue(TEXT("当前订单按钮打开配方页"), S->IsRecipeMenuMode());
    TestEqual(TEXT("当前订单按钮找到 BNT"), S->GetSelectedMenuIndex(), S->RecipeMenu->FindPinyinIndex(TEXT("BNT")));
    TestEqual(TEXT("当前订单按钮恢复全部筛选"), S->GetMenuFilter(), 0);
    TestTrue(TEXT("再次真实切订单页"), TutorialMenuTestHelpers::TouchAction(S, 0));
    TestTrue(TEXT("配方页签真实点击"), TutorialMenuTestHelpers::TouchAction(S, 1));
    TestTrue(TEXT("已切配方页"), S->IsRecipeMenuMode());
    TestTrue(TEXT("屏内操作未替换原订单"), S->GetCurrentRecipe() == Recipe && Spot->ActiveRecipe == Recipe);
    TestEqual(TEXT("屏内操作未改变订单序号"), Spot->GetOrderSerial(), Serial);
    for (auto* Zone : { S->MenuModeZone.Get(), S->MenuPreviousZone.Get(), S->MenuNextZone.Get() })
    {
        const int32 Before = S->GetSelectedMenuIndex(), Action = S->GetLastScreenAction();
        ScreenAway();
        PowerAway();
        TestFalse(TEXT("开屏后旧 MENU/PREV/NEXT 区无菜单动作"), S->ProcessMenuTouchSample(EControllerHand::Left, Zone->GetComponentLocation()));
        TestFalse(TEXT("开屏后旧三键不能触发电源"), S->ProcessTouchSample(EControllerHand::Left, Zone->GetComponentLocation()));
        TestTrue(TEXT("旧三键不换页、不关屏"), S->IsPanelOpen() && S->IsRecipeMenuMode());
        TestEqual(TEXT("旧三键不改变选择"), S->GetSelectedMenuIndex(), Before);
        TestEqual(TEXT("旧三键不产生屏幕动作"), S->GetLastScreenAction(), Action);
    }
    TestTrue(TEXT("唯一实体电源真实关屏"), F.TouchPower());
    TestFalse(TEXT("电源已关闭"), S->IsPanelOpen());
    TestFalse(TEXT("关屏不能浏览"), S->BrowseRecipeMenu(1));
    TestFalse(TEXT("关屏不能按屏内页签"), TutorialMenuTestHelpers::TouchAction(S, 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPresentation, "CyberCafe.RecipeMenu.WidgetTwoPagesExactRecipeLines", RecipeMenuTest::MenuTestFlags)
bool FMenuPresentation::RunTest(const FString&)
{
    RecipeMenuTest::World F;
    if (!TestNotNull(TEXT("保存屏幕"), F.Screen)) return false;
    F.Begin();
    auto* S = F.Screen;
    auto* Widget = S->GetDisplayWidget();
    if (!TestNotNull(TEXT("真实保存 WBP 实例"), Widget) || !TestNotNull(TEXT("真实 UMG 控件树"), Widget->WidgetTree.Get())
        || !TestNotNull(TEXT("真实菜单数据"), S->RecipeMenu.Get())) return false;
    auto* Switch = Cast<UWidgetSwitcher>(Widget->WidgetTree->FindWidget(TEXT("OrderMenuSwitcher")));
    auto* CupIcon = Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("MenuCupIcon")));
    auto* IceIcon = Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("MenuIceIcon")));
    auto* CupName = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("MenuCupName")));
    auto* IceCount = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("MenuIceCount")));
    if (!TestNotNull(TEXT("真实页切换器"), Switch) || !TestNotNull(TEXT("真实杯图"), CupIcon)
        || !TestNotNull(TEXT("真实冰图"), IceIcon) || !TestNotNull(TEXT("真实杯名"), CupName)
        || !TestNotNull(TEXT("真实冰量文字"), IceCount)) return false;
    TestEqual(TEXT("仍恰好两页"), Switch->GetChildrenCount(), 2);
    TestTrue(TEXT("实体电源开屏"), F.TouchPower());
    F.Tick(20);
    bool CheckedNoIce = false, CheckedIce = false;
    for (const auto& E : S->RecipeMenu->Entries)
    {
        if (!TestNotNull(TEXT("保存配方"), E.Recipe.Get())
            || !TestTrue(TEXT("真实可见行点击"), TutorialMenuTestHelpers::FindRecipe(S, E.PinyinIndex))) return false;
        TestEqual(TEXT("真实 UMG 配方页显示"), Switch->GetActiveWidgetIndex(), 1);
        const FString Text = Widget->GetMenuText().ToString();
        TestTrue(TEXT("索引与显示名精确出现"), Text.Contains(E.PinyinIndex.ToString()) && Text.Contains(E.DisplayName.ToString()));
        TestTrue(TEXT("指定杯名出现"), Text.Contains(E.CupName.ToString()));
        TestEqual(TEXT("真实杯名控件一致"), CupName->GetText().ToString(), E.CupName.ToString());
        if (!TestNotNull(TEXT("保存杯型图资源"), E.CupIcon.Get())) return false;
        TestTrue(TEXT("杯型图实际绑定而非仅有文字"), CupIcon->GetBrush().GetResourceObject() == E.CupIcon.Get()
            && CupIcon->GetVisibility() == ESlateVisibility::HitTestInvisible);
        const FString ExpectedIce = E.Recipe->ExpectedIceCount == 0 ? TEXT("不加冰") : TEXT("冰块 3 块");
        TestTrue(TEXT("实际配方文字含准确冰量"), Text.Contains(ExpectedIce));
        TestEqual(TEXT("实际冰量控件精确一致"), IceCount->GetText().ToString(), ExpectedIce);
        if (E.Recipe->ExpectedIceCount == 3)
        {
            CheckedIce = true;
            if (!TestNotNull(TEXT("保存冰图资源"), S->RecipeMenu->IceIcon.Get())) return false;
            TestTrue(TEXT("三冰配方显示真实冰图"), IceIcon->GetBrush().GetResourceObject() == S->RecipeMenu->IceIcon.Get()
                && IceIcon->GetVisibility() == ESlateVisibility::HitTestInvisible);
        }
        else
        {
            CheckedNoIce = true;
            TestTrue(TEXT("无冰配方清掉上一杯冰图并隐藏"), IceIcon->GetBrush().GetResourceObject() == nullptr
                && IceIcon->GetVisibility() == ESlateVisibility::Hidden);
        }
        TestTrue(TEXT("液体总量明确不含冰"), Text.Contains(FString::Printf(TEXT("%.0f mL"), E.Recipe->TargetVolumeML))
            && Text.Contains(TEXT("液体 mL 不含冰块")));
        for (int32 I = 0; I < E.Ingredients.Num(); ++I)
        {
            const auto& P = E.Ingredients[I];
            const FString Line = FString::Printf(TEXT("%d  %s    %.0f mL"), I + 1,
                *UDrinkMenuData::IngredientDisplayName(P.IngredientId).ToString(), P.VolumeML);
            TestTrue(TEXT("原料名与精确 mL 在同一实际配方行"), Text.Contains(Line));
            auto* Icon = Cast<UImage>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIngredientIcon%d"), I))));
            if (!TestNotNull(TEXT("真实原料图控件"), Icon) || !TestNotNull(TEXT("保存原料图资源"), P.Icon.Get())) return false;
            TestTrue(TEXT("原料图实际绑定保存贴图且可见"), Icon->GetBrush().GetResourceObject() == P.Icon.Get()
                && Icon->GetVisibility() == ESlateVisibility::HitTestInvisible);
        }
    }
    TestTrue(TEXT("实际检查了无冰和三冰两种 UI"), CheckedNoIce && CheckedIce);
    TestTrue(TEXT("屏内订单页签切换"), TutorialMenuTestHelpers::TouchAction(S, 0));
    TestEqual(TEXT("原订单 UMG 页恢复"), Switch->GetActiveWidgetIndex(), 0);
    TestTrue(TEXT("原订单文字仍可用"), !Widget->GetOrderText().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuRecipeEvaluate, "CyberCafe.RecipeMenu.AllDisplayedVolumesPassExistingRecipes", RecipeMenuTest::MenuTestFlags)
bool FMenuRecipeEvaluate::RunTest(const FString&)
{
    auto* D = RecipeMenuTest::Data();
    if (!TestNotNull(TEXT("保存菜单"), D) || !TestTrue(TEXT("菜单有效"), D->IsValidMenu())
        || !TestEqual(TEXT("验收覆盖十款"), D->Entries.Num(), 10)) return false;
    for (const auto& E : D->Entries)
    {
        RecipeMenuTest::World F;
        auto* R = E.Recipe.Get();
        if (!TestNotNull(TEXT("保存配方"), R) || !TestNotNull(TEXT("实际 AcceptedCupClass"), R->AcceptedCupClass.Get())
            || !TestTrue(TEXT("冰量仅允许 0/3"), R->ExpectedIceCount == 0 || R->ExpectedIceCount == 3)) return false;
        auto* C = F.SpawnAcceptedCup(R, FVector(0, 0, 200));
        if (!TestNotNull(TEXT("真实保存杯类生成的杯"), C)) return false;
        F.Begin();
        C->ContainerMesh->SetSimulatePhysics(false);
        TestTrue(TEXT("真实杯类和 World 精确匹配"), C->GetClass() == R->AcceptedCupClass.Get() && C->GetWorld() == F.W);
        TestTrue(TEXT("根缩放匹配已批准的实际场景实例，不改模型"), C->GetActorScale3D().Equals(
            TutorialMenuTestHelpers::SceneCupScale(R->AcceptedCupClass.Get()), .00001));
        TestTrue(TEXT("杯最初为空且未碎未冻结"), FMath::IsNearlyZero(C->GetPreciseVolumeML(), .000001)
            && !C->IsBroken() && !C->IsLiquidFrozen());
        for (const auto& I : E.Ingredients)
        {
            const double Added = C->AddIngredientLiquid(I.VolumeML, I.IngredientId, FLinearColor::White);
            TestTrue(TEXT("实际接收精确原料 mL"), FMath::IsNearlyEqual(Added, I.VolumeML, .000001));
        }
        if (R->ExpectedIceCount == 3)
        {
            const auto NoIce = R->Evaluate(C);
            TestFalse(TEXT("冰饮缺冰不能通过"), NoIce.bAccepted);
            TestTrue(TEXT("缺冰拒绝原因准确"), NoIce.Reason == EDrinkValidationReason::WrongIce);
        }
        if (!TestTrue(TEXT("通过真实 Ice crossing 按保存配方加冰"), TutorialMenuTestHelpers::AddIce(C, R->ExpectedIceCount))) return false;
        const auto Result = R->Evaluate(C);
        TestTrue(FString::Printf(TEXT("%s 实际配方杯、原料和冰量通过"), *E.PinyinIndex.ToString()), Result.bAccepted);
        TestEqual(TEXT("验收快照冰量精确"), Result.IceCount, R->ExpectedIceCount);
        TestTrue(TEXT("加冰不改变实际液体目标量"), FMath::IsNearlyEqual(Result.TotalML, R->TargetVolumeML, .000001));
        C->ConsumeLiquid(5);
        TestTrue(TEXT("实际加入配方外原料"), FMath::IsNearlyEqual(
            static_cast<double>(C->AddIngredientLiquid(5, TEXT("ForbiddenIngredient"), FLinearColor::White)), 5.0, .000001));
        const auto Contaminated = R->Evaluate(C);
        TestFalse(TEXT("多余原料仍拒绝"), Contaminated.bAccepted);
        TestTrue(TEXT("拒绝确因额外原料"), Contaminated.Reason == EDrinkValidationReason::WrongIngredient);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuOptIn, "CyberCafe.RecipeMenu.UnconfiguredScreenPreservesOldPower", RecipeMenuTest::MenuTestFlags)
bool FMenuOptIn::RunTest(const FString&)
{
    RecipeMenuTest::World F;
    if (!TestNotNull(TEXT("保存屏幕"), F.Screen)) return false;
    F.Screen->RecipeMenu = nullptr;
    F.Begin();
    TestFalse(TEXT("无菜单不能切菜单"), F.Screen->ToggleRecipeMenu());
    TestFalse(TEXT("无菜单不能选索引"), F.Screen->SelectRecipeByPinyin(TEXT("MYNT")));
    TestTrue(TEXT("原实体电源真实开屏"), F.TouchPower());
    TestTrue(TEXT("无菜单仍可开屏"), F.Screen->IsPanelOpen());
    F.Tick(20);
    TestFalse(TEXT("无菜单屏幕动作 API 拒绝"), F.Screen->ActivateScreenAction(1));
    TestFalse(TEXT("无菜单屏内按钮拒绝"), TutorialMenuTestHelpers::TouchAction(F.Screen, 1));
    TestTrue(TEXT("原实体电源真实关屏"), F.TouchPower());
    TestFalse(TEXT("无菜单仍可关屏"), F.Screen->IsPanelOpen());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuAcceptedOrder, "CyberCafe.RecipeMenu.AcceptedOrderWhileBrowsingKeepsMenuAndHistory", RecipeMenuTest::MenuTestFlags)
bool FMenuAcceptedOrder::RunTest(const FString&)
{
    RecipeMenuTest::World F;
    if (!TestNotNull(TEXT("保存屏幕"), F.Screen)) return false;
    auto* Recipe = LoadObject<UDrinkRecipe>(nullptr, TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));
    if (!TestNotNull(TEXT("原保存 DA_Latte"), Recipe) || !TestNotNull(TEXT("固定保存杯类"), Recipe->AcceptedCupClass.Get())) return false;
    TestEqual(TEXT("原 RecipeId 仍为 Latte"), Recipe->RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("原显示名改为冰拿铁"), Recipe->DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("固定 Highball BP"), Recipe->AcceptedCupClass->GetPathName(),
        FString(TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C")));
    TestEqual(TEXT("保存配方要求三块冰"), Recipe->ExpectedIceCount, 3);
    auto* Spot = F.W->SpawnActor<AServingSpotActor>();
    Spot->SetActiveRecipe(Recipe);
    Spot->SetSubmissionEnabled(false);
    auto* Cup = F.SpawnAcceptedCup(Recipe, Spot->GetActorLocation());
    if (!TestNotNull(TEXT("实际保存 Highball 杯"), Cup)) return false;
    F.Begin();
    Cup->ContainerMesh->SetSimulatePhysics(false);
    // Grab 的 BeginPlay 会恢复碰撞；本用例只验证显式 TrySubmitCup，不让动画 Tick 提前自动交付。
    Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("真实杯精确使用保存类"), Cup->GetClass() == Recipe->AcceptedCupClass.Get());
    TestTrue(TEXT("保存 Highball 根缩放仍为 1.1"), Cup->GetActorScale3D().Equals(FVector(1.1), .00001));
    TestTrue(TEXT("实际接收 Coffee 100 mL"), FMath::IsNearlyEqual(
        static_cast<double>(Cup->AddIngredientLiquid(100, TEXT("Coffee"), FLinearColor::White)), 100.0, .000001));
    TestTrue(TEXT("实际接收 Milk 100 mL"), FMath::IsNearlyEqual(
        static_cast<double>(Cup->AddIngredientLiquid(100, TEXT("Milk"), FLinearColor::White)), 100.0, .000001));
    if (!TestTrue(TEXT("三块真实 Ice crossing 进入杯口"), TutorialMenuTestHelpers::AddIce(Cup, 3))) return false;
    Spot->SetSubmissionEnabled(true);
    TestTrue(TEXT("设置原订单与最终序号"), F.Screen->BeginOrder(Recipe, Spot, nullptr, FText::GetEmpty()));
    const auto Serial = Spot->GetOrderSerial();
    TestFalse(TEXT("原订单自动打开不冒充玩家主动"), F.Screen->IsPlayerOpened());
    TestTrue(TEXT("实体电源关闭自动打开屏"), F.TouchPower());
    TestTrue(TEXT("实体电源主动重新开屏"), F.TouchPower());
    F.Tick(20);
    if (!TestTrue(TEXT("真实行点击查阅 MYNT"), TutorialMenuTestHelpers::FindRecipe(F.Screen, TEXT("MYNT")))) return false;
    TestTrue(TEXT("查配方是玩家主动开屏"), F.Screen->IsPlayerOpened());
    TestTrue(TEXT("浏览没有换掉拿铁订单"), F.Screen->GetCurrentRecipe() == Recipe && Spot->ActiveRecipe == Recipe);
    TestEqual(TEXT("浏览没有换订单序号"), Spot->GetOrderSerial(), Serial);
    TestEqual(TEXT("提交前没有虚构历史"), F.Screen->GetDeliveryRecords().Num(), 0);
    TestTrue(TEXT("原冰拿铁真实杯提交"), Spot->TrySubmitCup(Cup));
    TestTrue(TEXT("真实交付事件到达屏幕"), F.Screen->IsDelivered());
    TestEqual(TEXT("恰好一条真实交付记录"), F.Screen->GetDeliveryRecords().Num(), 1);
    TestEqual(TEXT("真实交付快照为三块冰"), Spot->GetLastValidation().IceCount, 3);
    TestTrue(TEXT("真实交付快照为 200 mL"), FMath::IsNearlyEqual(Spot->GetLastValidation().TotalML, 200.0, .000001));
    if (F.Screen->GetDeliveryRecords().Num() != 1) return false;
    const auto& Record = F.Screen->GetDeliveryRecords()[0];
    TestEqual(TEXT("历史仍记录原 Latte ID"), Record.RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("历史显示冰拿铁而非浏览饮品"), Record.DrinkName.ToString(), FString(TEXT("冰拿铁")));
    TestTrue(TEXT("历史记录真实 200 mL"), FMath::IsNearlyEqual(Record.VolumeML, 200.0, .000001));
    F.Tick(150);
    TestTrue(TEXT("主动阅读菜单交付后不自动关闭"), F.Screen->IsPanelOpen() && F.Screen->IsRecipeMenuMode());
    const auto* Selected = F.Screen->GetSelectedMenuEntry();
    if (!TestNotNull(TEXT("交付后仍保留选择"), Selected)) return false;
    TestEqual(TEXT("交付不切掉 MYNT"), Selected->PinyinIndex, FName(TEXT("MYNT")));
    TestTrue(TEXT("屏内按钮返回已交付原订单"), TutorialMenuTestHelpers::TouchAction(F.Screen, 0));
    TestFalse(TEXT("显示已交付订单页"), F.Screen->IsRecipeMenuMode());
    TestTrue(TEXT("同一原杯保持冻结"), Cup->IsLiquidFrozen());
    auto* Widget = F.Screen->GetDisplayWidget();
    if (!TestNotNull(TEXT("真实历史 UMG 控件"), Widget)) return false;
    TestTrue(TEXT("真实历史 UMG 显示冰拿铁及实际量"), Widget->GetHistoryText().ToString().Contains(TEXT("冰拿铁"))
        && Widget->GetHistoryText().ToString().Contains(TEXT("200 mL")));
    TestFalse(TEXT("同杯再次提交不能复制历史"), Spot->TrySubmitCup(Cup));
    TestEqual(TEXT("浏览和重复提交不复制历史"), F.Screen->GetDeliveryRecords().Num(), 1);
    return true;
}
#endif
