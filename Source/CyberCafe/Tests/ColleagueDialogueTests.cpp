// 文案来源：colleague_dialogue/source_rows.json 的 C2..C11；十行映射十一字段，C8 使用两次。
// 仅只读保存资产，修改只发生在瞬态夹具；不保存资产，不宣称头显或音频实测。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "Liquid/LiquidComposition.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/ServingSpotActor.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialStep.h"
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/Steps/TutorialStep_WaitForRecipeMenu.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "UI/OrderHologramWidget.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace ColleagueDialogueTests
{
constexpr auto Options = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const TCHAR* const LattePath = TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte");
const TCHAR* const MenuPath = TEXT("/Game/VRTemplate/MenuIngredients/Menu/DA_RecipeMenu.DA_RecipeMenu");
// 保留原字符、空格与标点；尤其 C7 是“么”，不能替换成“吗”。
const TCHAR* const ExcelTexts[] = {
    TEXT("你醒了？……刚刚说到我教你做一杯拿铁，你就睡着了。快，先看看你的台子。"),
    TEXT("台子上有咖啡液和牛奶，把瓶盖拿掉，往杯子里倒。"),
    TEXT("空杯子在你左手边。"),
    TEXT("够不着就别探身子了。左手按 X，或者右手按 A，隔空就能拿过来。"),
    TEXT("给我做一杯拿铁。咖啡和牛奶差不多一半一半，加起来一百八到两百毫升。做好了放到吧台前面，不对的话拿回去改。"),
    TEXT("咖啡多了，苦得像上城的公告，你是不想让我睡觉了么。再补点牛奶，杯子要是满了，先倒掉一点。"),
    TEXT("分量对，比例也对。这杯我收下了。"),
    TEXT("奶多了，喝着像热牛奶，现在也不是睡觉的时间吧？补点咖啡，杯子要是满了，先倒掉一点。"),
    TEXT("……嗯，奶和咖啡融得挺好。这杯是给人喝的了。"),
    TEXT("行了。欢迎来到檐下。")
};
const TCHAR* const GenericRatio = TEXT("配比还没对上。照订单调整；杯子太满时先倒掉一些。");
const TCHAR* const GenericAccepted = TEXT("饮品份量和配比符合订单。");
const TCHAR* const GenericAcceptedWithIce = TEXT("饮品份量、配比和冰要求符合订单。");
const TCHAR* const GenericWrongIce = TEXT("这杯需要加冰。有冰即可，不限块数；补一些冰再送来。");

const TCHAR* Excel(int32 Row) { return ExcelTexts[Row - 2]; }

UDrinkRecipe* SavedLatte(FAutomationTestBase& Test)
{
    auto* Recipe = LoadObject<UDrinkRecipe>(nullptr, LattePath);
    if (!Test.TestNotNull(TEXT("读取原保存 DA_Latte"), Recipe)) return nullptr;
    Test.TestEqual(TEXT("权威 RecipeId 保持 Latte"), Recipe->RecipeId, FName(TEXT("Latte")));
    return Recipe;
}

void CheckComposition(FAutomationTestBase& Test, const FString& Label,
    const FLiquidComposition& Actual, const FLiquidComposition& Expected)
{
    Test.TestEqual(Label + TEXT("成分项数"), Actual.Ingredients.Num(), Expected.Ingredients.Num());
    for (const auto& Pair : Expected.Ingredients)
    {
        Test.TestTrue(Label + Pair.Key.ToString() + TEXT("身份存在"), Actual.Ingredients.Contains(Pair.Key));
        Test.TestEqual(Label + Pair.Key.ToString() + TEXT("权威 mL"), Actual.Get(Pair.Key), Pair.Value, 0.0);
    }
}

void CheckRules(FAutomationTestBase& Test, const UDrinkRecipe* Recipe,
    const TArray<FDrinkIngredientRule>& Expected)
{
    Test.TestEqual(TEXT("共享配方规则数不变"), Recipe->Rules.Num(), Expected.Num());
    for (int32 I = 0; I < FMath::Min(Recipe->Rules.Num(), Expected.Num()); ++I)
    {
        Test.TestEqual(TEXT("共享规则身份和顺序不变"), Recipe->Rules[I].IngredientId, Expected[I].IngredientId);
        Test.TestEqual(TEXT("共享规则最小比例不变"), Recipe->Rules[I].MinFraction, Expected[I].MinFraction, 0.0);
        Test.TestEqual(TEXT("共享规则最大比例不变"), Recipe->Rules[I].MaxFraction, Expected[I].MaxFraction, 0.0);
    }
}

struct FWorld
{
    UWorld* World;
    UGameInstance* GI;
    int32 CupIndex = 0;

    FWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("ColleagueDialogueWorld"));
        World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage(),
            true, ERHIFeatureLevel::SM5, &IV);
        World->SetFlags(RF_Transient);
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine, NAME_None, RF_Transient);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
        World->GetWorldSettings()->NotifyBeginPlay();
    }

    ~FWorld()
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    static void Silence(ACupActor* Cup)
    {
        Cup->ContainerMesh->SetSimulatePhysics(false);
        Cup->ContainerMesh->SetEnableGravity(false);
        Cup->bCanPour = false;
        Cup->GlassBreakProfile = nullptr;
        Cup->LiquidFXTemplate = nullptr;
        Cup->SplashEffectTemplate = nullptr;
        Cup->bEnableSplash = false;
        Cup->bEnablePourAudio = false;
        Cup->PourLoopSound = nullptr;
        Cup->CupIce->ClinkSound = nullptr;
        Cup->LiquidFX->SetAutoActivate(false);
        Cup->LiquidFX->SetAsset(nullptr);
        Cup->PourFX->SetAutoActivate(false);
        Cup->PourFX->SetAsset(nullptr);
    }

    ACupActor* Cup(FAutomationTestBase& Test, UClass* Class)
    {
        if (!Test.TestNotNull(TEXT("真实配方杯类"), Class)
            || !Test.TestTrue(TEXT("杯类派生自 ACupActor"), Class->IsChildOf(ACupActor::StaticClass()))) return nullptr;
        const FTransform Transform(FRotator::ZeroRotator, FVector(250 * CupIndex++, 0, 150),
            TutorialMenuTestHelpers::SceneCupScale(Class));
        auto* Result = World->SpawnActorDeferred<ACupActor>(Class, Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Test.TestNotNull(TEXT("创建真实瞬态杯"), Result)) return nullptr;
        Result->SetFlags(RF_Transient);
        Silence(Result);
        Result->FillAmount = 0.f;
        Result->FinishSpawning(Transform);
        Silence(Result);
        Test.TestTrue(TEXT("杯属于夹具 World"), Result->GetWorld() == World && Result->HasAnyFlags(RF_Transient));
        Test.TestTrue(TEXT("杯执行真实 BeginPlay"), Result->HasActorBegunPlay());
        Test.TestEqual(TEXT("夹具杯初始权威液量为零"), Result->GetPreciseVolumeML(), 0.0);
        Test.TestFalse(TEXT("夹具关闭容器物理模拟"), Result->ContainerMesh->IsSimulatingPhysics());
        Test.TestFalse(TEXT("夹具无水声播放请求"), Result->IsPourAudioRequested());
        return Result;
    }

    ACupActor* Mixture(FAutomationTestBase& Test, const UDrinkRecipe* Recipe,
        float CoffeeML, float MilkML, int32 IceCount)
    {
        auto* Result = Cup(Test, Recipe->AcceptedCupClass.Get());
        if (!Result || !Test.TestNotNull(TEXT("真实杯内冰组件"), Result->CupIce.Get())) return nullptr;
        if (CoffeeML > 0.f)
            Test.TestEqual(TEXT("实际加入 Coffee mL"), Result->AddIngredientLiquid(CoffeeML, TEXT("Coffee"), FLinearColor::White), CoffeeML);
        if (MilkML > 0.f)
            Test.TestEqual(TEXT("实际加入 Milk mL"), Result->AddIngredientLiquid(MilkML, TEXT("Milk"), FLinearColor::White), MilkML);
        const int32 LiquidRevision = Result->GetLiquidRevision();
        if (IceCount > 0 && !Test.TestTrue(TEXT("使用原助手加入真实冰块"),
            TutorialMenuTestHelpers::AddIce(Result, IceCount))) return nullptr;
        Test.TestEqual(TEXT("实际冰组件计数精确匹配"), Result->CupIce->GetIceCount(), IceCount);
        Test.TestEqual(TEXT("冰不增加权威液量"), Result->GetPreciseVolumeML(), static_cast<double>(CoffeeML) + MilkML);
        Test.TestEqual(TEXT("冰不改变液体版本"), Result->GetLiquidRevision(), LiquidRevision);
        return Result;
    }

    UDrinkRecipe* Duplicate(FAutomationTestBase& Test, UDrinkRecipe* Source)
    {
        auto* Result = DuplicateObject<UDrinkRecipe>(Source, World);
        if (!Test.TestNotNull(TEXT("独立配方副本"), Result)) return nullptr;
        Result->ClearFlags(RF_Public | RF_Standalone);
        Result->SetFlags(RF_Transient);
        Test.TestTrue(TEXT("只改瞬态副本不共享资产"), Result != Source && Result->GetOutermost() == GetTransientPackage());
        return Result;
    }
};

FDrinkValidationResult EvaluateReadOnly(FAutomationTestBase& Test, const UDrinkRecipe* Recipe, ACupActor* Cup)
{
    const double Volume = Cup->GetPreciseVolumeML();
    const float Fill = Cup->FillAmount;
    const auto Composition = Cup->GetComposition();
    const int32 LiquidRevision = Cup->GetLiquidRevision();
    const int32 IceRevision = Cup->CupIce->GetPreparationRevision();
    const int32 IceCount = Cup->CupIce->GetIceCount();
    const auto Transform = Cup->GetActorTransform();
    const bool bHeld = Cup->IsHeld();
    auto* Transfer = Cup->GetWorld()->GetSubsystem<ULiquidTransferSubsystem>();
    Test.TestNotNull(TEXT("真实 World 液体账本"), Transfer);
    const FLiquidBalance Before = Transfer ? Transfer->GetBalance() : FLiquidBalance();
    Test.TestFalse(TEXT("Evaluate 前未冻结"), Cup->IsLiquidFrozen());
    const auto Result = Recipe->Evaluate(Cup);
    Test.TestFalse(TEXT("Evaluate 不冻结杯子"), Cup->IsLiquidFrozen());
    Test.TestEqual(TEXT("Evaluate 不改变权威液量"), Cup->GetPreciseVolumeML(), Volume, 0.0);
    Test.TestEqual(TEXT("Evaluate 不改变显示液面"), Cup->FillAmount, Fill, 0.f);
    Test.TestEqual(TEXT("Evaluate 不改变液体版本"), Cup->GetLiquidRevision(), LiquidRevision);
    Test.TestEqual(TEXT("Evaluate 不改变冰版本"), Cup->CupIce->GetPreparationRevision(), IceRevision);
    Test.TestEqual(TEXT("Evaluate 不改变真实冰数"), Cup->CupIce->GetIceCount(), IceCount);
    Test.TestEqual(TEXT("Evaluate 不改变持有状态"), Cup->IsHeld(), bHeld);
    Test.TestTrue(TEXT("Evaluate 不移动或锁定杯子"), Cup->GetActorTransform().Equals(Transform, 0.0));
    CheckComposition(Test, TEXT("Evaluate 后"), Cup->GetComposition(), Composition);
    Test.TestEqual(TEXT("结果记录真实总液量"), Result.TotalML, Volume, 0.0);
    Test.TestEqual(TEXT("结果记录当前液体版本"), Result.LiquidRevision, LiquidRevision);
    Test.TestEqual(TEXT("结果记录真实冰数"), Result.IceCount, IceCount);
    Test.TestEqual(TEXT("结果记录当前冰版本"), Result.IceRevision, IceRevision);
    Test.TestEqual(TEXT("结果保留真实配方 ID"), Result.RecipeId, Recipe->RecipeId);
    CheckComposition(Test, TEXT("Evaluate 快照"), Result.Composition, Composition);
    if (Transfer)
    {
        const auto After = Transfer->GetBalance();
        Test.TestEqual(TEXT("Evaluate 不改变 InitialML"), After.InitialML, Before.InitialML, 0.0);
        Test.TestEqual(TEXT("Evaluate 不改变 InjectedML"), After.InjectedML, Before.InjectedML, 0.0);
        Test.TestEqual(TEXT("Evaluate 不改变 RemovedML"), After.RemovedML, Before.RemovedML, 0.0);
        Test.TestEqual(TEXT("Evaluate 不改变 ContainerML"), After.ContainerML, Before.ContainerML, 0.0);
        Test.TestEqual(TEXT("Evaluate 不改变 InFlightML"), After.InFlightML, Before.InFlightML, 0.0);
        Test.TestEqual(TEXT("Evaluate 不改变 SpilledML"), After.SpilledML, Before.SpilledML, 0.0);
        Test.TestEqual(TEXT("Evaluate 不改变 ErrorML"), After.ErrorML, Before.ErrorML, 0.0);
        Test.TestEqual(TEXT("Evaluate 不改变 PacketCount"), After.PacketCount, Before.PacketCount);
        Test.TestTrue(TEXT("真实账本守恒"), FMath::Abs(After.ErrorML) < 1.e-6);
    }
    return Result;
}

void CheckResult(FAutomationTestBase& Test, const FDrinkValidationResult& Result,
    EDrinkValidationReason Reason, const TCHAR* Feedback)
{
    Test.TestEqual(TEXT("真实 Evaluate 原因"), Result.Reason, Reason);
    Test.TestEqual(TEXT("真实 Evaluate 接受状态"), Result.bAccepted, Reason == EDrinkValidationReason::Accepted);
    Test.TestEqualSensitive(TEXT("反馈全文精确匹配"), Result.Feedback.ToString(), FString(Feedback));
}

bool Direction(FAutomationTestBase& Test, bool bCoffeeHigh)
{
    auto* Recipe = SavedLatte(Test);
    if (!Recipe || !Test.TestNotNull(TEXT("测试需要 Engine"), GEngine)) return false;
    FWorld F;
    auto* Cup = F.Mixture(Test, Recipe, bCoffeeHigh ? 130.f : 70.f, bCoffeeHigh ? 70.f : 130.f, 1);
    if (!Cup) return false;
    const auto Result = EvaluateReadOnly(Test, Recipe, Cup);
    Test.TestEqual(TEXT("失败仍为真实 200 mL"), Result.TotalML, 200.0);
    Test.TestEqual(TEXT("咖啡实际占比"), Result.CoffeeFraction, bCoffeeHigh ? .65 : .35);
    CheckResult(Test, Result, EDrinkValidationReason::WrongRatio, Excel(bCoffeeHigh ? 7 : 9));
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColleagueSavedTenLinesExact,
    "CyberCafe.ColleagueDialogue.SavedTenLinesExact", ColleagueDialogueTests::Options)
bool FColleagueSavedTenLinesExact::RunTest(const FString&)
{
    using namespace ColleagueDialogueTests;
    auto* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
    if (!TestNotNull(TEXT("读取保存原地图"), Map) || !TestNotNull(TEXT("保存 PersistentLevel"), Map->PersistentLevel.Get())) return false;
    auto* Director = FindObject<ATutorialDirector>(nullptr,
        TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_TutorialDirecter_C_0"));
    auto* Recipe = SavedLatte(*this);
    if (!TestNotNull(TEXT("保存原导演实例"), Director) || !Recipe
        || !TestEqual(TEXT("保存地图恰好八步"), Director->Steps.Num(), 8)) return false;
    const TCHAR* const Names[] = {TEXT("TutorialStep_ShowMessage_0"), TEXT("TutorialStep_ShowMessage_1"),
        TEXT("TutorialStep_ShowMessage_2"), TEXT("TutorialStep_ShowMessage_3"),
        TEXT("TutorialStep_ShowMessage_Menu_0"), TEXT("TutorialStep_WaitForRecipeMenu_0"),
        TEXT("TutorialStep_WaitForServe_0"), TEXT("TutorialStep_ConsumeServedDrink_0")};
    for (int32 I = 0; I < 8; ++I)
    {
        if (!TestNotNull(TEXT("保存步骤非空"), Director->Steps[I].Get())) return false;
        TestEqual(TEXT("八步身份与顺序保留"), Director->Steps[I]->GetName(), FString(Names[I]));
        TestTrue(TEXT("步骤 Outer 仍为原导演"), Director->Steps[I]->GetOuter() == Director);
    }
    struct FTextMapping { const TCHAR* Label; const FText* Actual; int32 Row; };
    const FTextMapping Mappings[] = {
        {TEXT("Step0.Hint/C2"), &Director->Steps[0]->HintText, 2},
        {TEXT("Step1.Hint/C3"), &Director->Steps[1]->HintText, 3},
        {TEXT("Step2.Hint/C4"), &Director->Steps[2]->HintText, 4},
        {TEXT("Step3.Hint/C5"), &Director->Steps[3]->HintText, 5},
        {TEXT("Step6.Hint/C6"), &Director->Steps[6]->HintText, 6},
        {TEXT("Step6.Completed/C8"), &Director->Steps[6]->CompletedText, 8},
        {TEXT("Step7.Completed/C10"), &Director->Steps[7]->CompletedText, 10},
        {TEXT("Director.Finished/C11"), &Director->FinishedText, 11},
        {TEXT("Recipe.CoffeeHigh/C7"), &Recipe->CoffeeHighFeedback, 7},
        {TEXT("Recipe.MilkHigh/C9"), &Recipe->MilkHighFeedback, 9},
        {TEXT("Recipe.Accepted/C8"), &Recipe->AcceptedFeedback, 8}
    };
    TSet<int32> CoveredRows;
    for (const auto& Mapping : Mappings)
    {
        TestEqualSensitive(Mapping.Label, Mapping.Actual->ToString(), FString(Excel(Mapping.Row)));
        CoveredRows.Add(Mapping.Row);
    }
    TestEqual(TEXT("精确核对十一目标字段"), static_cast<int32>(UE_ARRAY_COUNT(Mappings)), 11);
    TestEqual(TEXT("十一字段覆盖十行 Excel 文案"), CoveredRows.Num(), 10);
    for (int32 Row = 2; Row <= 11; ++Row) TestTrue(TEXT("C2..C11 无遗漏"), CoveredRows.Contains(Row));
    TestEqual(TEXT("说话人仍是时零"), Director->DialogueSpeakerName.ToString(), FString(TEXT("时零")));
    if (!TestNotNull(TEXT("保留原 NPC 外观实例"), Director->NPCRef.Get())) return false;
    TestEqual(TEXT("原 NPC 引用不换人"), Director->NPCRef->GetPathName(),
        FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_TutorialNPC_Manny_C_1")));
    TestTrue(TEXT("原 NPC 仍属于保存地图"), Director->NPCRef->GetWorld() == Map);
    if (!TestNotNull(TEXT("原 NPC 外观组件"), Director->NPCRef->GetMesh())) return false;
    TestEqual(TEXT("时零保留原 BarGirl 外观"), GetPathNameSafe(Director->NPCRef->GetMesh()->GetSkeletalMeshAsset()),
        FString(TEXT("/Game/Characters/BarGirl/SK_BarGirl.SK_BarGirl")));
    TestEqual(TEXT("完成反馈停留配置不变"), Director->CheerDuration, 1.5f);
    for (int32 I : {0, 1, 2, 3, 4})
    {
        auto* Show = Cast<UTutorialStep_ShowMessage>(Director->Steps[I]);
        if (!TestNotNull(TEXT("对话保持 ShowMessage 类型"), Show)) return false;
        TestEqual(TEXT("原阅读时长不变"), Show->Duration, 30.f);
        TestTrue(TEXT("保留逐字和输入跳过"), Show->bUseTypewriter && Show->bAllowSkipByInput);
        TestEqual(TEXT("输入宽限不变"), Show->InputGracePeriod, .3f);
        TestEqual(TEXT("逐字速度不变"), Show->CharsPerSecond, 30.f);
        TestTrue(TEXT("原对话完成字段保持空"), Show->CompletedText.IsEmpty());
        if (I != 1)
        {
            TestNull(TEXT("其他对话不增加自动开屏引用"), Show->FirstOrderMenu.Get());
            TestNull(TEXT("其他对话不增加自动配方引用"), Show->FirstOrderRecipe.Get());
        }
    }
    TestEqual(TEXT("Step4 原菜单介绍全文"), Director->Steps[4]->HintText.ToString(),
        FString(TEXT("菜单上点一下冰拿铁，就能看怎么做。")));
    TestEqual(TEXT("Step5 原操作提示全文"), Director->Steps[5]->HintText.ToString(),
        FString(TEXT("点一下屏幕里的冰拿铁。")));
    TestEqual(TEXT("Step5 原完成全文"), Director->Steps[5]->CompletedText.ToString(),
        FString(TEXT("对，就照这份来。先放一铲冰。")));
    TestEqual(TEXT("Step7 原等待提示全文"), Director->Steps[7]->HintText.ToString(),
        FString(TEXT("时零准备尝一口，稍等一下。")));
    auto* MenuStep = Cast<UTutorialStep_WaitForRecipeMenu>(Director->Steps[5]);
    auto* Serve = Cast<UTutorialStep_WaitForServe>(Director->Steps[6]);
    auto* Consume = Cast<UTutorialStep_ConsumeServedDrink>(Director->Steps[7]);
    auto* First = Cast<UTutorialStep_ShowMessage>(Director->Steps[1]);
    if (!TestNotNull(TEXT("真实菜单步骤"), MenuStep) || !TestNotNull(TEXT("真实交付步骤"), Serve)
        || !TestNotNull(TEXT("真实品尝步骤"), Consume) || !TestNotNull(TEXT("原保存菜单屏"), MenuStep->OrderHologram.Get())
        || !TestNotNull(TEXT("原交付 Spot"), Serve->RequiredSpot.Get())) return false;
    TestEqual(TEXT("必选菜单身份仍 BNT"), MenuStep->RequiredPinyinIndex, FName(TEXT("BNT")));
    TestTrue(TEXT("Show1/Menu5/Serve6 仍同一原屏"), First->FirstOrderMenu == MenuStep->OrderHologram
        && Serve->OrderHologram == MenuStep->OrderHologram && MenuStep->OrderHologram->GetWorld() == Map);
    TestEqual(TEXT("保存原屏身份"), MenuStep->OrderHologram->GetPathName(),
        FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_OrderHologram_Tutorial")));
    TestTrue(TEXT("Show1 与 Serve6 保留同一真实 Latte DA"), First->FirstOrderRecipe.Get() == Recipe && Serve->RequiredRecipe.Get() == Recipe);
    TestEqual(TEXT("原交付 Spot 身份"), Serve->RequiredSpot->GetPathName(),
        FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_ServingSpot_C_1")));
    TestFalse(TEXT("原 Spot 不提前启用交付"), Serve->RequiredSpot->bAcceptSubmissions);
    TestEqual(TEXT("品尝前等待不变"), Consume->PreDrinkDelay, 1.f);
    TestEqual(TEXT("品尝后等待不变"), Consume->AfterDrinkDelay, .3f);
    TestFalse(TEXT("不把原消费变为销毁杯子"), Consume->bDestroyCupInsteadOfHide);
    TestEqual(TEXT("Latte 仍显示冰拿铁"), Recipe->DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("原 Min 180 不变"), Recipe->MinVolumeML, 180.0);
    TestEqual(TEXT("原 Max 220 不按提示改成 200"), Recipe->MaxVolumeML, 220.0);
    TestEqual(TEXT("原 Target 200 不变"), Recipe->TargetVolumeML, 200.0);
    TestEqual(TEXT("原加冰建议 3 不变"), Recipe->ExpectedIceCount, 3);
    TestEqual(TEXT("原 Highball 杯不变"), GetPathNameSafe(Recipe->AcceptedCupClass.Get()),
        FString(TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C")));
    if (!TestEqual(TEXT("原配方恰好两规则"), Recipe->Rules.Num(), 2)) return false;
    TestEqual(TEXT("原 Coffee 规则顺序"), Recipe->Rules[0].IngredientId, FName(TEXT("Coffee")));
    TestEqual(TEXT("原 Milk 规则顺序"), Recipe->Rules[1].IngredientId, FName(TEXT("Milk")));
    for (const auto& Rule : Recipe->Rules)
    {
        TestEqual(TEXT("原规则下界 45%"), Rule.MinFraction, .45);
        TestEqual(TEXT("原规则上界 55%"), Rule.MaxFraction, .55);
    }
    auto* Menu = MenuStep->OrderHologram->RecipeMenu.Get();
    if (!TestNotNull(TEXT("原屏保存菜单"), Menu) || !TestTrue(TEXT("原菜单有效"), Menu->IsValidMenu())) return false;
    const int32 Index = Menu->FindPinyinIndex(TEXT("BNT"));
    if (!TestEqual(TEXT("BNT 仍首项"), Index, 0) || !Menu->Entries.IsValidIndex(Index)) return false;
    TestEqual(TEXT("原菜单和地图同一 DA"), Menu->Entries[Index].Recipe.Get(), Recipe);

    // 只复制 Show1 的配置，并重定向其 Actor 引用到夹具；不进入保存地图的任何步骤。
    if (!TestNotNull(TEXT("自动展示测试需要 Engine"), GEngine)) return false;
    FWorld F;
    auto* Screen = F.World->SpawnActor<AOrderHologramActor>(MenuStep->OrderHologram->GetClass());
    if (!TestNotNull(TEXT("真实原屏类的夹具实例"), Screen)) return false;
    Screen->SetFlags(RF_Transient);
    Screen->RecipeMenu = Menu;
    const FTransform DirectorTM(FVector(0, 500, 0));
    auto* RuntimeDirector = F.World->SpawnActorDeferred<ATutorialDirector>(ATutorialDirector::StaticClass(),
        DirectorTM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("瞬态导演"), RuntimeDirector)) return false;
    RuntimeDirector->SetFlags(RF_Transient);
    RuntimeDirector->bAutoStart = false;
    RuntimeDirector->bDialogueVoice = false;
    RuntimeDirector->FinishedText = FText::GetEmpty();
    RuntimeDirector->FinishSpawning(DirectorTM);
    auto* RuntimeFirst = DuplicateObject<UTutorialStep_ShowMessage>(First, RuntimeDirector);
    if (!TestNotNull(TEXT("只复制原 Show1"), RuntimeFirst)) return false;
    RuntimeFirst->SetFlags(RF_Transient);
    RuntimeFirst->FirstOrderMenu = Screen;
    RuntimeFirst->FirstOrderRecipe = Recipe;
    RuntimeDirector->Steps = {RuntimeFirst};
    TestFalse(TEXT("原 Show1 进入前夹具屏未开"), Screen->IsPanelOpen());
    RuntimeDirector->StartTutorial();
    TestTrue(TEXT("原 Show1 配置仍自动展开真实屏"), Screen->IsPanelOpen() && Screen->HasRevealedFirstOrderRecipe());
    TestFalse(TEXT("自动首屏仍为当前订单页"), Screen->IsRecipeMenuMode());
    TestEqual(TEXT("实际显示原 Latte 配方"), Screen->GetPresentedRecipe(), Recipe);
    TestNull(TEXT("自动预览不提前建立实际订单"), Screen->GetCurrentRecipe());
    TestFalse(TEXT("自动预览不冒充手输入"), Screen->IsPlayerOpened());
    TestFalse(TEXT("自动预览不建立 pending"), Screen->IsPending());
    TestEqual(TEXT("自动预览无触屏动作"), Screen->GetLastScreenAction(), INDEX_NONE);
    TestEqual(TEXT("自动预览无交付历史"), Screen->GetDeliveryRecords().Num(), 0);
    auto* Widget = Screen->GetDisplayWidget();
    if (!TestNotNull(TEXT("真实 UMG 控件"), Widget) || !TestNotNull(TEXT("真实 UMG 树"), Widget->WidgetTree.Get())) return false;
    auto* Switcher = Cast<UWidgetSwitcher>(Widget->WidgetTree->FindWidget(TEXT("OrderMenuSwitcher")));
    if (!TestNotNull(TEXT("实际订单与配方页控件"), Switcher)) return false;
    TestEqual(TEXT("实际 UMG 首屏索引 0"), Switcher->GetActiveWidgetIndex(), 0);
    const FString OrderText = Widget->GetOrderText().ToString();
    TestTrue(TEXT("实际 UMG 展示冰拿铁与 200 mL"), OrderText.Contains(TEXT("冰拿铁")) && OrderText.Contains(TEXT("200 mL")));
    RuntimeDirector->SkipTutorial();
    TestTrue(TEXT("保存 Show1 引用没有被夹具重定向"), First->FirstOrderMenu == MenuStep->OrderHologram && First->FirstOrderRecipe.Get() == Recipe);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColleagueCoffeeHigh,
    "CyberCafe.ColleagueDialogue.CoffeeHigh", ColleagueDialogueTests::Options)
bool FColleagueCoffeeHigh::RunTest(const FString&)
{
    return ColleagueDialogueTests::Direction(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColleagueMilkHigh,
    "CyberCafe.ColleagueDialogue.MilkHigh", ColleagueDialogueTests::Options)
bool FColleagueMilkHigh::RunTest(const FString&)
{
    return ColleagueDialogueTests::Direction(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColleagueReorderedRules,
    "CyberCafe.ColleagueDialogue.ReorderedRules", ColleagueDialogueTests::Options)
bool FColleagueReorderedRules::RunTest(const FString&)
{
    using namespace ColleagueDialogueTests;
    auto* Source = SavedLatte(*this);
    if (!Source || !TestNotNull(TEXT("规则重排测试需要 Engine"), GEngine)
        || !TestEqual(TEXT("原规则恰好两项"), Source->Rules.Num(), 2)) return false;
    const auto OriginalRules = Source->Rules;
    const FString OriginalCoffee = Source->CoffeeHighFeedback.ToString();
    const FString OriginalMilk = Source->MilkHighFeedback.ToString();
    const FString OriginalAccepted = Source->AcceptedFeedback.ToString();
    FWorld F;
    auto* Reordered = F.Duplicate(*this, Source);
    if (!Reordered) return false;
    Reordered->Rules.Swap(0, 1);
    TestEqual(TEXT("副本先检查 Milk"), Reordered->Rules[0].IngredientId, FName(TEXT("Milk")));
    TestEqual(TEXT("副本后检查 Coffee"), Reordered->Rules[1].IngredientId, FName(TEXT("Coffee")));
    for (bool bCoffeeHigh : {true, false})
    {
        auto* Cup = F.Mixture(*this, Source, bCoffeeHigh ? 130.f : 70.f, bCoffeeHigh ? 70.f : 130.f, 1);
        if (!Cup) return false;
        const auto Original = EvaluateReadOnly(*this, Source, Cup);
        const auto Swapped = EvaluateReadOnly(*this, Reordered, Cup);
        CheckResult(*this, Original, EDrinkValidationReason::WrongRatio, Excel(bCoffeeHigh ? 7 : 9));
        CheckResult(*this, Swapped, EDrinkValidationReason::WrongRatio, Excel(bCoffeeHigh ? 7 : 9));
        TestEqual(TEXT("重排不改变方向反馈"), Swapped.Feedback.ToString(), Original.Feedback.ToString());
    }
    CheckRules(*this, Source, OriginalRules);
    TestEqual(TEXT("共享 Coffee 文案不变"), Source->CoffeeHighFeedback.ToString(), OriginalCoffee);
    TestEqual(TEXT("共享 Milk 文案不变"), Source->MilkHighFeedback.ToString(), OriginalMilk);
    TestEqual(TEXT("共享 Accepted 文案不变"), Source->AcceptedFeedback.ToString(), OriginalAccepted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColleagueAcceptedAndLimits,
    "CyberCafe.ColleagueDialogue.AcceptedAndLimits", ColleagueDialogueTests::Options)
bool FColleagueAcceptedAndLimits::RunTest(const FString&)
{
    using namespace ColleagueDialogueTests;
    auto* Recipe = SavedLatte(*this);
    if (!Recipe || !TestNotNull(TEXT("边界测试需要 Engine"), GEngine)) return false;
    FWorld F;
    struct FCase { const TCHAR* Label; float Coffee; float Milk; int32 Ice; EDrinkValidationReason Reason; const TCHAR* Feedback; };
    // 超界值明确超过 1e-6 mL 容差；每项使用独立真实杯和真实冰槽，绝不伪造计数。
    const FCase Cases[] = {
        {TEXT("200mL/100+100/1冰"), 100, 100, 1, EDrinkValidationReason::Accepted, Excel(8)},
        {TEXT("180mL下界/1冰"), 90, 90, 1, EDrinkValidationReason::Accepted, Excel(8)},
        {TEXT("220mL上界/1冰"), 110, 110, 1, EDrinkValidationReason::Accepted, Excel(8)},
        {TEXT("221mL超上界/1冰"), 110.5f, 110.5f, 1, EDrinkValidationReason::TooMuch, TEXT("杯里多了一些。倒掉一点，保留 180–220 mL，再检查配比。")},
        {TEXT("179mL低于下界/1冰"), 89.5f, 89.5f, 1, EDrinkValidationReason::TooLittle, TEXT("还差一点。照订单的配比，补到至少 180 mL。")},
        {TEXT("空杯/0冰"), 0, 0, 0, EDrinkValidationReason::Empty, TEXT("杯子还是空的。照订单加原料，再放回来。")},
        {TEXT("200mL正确比例/0冰"), 100, 100, 0, EDrinkValidationReason::WrongIce, GenericWrongIce},
        {TEXT("200mL/45%Coffee边界"), 90, 110, 1, EDrinkValidationReason::Accepted, Excel(8)},
        {TEXT("200mL/55%Coffee边界"), 110, 90, 1, EDrinkValidationReason::Accepted, Excel(8)},
        {TEXT("200mL/高于55%Coffee"), 111, 89, 1, EDrinkValidationReason::WrongRatio, Excel(7)},
        {TEXT("200mL/高于55%Milk"), 89, 111, 1, EDrinkValidationReason::WrongRatio, Excel(9)},
        {TEXT("仅Coffee/缺Milk"), 200, 0, 1, EDrinkValidationReason::MissingIngredient, TEXT("牛奶还没加。对照订单补上牛奶，再送来。")},
        {TEXT("仅Milk/缺Coffee"), 0, 200, 1, EDrinkValidationReason::MissingIngredient, TEXT("订单里的原料还没齐。对照配方补上，再送来。")}
    };
    for (const auto& Case : Cases)
    {
        AddInfo(Case.Label);
        auto* Cup = F.Mixture(*this, Recipe, Case.Coffee, Case.Milk, Case.Ice);
        if (!Cup) return false;
        CheckResult(*this, EvaluateReadOnly(*this, Recipe, Cup), Case.Reason, Case.Feedback);
    }
    auto* Contaminated = F.Mixture(*this, Recipe, 99, 99, 1);
    if (!Contaminated) return false;
    TestEqual(TEXT("通过真实 API 加入配方外原料"), Contaminated->AddIngredientLiquid(2, TEXT("Water"), FLinearColor::White), 2.f);
    CheckResult(*this, EvaluateReadOnly(*this, Recipe, Contaminated), EDrinkValidationReason::WrongIngredient,
        TEXT("这杯多了配方以外的原料。倒空后重做，或换一只空杯。"));
    // Native ACupActor has no authored ice interior. Use an actual configured
    // Rocks cup for a wrong-class fixture, not a fabricated ice count/geometry.
    auto* WrongClass=LoadClass<ACupActor>(nullptr,TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Rocks.BP_MenuCup_Rocks_C"));
    if(!TestNotNull(TEXT("另一实际杯型Rocks存在"),WrongClass))return false;
    auto* WrongCup = F.Cup(*this, WrongClass);
    if (!WrongCup) return false;
    TestEqual(TEXT("错误杯实际加咖啡"), WrongCup->AddIngredientLiquid(100, TEXT("Coffee"), FLinearColor::White), 100.f);
    TestEqual(TEXT("错误杯实际加奶"), WrongCup->AddIngredientLiquid(100, TEXT("Milk"), FLinearColor::White), 100.f);
    if (!TestTrue(TEXT("错误杯也真实加冰"), TutorialMenuTestHelpers::AddIce(WrongCup, 1))) return false;
    CheckResult(*this, EvaluateReadOnly(*this, Recipe, WrongCup), EDrinkValidationReason::WrongCup,
        TEXT("这只容器不合订单。换指定的杯子，再把饮品倒进去。"));

    auto* Legacy = NewObject<UDrinkRecipe>(F.World, NAME_None, RF_Transient);
    TestTrue(TEXT("原生新配方默认三反馈为空"), Legacy->CoffeeHighFeedback.IsEmpty()
        && Legacy->MilkHighFeedback.IsEmpty() && Legacy->AcceptedFeedback.IsEmpty());
    const auto* Defaults = GetDefault<UDrinkRecipe>();
    TestTrue(TEXT("原生 CDO 默认三反馈为空"), Defaults->CoffeeHighFeedback.IsEmpty()
        && Defaults->MilkHighFeedback.IsEmpty() && Defaults->AcceptedFeedback.IsEmpty());
    TestEqual(TEXT("旧原生配方默认不检查冰"), Legacy->ExpectedIceCount, -1);
    auto* LegacyCup = F.Mixture(*this, Legacy, 100, 100, 0);
    if (!LegacyCup) return false;
    CheckResult(*this, EvaluateReadOnly(*this, Legacy, LegacyCup), EDrinkValidationReason::Accepted, GenericAccepted);
    AddInfo(TEXT("保存配方覆盖15个实际杯边界/拒绝案例，另核对原生默认配方成功回退。"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColleagueOtherRecipesDoNotSpeakAsShiLing,
    "CyberCafe.ColleagueDialogue.OtherRecipesDoNotSpeakAsShiLing", ColleagueDialogueTests::Options)
bool FColleagueOtherRecipesDoNotSpeakAsShiLing::RunTest(const FString&)
{
    using namespace ColleagueDialogueTests;
    auto* Menu = LoadObject<UDrinkMenuData>(nullptr, MenuPath);
    auto* Latte = SavedLatte(*this);
    if (!TestNotNull(TEXT("读取十项保存菜单"), Menu) || !Latte
        || !TestEqual(TEXT("保存菜单恰好十项"), Menu->Entries.Num(), 10)
        || !TestTrue(TEXT("保存菜单仍有效"), Menu->IsValidMenu())
        || !TestNotNull(TEXT("其他配方测试需要 Engine"), GEngine)) return false;
    FWorld F;
    int32 LatteCount = 0;
    int32 OtherCount = 0;
    int32 RatioCases = 0;
    TSet<UDrinkRecipe*> Seen;
    for (const auto& Entry : Menu->Entries)
    {
        auto* Source = Entry.Recipe.Get();
        if (!TestNotNull(TEXT("每个保存条目真实配方"), Source)) return false;
        TestFalse(TEXT("菜单不重复引用同一配方"), Seen.Contains(Source));
        Seen.Add(Source);
        if (Source == Latte)
        {
            ++LatteCount;
            TestEqual(TEXT("Latte 条目仍 BNT"), Entry.PinyinIndex, FName(TEXT("BNT")));
            continue;
        }
        ++OtherCount;
        TestTrue(Entry.PinyinIndex.ToString() + TEXT("保存其他配方三字段为空"), Source->CoffeeHighFeedback.IsEmpty()
            && Source->MilkHighFeedback.IsEmpty() && Source->AcceptedFeedback.IsEmpty());
        const auto OriginalRules = Source->Rules;
        auto* Clone = F.Duplicate(*this, Source);
        if (!Clone) return false;
        auto* Cup = F.Cup(*this, Source->AcceptedCupClass.Get());
        if (!Cup || !TestNotNull(TEXT("其他配方真实冰组件"), Cup->CupIce.Get())) return false;
        for (const auto& Ingredient : Entry.Ingredients)
            TestEqual(TEXT("按其他菜单条目实际加原料"), Cup->AddIngredientLiquid(static_cast<float>(Ingredient.VolumeML),
                Ingredient.IngredientId, FLinearColor::White), static_cast<float>(Ingredient.VolumeML));
        if (Source->ExpectedIceCount > 0 && !TestTrue(TEXT("其他冰饮实际加一冰"), TutorialMenuTestHelpers::AddIce(Cup, 1))) return false;
        CheckResult(*this, EvaluateReadOnly(*this, Source, Cup), EDrinkValidationReason::Accepted,
            Source->ExpectedIceCount < 0 ? GenericAccepted : GenericAcceptedWithIce);
        // 只收窄副本的真实比例区间，保持总量、原料、杯型和冰要求；让第一项在原料齐全时失败。
        if (!TestTrue(TEXT("其他配方有比例规则"), !Clone->Rules.IsEmpty())) return false;
        const double Fraction = Cup->GetIngredientVolumeML(Clone->Rules[0].IngredientId) / Cup->GetPreciseVolumeML();
        if (!TestTrue(TEXT("可收窄的混合配方第一料占比"), Fraction > 0.0 && Fraction < 1.0)) return false;
        Clone->Rules[0].MinFraction = 0.0;
        Clone->Rules[0].MaxFraction = Fraction * .5;
        for (int32 I = 1; I < Clone->Rules.Num(); ++I)
        {
            Clone->Rules[I].MinFraction = 0.0;
            Clone->Rules[I].MaxFraction = 1.0;
        }
        const auto Result = EvaluateReadOnly(*this, Clone, Cup);
        CheckResult(*this, Result, EDrinkValidationReason::WrongRatio, GenericRatio);
        TestFalse(TEXT("其他配方绝不选 Latte 咖啡反馈"), Result.Feedback.ToString() == Excel(7));
        TestFalse(TEXT("其他配方绝不选 Latte 牛奶反馈"), Result.Feedback.ToString() == Excel(9));
        ++RatioCases;
        CheckRules(*this, Source, OriginalRules);
        TestTrue(TEXT("修改副本后其他保存三字段仍空"), Source->CoffeeHighFeedback.IsEmpty()
            && Source->MilkHighFeedback.IsEmpty() && Source->AcceptedFeedback.IsEmpty());
    }
    TestEqual(TEXT("只有一项保存原 Latte"), LatteCount, 1);
    TestEqual(TEXT("其余九配方全部只读核对"), OtherCount, 9);
    TestEqual(TEXT("九个其他配方副本比例失败仍 generic"), RatioCases, 9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColleagueUnconfiguredFeedbackFallback,
    "CyberCafe.ColleagueDialogue.UnconfiguredFeedbackFallback", ColleagueDialogueTests::Options)
bool FColleagueUnconfiguredFeedbackFallback::RunTest(const FString&)
{
    using namespace ColleagueDialogueTests;
    auto* Source = SavedLatte(*this);
    if (!Source || !TestNotNull(TEXT("未配置回退测试需要 Engine"), GEngine)) return false;
    const auto OriginalRules = Source->Rules;
    const FString OriginalCoffee = Source->CoffeeHighFeedback.ToString();
    const FString OriginalMilk = Source->MilkHighFeedback.ToString();
    const FString OriginalAccepted = Source->AcceptedFeedback.ToString();
    FWorld F;
    auto* Clone = F.Duplicate(*this, Source);
    if (!Clone) return false;
    Clone->CoffeeHighFeedback = FText::GetEmpty();
    Clone->MilkHighFeedback = FText::GetEmpty();
    Clone->AcceptedFeedback = FText::GetEmpty();
    auto* Legacy = NewObject<UDrinkRecipe>(F.World, NAME_None, RF_Transient);
    Legacy->AcceptedCupClass = Clone->AcceptedCupClass;
    Legacy->ExpectedIceCount = Clone->ExpectedIceCount;
    for (bool bCoffeeHigh : {true, false})
    {
        auto* Cup = F.Mixture(*this, Clone, bCoffeeHigh ? 130.f : 70.f, bCoffeeHigh ? 70.f : 130.f, 1);
        if (!Cup) return false;
        const auto Result = EvaluateReadOnly(*this, Clone, Cup);
        const auto Old = EvaluateReadOnly(*this, Legacy, Cup);
        CheckResult(*this, Result, EDrinkValidationReason::WrongRatio, GenericRatio);
        CheckResult(*this, Old, EDrinkValidationReason::WrongRatio, GenericRatio);
        TestEqual(TEXT("未配置方向反馈与旧原生全文一致"), Result.Feedback.ToString(), Old.Feedback.ToString());
    }
    auto* AcceptedCup = F.Mixture(*this, Clone, 100, 100, 1);
    auto* NoIceCup = F.Mixture(*this, Clone, 100, 100, 0);
    if (!AcceptedCup || !NoIceCup) return false;
    const auto Accepted = EvaluateReadOnly(*this, Clone, AcceptedCup);
    const auto OldAccepted = EvaluateReadOnly(*this, Legacy, AcceptedCup);
    CheckResult(*this, Accepted, EDrinkValidationReason::Accepted, GenericAcceptedWithIce);
    CheckResult(*this, OldAccepted, EDrinkValidationReason::Accepted, GenericAcceptedWithIce);
    TestEqual(TEXT("未配置成功反馈与旧冰要求全文一致"), Accepted.Feedback.ToString(), OldAccepted.Feedback.ToString());
    CheckResult(*this, EvaluateReadOnly(*this, Clone, NoIceCup), EDrinkValidationReason::WrongIce, GenericWrongIce);
    CheckRules(*this, Source, OriginalRules);
    TestEqual(TEXT("源 Coffee 字段未被清空"), Source->CoffeeHighFeedback.ToString(), OriginalCoffee);
    TestEqual(TEXT("源 Milk 字段未被清空"), Source->MilkHighFeedback.ToString(), OriginalMilk);
    TestEqual(TEXT("源 Accepted 字段未被清空"), Source->AcceptedFeedback.ToString(), OriginalAccepted);
    TestEqual(TEXT("源 Coffee 仍精确 C7"), Source->CoffeeHighFeedback.ToString(), FString(Excel(7)));
    TestEqual(TEXT("源 Milk 仍精确 C9"), Source->MilkHighFeedback.ToString(), FString(Excel(9)));
    TestEqual(TEXT("源 Accepted 仍精确 C8"), Source->AcceptedFeedback.ToString(), FString(Excel(8)));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
