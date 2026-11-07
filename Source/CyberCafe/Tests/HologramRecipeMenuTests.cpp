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
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "CoreGlobals.h"
#include "UObject/UnrealType.h"

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
        TestTrue(TEXT("保存冰量字段仍为无冰 0 或旧建议量 3"), E.Recipe->ExpectedIceCount == 0 || E.Recipe->ExpectedIceCount == 3);
        WithoutIce += E.Recipe->ExpectedIceCount == 0 ? 1 : 0;
        WithIce += E.Recipe->ExpectedIceCount == 3 ? 1 : 0;
    }
    TestTrue(TEXT("菜单覆盖无冰饮品"), WithoutIce > 0);
    TestTrue(TEXT("菜单覆盖保留旧建议量 3 的需冰饮品"), WithIce > 0);
    const auto& BNT = D->Entries[0];
    TestEqual(TEXT("BNT 仍引用原 DA_Latte"), BNT.Recipe.Get(), LoadObject<UDrinkRecipe>(nullptr,
        TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
    TestEqual(TEXT("RecipeId 仍为 Latte"), BNT.Recipe->RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("菜单显示冰拿铁"), BNT.DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("权威配方显示冰拿铁"), BNT.Recipe->DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("冰拿铁保留旧建议量 3，不作为精确验收要求"), BNT.Recipe->ExpectedIceCount, 3);
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
    auto* IceHint = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("MenuIceHint")));
    auto* Method = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("MenuPourMethod")));
    auto* OrderIce = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("OrderIceCount")));
    if (!TestNotNull(TEXT("真实页切换器"), Switch) || !TestNotNull(TEXT("真实杯图"), CupIcon)
        || !TestNotNull(TEXT("真实冰图"), IceIcon) || !TestNotNull(TEXT("真实杯名"), CupName)
        || !TestNotNull(TEXT("真实冰量文字"), IceCount) || !TestNotNull(TEXT("真实冰量提示"), IceHint)
        || !TestNotNull(TEXT("真实制作方法"), Method) || !TestNotNull(TEXT("真实订单冰量文字"), OrderIce)) return false;
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
        TestTrue(TEXT("中文饮品名精确出现"), Text.Contains(E.DisplayName.ToString()));
        TestFalse(TEXT("拼音索引仅用于内部检索，不出现在配方文字"), Text.Contains(E.PinyinIndex.ToString()));
        TestTrue(TEXT("移除制作方法与冰量小字但保留控件"), IceHint->GetText().IsEmpty() && Method->GetText().IsEmpty()
            && IceHint->GetVisibility() == ESlateVisibility::Collapsed && Method->GetVisibility() == ESlateVisibility::Collapsed);
        TestTrue(TEXT("指定杯名出现"), Text.Contains(E.CupName.ToString()));
        TestEqual(TEXT("真实杯名控件一致"), CupName->GetText().ToString(), E.CupName.ToString());
        if (!TestNotNull(TEXT("保存杯型图资源"), E.CupIcon.Get())) return false;
        TestTrue(TEXT("杯型图实际绑定而非仅有文字"), CupIcon->GetBrush().GetResourceObject() == E.CupIcon.Get()
            && CupIcon->GetVisibility() == ESlateVisibility::HitTestInvisible);
        const FString ExpectedIce = E.Recipe->ExpectedIceCount == 0 ? TEXT("不加冰") : TEXT("加冰即可");
        TestTrue(TEXT("实际配方文字含同源加冰要求"), Text.Contains(ExpectedIce));
        TestEqual(TEXT("实际加冰要求控件一致"), IceCount->GetText().ToString(), ExpectedIce);
        TestFalse(TEXT("菜单不显示精确三冰或一铲硬要求"), Text.Contains(TEXT("冰块 3 块")) || Text.Contains(TEXT("冰块3块"))
            || Text.Contains(TEXT("1 铲")) || Text.Contains(TEXT("1铲")) || Text.Contains(TEXT("按冰量加冰")));
        if (E.Recipe->ExpectedIceCount > 0)
        {
            CheckedIce = true;
            TestFalse(TEXT("需冰菜单不重复冰量说明"), Text.Contains(TEXT("有冰即可，不限块数")));
            if (!TestNotNull(TEXT("保存冰图资源"), S->RecipeMenu->IceIcon.Get())) return false;
            TestTrue(TEXT("需冰配方显示真实冰图"), IceIcon->GetBrush().GetResourceObject() == S->RecipeMenu->IceIcon.Get()
                && IceIcon->GetVisibility() == ESlateVisibility::HitTestInvisible);
        }
        else
        {
            CheckedNoIce = true;
            TestFalse(TEXT("无冰菜单不重复操作说明"), Text.Contains(TEXT("直接加入原料")));
            TestTrue(TEXT("无冰配方清掉上一杯冰图并隐藏"), IceIcon->GetBrush().GetResourceObject() == nullptr
                && IceIcon->GetVisibility() == ESlateVisibility::Hidden);
        }
        TestTrue(TEXT("液体总量保留精确 mL"), Text.Contains(FString::Printf(TEXT("液体总量  %.0f mL"), E.Recipe->TargetVolumeML)));
        TestFalse(TEXT("菜单不重复液体与冰块说明"), Text.Contains(TEXT("液体 mL 不含冰块")));
        TArray<FString> Lines;
        Text.ParseIntoArrayLines(Lines, false);
        TestEqual(TEXT("紧凑文字只保留四项核心与各原料行"), Lines.Num(), 4 + E.Ingredients.Num());
        for (int32 I = 0; I < E.Ingredients.Num(); ++I)
        {
            const auto& P = E.Ingredients[I];
            const FString Line = FString::Printf(TEXT("%s  %.0f mL"),
                *UDrinkMenuData::IngredientDisplayName(P.IngredientId).ToString(), P.VolumeML);
            if (!TestTrue(TEXT("原料行索引有效"), Lines.IsValidIndex(4 + I))) return false;
            TestEqual(TEXT("原料中文名与精确 mL 同行且不带数字序号"), Lines[4 + I], Line);
            auto* Icon = Cast<UImage>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIngredientIcon%d"), I))));
            if (!TestNotNull(TEXT("真实原料图控件"), Icon) || !TestNotNull(TEXT("保存原料图资源"), P.Icon.Get())) return false;
            TestTrue(TEXT("原料图实际绑定保存贴图且可见"), Icon->GetBrush().GetResourceObject() == P.Icon.Get()
                && Icon->GetVisibility() == ESlateVisibility::HitTestInvisible);
        }
    }
    TestTrue(TEXT("实际检查了无冰和需冰两种 UI"), CheckedNoIce && CheckedIce);
    auto* Spot = F.W->SpawnActor<AServingSpotActor>();
    for (const auto& E : S->RecipeMenu->Entries)
    {
        Spot->SetActiveRecipe(E.Recipe.Get());
        if (!TestTrue(TEXT("逐款设置真实订单以检查同源冰量标签"), S->BeginOrder(E.Recipe.Get(), Spot, nullptr, FText::GetEmpty()))
            || !TestTrue(TEXT("真实行点击查看当前订单配方"), TutorialMenuTestHelpers::FindRecipe(S, E.PinyinIndex))) return false;
        const FString ExpectedIce = E.Recipe->ExpectedIceCount > 0 ? TEXT("加冰即可") : TEXT("不加冰");
        TestEqual(TEXT("订单冰量标签取当前配方字段"), OrderIce->GetText().ToString(), ExpectedIce);
        TestEqual(TEXT("订单与配方冰量标签同源"), OrderIce->GetText().ToString(), IceCount->GetText().ToString());
    }
    TestTrue(TEXT("屏内订单页签切换"), TutorialMenuTestHelpers::TouchAction(S, 0));
    TestEqual(TEXT("原订单 UMG 页恢复"), Switch->GetActiveWidgetIndex(), 0);
    TestTrue(TEXT("原订单文字仍可用"), !Widget->GetOrderText().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuCompactCore, "CyberCafe.RecipeMenu.CompactCoreTextAndLayoutAllTenDrinks", RecipeMenuTest::MenuTestFlags)
bool FMenuCompactCore::RunTest(const FString&)
{
    RecipeMenuTest::World F;
    if (!TestNotNull(TEXT("保存屏幕"), F.Screen)) return false;
    F.Begin();
    auto* S = F.Screen;
    auto* Widget = S->GetDisplayWidget();
    if (!TestNotNull(TEXT("真实保存 WBP"), Widget) || !TestNotNull(TEXT("真实控件树"), Widget->WidgetTree.Get())
        || !TestNotNull(TEXT("真实菜单"), S->RecipeMenu.Get())
        || !TestTrue(TEXT("菜单完整校验仍有效"), S->RecipeMenu->IsValidMenu())
        || !TestEqual(TEXT("紧凑展示覆盖十款"), S->RecipeMenu->Entries.Num(), 10)) return false;
    auto* Switch = Cast<UWidgetSwitcher>(Widget->WidgetTree->FindWidget(TEXT("OrderMenuSwitcher")));
    auto* Details = Cast<UCanvasPanel>(Widget->WidgetTree->FindWidget(TEXT("MenuDetailsColumn")));
    // UE5.6 没有 GetJustification；只读反射实际 UPROPERTY，不改控件来满足断言。
    const auto* Justification = FindFProperty<FByteProperty>(UTextBlock::StaticClass(), TEXT("Justification"));
    if (!TestNotNull(TEXT("真实两页切换器"), Switch) || !TestNotNull(TEXT("真实详情画布"), Details)
        || !TestNotNull(TEXT("真实文字对齐属性"), Justification)) return false;
    TestEqual(TEXT("精简仍保持两页"), Switch->GetChildrenCount(), 2);
    TestNull(TEXT("只浏览菜单没有真实订单"), S->GetCurrentRecipe());
    TestNull(TEXT("普通菜单浏览没有首单预览"), S->GetPresentedRecipe());
    TestEqual(TEXT("普通浏览订单序号仍为零"), S->GetCurrentOrderSerial(), int64(0));
    TestEqual(TEXT("初始没有伪造交付记录"), S->GetDeliveryRecords().Num(), 0);
    TestEqual(TEXT("历史空态来自真实空账本"), Widget->GetHistoryText().ToString(), FString(TEXT("暂无本局交付")));
    for (int32 I = 0; I < 3; ++I)
    {
        auto* Name = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryName%d"), I))));
        auto* Volume = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryVolume%d"), I))));
        auto* Record = Cast<UCanvasPanel>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryRecord%d"), I))));
        if (!TestNotNull(TEXT("原三行历史名控件仍在"), Name) || !TestNotNull(TEXT("原三行历史量控件仍在"), Volume)
            || !TestNotNull(TEXT("原三行历史画布仍在"), Record) || !TestNotNull(TEXT("原历史行容器仍在"), Record->GetParent())) return false;
        TestTrue(TEXT("空账本三行真实清空且折叠，不伪造历史"), Name->GetText().IsEmpty() && Volume->GetText().IsEmpty()
            && Record->GetParent()->GetVisibility() == ESlateVisibility::Collapsed);
    }
    if (!TestTrue(TEXT("真实电源开屏"), F.TouchPower())) return false;
    F.Tick(20);
    if (!TestTrue(TEXT("真实切配方页"), TutorialMenuTestHelpers::TouchAction(S, 1))
        || !TestTrue(TEXT("真实点击全部筛选"), TutorialMenuTestHelpers::TouchAction(S, 10))) return false;
    const TArray<int32> Rows = S->GetVisibleMenuIndices();
    if (!TestEqual(TEXT("全部十行真实可见"), Rows.Num(), 10)) return false;
    for (int32 Row = 0; Row < Rows.Num(); ++Row)
    {
        auto* Label = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIndex%d"), Row))));
        auto* Button = Cast<URecipeScreenButton>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIndexButton%d"), Row))));
        if (!TestNotNull(TEXT("真实索引文字"), Label) || !TestNotNull(TEXT("真实索引按钮"), Button)) return false;
        TestEqual(TEXT("十行内部 DataIndex 映射仍精确"), Rows[Row], Row);
        TestEqual(TEXT("索引文字仅 DisplayName，无序号或缩写"), Label->GetText().ToString(), S->RecipeMenu->Entries[Rows[Row]].DisplayName.ToString());
        TestTrue(TEXT("索引标签与原 action 按钮可见"), Label->GetVisibility() == ESlateVisibility::HitTestInvisible
            && Button->GetVisibility() == ESlateVisibility::Visible && Button->Action == 100 + Row);
    }
    const TCHAR* RemovedText[] = {
        TEXT("ScreenBrandSubline"), TEXT("ScreenTouchHint"), TEXT("MenuHeading"),
        TEXT("MenuIndexHint"), TEXT("MenuNote"), TEXT("MenuIndexBadge"), TEXT("MenuDrinkDescription"),
        TEXT("MenuCupVolume"), TEXT("MenuIceHint"), TEXT("MenuTotalHint"), TEXT("MenuIngredientHeading"),
        TEXT("MenuIngredientsML"), TEXT("MenuPourMethod"), TEXT("MenuAcceptanceRange"), TEXT("MenuCurrentOrderHint"),
        TEXT("MenuIngredientNumber0"), TEXT("MenuIngredientNumber1"), TEXT("MenuIngredientNumber2"),
        TEXT("CustomerId"), TEXT("CustomerNotes"), TEXT("NotesHeading"), TEXT("OrderTolerance")
    };
    const auto CheckSlot = [this](UWidget* Control, FVector2D Position, FVector2D Size)
    {
        auto* Slot = Cast<UCanvasPanelSlot>(Control->Slot);
        if (!TestNotNull(Control->GetName() + TEXT("：真实 CanvasSlot"), Slot)) return;
        TestTrue(Control->GetName() + TEXT("：紧凑位置和尺寸精确"), Slot->GetPosition().Equals(Position)
            && Slot->GetSize().Equals(Size));
        TestTrue(Control->GetName() + TEXT("：零锚点零对齐且非自动尺寸"), Slot->GetAnchors().Minimum.Equals(FVector2D::ZeroVector)
            && Slot->GetAnchors().Maximum.Equals(FVector2D::ZeroVector) && Slot->GetAlignment().Equals(FVector2D::ZeroVector)
            && !Slot->GetAutoSize());
    };
    const TCHAR* CardNames[] = { TEXT("MenuCupCard"), TEXT("MenuIceCard"), TEXT("MenuTotalCard") };
    const FVector2D CardPositions[] = { FVector2D(30.f, 100.f), FVector2D(354.f, 100.f), FVector2D(648.f, 100.f) };
    const FVector2D CardSizes[] = { FVector2D(312.f, 151.f), FVector2D(282.f, 151.f), FVector2D(222.f, 151.f) };
    bool CheckedTwo = false, CheckedThree = false;
    for (int32 Row = 0; Row < Rows.Num(); ++Row)
    {
        const auto& E = S->RecipeMenu->Entries[Rows[Row]];
        if (!TestNotNull(TEXT("真实权威配方"), E.Recipe.Get())
            || !TestTrue(TEXT("指定杯中文名非空"), !E.CupName.IsEmpty())
            || !TestTrue(TEXT("每款使用实际 action Rect 中心手采样"), TutorialMenuTestHelpers::TouchAction(S, 100 + Row))) return false;
        TestEqual(TEXT("真实选中原始 DataIndex"), S->GetSelectedMenuIndex(), Rows[Row]);
        TestEqual(TEXT("真实配方页处于显示状态"), Switch->GetActiveWidgetIndex(), 1);
        const int32 Count = E.Ingredients.Num();
        if (!TestTrue(TEXT("十款每款完整展示两或三原料，不截断"), Count == 2 || Count == 3)) return false;
        CheckedTwo |= Count == 2;
        CheckedThree |= Count == 3;
        const float Width = Count == 2 ? 414.f : 272.f;
        const FString Ice = E.Recipe->ExpectedIceCount > 0 ? TEXT("加冰即可") : TEXT("不加冰");
        const FString Total = FString::Printf(TEXT("%.0f mL"), E.Recipe->TargetVolumeML);
        TArray<FString> ExpectedLines = { E.DisplayName.ToString(), E.CupName.ToString(), Ice, TEXT("液体总量  ") + Total };
        const TCHAR* CoreNames[] = { TEXT("MenuDrinkTitle"), TEXT("MenuCupName"), TEXT("MenuIceCount"), TEXT("MenuTargetVolume") };
        const FString CoreValues[] = { E.DisplayName.ToString(), E.CupName.ToString(), Ice, Total };
        const TCHAR* CoreParents[] = { TEXT("MenuDetailsColumn"), TEXT("MenuCupCard"), TEXT("MenuIceCard"), TEXT("MenuTotalCard") };
        for (int32 I = 0; I < 4; ++I)
        {
            auto* Text = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(CoreNames[I])));
            if (!TestNotNull(TEXT("实际核心文字控件"), Text)) return false;
            TestEqual(Text->GetName() + TEXT("：同源核心内容"), Text->GetText().ToString(), CoreValues[I]);
            TestTrue(Text->GetName() + TEXT("：核心文字可见"), Text->GetVisibility() == ESlateVisibility::HitTestInvisible);
            TestTrue(Text->GetName() + TEXT("：核心属于正确详情卡而非旧空余区域"), Text->GetParent()
                && Text->GetParent()->GetFName() == FName(CoreParents[I]));
        }
        for (int32 I = 0; I < 3; ++I)
        {
            auto* Card = Cast<UCanvasPanel>(Widget->WidgetTree->FindWidget(FName(CardNames[I])));
            if (!TestNotNull(TEXT("实际杯冰总量卡片"), Card)) return false;
            TestTrue(TEXT("杯冰总量卡片仍可见且属于详情列"), Card->GetVisibility() == ESlateVisibility::SelfHitTestInvisible
                && Card->GetParent() == Details);
            CheckSlot(Card, CardPositions[I], CardSizes[I]);
        }
        auto* CupIcon = Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("MenuCupIcon")));
        auto* IceIcon = Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("MenuIceIcon")));
        if (!TestNotNull(TEXT("真实杯图"), CupIcon) || !TestNotNull(TEXT("真实冰图"), IceIcon)
            || !TestNotNull(TEXT("保存杯图资源"), E.CupIcon.Get()) || !TestNotNull(TEXT("保存冰图资源"), S->RecipeMenu->IceIcon.Get())) return false;
        TestTrue(TEXT("指定杯图真实绑定且可见"), CupIcon->GetBrush().GetResourceObject() == E.CupIcon.Get()
            && CupIcon->GetVisibility() == ESlateVisibility::HitTestInvisible);
        TestTrue(TEXT("杯冰图仍位于对应核心卡内"), CupIcon->GetParent() && IceIcon->GetParent()
            && CupIcon->GetParent()->GetFName() == FName(TEXT("MenuCupCard")) && IceIcon->GetParent()->GetFName() == FName(TEXT("MenuIceCard")));
        CheckSlot(CupIcon, FVector2D(10.f, 12.f), FVector2D(128.f, 128.f));
        CheckSlot(IceIcon, FVector2D(8.f, 25.f), FVector2D(94.f, 94.f));
        TestTrue(TEXT("冰图按需真实绑定或清空隐藏"), IceIcon->GetBrush().GetResourceObject() == (E.Recipe->ExpectedIceCount > 0 ? S->RecipeMenu->IceIcon.Get() : nullptr)
            && IceIcon->GetVisibility() == (E.Recipe->ExpectedIceCount > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden));
        for (int32 I = 0; I < 3; ++I)
        {
            auto* Card = Cast<UCanvasPanel>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIngredientCard%d"), I))));
            auto* Background = Cast<UBorder>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIngredientCard%dBackground"), I))));
            auto* Icon = Cast<UImage>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIngredientIcon%d"), I))));
            auto* Name = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIngredientName%d"), I))));
            auto* Volume = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("MenuIngredientVolume%d"), I))));
            if (!TestNotNull(TEXT("真实原料卡"), Card) || !TestNotNull(TEXT("真实原料背景"), Background)
                || !TestNotNull(TEXT("真实原料图"), Icon) || !TestNotNull(TEXT("真实原料名"), Name)
                || !TestNotNull(TEXT("真实原料量"), Volume)) return false;
            if (I >= Count)
            {
                TestTrue(TEXT("两列时第三卡折叠且不残留上一款原料图文"), Card->GetVisibility() == ESlateVisibility::Collapsed
                    && Icon->GetBrush().GetResourceObject() == nullptr && Icon->GetVisibility() == ESlateVisibility::Hidden
                    && Name->GetText().IsEmpty() && Volume->GetText().IsEmpty());
                continue;
            }
            const auto& P = E.Ingredients[I];
            const FString IngredientName = UDrinkMenuData::IngredientDisplayName(P.IngredientId).ToString();
            const FString ML = FString::Printf(TEXT("%.0f mL"), P.VolumeML);
            ExpectedLines.Add(IngredientName + TEXT("  ") + ML);
            TestTrue(TEXT("原料卡与图名量仍真实可见"), Card->GetVisibility() == ESlateVisibility::SelfHitTestInvisible
                && Icon->GetVisibility() == ESlateVisibility::HitTestInvisible && Name->GetVisibility() == ESlateVisibility::HitTestInvisible
                && Volume->GetVisibility() == ESlateVisibility::HitTestInvisible);
            TestTrue(TEXT("原料卡仍属于详情列，图名量属于该卡"), Card->GetParent() == Details
                && Icon->GetParent() == Card && Name->GetParent() == Card && Volume->GetParent() == Card && Background->GetParent() == Card);
            if (!TestNotNull(TEXT("保存原料图资源"), P.Icon.Get())) return false;
            TestTrue(TEXT("原料图绑定正确资源而非文字替代"), Icon->GetBrush().GetResourceObject() == P.Icon.Get());
            TestEqual(TEXT("原料中文名完整"), Name->GetText().ToString(), IngredientName);
            TestFalse(TEXT("原料标签不以英文 ID 兜底"), Name->GetText().ToString().Contains(P.IngredientId.ToString()));
            TestEqual(TEXT("原料精确 mL 完整"), Volume->GetText().ToString(), ML);
            CheckSlot(Card, FVector2D(30.f + I * (Width + 12.f), 275.f), FVector2D(Width, 214.f));
            CheckSlot(Background, FVector2D::ZeroVector, FVector2D(Width, 214.f));
            CheckSlot(Icon, FVector2D((Width - 128.f) * .5f, 8.f), FVector2D(128.f, 128.f));
            CheckSlot(Name, FVector2D(12.f, 140.f), FVector2D(Width - 24.f, 32.f));
            CheckSlot(Volume, FVector2D(12.f, 175.f), FVector2D(Width - 24.f, 38.f));
            TestTrue(TEXT("中文标签与 mL 文字实际居中"), Justification->GetPropertyValue_InContainer(Name) == uint8(ETextJustify::Center)
                && Justification->GetPropertyValue_InContainer(Volume) == uint8(ETextJustify::Center));
        }
        const FString MenuText = Widget->GetMenuText().ToString();
        TestEqual(TEXT("全文只含中文饮品杯冰总量与无编号原料行"), MenuText, FString::Join(ExpectedLines, TEXT("\n")));
        for (const TCHAR* Removed : RemovedText)
        {
            auto* Text = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(Removed)));
            if (!TestNotNull(FString(Removed) + TEXT("：旧命名控件保留"), Text)) return false;
            TestTrue(FString(Removed) + TEXT("：每款更新后仍 empty 且 Collapsed"), Text->GetText().IsEmpty()
                && Text->GetVisibility() == ESlateVisibility::Collapsed);
        }
        auto* Footer = Cast<UBorder>(Widget->WidgetTree->FindWidget(TEXT("MenuFooterDivider")));
        if (!TestNotNull(TEXT("旧页脚分隔线保留"), Footer)) return false;
        TestTrue(TEXT("页脚分隔线 Collapsed 且没有内容"), Footer->GetVisibility() == ESlateVisibility::Collapsed && Footer->GetContent() == nullptr);
        FString VisibleText = MenuText;
        TArray<UWidget*> Controls;
        Widget->WidgetTree->GetAllWidgets(Controls);
        for (UWidget* Control : Controls)
        {
            auto* Text = Cast<UTextBlock>(Control);
            if (!Text || Text->GetText().IsEmpty()) continue;
            UWidget* Cursor = Text;
            FVector2D Position = FVector2D::ZeroVector;
            bool bVisible = true;
            while (Cursor && Cursor != Details)
            {
                if (Cursor->GetVisibility() == ESlateVisibility::Collapsed || Cursor->GetVisibility() == ESlateVisibility::Hidden) bVisible = false;
                if (auto* Slot = Cast<UCanvasPanelSlot>(Cursor->Slot)) Position += Slot->GetPosition();
                Cursor = Cursor->GetParent();
            }
            if (Cursor != Details || !bVisible) continue;
            VisibleText += TEXT("\n") + Text->GetText().ToString();
            auto* Slot = Cast<UCanvasPanelSlot>(Text->Slot);
            if (!TestNotNull(TEXT("可见详情文字使用实际 CanvasSlot"), Slot)) return false;
            TestTrue(Text->GetName() + TEXT("：核心不散落旧下半屏"), Position.Y >= 0.f && Position.Y + Slot->GetSize().Y <= 489.f);
        }
        const TCHAR* RemovedWords[] = { TEXT("BNT"), TEXT("CAFE"), TEXT("AZ"), TEXT("A-Z"), TEXT("A–Z"), TEXT("Index"),
            TEXT("先取"), TEXT("顺序倒入"), TEXT("依次倒入"), TEXT("ratio"), TEXT("%"), TEXT("验收"), TEXT("范围"), TEXT("本局最近实际") };
        for (const TCHAR* Word : RemovedWords) TestFalse(FString(Word) + TEXT("：无缩写或长操作验收文字"), VisibleText.Contains(Word));
        for (const auto& Entry : S->RecipeMenu->Entries)
            TestFalse(TEXT("全部 PinyinIndex 只用于数据，不用于显示"), VisibleText.Contains(Entry.PinyinIndex.ToString()));
        TestNull(TEXT("全十款浏览没有创建真实订单"), S->GetCurrentRecipe());
        TestNull(TEXT("全十款浏览没有伪造预览"), S->GetPresentedRecipe());
        TestEqual(TEXT("全十款浏览不推进 serial"), S->GetCurrentOrderSerial(), int64(0));
        TestEqual(TEXT("全十款浏览不生成历史"), S->GetDeliveryRecords().Num(), 0);
        TestEqual(TEXT("删除页脚不删除真实历史空态"), Widget->GetHistoryText().ToString(), FString(TEXT("暂无本局交付")));
    }
    TestTrue(TEXT("真实检查了动态两列和三列"), CheckedTwo && CheckedThree);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPalette, "CyberCafe.RecipeMenu.RealUMGDarkPaletteAndButtonStates", RecipeMenuTest::MenuTestFlags)
bool FMenuPalette::RunTest(const FString&)
{
    RecipeMenuTest::World F;
    if (!TestNotNull(TEXT("保存屏幕"), F.Screen)) return false;
    F.Begin();
    auto* S = F.Screen;
    auto* Widget = S->GetDisplayWidget();
    if (!TestNotNull(TEXT("真实保存 WBP 实例"), Widget) || !TestNotNull(TEXT("真实 UMG 控件树"), Widget->WidgetTree.Get())
        || !TestNotNull(TEXT("真实保存菜单"), S->RecipeMenu.Get())) return false;
    auto* Root = Cast<USizeBox>(Widget->WidgetTree->RootWidget);
    auto* Body = Cast<UBorder>(Widget->WidgetTree->FindWidget(TEXT("OpaqueBody")));
    auto* Hint = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("MenuIceHint")));
    if (!TestNotNull(TEXT("真实固定画布根"), Root) || !TestNotNull(TEXT("真实不透明底板"), Body)
        || !TestNotNull(TEXT("真实冰量提示"), Hint)) return false;
    TestEqual(TEXT("根画布宽度仍为 1200"), Root->GetWidthOverride(), 1200.f);
    TestEqual(TEXT("根画布高度仍为 750"), Root->GetHeightOverride(), 750.f);
    TestTrue(TEXT("世界屏幕绘制尺寸仍为 1200x750"), S->Panel->GetDrawSize() == FIntPoint(1200, 750));
    auto* BodySlot = Cast<UCanvasPanelSlot>(Body->Slot);
    auto* HintSlot = Cast<UCanvasPanelSlot>(Hint->Slot);
    if (!TestNotNull(TEXT("真实底板 CanvasSlot"), BodySlot) || !TestNotNull(TEXT("真实冰量提示 CanvasSlot"), HintSlot)) return false;
    TestTrue(TEXT("底板仍覆盖原画布且无位移"), BodySlot->GetPosition().Equals(FVector2D::ZeroVector)
        && BodySlot->GetSize().Equals(FVector2D(1200.f, 750.f)));
    TestTrue(TEXT("冰量提示保留原 160x40 尺寸及位置"), HintSlot->GetSize().Equals(FVector2D(160.f, 40.f))
        && HintSlot->GetPosition().Equals(FVector2D(110.f, 101.f)));
    TestTrue(TEXT("原位置冰量小字真实清空并折叠"), Hint->GetText().IsEmpty()
        && Hint->GetVisibility() == ESlateVisibility::Collapsed);

    const FColor Paper(17, 29, 36), Panel(21, 37, 45), Card(32, 52, 61), Ink(227, 237, 234);
    const FColor Muted(183, 201, 201), Line(66, 90, 100), Teal(128, 213, 197), Accent(42, 75, 80);
    const FColor SelectedBorder(80, 124, 122), Pressed(53, 93, 96);
    const TCHAR* Backgrounds[] = { TEXT("OrderColumnBackground"), TEXT("CustomerColumnBackground"),
        TEXT("MenuCupCardBackground"), TEXT("MenuIceCardBackground"), TEXT("MenuTotalCardBackground"),
        TEXT("MenuIngredientCard0Background"), TEXT("MenuIngredientCard1Background"), TEXT("MenuIngredientCard2Background") };
    for (const TCHAR* Name : Backgrounds)
    {
        if (!TestNotNull(FString::Printf(TEXT("真实卡片背景 %s"), Name),
            Cast<UBorder>(Widget->WidgetTree->FindWidget(FName(Name))))) return false;
    }
    TArray<UWidget*> Controls;
    Widget->WidgetTree->GetAllWidgets(Controls);
    TSet<int32> SelectedActions, UnselectedActions, DisabledActions;
    const auto CheckDarkBrush = [this](const FString& Name, const FSlateBrush& Brush, const FColor& Expected)
    {
        const FLinearColor Color = Brush.TintColor.GetSpecifiedColor();
        TestTrue(Name + TEXT("：实际 brush sRGB 等于指定暗色"), Color.ToFColor(true) == Expected);
        TestTrue(Name + TEXT("：实际 brush 不透明且保持暗底"), FMath::IsNearlyEqual(Color.A, 1.f)
            && (Color.R + Color.G + Color.B) / 3.f < .15f);
    };
    const auto CheckPalette = [&]()
    {
        int32 BackgroundCount = 0, TextCount = 0, ButtonCount = 0;
        const TArray<int32> Rows = S->GetVisibleMenuIndices();
        for (UWidget* Control : Controls)
        {
            const FString Name = Control->GetName();
            if (auto* Border = Cast<UBorder>(Control))
            {
                const bool bCard = Name.EndsWith(TEXT("Background"));
                BackgroundCount += bCard ? 1 : 0;
                CheckDarkBrush(Name, Border->Background, Name == TEXT("OpaqueBody") ? Panel : (bCard ? Card : Line));
                TestTrue(Name + TEXT("：边框 brush 调制仍为不透明白色"), Border->GetBrushColor().Equals(FLinearColor::White));
            }
            if (auto* Text = Cast<UTextBlock>(Control))
            {
                ++TextCount;
                const FLinearColor Color = Text->GetColorAndOpacity().GetSpecifiedColor();
                const bool bMuted = Color.Equals(FLinearColor::FromSRGBColor(Muted));
                TestTrue(Name + TEXT("：实际文字使用浅色 Ink、Muted 或 Teal"), bMuted
                    || Color.Equals(FLinearColor::FromSRGBColor(Ink)) || Color.Equals(FLinearColor::FromSRGBColor(Teal)));
                // 检查线性 RGB 亮度；次级文字阈值 0.25，主文字与强调文字阈值 0.35。
                TestTrue(Name + TEXT("：实际文字保持可读亮度及不透明度"), (Color.R + Color.G + Color.B) / 3.f > (bMuted ? .25f : .35f)
                    && FMath::IsNearlyEqual(Color.A, 1.f));
            }
            if (auto* Button = Cast<URecipeScreenButton>(Control))
            {
                ++ButtonCount;
                const int32 Action = Button->Action;
                bool bSelected = false;
                FIntRect ExpectedRect(0, 0, 0, 0);
                if (Action == 0) { bSelected = !S->IsRecipeMenuMode(); ExpectedRect = FIntRect(230, 14, 410, 76); }
                else if (Action == 1) { bSelected = S->IsRecipeMenuMode(); ExpectedRect = FIntRect(425, 14, 625, 76); }
                else if (Action == 2) ExpectedRect = FIntRect(50, 510, 620, 573);
                else if (Action >= 10 && Action <= 12)
                {
                    bSelected = S->GetMenuFilter() == Action - 10;
                    const int32 X = 24 + (Action - 10) * 84;
                    ExpectedRect = FIntRect(X, 132, X + 80, 170);
                }
                else if (Action >= 100 && Action < 110)
                {
                    const int32 Row = Action - 100;
                    bSelected = Rows.IsValidIndex(Row) && Rows[Row] == S->GetSelectedMenuIndex();
                    const int32 Y = 184 + Row * 48;
                    ExpectedRect = FIntRect(24, Y, 278, Y + 46);
                }
                TestTrue(Name + TEXT("：原屏幕动作 Rect 不变"), UOrderHologramWidget::GetScreenActionRect(Action) == ExpectedRect);
                auto* Slot = Cast<UCanvasPanelSlot>(Button->Slot);
                if (TestNotNull(Name + TEXT("：真实按钮 CanvasSlot"), Slot))
                {
                    const FVector2D Origin(0.f, Action <= 1 ? 0.f : 88.f);
                    TestTrue(Name + TEXT("：真实按钮位置及尺寸仍对应 Rect"), Slot->GetPosition().Equals(
                        FVector2D(ExpectedRect.Min.X, ExpectedRect.Min.Y) - Origin)
                        && Slot->GetSize().Equals(FVector2D(ExpectedRect.Width(), ExpectedRect.Height())));
                }
                const FButtonStyle& Style = Button->GetStyle();
                CheckDarkBrush(Name + TEXT(" Normal"), Style.Normal, bSelected ? Accent : Panel);
                CheckDarkBrush(Name + TEXT(" Hovered"), Style.Hovered, Accent);
                CheckDarkBrush(Name + TEXT(" Pressed"), Style.Pressed, Pressed);
                CheckDarkBrush(Name + TEXT(" Disabled"), Style.Disabled, Paper);
                TestTrue(Name + TEXT("：选中轮廓使用指定色，未选中透明"), Style.Normal.OutlineSettings.Color.GetSpecifiedColor().Equals(
                    bSelected ? FLinearColor::FromSRGBColor(SelectedBorder) : FLinearColor::Transparent));
                TestTrue(Name + TEXT("：悬停与按下轮廓仍使用 Line 与 Teal"), Style.Hovered.OutlineSettings.Color.GetSpecifiedColor().Equals(
                    FLinearColor::FromSRGBColor(Line)) && Style.Pressed.OutlineSettings.Color.GetSpecifiedColor().Equals(FLinearColor::FromSRGBColor(Teal)));
                TestTrue(Name + TEXT("：按钮调制不使背景或文字变色"), Button->GetBackgroundColor().Equals(FLinearColor::White)
                    && Button->GetColorAndOpacity().Equals(FLinearColor::White));
                auto* Label = Cast<UTextBlock>(Button->GetContent());
                if (TestNotNull(Name + TEXT("：真实按钮文字"), Label))
                {
                    TestTrue(Name + TEXT("：选中与未选中文字保持浅色"), Label->GetColorAndOpacity().GetSpecifiedColor().Equals(
                        FLinearColor::FromSRGBColor(bSelected ? Teal : Ink)));
                }
                if (bSelected) SelectedActions.Add(Action);
                else UnselectedActions.Add(Action);
                if (!Button->GetIsEnabled()) DisabledActions.Add(Action);
            }
        }
        TestEqual(TEXT("真实检查全部八个卡片背景"), BackgroundCount, 8);
        TestTrue(TEXT("真实收集全部 UTextBlock，不能空测"), TextCount > 0);
        TestEqual(TEXT("真实检查全部十六个屏幕按钮"), ButtonCount, 16);
    };

    TestFalse(TEXT("初始关闭以检查真实 disabled 状态"), S->IsPanelOpen());
    CheckPalette();
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        if (!TestTrue(TEXT("实体电源真实开屏"), F.TouchPower())) return false;
        F.Tick(20);
        CheckPalette();
        for (const auto& Entry : S->RecipeMenu->Entries)
        {
            if (!TestTrue(TEXT("逐款真实行点击反复更新 selected/unselected 样式"),
                TutorialMenuTestHelpers::FindRecipe(S, Entry.PinyinIndex))) return false;
            CheckPalette();
        }
        for (int32 Filter = 1; Filter <= 2; ++Filter)
        {
            if (!TestTrue(TEXT("真实切换分类选中状态"), TutorialMenuTestHelpers::TouchAction(S, 10 + Filter))) return false;
            CheckPalette();
        }
        if (!TestTrue(TEXT("真实恢复全部分类"), TutorialMenuTestHelpers::TouchAction(S, 10))) return false;
        CheckPalette();
        if (!TestTrue(TEXT("真实切回订单页签"), TutorialMenuTestHelpers::TouchAction(S, 0))) return false;
        CheckPalette();
        if (!TestTrue(TEXT("真实切回配方页签"), TutorialMenuTestHelpers::TouchAction(S, 1))) return false;
        CheckPalette();
        if (!TestTrue(TEXT("实体电源真实关屏恢复 disabled 状态"), F.TouchPower())) return false;
        TestFalse(TEXT("关屏状态确实生效"), S->IsPanelOpen());
        CheckPalette();
        F.Tick(20);
    }
    TestEqual(TEXT("两页签、三分类与十行都检查实际选中样式"), SelectedActions.Num(), 15);
    TestEqual(TEXT("全部按钮都检查实际未选中样式"), UnselectedActions.Num(), 16);
    TestEqual(TEXT("全部按钮都检查实际禁用样式"), DisabledActions.Num(), 16);
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
            || !TestTrue(TEXT("保存冰量字段仍为无冰 0 或旧建议量 3"), R->ExpectedIceCount == 0 || R->ExpectedIceCount == 3)) return false;
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
    TestEqual(TEXT("保存配方保留旧建议量 3，不作为精确验收要求"), Recipe->ExpectedIceCount, 3);
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
    auto* Widget = F.Screen->GetDisplayWidget();
    if (!TestNotNull(TEXT("真实历史 UMG 控件"), Widget) || !TestNotNull(TEXT("真实历史控件树"), Widget->WidgetTree.Get())) return false;
    TestEqual(TEXT("浏览菜单不伪造本局历史 UI"), Widget->GetHistoryText().ToString(), FString(TEXT("暂无本局交付")));
    TestFalse(TEXT("精简配方全文没有历史页脚"), Widget->GetMenuText().ToString().Contains(TEXT("本局最近实际")));
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
    const FString HistoryText = Widget->GetHistoryText().ToString();
    TestEqual(TEXT("真实历史全文只记录实际交付冰拿铁和实际 200 mL"), HistoryText,
        FString(TEXT("冰拿铁 · 已交付\n实际 200 mL")));
    TestFalse(TEXT("真实交付后历史不是空态或伪造评价"), HistoryText.Contains(TEXT("暂无本局交付"))
        || HistoryText.Contains(TEXT("刚刚好")) || HistoryText.Contains(TEXT("喜欢")));
    for (int32 I = 0; I < 3; ++I)
    {
        auto* Name = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryName%d"), I))));
        auto* Volume = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryVolume%d"), I))));
        auto* Row = Cast<UCanvasPanel>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryRecord%d"), I))));
        if (!TestNotNull(TEXT("真实历史名"), Name) || !TestNotNull(TEXT("真实历史量"), Volume)
            || !TestNotNull(TEXT("真实历史画布"), Row) || !TestNotNull(TEXT("真实历史行容器"), Row->GetParent())) return false;
        TestTrue(TEXT("三行历史按真实记录显示或折叠"), Row->GetParent()->GetVisibility()
            == (I == 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed));
        TestEqual(TEXT("真实历史名无浏览饮品残留"), Name->GetText().ToString(), I == 0 ? FString(TEXT("冰拿铁 · 已交付")) : FString());
        TestEqual(TEXT("真实历史量无虚构额外记录"), Volume->GetText().ToString(), I == 0 ? FString(TEXT("实际 200 mL")) : FString());
    }
    TestFalse(TEXT("同杯再次提交不能复制历史"), Spot->TrySubmitCup(Cup));
    TestEqual(TEXT("浏览和重复提交不复制历史"), F.Screen->GetDeliveryRecords().Num(), 1);
    return true;
}
#endif
