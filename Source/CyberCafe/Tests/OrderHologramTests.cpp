// 仅使用公开 API、真实 World/GI 与总线；坐标为合成触摸样本，不代表硬件实测。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/ServingSpotActor.h"
#include "Liquid/CupActor.h"
#include "GrabComponent.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "UI/Narrative/NarrativeHUDActor.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "UI/OrderHologramWidget.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include <limits>

namespace OrderHologramTests
{
const FName CoffeeId(TEXT("Coffee"));
const FName MilkId(TEXT("Milk"));
constexpr double VolumeTolerance = 1.e-6;

// 复用 RecipeTutorialIntegrationTests 的生命周期和 deferred cup 配置。
struct FHologramWorld
{
    UWorld* World;
    UGameInstance* GI;
    UCyberCafeEventSubsystem* Events;
    ATutorialNPC* NPC;
    AOrderHologramActor* Hologram;
    ATutorialDirector* Director = nullptr;
    TArray<ACupActor*> Cups;

    FHologramWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
            true, ERHIFeatureLevel::SM5, &IV);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
        Events = GI->GetSubsystem<UCyberCafeEventSubsystem>();

        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        NPC = World->SpawnActor<ATutorialNPC>(FVector(-300, 0, 100), FRotator::ZeroRotator, Params);
        Hologram = World->SpawnActor<AOrderHologramActor>(
            FVector(-150, 60, 0), FRotator(0, 40, 0), Params);
    }

    ~FHologramWorld()
    {
        // 订阅者先 EndPlay，保证解绑和步骤清理发生在 GI 总线仍有效时。
        if (IsValid(Director)) Director->Destroy();
        if (IsValid(Hologram)) Hologram->Destroy();
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    UDrinkRecipe* Recipe(FName Id = FName(TEXT("Latte")))
    {
        UDrinkRecipe* Result = NewObject<UDrinkRecipe>(World);
        Result->RecipeId = Id;
        Result->DisplayName = FText::FromString(Id.ToString());
        return Result;
    }

    AServingSpotActor* Spot(UDrinkRecipe* Recipe, const FVector& Position = FVector(300, 40, 20))
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AServingSpotActor* Result = World->SpawnActor<AServingSpotActor>(Position, FRotator(0, 35, 0), Params);
        Result->DetectBox->SetRelativeLocation(FVector(5, -3, 10));
        Result->DetectBox->SetBoxExtent(FVector(20, 15, 20));
        Result->StabilitySeconds = .4f;
        Result->bOneShot = true;
        Result->bLockCupAfterServing = true;
        Result->SetActiveRecipe(Recipe);
        return Result;
    }

    ACupActor* Cup(AServingSpotActor* Spot, float CoffeeML = 100.f, float MilkML = 100.f)
    {
        const FVector Position = Spot->DetectBox->GetComponentTransform().TransformPosition(FVector(1, 1, 1));
        const FTransform Transform(FRotator::ZeroRotator, Position);
        ACupActor* Result = World->SpawnActorDeferred<ACupActor>(ACupActor::StaticClass(),
            Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Result->ContainerMesh->SetSimulatePhysics(false);
        Result->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Result->bCanPour = false;
        Result->bEnableSplash = false;
        Result->LiquidFXTemplate = nullptr;
        Result->SplashEffectTemplate = nullptr;
        Result->LiquidFX->SetAutoActivate(false);
        Result->LiquidFX->SetAsset(nullptr);
        Result->PourFX->SetAutoActivate(false);
        Result->PourFX->SetAsset(nullptr);
        Result->MaxVolumeML = 300.f;
        Result->FillAmount = 0.f;
        Result->GrabComp->GrabType = EGrabType::Free;
        Result->GrabComp->PhysicsMode = EGrabPhysicsMode::Kinematic;
        Result->GrabComp->bSimulateOnDrop = false;
        Result->FinishSpawning(Transform);
        Result->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Result->InitializeLiquidState();
        if (CoffeeML > 0.f) Result->AddIngredientLiquid(CoffeeML, CoffeeId, FLinearColor::Red);
        if (MilkML > 0.f) Result->AddIngredientLiquid(MilkML, MilkId, FLinearColor::White);
        Cups.Add(Result);
        return Result;
    }

    UTutorialStep_WaitForServe* WaitStep(AServingSpotActor* Spot, UDrinkRecipe* Recipe)
    {
        ANarrativeHUDActor* HUD = World->SpawnActor<ANarrativeHUDActor>();
        UNarrativeWidget* Widget = NewObject<UNarrativeWidget>(World);
        Widget->bEnableVoice = false;
        HUD->FindComponentByClass<UWidgetComponent>()->SetWidget(Widget);
        Director = World->SpawnActor<ATutorialDirector>();
        Director->bAutoStart = false;
        Director->NPCRef = NPC;
        Director->bDialogueVoice = false;
        Director->CheerDuration = 0.f;
        Director->FinishedText = FText::GetEmpty();
        Director->FinishedTextAutoHideSeconds = 0.f;
        UTutorialStep_WaitForServe* Step = NewObject<UTutorialStep_WaitForServe>(Director);
        Step->RequiredSpot = Spot;
        Step->RequiredRecipe = Recipe;
        Step->HintText = FText::GetEmpty();
        Step->CompletedText = FText::GetEmpty();
        Director->Steps = {Step};
        return Step;
    }

    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        // GrabComponent::BeginPlay 会恢复根组件碰撞；测试使用显式提交，不依赖 overlap。
        for (ACupActor* Cup : Cups)
        {
            Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
};

bool CheckFixture(FAutomationTestBase& Test, const FHologramWorld& T, AServingSpotActor* Spot)
{
    const bool bEvents = Test.TestNotNull(TEXT("真实 GI 事件子系统"), T.Events);
    const bool bWorld = Test.TestTrue(TEXT("真实 World 已通过 NotifyBeginPlay 开始游戏"), T.World->HasBegunPlay());
    const bool bSpot = Test.TestTrue(TEXT("Spot 已通过世界生命周期 BeginPlay"), Spot->HasActorBegunPlay());
    const bool bHologram = Test.TestTrue(TEXT("全息 Actor 已通过世界生命周期 BeginPlay"), T.Hologram->HasActorBegunPlay());
    Test.TestTrue(TEXT("Spot 使用 fixture 的 GI 总线"), UCyberCafeEventSubsystem::Get(Spot) == T.Events);
    Test.TestTrue(TEXT("全息 Actor 使用同一 GI 总线"), UCyberCafeEventSubsystem::Get(T.Hologram) == T.Events);
    const bool bSubscribed = bEvents && Test.TestTrue(TEXT("全息 Actor 已订阅 GI OnDrinkServed"),
        T.Events->OnDrinkServed.IsBound());
    return bEvents && bWorld && bSpot && bHologram && bSubscribed;
}

void CheckPending(FAutomationTestBase& Test, const FString& Label,
    AOrderHologramActor* Hologram, bool bOpen, int32 HistoryCount)
{
    Test.TestTrue(Label + TEXT("：订单待交付"), Hologram->IsPending());
    Test.TestFalse(Label + TEXT("：未交付"), Hologram->IsDelivered());
    Test.TestEqual(Label + TEXT("：面板开关"), Hologram->IsPanelOpen(), bOpen);
    Test.TestEqual(Label + TEXT("：关闭待交付时亮琥珀灯"), Hologram->IsAmberLit(), !bOpen);
    Test.TestEqual(Label + TEXT("：交付历史条数"), Hologram->GetDeliveryRecords().Num(), HistoryCount);
}

void CheckDelivered(FAutomationTestBase& Test, const FString& Label,
    AOrderHologramActor* Hologram, bool bOpen, int32 HistoryCount)
{
    Test.TestFalse(Label + TEXT("：不再待交付"), Hologram->IsPending());
    Test.TestTrue(Label + TEXT("：已交付"), Hologram->IsDelivered());
    Test.TestEqual(Label + TEXT("：面板开关"), Hologram->IsPanelOpen(), bOpen);
    Test.TestFalse(Label + TEXT("：交付后琥珀灯熄灭"), Hologram->IsAmberLit());
    Test.TestEqual(Label + TEXT("：交付历史条数"), Hologram->GetDeliveryRecords().Num(), HistoryCount);
}

void CheckRecord(FAutomationTestBase& Test, const FString& Label,
    AOrderHologramActor* Hologram, int32 Index, UDrinkRecipe* Recipe, const FDrinkValidationResult& Result)
{
    const TArray<FHologramDeliveryRecord>& Records = Hologram->GetDeliveryRecords();
    if (!Test.TestTrue(Label + TEXT("：历史索引有效"), Records.IsValidIndex(Index))) return;
    Test.TestTrue(Label + TEXT("：数据来自真实 Accepted 结果"), Result.bAccepted);
    Test.TestEqual(Label + TEXT("：配方 ID"), Records[Index].RecipeId, Result.RecipeId);
    Test.TestEqual(Label + TEXT("：饮品名称"), Records[Index].DrinkName.ToString(), Recipe->DisplayName.ToString());
    Test.TestEqual(Label + TEXT("：顾客 ID"), Records[Index].CustomerId, Hologram->CustomerId);
    Test.TestTrue(Label + TEXT("：历史使用 Result.TotalML 而非目标体积"),
        FMath::Abs(Records[Index].VolumeML - Result.TotalML) < VolumeTolerance);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramManualRejectRepairTest,
    "CyberCafe.Hologram.AutoOrderManualCloseRejectRepairPlayerStaysOpen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramManualRejectRepairTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    FHologramWorld T;
    UDrinkRecipe* Recipe = T.Recipe();
    AServingSpotActor* Spot = T.Spot(Recipe);
    ACupActor* Cup = T.Cup(Spot, 100.f, 0.f);
    T.BeginPlay();
    if (!CheckFixture(*this, T, Spot)) return false;
    if (!TestTrue(TEXT("新订单接受 BeginOrder"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC,
        FText::FromString(TEXT("测试顾客"))))) return false;
    CheckPending(*this, TEXT("自动点单"), T.Hologram, true, 0);
    TestTrue(TEXT("首次订单已自动展开"), T.Hologram->HasAutoOpened());
    TestFalse(TEXT("自动开不标记玩家手动开"), T.Hologram->IsPlayerOpened());
    TestTrue(TEXT("绑定当前配方"), T.Hologram->GetCurrentRecipe() == Recipe);
    TestEqual(TEXT("绑定 Spot 的最终 serial"), T.Hologram->GetCurrentOrderSerial(),
        static_cast<int64>(Spot->GetOrderSerial()));
    T.Hologram->Tick(3.f);
    CheckPending(*this, TEXT("未验收不启动收起计时"), T.Hologram, true, 0);

    T.Hologram->PressButton();
    CheckPending(*this, TEXT("玩家手动关闭"), T.Hologram, false, 0);
    TestFalse(TEXT("只有 Coffee 的真实杯提交失败"), Spot->TrySubmitCup(Cup));
    TestEqual(TEXT("真实缺奶拒收原因"), Spot->GetLastValidation().Reason, EDrinkValidationReason::MissingIngredient);
    TestFalse(TEXT("拒收未产生 Accepted 状态"), Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
    TestEqual(TEXT("拒收未产生 accepted serial"), Spot->GetAcceptedOrderSerial(), uint32(0));
    CheckPending(*this, TEXT("失败不能自动重开"), T.Hologram, false, 0);
    T.Hologram->Tick(3.f);
    CheckPending(*this, TEXT("失败后等待也不重开"), T.Hologram, false, 0);

    T.Hologram->PressButton();
    CheckPending(*this, TEXT("玩家再次手动展开"), T.Hologram, true, 0);
    TestTrue(TEXT("标记玩家手动开"), T.Hologram->IsPlayerOpened());
    TestEqual(TEXT("真实补足 Milk 100 mL"), Cup->AddIngredientLiquid(100.f, MilkId, FLinearColor::White), 100.f);
    if (!TestTrue(TEXT("修正杯通过 Spot 公开 API 验收"), Spot->TrySubmitCup(Cup))) return false;
    CheckDelivered(*this, TEXT("GI 成功事件同步更新全息"), T.Hologram, true, 1);
    CheckRecord(*this, TEXT("修正杯历史"), T.Hologram, 0, Recipe, Spot->GetLastValidation());
    if (!TestEqual(TEXT("修正杯必须有一条历史才读取"), T.Hologram->GetDeliveryRecords().Num(), 1)) return false;
    TestTrue(TEXT("历史是实际 200 mL"), FMath::Abs(T.Hologram->GetDeliveryRecords()[0].VolumeML - 200.0) < VolumeTolerance);
    T.Hologram->Tick(1.5f);
    T.Hologram->Tick(.5f);
    T.Hologram->Tick(3.f);
    CheckDelivered(*this, TEXT("玩家展开的成功面板超过两秒仍不收"), T.Hologram, true, 1);
    TestTrue(TEXT("成功后仍保留玩家手动开标记"), T.Hologram->IsPlayerOpened());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramTimeoutGenerationTest,
    "CyberCafe.Hologram.AutoCloseTwoSecondsOrderIsolationAndSameSerialIdempotence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramTimeoutGenerationTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    FHologramWorld T;
    UDrinkRecipe* Recipe = T.Recipe();
    UDrinkRecipe* NextRecipe = T.Recipe(FName(TEXT("NextLatte")));
    AServingSpotActor* Spot = T.Spot(Recipe);
    ACupActor* FirstCup = T.Cup(Spot);
    T.BeginPlay();
    if (!CheckFixture(*this, T, Spot)) return false;
    TestEqual(TEXT("默认成功后收起时长为两秒"), T.Hologram->AutoCloseSeconds, 2.f);
    if (!TestTrue(TEXT("第一单自动开"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty())) ||
        !TestTrue(TEXT("第一单真实成功"), Spot->TrySubmitCup(FirstCup))) return false;
    CheckDelivered(*this, TEXT("第一单成功即刻"), T.Hologram, true, 1);
    T.Hologram->Tick(1.5f);
    CheckDelivered(*this, TEXT("成功后 1.5 秒尚未收起"), T.Hologram, true, 1);
    T.Events->OnDrinkServed.Broadcast(Spot, FirstCup);
    T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty());
    CheckDelivered(*this, TEXT("重复成功和同 serial BeginOrder 不重置完成状态"), T.Hologram, true, 1);
    T.Hologram->Tick(.5f);
    CheckDelivered(*this, TEXT("成功后恰好两秒收起"), T.Hologram, false, 1);

    Spot->ResetServing();
    Spot->SetActiveRecipe(Recipe);
    const int64 OldSerial = Spot->GetOrderSerial();
    ACupActor* OldCup = T.Cup(Spot);
    if (!TestTrue(TEXT("同配方新 serial 接受新订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty())) ||
        !TestTrue(TEXT("旧计时订单真实成功"), Spot->TrySubmitCup(OldCup))) return false;
    T.Hologram->Tick(.5f);
    CheckDelivered(*this, TEXT("旧计时仍余 1.5 秒"), T.Hologram, true, 2);

    Spot->ResetServing();
    Spot->SetActiveRecipe(NextRecipe);
    const int64 NewSerial = Spot->GetOrderSerial();
    TestTrue(TEXT("公开 Spot API 推进 serial"), NewSerial != OldSerial);
    if (!TestTrue(TEXT("更换订单重置计时归属"), T.Hologram->BeginOrder(NextRecipe, Spot, T.NPC,
        FText::FromString(TEXT("下一位顾客"))))) return false;
    TestTrue(TEXT("当前配方已经更换"), T.Hologram->GetCurrentRecipe() == NextRecipe);
    TestEqual(TEXT("当前 serial 是新订单"), T.Hologram->GetCurrentOrderSerial(), NewSerial);
    CheckPending(*this, TEXT("新订单重新自动开"), T.Hologram, true, 2);
    TestFalse(TEXT("新订单不继承玩家开标记"), T.Hologram->IsPlayerOpened());
    T.Hologram->Tick(1.5f);
    CheckPending(*this, TEXT("越过旧订单两秒截止点仍待交付且展开"), T.Hologram, true, 2);
    T.Hologram->Tick(2.f);
    CheckPending(*this, TEXT("新订单未成功不会自行收起"), T.Hologram, true, 2);
    T.Events->OnDrinkServed.Broadcast(Spot, OldCup);
    CheckPending(*this, TEXT("旧杯迟到事件不能给新订单启动计时"), T.Hologram, true, 2);

    ACupActor* NewCup = T.Cup(Spot, 90.f, 90.f);
    if (!TestTrue(TEXT("新订单真实 180 mL 成功"), Spot->TrySubmitCup(NewCup))) return false;
    CheckRecord(*this, TEXT("新单记录实际体积"), T.Hologram, 2, NextRecipe, Spot->GetLastValidation());
    CheckDelivered(*this, TEXT("新单自己重新获得完整两秒"), T.Hologram, true, 3);
    T.Hologram->Tick(1.5f);
    CheckDelivered(*this, TEXT("新单成功后 1.5 秒保持展开"), T.Hologram, true, 3);
    T.Hologram->Tick(.5f);
    CheckDelivered(*this, TEXT("新单成功后两秒正常收起"), T.Hologram, false, 3);

    Spot->ResetServing();
    Spot->SetActiveRecipe(NextRecipe);
    const int64 DuplicateSerial = Spot->GetOrderSerial();
    if (!TestTrue(TEXT("重复调用场景先建立新订单"), T.Hologram->BeginOrder(NextRecipe, Spot, T.NPC, FText::GetEmpty()))) return false;
    T.Hologram->PressButton();
    CheckPending(*this, TEXT("新单玩家手动关闭"), T.Hologram, false, 3);
    const bool bPlayerOpened = T.Hologram->IsPlayerOpened();
    const bool bAutoOpened = T.Hologram->HasAutoOpened();
    // 重复调用的 bool 返回值不作为额外契约，只验证同 Spot/serial 没有状态副作用。
    for (int32 Attempt = 0; Attempt < 3; ++Attempt)
    {
        T.Hologram->BeginOrder(NextRecipe, Spot, T.NPC, FText::FromString(TEXT("重复进入")));
        CheckPending(*this, TEXT("同 Spot 同 serial 忽略重复 BeginOrder"), T.Hologram, false, 3);
        TestEqual(TEXT("重复不改变绑定 serial"), T.Hologram->GetCurrentOrderSerial(), DuplicateSerial);
        TestTrue(TEXT("重复不替换当前配方"), T.Hologram->GetCurrentRecipe() == NextRecipe);
        TestEqual(TEXT("重复不清手动状态"), T.Hologram->IsPlayerOpened(), bPlayerOpened);
        TestEqual(TEXT("重复不清自动开历史"), T.Hologram->HasAutoOpened(), bAutoOpened);
    }
    T.Hologram->Tick(3.f);
    CheckPending(*this, TEXT("重复调用之后仍保持手动关闭"), T.Hologram, false, 3);
    auto* Widget = T.Hologram->GetDisplayWidget();
    if (!TestNotNull(TEXT("三条真实交付历史 Widget"), Widget) || !TestNotNull(TEXT("真实历史控件树"), Widget->WidgetTree.Get())) return false;
    const auto& Records = T.Hologram->GetDeliveryRecords();
    if (!TestEqual(TEXT("原交付流程产生三条真实记录"), Records.Num(), 3)) return false;
    TArray<FString> HistoryLines;
    for (int32 I = 0; I < 3; ++I)
    {
        const auto& Record = Records[Records.Num() - 1 - I];
        const FString ExpectedName = Record.DrinkName.ToString() + TEXT(" · 已交付");
        const FString ExpectedVolume = FString::Printf(TEXT("实际 %.0f mL"), Record.VolumeML);
        HistoryLines.Add(ExpectedName);
        HistoryLines.Add(ExpectedVolume);
        auto* Name = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryName%d"), I))));
        auto* Volume = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HistoryVolume%d"), I))));
        if (!TestNotNull(TEXT("三行真实历史名控件"), Name) || !TestNotNull(TEXT("三行真实历史量控件"), Volume)
            || !TestNotNull(TEXT("真实历史画布"), Name->GetParent()) || !TestNotNull(TEXT("真实历史行容器"), Name->GetParent()->GetParent())) return false;
        TestEqual(TEXT("三行历史倒序显示真实饮品名"), Name->GetText().ToString(), ExpectedName);
        TestEqual(TEXT("三行历史倒序显示真实验收量，不以目标代替"), Volume->GetText().ToString(), ExpectedVolume);
        TestTrue(TEXT("三行真实历史保持可见，面板关闭不删除账本 UI"), Name->GetVisibility() == ESlateVisibility::HitTestInvisible
            && Volume->GetVisibility() == ESlateVisibility::HitTestInvisible
            && Name->GetParent()->GetParent()->GetVisibility() == ESlateVisibility::HitTestInvisible);
    }
    TestEqual(TEXT("删除菜单历史页脚不删除三条真实交付 UI"), Widget->GetHistoryText().ToString(), FString::Join(HistoryLines, TEXT("\n")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramEventHistoryIdentityTest,
    "CyberCafe.Hologram.WrongSpotForgedEventsAcceptedSerialAndHistoryOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramEventHistoryIdentityTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    FHologramWorld T;
    UDrinkRecipe* Recipe = T.Recipe();
    Recipe->TargetVolumeML = 210.0;
    AServingSpotActor* Spot = T.Spot(Recipe);
    AServingSpotActor* WrongSpot = T.Spot(Recipe, FVector(700, 40, 20));
    ACupActor* Cup = T.Cup(Spot);
    ACupActor* OtherCup = T.Cup(Spot);
    ACupActor* WrongCup = T.Cup(WrongSpot);
    T.BeginPlay();
    if (!CheckFixture(*this, T, Spot)) return false;
    if (!TestTrue(TEXT("建立事件鉴别订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty()))) return false;
    TestEqual(TEXT("两个 Spot 故意具有相同 serial，不能只比较 serial"), Spot->GetOrderSerial(), WrongSpot->GetOrderSerial());
    TestTrue(TEXT("待提交杯 Evaluate 真实合格"), Recipe->Evaluate(Cup).bAccepted);
    TestFalse(TEXT("Evaluate 没有建立 AcceptedCup"), Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
    TestEqual(TEXT("提交前 accepted serial 为零"), Spot->GetAcceptedOrderSerial(), uint32(0));

    // 负向注入只广播公开总线，绝不改写 Spot 验收字段。
    T.Events->OnDrinkServed.Broadcast(Spot, Cup);
    CheckPending(*this, TEXT("未经提交的合格杯伪造成功事件"), T.Hologram, true, 0);
    T.Events->OnDrinkServed.Broadcast(Spot, nullptr);
    CheckPending(*this, TEXT("空杯成功事件"), T.Hologram, true, 0);
    T.Events->OnDrinkServed.Broadcast(nullptr, Cup);
    CheckPending(*this, TEXT("空 Spot 成功事件"), T.Hologram, true, 0);
    if (!TestTrue(TEXT("错误 Spot 通过真实 API 验收"), WrongSpot->TrySubmitCup(WrongCup))) return false;
    TestTrue(TEXT("错误 Spot 确实保存了 AcceptedCup"), WrongSpot->HasAcceptedCup(WrongCup, Recipe->RecipeId));
    CheckPending(*this, TEXT("错误 Spot 的真实成功总线事件"), T.Hologram, true, 0);
    T.Events->OnDrinkServed.Broadcast(Spot, WrongCup);
    CheckPending(*this, TEXT("把别处已验收杯冒充当前 Spot"), T.Hologram, true, 0);

    if (!TestTrue(TEXT("当前 Spot 的真实提交成功"), Spot->TrySubmitCup(Cup))) return false;
    TestTrue(TEXT("保存了当前 Cup 的真实 Accepted 状态"), Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
    TestEqual(TEXT("实际 accepted serial 匹配全息绑定"), static_cast<int64>(Spot->GetAcceptedOrderSerial()),
        T.Hologram->GetCurrentOrderSerial());
    CheckDelivered(*this, TEXT("唯一真实交付"), T.Hologram, true, 1);
    CheckRecord(*this, TEXT("实际体积而非 210 mL 目标"), T.Hologram, 0, Recipe, Spot->GetLastValidation());
    if (!TestEqual(TEXT("实际交付只生成一条历史"), T.Hologram->GetDeliveryRecords().Num(), 1)) return false;
    const FHologramDeliveryRecord FirstRecord = T.Hologram->GetDeliveryRecords()[0];
    TestTrue(TEXT("历史实际 200 mL，不误记为目标 210 mL"),
        FMath::Abs(FirstRecord.VolumeML - 200.0) < VolumeTolerance &&
        FMath::Abs(FirstRecord.VolumeML - Recipe->TargetVolumeML) > VolumeTolerance);
    for (int32 Attempt = 0; Attempt < 3; ++Attempt)
    {
        T.Events->OnDrinkServed.Broadcast(Spot, Cup);
        TestFalse(TEXT("one-shot 真实重复提交被拒"), Spot->TrySubmitCup(Cup));
        CheckDelivered(*this, TEXT("重复成功事件仅记一次"), T.Hologram, true, 1);
    }
    T.Events->OnDrinkServed.Broadcast(Spot, OtherCup);
    T.Events->OnDrinkServed.Broadcast(WrongSpot, WrongCup);
    CheckDelivered(*this, TEXT("完成后也不能用别的杯或 Spot 增记"), T.Hologram, true, 1);

    // SetActiveRecipe 会推进 OrderSerial，但保留已服务杯的旧 AcceptedOrderSerial。
    // 通过真实公开 API 制造旧验收/新订单错配，不能靠清空状态掩盖 serial 漏检。
    const uint32 AcceptedSerial = Spot->GetAcceptedOrderSerial();
    Spot->SetActiveRecipe(Recipe);
    TestTrue(TEXT("旧杯 Accepted 仍真实存在"), Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
    TestEqual(TEXT("保留真实旧 accepted serial"), Spot->GetAcceptedOrderSerial(), AcceptedSerial);
    TestTrue(TEXT("当前订单 serial 已不同"), Spot->GetOrderSerial() != AcceptedSerial);
    if (!TestTrue(TEXT("同 Spot 同配方的新 serial 接受新订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty()))) return false;
    T.Events->OnDrinkServed.Broadcast(Spot, Cup);
    CheckPending(*this, TEXT("AcceptedCup 匹配但 accepted serial 过期仍必须忽略"), T.Hologram, true, 1);

    Spot->ResetServing();
    Spot->SetActiveRecipe(Recipe);
    if (!TestTrue(TEXT("公开重置后绑定可提交的新订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty()))) return false;
    ACupActor* NewCup = T.Cup(Spot, 90.f, 90.f);
    if (!TestTrue(TEXT("新杯实际 180 mL 验收成功"), Spot->TrySubmitCup(NewCup))) return false;
    CheckDelivered(*this, TEXT("新 serial 可以追加第二条历史"), T.Hologram, true, 2);
    CheckRecord(*this, TEXT("第二条实际体积"), T.Hologram, 1, Recipe, Spot->GetLastValidation());
    T.Events->OnDrinkServed.Broadcast(Spot, Cup);
    T.Events->OnDrinkServed.Broadcast(Spot, NewCup);
    if (!TestEqual(TEXT("跨订单旧杯和新杯重复事件均不增记"), T.Hologram->GetDeliveryRecords().Num(), 2)) return false;
    const FHologramDeliveryRecord& Retained = T.Hologram->GetDeliveryRecords()[0];
    TestEqual(TEXT("历史旧配方未被新订单改写"), Retained.RecipeId, FirstRecord.RecipeId);
    TestEqual(TEXT("历史旧饮品名称未改写"), Retained.DrinkName.ToString(), FirstRecord.DrinkName.ToString());
    TestEqual(TEXT("历史旧顾客 ID 未改写"), Retained.CustomerId, FirstRecord.CustomerId);
    TestEqual(TEXT("历史旧验收体积未改写"), Retained.VolumeML, FirstRecord.VolumeML);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramTouchGeometryTest,
    "CyberCafe.Hologram.TouchGeometryTrackingHoldingHysteresisAndBothHands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramTouchGeometryTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    for (bool bTransformed : {false, true})
    {
        for (EControllerHand Hand : {EControllerHand::Left, EControllerHand::Right})
        {
            FHologramWorld T;
            UDrinkRecipe* Recipe = T.Recipe();
            AServingSpotActor* Spot = T.Spot(Recipe);
            T.Hologram->SetActorLocationAndRotation(bTransformed ? FVector(123, -77, 45) : FVector::ZeroVector,
                bTransformed ? FRotator(20, 65, 10) : FRotator::ZeroRotator);
            T.BeginPlay();
            if (!CheckFixture(*this, T, Spot) ||
                !TestNotNull(TEXT("公开 ButtonZone"), T.Hologram->ButtonZone.Get())) return false;
            TestTrue(TEXT("按钮默认局部中心 (7,0,4.5)"),
                T.Hologram->ButtonZone->GetRelativeLocation().Equals(FVector(7, 0, 4.5), .00001));
            TestTrue(TEXT("按钮默认半尺寸 (2.7,3.2,1.5)"),
                T.Hologram->ButtonZone->GetUnscaledBoxExtent().Equals(FVector(2.7, 3.2, 1.5), .00001));
            TestEqual(TEXT("默认重臂扩大边界 3 cm"), T.Hologram->TouchRearmMarginCM, 3.f);
            if (!TestTrue(TEXT("触摸测试建立待交付订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty()))) return false;
            const FString Label = FString::Printf(TEXT("%s/%s"),
                bTransformed ? TEXT("旋转平移") : TEXT("原点"), Hand == EControllerHand::Left ? TEXT("左手") : TEXT("右手"));
            const EControllerHand OtherHand = Hand == EControllerHand::Left ? EControllerHand::Right : EControllerHand::Left;
            const FTransform ZoneTransform = T.Hologram->ButtonZone->GetComponentTransform();
            const FVector Extent = T.Hologram->ButtonZone->GetUnscaledBoxExtent();
            const FVector Inside = ZoneTransform.TransformPosition(FVector::ZeroVector);
            const FVector Far = ZoneTransform.TransformPosition(FVector(100, 100, 100));
            TestFalse(Label + TEXT("：另一只手区外样本不按"), T.Hologram->ProcessTouchSample(OtherHand, Far));
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                for (double Sign : {-1.0, 1.0})
                {
                    FVector Local = FVector::ZeroVector;
                    Local[Axis] = Sign * (Extent[Axis] + .2);
                    TestFalse(Label + TEXT("：任一轴正负面超出真实按钮盒不触发"),
                        T.Hologram->ProcessTouchSample(Hand, ZoneTransform.TransformPosition(Local)));
                }
            }
            TestFalse(Label + TEXT("：不 tracked 的区内样本忽略"), T.Hologram->ProcessTouchSample(Hand, Inside, false));
            CheckPending(*this, Label + TEXT("不追踪不会关闭面板"), T.Hologram, true, 0);
            TestTrue(Label + TEXT("：tracked 首次进区触发关闭"), T.Hologram->ProcessTouchSample(Hand, Inside));
            CheckPending(*this, Label + TEXT("触摸关闭"), T.Hologram, false, 0);
            for (int32 Sample = 0; Sample < 3; ++Sample)
            {
                TestFalse(Label + TEXT("：持续按住区内去重"), T.Hologram->ProcessTouchSample(Hand, Inside));
            }
            TestFalse(Label + TEXT("：另一只手紧接进入不能连跳"), T.Hologram->ProcessTouchSample(OtherHand, Inside));
            TestFalse(Label + TEXT("：丢追踪的区外坐标不能重臂"), T.Hologram->ProcessTouchSample(Hand, Far, false));
            TestFalse(Label + TEXT("：另一只手丢追踪不能重臂"), T.Hologram->ProcessTouchSample(OtherHand, Far, false));
            TestFalse(Label + TEXT("：恢复追踪仍在区内不重复触发"), T.Hologram->ProcessTouchSample(Hand, Inside));
            CheckPending(*this, Label + TEXT("去重和丢追踪保留关闭"), T.Hologram, false, 0);

            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                // 退出紧盒但仅离开 2.9 cm，仍在扩大盒内，不可重新触发。
                FVector NearLocal = FVector::ZeroVector;
                NearLocal[Axis] = Extent[Axis] + 2.9;
                const FVector Near = ZoneTransform.TransformPosition(NearLocal);
                TestFalse(Label + TEXT("：扩大盒内的退出样本不按"), T.Hologram->ProcessTouchSample(Hand, Near));
                TestFalse(Label + TEXT("：另一手也仅离开 2.9 cm 不重臂"), T.Hologram->ProcessTouchSample(OtherHand, Near));
                TestFalse(Label + TEXT("：未超过 3 cm 再进区不按"), T.Hologram->ProcessTouchSample(Hand, Inside));
                TestFalse(Label + TEXT("：另一手未重臂再进也不按"), T.Hologram->ProcessTouchSample(OtherHand, Inside));
            }
            CheckPending(*this, Label + TEXT("扩大边界滞回"), T.Hologram, false, 0);
            bool bOpen = false;
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                FVector ExitLocal = FVector::ZeroVector;
                ExitLocal[Axis] = Extent[Axis] + 3.1;
                const FVector Exit = ZoneTransform.TransformPosition(ExitLocal);
                TestFalse(Label + TEXT("：第一手超过扩大盒退出不按"), T.Hologram->ProcessTouchSample(Hand, Exit));
                TestFalse(Label + TEXT("：双手均超过扩大盒退出不按"), T.Hologram->ProcessTouchSample(OtherHand, Exit));
                TestTrue(Label + TEXT("：双方退出后重新进入触发一次"), T.Hologram->ProcessTouchSample(Hand, Inside));
                bOpen = !bOpen;
                CheckPending(*this, Label + TEXT("任一轴超过 3 cm 后可重臂"), T.Hologram, bOpen, 0);
                TestFalse(Label + TEXT("：重臂触发后另一手不能立即再跳"), T.Hologram->ProcessTouchSample(OtherHand, Inside));
            }
            TestFalse(Label + TEXT("：握物测试前第一手真实区外样本"), T.Hologram->ProcessTouchSample(Hand, Far));
            TestFalse(Label + TEXT("：握物测试前第二手真实区外样本"), T.Hologram->ProcessTouchSample(OtherHand, Far));
            TestFalse(Label + TEXT("：tracked 但手握物时区内样本忽略"), T.Hologram->ProcessTouchSample(Hand, Inside, true, true));
            TestFalse(Label + TEXT("：另一手握物也不能按"), T.Hologram->ProcessTouchSample(OtherHand, Inside, true, true));
            CheckPending(*this, Label + TEXT("握物不改变面板"), T.Hologram, bOpen, 0);
            TestTrue(Label + TEXT("：非握物的有效进区样本仍可触发"), T.Hologram->ProcessTouchSample(Hand, Inside, true, false));
            CheckPending(*this, Label + TEXT("握物样本没有消耗触发"), T.Hologram, !bOpen, 0);
        }
    }

    for (EControllerHand First : {EControllerHand::Left, EControllerHand::Right})
    {
        FHologramWorld T;
        UDrinkRecipe* Recipe = T.Recipe();
        AServingSpotActor* Spot = T.Spot(Recipe);
        T.BeginPlay();
        if (!CheckFixture(*this, T, Spot)) return false;
        if (!TestTrue(TEXT("双手交替测试建立订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty()))) return false;
        const EControllerHand Second = First == EControllerHand::Left ? EControllerHand::Right : EControllerHand::Left;
        const FTransform Transform = T.Hologram->ButtonZone->GetComponentTransform();
        const FVector Inside = Transform.TransformPosition(FVector::ZeroVector);
        const FVector Far = Transform.TransformPosition(FVector(100, 0, 0));
        TestFalse(TEXT("双手测试第一手初始区外"), T.Hologram->ProcessTouchSample(First, Far));
        TestFalse(TEXT("双手测试第二手初始区外"), T.Hologram->ProcessTouchSample(Second, Far));
        TestTrue(TEXT("第一手首次进入关闭面板"), T.Hologram->ProcessTouchSample(First, Inside));
        TestFalse(TEXT("第二手紧接进入不能立即重开"), T.Hologram->ProcessTouchSample(Second, Inside));
        TestFalse(TEXT("只有第一手离开扩大边界"), T.Hologram->ProcessTouchSample(First, Far));
        TestFalse(TEXT("第二手仍停留不会触发"), T.Hologram->ProcessTouchSample(Second, Inside));
        TestFalse(TEXT("另一手仍在区内时第一手再进不得重臂"), T.Hologram->ProcessTouchSample(First, Inside));
        TestFalse(TEXT("只有第二手离开扩大边界"), T.Hologram->ProcessTouchSample(Second, Far));
        TestFalse(TEXT("第一手仍停留不会触发"), T.Hologram->ProcessTouchSample(First, Inside));
        TestFalse(TEXT("双方交错退出但未同时离开不得重臂"), T.Hologram->ProcessTouchSample(Second, Inside));
        CheckPending(*this, TEXT("双手交错仍保持关闭"), T.Hologram, false, 0);
        TestFalse(TEXT("最后第一手离开"), T.Hologram->ProcessTouchSample(First, Far));
        TestFalse(TEXT("最后第二手也离开"), T.Hologram->ProcessTouchSample(Second, Far));
        TestTrue(TEXT("双方均离开后允许第二手再次触发"), T.Hologram->ProcessTouchSample(Second, Inside));
        TestFalse(TEXT("同一轮第一手不能马上反向关闭"), T.Hologram->ProcessTouchSample(First, Inside));
        CheckPending(*this, TEXT("双方退出后仅切换一次"), T.Hologram, true, 0);
        TestTrue(TEXT("触摸展开属于玩家手动开"), T.Hologram->IsPlayerOpened());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramRealCupTutorialTest,
    "CyberCafe.Hologram.RealCoffee100Milk100SubmitAdvancesTutorialAndRecords200ML",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramRealCupTutorialTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    FHologramWorld T;
    UDrinkRecipe* Recipe = T.Recipe();
    Recipe->TargetVolumeML = 210.0;
    AServingSpotActor* Spot = T.Spot(Recipe);
    Spot->SetSubmissionEnabled(false);
    ACupActor* Cup = T.Cup(Spot);
    UTutorialStep_WaitForServe* Step = T.WaitStep(Spot, Recipe);
    Step->OrderHologram = T.Hologram;
    T.BeginPlay();
    if (!CheckFixture(*this, T, Spot)) return false;
    T.Director->StartTutorial();
    TestTrue(TEXT("WaitForServe.EnterStep 已启用 Spot"), Spot->bAcceptSubmissions);
    TestTrue(TEXT("WaitForServe.EnterStep 已安装所需配方"), Spot->ActiveRecipe.Get() == Recipe);
    CheckPending(*this, TEXT("StartTutorial 自动点单，无需显式 BeginOrder"), T.Hologram, true, 0);
    TestTrue(TEXT("Enter 自动绑定真实配方"), T.Hologram->GetCurrentRecipe() == Recipe);
    TestTrue(TEXT("Enter 首次点单已自动展开"), T.Hologram->HasAutoOpened());
    TestFalse(TEXT("Enter 自动展开不标记玩家手动开"), T.Hologram->IsPlayerOpened());
    TestEqual(TEXT("显式重复调用前已自动绑定 Enter 最终 serial"),
        T.Hologram->GetCurrentOrderSerial(), static_cast<int64>(Spot->GetOrderSerial()));
    // 重复显式调用只验证幂等状态，不约束其 bool 返回值。
    T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::FromString(TEXT("咖啡馆向导")));
    CheckPending(*this, TEXT("自动点单后重复 BeginOrder 不改变状态"), T.Hologram, true, 0);
    const int64 OrderSerial = Spot->GetOrderSerial();
    TestEqual(TEXT("全息绑定 Enter 后最终 serial"), T.Hologram->GetCurrentOrderSerial(), OrderSerial);
    TestTrue(TEXT("真实杯已通过世界生命周期 BeginPlay"), Cup->HasActorBegunPlay());
    TestTrue(TEXT("真实杯在旋转 Spot 的检测区域"), Spot->IsInServingArea(Cup));
    TestFalse(TEXT("杯子未被持有"), Cup->IsHeld());
    TestTrue(TEXT("实际 Coffee 是 100 mL"), FMath::Abs(Cup->GetIngredientVolumeML(CoffeeId) - 100.0) < VolumeTolerance);
    TestTrue(TEXT("实际 Milk 是 100 mL"), FMath::Abs(Cup->GetIngredientVolumeML(MilkId) - 100.0) < VolumeTolerance);
    TestTrue(TEXT("真实权威体积为 200 mL"), FMath::Abs(Cup->GetPreciseVolumeML() - 200.0) < VolumeTolerance);
    TestFalse(TEXT("提交前教学步骤未完成"), Step->IsCompleted());
    TestNull(TEXT("提交前 Director 没有上菜杯"), T.Director->LastServedCup.Get());
    T.Events->OnDrinkServed.Broadcast(Spot, Cup);
    TestFalse(TEXT("伪造总线成功不能推进教学"), Step->IsCompleted());
    CheckPending(*this, TEXT("伪造事件也不能推进全息"), T.Hologram, true, 0);

    if (!TestTrue(TEXT("唯一成功来源是 Spot.TrySubmitCup"), Spot->TrySubmitCup(Cup))) return false;
    const FDrinkValidationResult Result = Spot->GetLastValidation();
    TestTrue(TEXT("Spot 真实 AcceptedCup 是这只杯"), Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
    TestEqual(TEXT("Spot 保存匹配的 accepted serial"), static_cast<int64>(Spot->GetAcceptedOrderSerial()), OrderSerial);
    TestTrue(TEXT("Result 确实 Accepted"), Result.bAccepted);
    TestEqual(TEXT("Result 原因为 Accepted"), Result.Reason, EDrinkValidationReason::Accepted);
    TestTrue(TEXT("Result.TotalML 是实际 200 mL"), FMath::Abs(Result.TotalML - 200.0) < VolumeTolerance);
    TestTrue(TEXT("Result Coffee 快照为 100 mL"), FMath::Abs(Result.Composition.Get(CoffeeId) - 100.0) < VolumeTolerance);
    TestTrue(TEXT("Result Milk 快照为 100 mL"), FMath::Abs(Result.Composition.Get(MilkId) - 100.0) < VolumeTolerance);
    TestEqual(TEXT("Result 使用杯子真实 revision"), Result.LiquidRevision, Cup->GetLiquidRevision());
    TestTrue(TEXT("Spot 广播 GI 总线同步推进教学"), Step->IsCompleted());
    TestTrue(TEXT("Director 记录同一真实杯"), T.Director->LastServedCup.Get() == Cup);
    TestTrue(TEXT("Spot API 完成冻结"), Cup->IsLiquidFrozen());
    TestTrue(TEXT("Spot API 完成吸附"), Cup->IsAttachedTo(Spot));
    TestFalse(TEXT("Spot API 关闭物理"), Cup->ContainerMesh->IsSimulatingPhysics());
    TestFalse(TEXT("Spot API 停用抓取"), Cup->GrabComp->IsActive());
    CheckDelivered(*this, TEXT("同一 GI 成功事件更新全息"), T.Hologram, true, 1);
    CheckRecord(*this, TEXT("真实杯验收历史"), T.Hologram, 0, Recipe, Result);
    if (!TestEqual(TEXT("真实提交只有一条历史"), T.Hologram->GetDeliveryRecords().Num(), 1)) return false;
    TestTrue(TEXT("GetDeliveryRecords 记录实际 mL200 而非目标 210"),
        FMath::Abs(T.Hologram->GetDeliveryRecords()[0].VolumeML - 200.0) < VolumeTolerance);
    T.Events->OnDrinkServed.Broadcast(Spot, Cup);
    TestFalse(TEXT("one-shot 不重复接受真实杯"), Spot->TrySubmitCup(Cup));
    TestEqual(TEXT("重复成功事件不重复写历史"), T.Hologram->GetDeliveryRecords().Num(), 1);

    ++GFrameCounter;
    T.World->Tick(LEVELTICK_All, .02f);
    TestNull(TEXT("Director 正常下一 tick 推进并 ExitStep"), Step->GetDirector());
    TestFalse(TEXT("正常退出解绑步骤的配方验收事件"), Spot->OnDrinkValidated.IsBound());
    TestTrue(TEXT("正常教学推进仍保留真实上菜杯"), T.Director->LastServedCup.Get() == Cup);
    CheckDelivered(*this, TEXT("教学退出不重复历史"), T.Hologram, true, 1);

    for (bool bDuringSuccess : {false, true})
    {
        FHologramWorld CancelWorld;
        UDrinkRecipe* CancelRecipe = CancelWorld.Recipe();
        AServingSpotActor* CancelSpot = CancelWorld.Spot(CancelRecipe);
        CancelSpot->SetSubmissionEnabled(false);
        ACupActor* CancelCup = CancelWorld.Cup(CancelSpot);
        UTutorialStep_WaitForServe* CancelStep = CancelWorld.WaitStep(CancelSpot, CancelRecipe);
        CancelStep->OrderHologram = CancelWorld.Hologram;
        CancelWorld.BeginPlay();
        if (!CheckFixture(*this, CancelWorld, CancelSpot)) return false;
        CancelWorld.Director->StartTutorial();
        CheckPending(*this, TEXT("取消场景也由 Enter 自动点单"), CancelWorld.Hologram, true, 0);
        const int64 CancelSerial = CancelSpot->GetOrderSerial();
        CancelWorld.Hologram->CancelOrder(CancelSpot, CancelSerial + 1);
        CheckPending(*this, TEXT("错误 serial 不能取消当前订单"), CancelWorld.Hologram, true, 0);
        if (!bDuringSuccess)
        {
            CancelWorld.Director->SkipTutorial();
            TestNull(TEXT("未交付跳过教学会正常 ExitStep"), CancelStep->GetDirector());
            TestFalse(TEXT("取消未完成订单清除 pending"), CancelWorld.Hologram->IsPending());
            TestFalse(TEXT("取消不能伪造交付成功"), CancelWorld.Hologram->IsDelivered());
            TestFalse(TEXT("取消自动订单收起面板"), CancelWorld.Hologram->IsPanelOpen());
            TestNull(TEXT("取消清除当前配方引用"), CancelWorld.Hologram->GetCurrentRecipe());
            TestEqual(TEXT("取消不生成交付历史"), CancelWorld.Hologram->GetDeliveryRecords().Num(), 0);
            TestFalse(TEXT("取消步骤不能完成"), CancelStep->IsCompleted());
            TestFalse(TEXT("取消恢复进入前禁止提交状态"), CancelSpot->bAcceptSubmissions);
            TestFalse(TEXT("取消解绑验收订阅"), CancelSpot->OnDrinkValidated.IsBound());
            continue;
        }

        // 借真实完成表现回调在成功广播栈内 Exit，不新增 UCLASS 或改写验收字段。
        UNarrativeSubsystem* Narrative = CancelWorld.World->GetSubsystem<UNarrativeSubsystem>();
        if (!TestNotNull(TEXT("成功广播退出场景有真实叙事子系统"), Narrative)) return false;
        CancelStep->CompletedText = FText::FromString(TEXT("真实交付完成。"));
        CancelWorld.Director->bDialogueTypewriter = false;
        int32 ExitDuringSuccessCount = 0;
        const FDelegateHandle Observation = Narrative->OnTextPresentationFinishedNative.AddLambda(
            [&](uint32)
            {
                if (!CancelStep->IsCompleted() || ExitDuringSuccessCount != 0) return;
                ++ExitDuringSuccessCount;
                TestTrue(TEXT("广播栈内已保存真实 AcceptedCup"),
                    CancelSpot->HasAcceptedCup(CancelCup, CancelRecipe->RecipeId));
                TestEqual(TEXT("广播栈内 accepted serial 匹配订单"),
                    static_cast<int64>(CancelSpot->GetAcceptedOrderSerial()), CancelSerial);
                // Dynamic multicast invocation order is not a contract: the display may have
                // already accepted this same event. Exit must preserve either valid state.
                const bool bPendingBefore = CancelWorld.Hologram->IsPending();
                const bool bDeliveredBefore = CancelWorld.Hologram->IsDelivered();
                const int32 HistoryBefore = CancelWorld.Hologram->GetDeliveryRecords().Num();
                TestTrue(TEXT("广播栈内全息处于合法订单状态"), bPendingBefore || bDeliveredBefore);
                CancelStep->ExitStep();
                TestNull(TEXT("成功广播栈内确实执行 ExitStep"), CancelStep->GetDirector());
                TestEqual(TEXT("Exit不改变广播中的pending状态"), CancelWorld.Hologram->IsPending(), bPendingBefore);
                TestEqual(TEXT("Exit不抹除已到达的成功状态"), CancelWorld.Hologram->IsDelivered(), bDeliveredBefore);
                TestEqual(TEXT("Exit不重写真实交付历史"), CancelWorld.Hologram->GetDeliveryRecords().Num(), HistoryBefore);
                TestTrue(TEXT("成功广播期间退出保留当前配方"),
                    CancelWorld.Hologram->GetCurrentRecipe() == CancelRecipe);
            });
        const bool bSubmitted = CancelSpot->TrySubmitCup(CancelCup);
        Narrative->OnTextPresentationFinishedNative.Remove(Observation);
        if (!TestTrue(TEXT("广播期间退出仍允许真实提交成功"), bSubmitted)) return false;
        TestEqual(TEXT("成功广播栈内退出回调恰好一次"), ExitDuringSuccessCount, 1);
        TestTrue(TEXT("广播期间退出仍保留步骤完成状态"), CancelStep->IsCompleted());
        CheckDelivered(*this, TEXT("退出后同一广播仍完成全息交付"), CancelWorld.Hologram, true, 1);
        CheckRecord(*this, TEXT("广播期间退出保留真实 200 mL 历史"), CancelWorld.Hologram,
            0, CancelRecipe, CancelSpot->GetLastValidation());
        if (!TestEqual(TEXT("广播期间退出也仅记录一次"), CancelWorld.Hologram->GetDeliveryRecords().Num(), 1)) return false;
        TestTrue(TEXT("广播期间退出的记录实际为 200 mL"),
            FMath::Abs(CancelWorld.Hologram->GetDeliveryRecords()[0].VolumeML - 200.0) < VolumeTolerance);
        TestFalse(TEXT("广播期间退出解绑验收事件"), CancelSpot->OnDrinkValidated.IsBound());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramSavedBlueprintWidgetAndServingFlowTest,
    "CyberCafe.Hologram.SavedBlueprintWidgetAndServingFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramSavedBlueprintWidgetAndServingFlowTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    UWorld* SavedWorld = LoadObject<UWorld>(nullptr, TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
    if (!TestNotNull(TEXT("加载已保存 L_Cafe"), SavedWorld) ||
        !TestNotNull(TEXT("已保存地图有 PersistentLevel"), SavedWorld->PersistentLevel.Get())) return false;
    ATutorialDirector* SavedDirector = nullptr;
    for (AActor* Actor : SavedWorld->PersistentLevel->Actors)
    {
        if (ATutorialDirector* Candidate = Cast<ATutorialDirector>(Actor))
        {
            if (Candidate->Steps.IsValidIndex(6) &&
                Cast<UTutorialStep_WaitForServe>(Candidate->Steps[6].Get()))
            {
                TestNull(TEXT("已保存地图的目标教学导演唯一"), SavedDirector);
                SavedDirector = Candidate;
            }
        }
    }
    if (!TestNotNull(TEXT("从 PersistentLevel 找到 Step[6] 教学导演"), SavedDirector)) return false;
    UTutorialStep_WaitForServe* SavedStep = Cast<UTutorialStep_WaitForServe>(SavedDirector->Steps[6].Get());
    AOrderHologramActor* SavedDisplay = SavedStep->OrderHologram.Get();
    if (!TestNotNull(TEXT("Step[6] 已保存 OrderHologram 引用"), SavedDisplay)) return false;
    TestTrue(TEXT("保存的全息引用属于 L_Cafe PersistentLevel"), SavedDisplay->GetLevel() == SavedWorld->PersistentLevel);
    UClass* SavedActorClass = LoadClass<AOrderHologramActor>(nullptr,
        TEXT("/Game/VRTemplate/Blueprints/Serving/BP_OrderHologram.BP_OrderHologram_C"));
    UClass* SavedWidgetClass = LoadClass<UOrderHologramWidget>(nullptr,
        TEXT("/Game/VRTemplate/Blueprints/Serving/UI/WBP_OrderHologram.WBP_OrderHologram_C"));
    if (!TestNotNull(TEXT("加载已保存 BP_OrderHologram 生成类"), SavedActorClass) ||
        !TestNotNull(TEXT("加载已保存 WBP_OrderHologram 生成类"), SavedWidgetClass)) return false;
    TestTrue(TEXT("地图引用真实 BP_OrderHologram 类而非 native Actor"), SavedDisplay->GetClass() == SavedActorClass);
    TestTrue(TEXT("保存 Actor 类确为蓝图生成类"), SavedActorClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
    TestTrue(TEXT("保存 Widget 类确为蓝图生成类"), SavedWidgetClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
    const auto CheckSavedDisplay = [&](const FString& Label, const AOrderHologramActor* Display)
    {
        TestTrue(Label + TEXT("：保存 WBP_OrderHologram 类引用"), Display->DisplayWidgetClass.Get() == SavedWidgetClass);
        TestTrue(Label + TEXT("：panel center 保存为 (0,0,55)"), Display->PanelCenter.Equals(FVector(0, 0, 55), .00001));
        TestEqual(Label + TEXT("：panel width 保存为 60 cm"), Display->PanelWidthCM, 60.f);
        if (TestNotNull(Label + TEXT("：PanelMaterial 引用有效"), Display->PanelMaterial.Get()))
        {
            TestTrue(Label + TEXT("：PanelMaterial 为 Unlit"), Display->PanelMaterial->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
        }
        // @trace BC-01. Instance now represents the user-approved first guest;
        // reusable BP defaults remain the original tutorial guide.
        const bool bFirstCustomerInstance = Display == SavedDisplay;
        TestEqual(Label + TEXT("：顾客 ID 与实例用途一致"), Display->CustomerId, FName(bFirstCustomerInstance ? TEXT("BarGirl_FirstCustomer") : TEXT("TutorialGuide")));
        const FString Name = Display->CustomerName.ToString();
        TestEqual(Label + TEXT("：实例客人或通用默认身份明确"), Name, FString(bFirstCustomerInstance ? TEXT("时零") : TEXT("咖啡馆向导")));
        if (bFirstCustomerInstance) TestEqual(Label + TEXT("：第一客人名字与对话导演一致"), Name, SavedDirector->DialogueSpeakerName.ToString());
        TestFalse(Label + TEXT("：顾客名无旧阿岚"), Name.Contains(TEXT("阿岚")));
        TestEqual(Label + TEXT("：公开备注条数与实例用途一致"), Display->CustomerNotes.Num(), bFirstCustomerInstance ? 3 : 2);
        if (Display->CustomerNotes.IsValidIndex(1))
        {
            TestEqual(Label + TEXT("：备注只记录公开称呼"), Display->CustomerNotes[0].ToString(), FString(bFirstCustomerInstance ? TEXT("称呼：时零") : TEXT("教学客人")));
            TestEqual(Label + TEXT("：仅当前订单事实，不虚构长期喜好"), Display->CustomerNotes[1].ToString(), FString(bFirstCustomerInstance ? TEXT("首杯：冰拿铁；咖啡与牛奶对半，加冰即可") : TEXT("喜好尚未了解")));
        }
        if (bFirstCustomerInstance && Display->CustomerNotes.IsValidIndex(2))
            TestEqual(Label + TEXT("：备注只记录本次来意"), Display->CustomerNotes[2].ToString(), FString(TEXT("本次：想在店里坐一会儿")));
        for (const FText& Note : Display->CustomerNotes)
        {
            TestFalse(Label + TEXT("：保存备注无旧阿岚"), Note.ToString().Contains(TEXT("阿岚")));
        }
    };
    CheckSavedDisplay(TEXT("L_Cafe 实例"), SavedDisplay);
    CheckSavedDisplay(TEXT("BP 保存默认值"), SavedActorClass->GetDefaultObject<AOrderHologramActor>());

    FHologramWorld T;
    if (!TestTrue(TEXT("先销毁 fixture native 全息 Actor"), T.Hologram->Destroy())) return false;
    T.Hologram = nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    T.Hologram = T.World->SpawnActor<AOrderHologramActor>(SavedDisplay->GetClass(),
        FVector(-150, 60, 0), FRotator(0, 40, 0), Params);
    if (!TestNotNull(TEXT("用保存实例的真实 BP 类生成全息 Actor"), T.Hologram)) return false;
    UDrinkRecipe* Recipe = LoadObject<UDrinkRecipe>(nullptr,
        TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));
    if (!TestNotNull(TEXT("加载已保存真实 DA_Latte 配方"), Recipe)) return false;
    UClass* CupClass = Recipe->AcceptedCupClass.Get();
    if (!TestNotNull(TEXT("保存 DA_Latte 指定真实杯类"), CupClass) ||
        !TestTrue(TEXT("保存杯类派生自 ACupActor"), CupClass->IsChildOf(ACupActor::StaticClass()))) return false;
    TestEqual(TEXT("保存配方 ID 仍为 Latte"), Recipe->RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("保存配方显示名为冰拿铁"), Recipe->DisplayName.ToString(), FString(TEXT("冰拿铁")));
    TestEqual(TEXT("旧加冰建议值保留"), Recipe->ExpectedIceCount, 3);
    AServingSpotActor* Spot = T.Spot(Recipe);
    Spot->SetSubmissionEnabled(false);
    const FVector CupPosition = Spot->DetectBox->GetComponentTransform().TransformPosition(FVector(1, 1, 1));
    const FTransform CupTransform(FRotator::ZeroRotator, CupPosition);
    ACupActor* Cup = T.World->SpawnActorDeferred<ACupActor>(CupClass,
        CupTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("生成保存 AcceptedCupClass 的真实杯"), Cup)) return false;
    Cup->bCanPour = false;
    Cup->bEnableSplash = false;
    Cup->LiquidFXTemplate = nullptr;
    Cup->SplashEffectTemplate = nullptr;
    Cup->LiquidFX->SetAutoActivate(false);
    Cup->LiquidFX->SetAsset(nullptr);
    Cup->PourFX->SetAutoActivate(false);
    Cup->PourFX->SetAsset(nullptr);
    Cup->ContainerMesh->SetSimulatePhysics(false);
    Cup->FinishSpawning(CupTransform);
    Cup->ContainerMesh->SetSimulatePhysics(false);
    Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    T.Cups.Add(Cup);
    UTutorialStep_WaitForServe* Step = T.WaitStep(Spot, Recipe);
    Step->OrderHologram = T.Hologram;
    T.BeginPlay();
    if (!CheckFixture(*this, T, Spot)) return false;
    TestTrue(TEXT("保存配方杯类与 World 一致"), Cup->GetClass() == CupClass && Cup->GetWorld() == T.World);
    if (!TestEqual(TEXT("真实杯接收 Coffee 100 mL"), Cup->AddIngredientLiquid(100.f, CoffeeId, FLinearColor::Red), 100.f) ||
        !TestEqual(TEXT("真实杯接收 Milk 100 mL"), Cup->AddIngredientLiquid(100.f, MilkId, FLinearColor::White), 100.f) ||
        !TestTrue(TEXT("同 World 三块真实冰穿杯口验收"), TutorialMenuTestHelpers::AddIce(Cup, 3))) return false;
    UOrderHologramWidget* Widget = T.Hologram->GetDisplayWidget();
    if (!TestNotNull(TEXT("BeginPlay 自动生成实际 DisplayWidget，未手动 SetWidget"), Widget)) return false;
    TestTrue(TEXT("实际运行控件类就是已保存 WBP"), Widget->GetClass() == SavedWidgetClass);
    TestTrue(TEXT("GetDisplayWidget 返回 Panel 实际控件"), T.Hologram->Panel->GetUserWidgetObject() == Widget);
    if (TestNotNull(TEXT("WBP 实际控件树已构建"), Widget->WidgetTree.Get()))
    {
        TestNotNull(TEXT("WBP 实际控件树有根布局"), Widget->WidgetTree->RootWidget.Get());
    }
    const TArray<FText> RuntimeNotesBefore = T.Hologram->CustomerNotes;
    T.Director->StartTutorial();
    CheckPending(*this, TEXT("真实 BP 由 WaitStep 自动点单"), T.Hologram, true, 0);
    TestTrue(TEXT("运行订单绑定加载的真实 DA_Latte"), T.Hologram->GetCurrentRecipe() == Recipe);
    TestEqual(TEXT("运行订单自动绑定 Enter 最终 serial"), T.Hologram->GetCurrentOrderSerial(),
        static_cast<int64>(Spot->GetOrderSerial()));
    TestTrue(TEXT("真实 BP 场景杯已 BeginPlay"), Cup->HasActorBegunPlay());
    TestTrue(TEXT("真实 BP 场景杯在上菜区域"), Spot->IsInServingArea(Cup));
    const double CoffeeML = Cup->GetIngredientVolumeML(CoffeeId);
    const double MilkML = Cup->GetIngredientVolumeML(MilkId);
    const double CupML = Cup->GetPreciseVolumeML();
    TestTrue(TEXT("真实杯 Coffee 为 100 mL"), FMath::Abs(CoffeeML - 100.0) < VolumeTolerance);
    TestTrue(TEXT("真实杯 Milk 为 100 mL"), FMath::Abs(MilkML - 100.0) < VolumeTolerance);
    TestTrue(TEXT("真实杯总量为 200 mL"), FMath::Abs(CupML - 200.0) < VolumeTolerance);
    const FString OrderText = Widget->GetOrderText().ToString();
    const FString InitialHistoryText = Widget->GetHistoryText().ToString();
    const bool bAutoPending = T.Hologram->IsPending();
    const bool bAutoOpen = T.Hologram->IsPanelOpen();
    const UDrinkRecipe* ActualRecipe = T.Hologram->GetCurrentRecipe();
    if (!TestNotNull(TEXT("真实订单权威配方"), ActualRecipe) || !TestNotNull(TEXT("真实 WBP 控件树"), Widget->WidgetTree.Get())
        || !TestNotNull(TEXT("真实保存菜单"), T.Hologram->RecipeMenu.Get())) return false;
    TestTrue(TEXT("展示取实际订单而非浏览配方或首单预览"), T.Hologram->GetPresentedRecipe() == ActualRecipe && ActualRecipe == Recipe);
    TestEqual(TEXT("实际配方目标仍为 200 mL"), ActualRecipe->TargetVolumeML, 200.0);
    TestEqual(TEXT("隐藏范围文字不改变实际最小验收量"), ActualRecipe->MinVolumeML, 180.0);
    TestEqual(TEXT("隐藏范围文字不改变实际最大验收量"), ActualRecipe->MaxVolumeML, 220.0);
    if (!TestEqual(TEXT("实际配方仍为两条权威原料规则"), ActualRecipe->Rules.Num(), 2)) return false;
    const FName RuleIds[] = { CoffeeId, MilkId };
    for (int32 I = 0; I < 2; ++I)
    {
        TestEqual(TEXT("实际原料英文 ID 保留于数据"), ActualRecipe->Rules[I].IngredientId, RuleIds[I]);
        TestEqual(TEXT("实际原料最小比例仍为 45%"), ActualRecipe->Rules[I].MinFraction, .45);
        TestEqual(TEXT("实际原料最大比例仍为 55%"), ActualRecipe->Rules[I].MaxFraction, .55);
    }
    const auto* Entry = T.Hologram->RecipeMenu->Entries.FindByPredicate([ActualRecipe](const FDrinkMenuEntry& E)
    {
        return E.Recipe.Get() == ActualRecipe;
    });
    if (!TestNotNull(TEXT("杯名取实际订单对应的保存菜单条目"), Entry)) return false;
    const FString ExpectedIngredients = TEXT("咖啡  100 mL\n牛奶  100 mL");
    const FString ExpectedOrder = TEXT("待制作 / 待交付\n冰拿铁\n目标总量  200 mL\n")
        + Entry->CupName.ToString() + TEXT("\n加冰即可\n") + ExpectedIngredients;
    TestEqual(TEXT("真实 WBP 订单全文仅状态中文名目标中文量及杯冰"), OrderText, ExpectedOrder);
    const TCHAR* CoreNames[] = { TEXT("OrderStatus"), TEXT("RecipeName"), TEXT("TargetVolume"),
        TEXT("OrderCupName"), TEXT("OrderIceCount"), TEXT("OrderIngredients") };
    const FString CoreValues[] = { TEXT("待制作 / 待交付"), TEXT("冰拿铁"), TEXT("目标总量  200 mL"),
        Entry->CupName.ToString(), TEXT("加冰即可"), ExpectedIngredients };
    for (int32 I = 0; I < 6; ++I)
    {
        auto* Text = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(CoreNames[I])));
        if (!TestNotNull(TEXT("真实订单核心控件"), Text)) return false;
        TestEqual(Text->GetName() + TEXT("：核心中文用量与真实订单一致"), Text->GetText().ToString(), CoreValues[I]);
        TestTrue(Text->GetName() + TEXT("：核心内容真实可见"), Text->GetVisibility() == ESlateVisibility::HitTestInvisible);
    }
    const TCHAR* RemovedNames[] = { TEXT("CustomerId"), TEXT("CustomerNotes"), TEXT("NotesHeading"), TEXT("OrderTolerance") };
    for (const TCHAR* Name : RemovedNames)
    {
        auto* Text = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(Name)));
        if (!TestNotNull(FString(Name) + TEXT("：旧命名控件仍在"), Text)) return false;
        TestTrue(FString(Name) + TEXT("：真实清空且折叠，不把 Notes 拼进订单"), Text->GetText().IsEmpty()
            && Text->GetVisibility() == ESlateVisibility::Collapsed);
    }
    if (!TestEqual(TEXT("精简展示保留运行 Actor 的 Notes 数据条数"), T.Hologram->CustomerNotes.Num(), RuntimeNotesBefore.Num())
        || !TestTrue(TEXT("Notes 数据非空，隐藏控件不是空测"), !RuntimeNotesBefore.IsEmpty())) return false;
    for (int32 I = 0; I < RuntimeNotesBefore.Num(); ++I)
        TestEqual(TEXT("精简只折叠 Notes UI，不删除备注数据"), T.Hologram->CustomerNotes[I].ToString(), RuntimeNotesBefore[I].ToString());
    const TCHAR* RemovedWords[] = { TEXT("Coffee"), TEXT("Milk"), TEXT("约"), TEXT("ratio"), TEXT("%"),
        TEXT("45"), TEXT("55"), TEXT("180"), TEXT("220"), TEXT("验收"), TEXT("比例"), TEXT("Notes") };
    for (const TCHAR* Word : RemovedWords) TestFalse(FString(Word) + TEXT("：实际订单不再显示英文 ID 或范围小字"), OrderText.Contains(Word));
    TestTrue(TEXT("真实 WBP 初始历史为暂无本局交付"), InitialHistoryText.Contains(TEXT("暂无本局交付")));
    T.Hologram->Tick(T.Hologram->AnimationSeconds);
    const FVector RuntimePanelCenter = T.Hologram->Panel->GetRelativeLocation();
    const double RuntimePanelWidthCM = T.Hologram->Panel->GetDrawSize().X * T.Hologram->Panel->GetRelativeScale3D().Y;
    TestTrue(TEXT("运行 BP 展开后实际面板中心 (0,0,55)"), RuntimePanelCenter.Equals(FVector(0, 0, 55), .00001));
    TestTrue(TEXT("运行 BP 实际面板宽度 60 cm"), FMath::Abs(RuntimePanelWidthCM - 60.0) < VolumeTolerance);
    if (!TestTrue(TEXT("真实 100+100 加三块冰杯通过 TrySubmitCup 成功"), Spot->TrySubmitCup(Cup))) return false;
    const FDrinkValidationResult Result = Spot->GetLastValidation();
    TestEqual(TEXT("保存配方真实验收快照恰好三块冰"), Result.IceCount, 3);
    const FString HistoryText = Widget->GetHistoryText().ToString();
    TestTrue(TEXT("真实 DA_Latte 提交同步完成 WaitStep"), Step->IsCompleted());
    CheckDelivered(*this, TEXT("真实保存 BP 收到交付"), T.Hologram, true, 1);
    CheckRecord(*this, TEXT("真实保存 BP 的交付快照"), T.Hologram, 0, Recipe, Result);
    TestTrue(TEXT("实际 WBP 历史含冰拿铁"), HistoryText.Contains(TEXT("冰拿铁")));
    TestTrue(TEXT("实际 WBP 历史含实际 200"), HistoryText.Contains(TEXT("200")));
    TestFalse(TEXT("实际 WBP 交付后不再显示空历史"), HistoryText.Contains(TEXT("暂无本局交付")));
    TestFalse(TEXT("实际 WBP 历史不虚构刚刚好评价"), HistoryText.Contains(TEXT("刚刚好")));
    TestFalse(TEXT("实际 WBP 历史不虚构喜欢评价"), HistoryText.Contains(TEXT("喜欢")));
    if (!TestEqual(TEXT("保存 BP 真实交付记录恰好一条"), T.Hologram->GetDeliveryRecords().Num(), 1)) return false;
    const FHologramDeliveryRecord& Record = T.Hologram->GetDeliveryRecords()[0];
    TestTrue(TEXT("保存 BP 记录实际体积为 200 mL"), FMath::Abs(Record.VolumeML - 200.0) < VolumeTolerance);

    // 仅在执行此测试时导出捕获值；不依赖 Json 模块，不用期望文本冒充显示测量。
    const auto Escape = [](FString Value)
    {
        Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
        Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
        Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
        Value.ReplaceInline(TEXT("\b"), TEXT("\\b"));
        Value.ReplaceInline(TEXT("\f"), TEXT("\\f"));
        return Value;
    };
    TArray<FString> Fields;
    const auto AddText = [&](const TCHAR* Label, const FString& Value)
    {
        Fields.Add(FString::Printf(TEXT("  \"%s\": \"%s\""), Label, *Escape(Value)));
    };
    const auto AddNumber = [&](const TCHAR* Label, double Value)
    {
        Fields.Add(FString::Printf(TEXT("  \"%s\": %.17g"), Label, Value));
    };
    const auto AddBool = [&](const TCHAR* Label, bool bValue)
    {
        Fields.Add(FString::Printf(TEXT("  \"%s\": %s"), Label, bValue ? TEXT("true") : TEXT("false")));
    };
    AddText(TEXT("label"), TEXT("已保存 BP/WBP 的真实自动点单与交付捕获"));
    AddText(TEXT("map"), SavedWorld->GetPathName());
    AddText(TEXT("saved_actor_class"), SavedDisplay->GetClass()->GetPathName());
    AddText(TEXT("actual_widget_class"), Widget->GetClass()->GetPathName());
    AddText(TEXT("panel_material"), GetPathNameSafe(SavedDisplay->PanelMaterial.Get()));
    AddBool(TEXT("panel_material_unlit"), IsValid(SavedDisplay->PanelMaterial.Get()) &&
        SavedDisplay->PanelMaterial->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
    AddNumber(TEXT("saved_panel_center_x_cm"), SavedDisplay->PanelCenter.X);
    AddNumber(TEXT("saved_panel_center_y_cm"), SavedDisplay->PanelCenter.Y);
    AddNumber(TEXT("saved_panel_center_z_cm"), SavedDisplay->PanelCenter.Z);
    AddNumber(TEXT("saved_panel_width_cm"), SavedDisplay->PanelWidthCM);
    AddNumber(TEXT("runtime_panel_center_x_cm"), RuntimePanelCenter.X);
    AddNumber(TEXT("runtime_panel_center_y_cm"), RuntimePanelCenter.Y);
    AddNumber(TEXT("runtime_panel_center_z_cm"), RuntimePanelCenter.Z);
    AddNumber(TEXT("runtime_panel_width_cm"), RuntimePanelWidthCM);
    AddText(TEXT("customer_name"), SavedDisplay->CustomerName.ToString());
    TArray<FString> Notes;
    for (const FText& Note : SavedDisplay->CustomerNotes) Notes.Add(Note.ToString());
    AddText(TEXT("customer_notes"), FString::Join(Notes, TEXT("\n")));
    AddText(TEXT("recipe"), Recipe->GetPathName());
    AddNumber(TEXT("recipe_target_ml"), Recipe->TargetVolumeML);
    AddNumber(TEXT("cup_coffee_ml"), CoffeeML);
    AddNumber(TEXT("cup_milk_ml"), MilkML);
    AddNumber(TEXT("cup_total_ml"), CupML);
    AddText(TEXT("actual_order_text"), OrderText);
    AddText(TEXT("actual_initial_history_text"), InitialHistoryText);
    AddBool(TEXT("auto_pending"), bAutoPending);
    AddBool(TEXT("auto_panel_open"), bAutoOpen);
    AddBool(TEXT("accepted"), Result.bAccepted);
    AddNumber(TEXT("accepted_total_ml"), Result.TotalML);
    AddBool(TEXT("step_completed"), Step->IsCompleted());
    AddBool(TEXT("delivered"), T.Hologram->IsDelivered());
    AddNumber(TEXT("history_count"), T.Hologram->GetDeliveryRecords().Num());
    AddNumber(TEXT("history_actual_ml"), Record.VolumeML);
    AddText(TEXT("history_drink_name"), Record.DrinkName.ToString());
    AddText(TEXT("actual_history_text"), HistoryText);
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Hologram"));
    if (!TestTrue(TEXT("创建测试捕获目录 Saved/Hologram"), IFileManager::Get().MakeDirectory(*Directory, true))) return false;
    const FString Json = TEXT("{\n") + FString::Join(Fields, TEXT(",\n")) + TEXT("\n}\n");
    TestTrue(TEXT("保存真实捕获值到 Saved/Hologram/saved_flow.json"),
        FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Directory, TEXT("saved_flow.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramCompactGenericPresentationTest,
    "CyberCafe.Hologram.CompactChineseMidpointAmountsAndInvalidNumericPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramCompactGenericPresentationTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    FHologramWorld T;
    auto* Recipe = T.Recipe(FName(TEXT("GenericDrink")));
    Recipe->DisplayName = FText::FromString(TEXT("调配饮品"));
    Recipe->TargetVolumeML = 240.0;
    Recipe->MinVolumeML = 180.0;
    Recipe->MaxVolumeML = 260.0;
    Recipe->ExpectedIceCount = 0;
    Recipe->Rules[0].MinFraction = .2;
    Recipe->Rules[0].MaxFraction = .4;
    Recipe->Rules[1].MinFraction = .5;
    Recipe->Rules[1].MaxFraction = .7;
    T.Hologram->CustomerNotes = { FText::FromString(TEXT("教学客人")), FText::FromString(TEXT("喜好尚未了解")) };
    const TArray<FText> NotesBefore = T.Hologram->CustomerNotes;
    auto* Spot = T.Spot(Recipe);
    T.BeginPlay();
    if (!CheckFixture(*this, T, Spot)
        || !TestTrue(TEXT("通用配方真实建立订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty()))) return false;
    auto* Widget = T.Hologram->GetDisplayWidget();
    if (!TestNotNull(TEXT("通用订单实际 Widget"), Widget) || !TestNotNull(TEXT("通用订单实际控件树"), Widget->WidgetTree.Get())) return false;
    const FString ExpectedIngredients = TEXT("咖啡  80 mL\n牛奶  160 mL");
    TestEqual(TEXT("非对半配方仍按 midpoint 总和归一化，而非固定 100 或目标对半"), Widget->GetOrderText().ToString(),
        FString(TEXT("待制作 / 待交付\n调配饮品\n目标总量  240 mL\n不加冰\n")) + ExpectedIngredients);
    auto* Ingredients = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("OrderIngredients")));
    if (!TestNotNull(TEXT("真实中文原料用量控件"), Ingredients)) return false;
    TestEqual(TEXT("真实可见中文 mL 与归一化建议量一致"), Ingredients->GetText().ToString(), ExpectedIngredients);
    TestTrue(TEXT("中文用量实际可见"), Ingredients->GetVisibility() == ESlateVisibility::HitTestInvisible);
    TestTrue(TEXT("显示与当前真实订单指针一致"), T.Hologram->GetPresentedRecipe() == Recipe && T.Hologram->GetCurrentRecipe() == Recipe);
    TestEqual(TEXT("展示不修改目标量"), Recipe->TargetVolumeML, 240.0);
    TestEqual(TEXT("展示不修改原最小量"), Recipe->MinVolumeML, 180.0);
    TestEqual(TEXT("展示不修改原最大量"), Recipe->MaxVolumeML, 260.0);
    TestEqual(TEXT("展示不修改咖啡最小比例"), Recipe->Rules[0].MinFraction, .2);
    TestEqual(TEXT("展示不修改咖啡最大比例"), Recipe->Rules[0].MaxFraction, .4);
    TestEqual(TEXT("展示不修改牛奶最小比例"), Recipe->Rules[1].MinFraction, .5);
    TestEqual(TEXT("展示不修改牛奶最大比例"), Recipe->Rules[1].MaxFraction, .7);
    const TCHAR* Removed[] = { TEXT("CustomerId"), TEXT("CustomerNotes"), TEXT("NotesHeading"), TEXT("OrderTolerance") };
    for (const TCHAR* Name : Removed)
    {
        auto* Text = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(Name)));
        if (!TestNotNull(TEXT("通用订单旧命名控件保留"), Text)) return false;
        TestTrue(TEXT("通用配方不显示备注和验收范围"), Text->GetText().IsEmpty() && Text->GetVisibility() == ESlateVisibility::Collapsed);
    }
    if (!TestEqual(TEXT("隐藏 Notes 保留真实数据条数"), T.Hologram->CustomerNotes.Num(), NotesBefore.Num())) return false;
    for (int32 I = 0; I < NotesBefore.Num(); ++I)
        TestEqual(TEXT("隐藏 Notes 不删除真实数据内容"), T.Hologram->CustomerNotes[I].ToString(), NotesBefore[I].ToString());

    // 仅给展示输入无效数值，不改验收逻辑、不提交杯，也不以隐藏范围跳过运算保护。
    for (int32 Case = 0; Case < 12; ++Case)
    {
        auto* Invalid = T.Recipe(FName(*FString::Printf(TEXT("InvalidDisplay%d"), Case)));
        Invalid->DisplayName = FText::FromString(TEXT("无效测试饮品"));
        Invalid->ExpectedIceCount = 0;
        switch (Case)
        {
        case 0: Invalid->TargetVolumeML = std::numeric_limits<double>::quiet_NaN(); break;
        case 1: Invalid->TargetVolumeML = std::numeric_limits<double>::infinity(); break;
        case 2: Invalid->MinVolumeML = std::numeric_limits<double>::quiet_NaN(); break;
        case 3: Invalid->MaxVolumeML = std::numeric_limits<double>::infinity(); break;
        case 4: Invalid->Rules[0].MinFraction = std::numeric_limits<double>::quiet_NaN(); break;
        case 5: Invalid->Rules[0].MinFraction = -.1; break;
        case 6: Invalid->Rules[0].MinFraction = .6; break;
        case 7: Invalid->Rules[0].MaxFraction = 1.1; break;
        case 8: Invalid->Rules.Reset(); break;
        case 9:
            for (auto& Rule : Invalid->Rules) { Rule.MinFraction = 0.0; Rule.MaxFraction = 0.0; }
            break;
        case 10: Invalid->TargetVolumeML = 0.0; break;
        case 11: Invalid->Rules[0].MaxFraction = std::numeric_limits<double>::infinity(); break;
        }
        Spot->SetActiveRecipe(Invalid);
        if (!TestTrue(TEXT("无效显示输入仍通过真实订单路径绑定"), T.Hologram->BeginOrder(Invalid, Spot, T.NPC, FText::GetEmpty()))) return false;
        const FString Text = Widget->GetOrderText().ToString();
        TestEqual(TEXT("无效数值不生成目标或伪造原料 mL"), Text,
            FString(TEXT("待制作 / 待交付\n无效测试饮品\n不加冰\n当前配方配置无效")));
        TestEqual(TEXT("无效数值真实显示配置无效"), Ingredients->GetText().ToString(), FString(TEXT("当前配方配置无效")));
        TestTrue(TEXT("无效输入仍保留真实绑定，而不是替换为可用配方"), T.Hologram->GetCurrentRecipe() == Invalid
            && T.Hologram->GetPresentedRecipe() == Invalid);
        TestEqual(TEXT("显示失败不伪造交付账本"), T.Hologram->GetDeliveryRecords().Num(), 0);
        TestFalse(TEXT("显示失败不伪造订单交付成功"), T.Hologram->IsDelivered());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHologramTouchHoldingOutsideRearmTest,
    "CyberCafe.Hologram.TouchHoldingOutsideRearm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHologramTouchHoldingOutsideRearmTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    for (EControllerHand First : {EControllerHand::Left, EControllerHand::Right})
    {
        FHologramWorld T;
        UDrinkRecipe* Recipe = T.Recipe();
        AServingSpotActor* Spot = T.Spot(Recipe);
        T.Hologram->SetActorLocationAndRotation(FVector(123, -77, 45), FRotator(20, 65, 10));
        T.BeginPlay();
        if (!CheckFixture(*this, T, Spot) ||
            !TestNotNull(TEXT("持物退出测试有真实 ButtonZone"), T.Hologram->ButtonZone.Get())) return false;
        if (!TestTrue(TEXT("持物退出测试建立订单"), T.Hologram->BeginOrder(Recipe, Spot, T.NPC, FText::GetEmpty()))) return false;
        const EControllerHand Second = First == EControllerHand::Left ? EControllerHand::Right : EControllerHand::Left;
        const FString Label = First == EControllerHand::Left ? TEXT("左手先触发") : TEXT("右手先触发");
        const FTransform ZoneTransform = T.Hologram->ButtonZone->GetComponentTransform();
        const FVector Outer = T.Hologram->ButtonZone->GetUnscaledBoxExtent() + FVector(T.Hologram->TouchRearmMarginCM);
        const FVector Inside = ZoneTransform.TransformPosition(FVector::ZeroVector);
        const FVector Far = ZoneTransform.TransformPosition(Outer + FVector(10));
        TestFalse(Label + TEXT("：第一手初始真实区外"), T.Hologram->ProcessTouchSample(First, Far));
        TestFalse(Label + TEXT("：第二手初始真实区外"), T.Hologram->ProcessTouchSample(Second, Far));
        TestTrue(Label + TEXT("：第一手首次进区触发关闭"), T.Hologram->ProcessTouchSample(First, Inside));
        TestFalse(Label + TEXT("：第二手进区不连跳并记录尚未退出"), T.Hologram->ProcessTouchSample(Second, Inside));
        CheckPending(*this, Label + TEXT("首轮锁存关闭"), T.Hologram, false, 0);
        TestFalse(Label + TEXT("：第二手持物但失去追踪不触发开关"),
            T.Hologram->ProcessTouchSample(Second, Far, false, true));
        TestFalse(Label + TEXT("：第一手真实 far 仅确认自己退出"), T.Hologram->ProcessTouchSample(First, Far));
        TestTrue(Label + TEXT("：另一手失追不堵住仍有效手的重新进入"), T.Hologram->ProcessTouchSample(First, Inside));
        CheckPending(*this, Label + TEXT("有效手在另一手失追后可展开"), T.Hologram, true, 0);
        // A second real outside/inside stroke restores the closed state for the
        // original holding/exit checks below; no state flags or API toggles.
        TestFalse(Label + TEXT("：有效手再次真实退出"), T.Hologram->ProcessTouchSample(First, Far));
        TestTrue(Label + TEXT("：有效手再次进入可关闭"), T.Hologram->ProcessTouchSample(First, Inside));
        CheckPending(*this, Label + TEXT("真实再次触摸关闭"), T.Hologram, false, 0);
        TestFalse(Label + TEXT("：另一手 tracked 持物 far 应确认 outside 而不触发"),
            T.Hologram->ProcessTouchSample(Second, Far, true, true));
        TestFalse(Label + TEXT("：持物另一手退出但第一手仍在区内，不得重臂"),
            T.Hologram->ProcessTouchSample(First, Inside));
        TestFalse(Label + TEXT("：第一手未 tracked 的 far 也不能重臂"),
            T.Hologram->ProcessTouchSample(First, Far, false, false));
        TestFalse(Label + TEXT("：丢追踪再回区内仍不重复触发"), T.Hologram->ProcessTouchSample(First, Inside));
        CheckPending(*this, Label + TEXT("未追踪样本不复位锁存"), T.Hologram, false, 0);
        TestFalse(Label + TEXT("：第一手 tracked 非持物 far 使双手同时 outside 重臂"),
            T.Hologram->ProcessTouchSample(First, Far, true, false));
        TestTrue(Label + TEXT("：持物另一手真实退出后第一手再进触发一次"),
            T.Hologram->ProcessTouchSample(First, Inside, true, false));
        CheckPending(*this, Label + TEXT("正确重臂再次展开"), T.Hologram, true, 0);
        TestFalse(Label + TEXT("：第二轮持续区内仍去重"), T.Hologram->ProcessTouchSample(First, Inside));
        TestFalse(Label + TEXT("：另一手持物区内不能反向切换"), T.Hologram->ProcessTouchSample(Second, Inside, true, true));
        TestTrue(Label + TEXT("：再次触摸展开属于玩家手动开"), T.Hologram->IsPlayerOpened());
    }
    return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
