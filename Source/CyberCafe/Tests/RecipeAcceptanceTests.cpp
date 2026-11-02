// UE5.6.1 / PC VR：依赖约定中的配方与成分 API；实现齐备后再编译、运行。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Liquid/LiquidComposition.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/CupActor.h"
#include "Liquid/BottleActor.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "GrabComponent.h"
#include "MotionControllerComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"

namespace
{
const FName CoffeeId(TEXT("Coffee"));
const FName MilkId(TEXT("Milk"));
const FName UnknownId(TEXT("Unknown"));
constexpr double VolumeTolerance = 1.e-6;

FString EscapeRecipeMeasurementJson(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    Value.ReplaceInline(TEXT("\b"), TEXT("\\b"));
    Value.ReplaceInline(TEXT("\f"), TEXT("\\f"));
    return Value;
}

// 沿用 LiquidConservationTests.cpp 的真实 World，无 BeginPlay、无蓝图或资产依赖。
struct FRecipeTestWorld
{
    UWorld* World;

    FRecipeTestWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
            true, ERHIFeatureLevel::SM5, &IV);
    }

    ~FRecipeTestWorld()
    {
        World->DestroyWorld(false);
    }

    void Configure(ALiquidContainerActor* Container, float CapacityML,
        float InitialFill = 0.f)
    {
        Container->ContainerMesh->SetSimulatePhysics(false);
        Container->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Container->LiquidFX->DeactivateImmediate();
        Container->PourFX->DeactivateImmediate();
        Container->bCanPour = false;
        Container->bEnableSplash = false;
        Container->MaxVolumeML = CapacityML;
        Container->FillAmount = InitialFill;
        Container->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector);
        Container->PourEntryRadius = 3.f;
    }

    ACupActor* Cup(float CoffeeML, float MilkML, float CapacityML = 300.f,
        FVector Position = FVector::ZeroVector)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ACupActor* Cup = World->SpawnActor<ACupActor>(Position, FRotator::ZeroRotator, Params);
        Configure(Cup, CapacityML);
        Cup->InitializeLiquidState();
        if (CoffeeML > 0.f)
        {
            Cup->AddIngredientLiquid(CoffeeML, CoffeeId, FLinearColor::Red);
        }
        if (MilkML > 0.f)
        {
            Cup->AddIngredientLiquid(MilkML, MilkId, FLinearColor::White);
        }
        return Cup;
    }

    UDrinkRecipe* Recipe()
    {
        return NewObject<UDrinkRecipe>(World);
    }

    AServingSpotActor* Spot(UDrinkRecipe* Recipe)
    {
        AServingSpotActor* Spot = World->SpawnActor<AServingSpotActor>();
        Spot->SetActorLocationAndRotation(FVector(300, 40, 20), FRotator(0, 35, 0));
        Spot->DetectBox->SetRelativeLocation(FVector(5, -3, 10));
        Spot->DetectBox->SetBoxExtent(FVector(20, 15, 20));
        Spot->bOneShot = true;
        Spot->bLockCupAfterServing = true;
        Spot->SetActiveRecipe(Recipe);
        return Spot;
    }

    UMotionControllerComponent* Controller()
    {
        AActor* Owner = World->SpawnActor<AActor>();
        UMotionControllerComponent* Controller = NewObject<UMotionControllerComponent>(Owner);
        Owner->SetRootComponent(Controller);
        Controller->RegisterComponent();
        return Controller;
    }

    ULiquidTransferSubsystem* Transfers()
    {
        return World->GetSubsystem<ULiquidTransferSubsystem>();
    }
};

void CheckComposition(FAutomationTestBase& Test, const FString& Label,
    const ALiquidContainerActor* Container, double CoffeeML, double MilkML,
    double UnknownML = 0.0)
{
    const FLiquidComposition Composition = Container->GetComposition();
    const double ExpectedTotal = CoffeeML + MilkML + UnknownML;
    Test.TestTrue(Label + TEXT("：权威 mL"),
        FMath::Abs(Container->GetPreciseVolumeML() - ExpectedTotal) < VolumeTolerance);
    Test.TestTrue(Label + TEXT("：成分合计等于权威 mL"),
        FMath::Abs(Composition.Total() - Container->GetPreciseVolumeML()) < VolumeTolerance);
    Test.TestTrue(Label + TEXT("：Coffee 快照"),
        FMath::Abs(Composition.Get(CoffeeId) - CoffeeML) < VolumeTolerance);
    Test.TestTrue(Label + TEXT("：Milk 快照"),
        FMath::Abs(Composition.Get(MilkId) - MilkML) < VolumeTolerance);
    Test.TestTrue(Label + TEXT("：Unknown 快照"),
        FMath::Abs(Composition.Get(UnknownId) - UnknownML) < VolumeTolerance);
    Test.TestTrue(Label + TEXT("：Coffee 查询"),
        FMath::Abs(Container->GetIngredientVolumeML(CoffeeId) - CoffeeML) < VolumeTolerance);
    Test.TestTrue(Label + TEXT("：Milk 查询"),
        FMath::Abs(Container->GetIngredientVolumeML(MilkId) - MilkML) < VolumeTolerance);
    Test.TestTrue(Label + TEXT("：Unknown 查询"),
        FMath::Abs(Container->GetIngredientVolumeML(UnknownId) - UnknownML) < VolumeTolerance);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeCompositionRevisionTest,
    "CyberCafe.Recipe.CompositionVolumeAndRevision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRecipeCompositionRevisionTest::RunTest(const FString&)
{
    FLiquidComposition Mix = FLiquidComposition::Pure(CoffeeId, 60.0);
    Mix.Add(FLiquidComposition::Pure(MilkId, 40.0));
    TestEqual(TEXT("纯液合并为 100 mL"), Mix.Total(), 100.0);
    TestEqual(TEXT("不存在的成分返回零"), Mix.Get(UnknownId), 0.0);
    const FLiquidComposition Portion = Mix.Portion(25.0);
    TestTrue(TEXT("25 mL 部分保留 60/40 配比"),
        FMath::Abs(Portion.Total() - 25.0) < VolumeTolerance &&
        FMath::Abs(Portion.Get(CoffeeId) - 15.0) < VolumeTolerance &&
        FMath::Abs(Portion.Get(MilkId) - 10.0) < VolumeTolerance);
    TestEqual(TEXT("取部分不修改原混合液"), Mix.Total(), 100.0);
    Mix.Remove(Portion);
    TestTrue(TEXT("移除部分同比扣除成分"),
        FMath::Abs(Mix.Total() - 75.0) < VolumeTolerance &&
        FMath::Abs(Mix.Get(CoffeeId) - 45.0) < VolumeTolerance &&
        FMath::Abs(Mix.Get(MilkId) - 30.0) < VolumeTolerance);
    Mix.Add(Portion);
    TestEqual(TEXT("加回部分恢复总量"), Mix.Total(), 100.0);

    FRecipeTestWorld T;
    ACupActor* Cup = T.World->SpawnActor<ACupActor>();
    T.Configure(Cup, 200.f, .5f);
    Cup->InitialIngredientId = CoffeeId;
    Cup->InitializeLiquidState();
    CheckComposition(*this, TEXT("初始填充使用配置的 Coffee ID"), Cup, 100.0, 0.0);
    const int32 InitialRevision = Cup->GetLiquidRevision();
    Cup->InitializeLiquidState();
    TestEqual(TEXT("重复初始化不推进 revision"), Cup->GetLiquidRevision(), InitialRevision);
    CheckComposition(*this, TEXT("重复初始化不重复注入"), Cup, 100.0, 0.0);

    TestEqual(TEXT("实际加入 Milk 50 mL"),
        Cup->AddIngredientLiquid(50.f, MilkId, FLinearColor::White), 50.f);
    TestTrue(TEXT("加入成分推进 revision"), Cup->GetLiquidRevision() > InitialRevision);
    CheckComposition(*this, TEXT("加入后"), Cup, 100.0, 50.0);
    const int32 MixedRevision = Cup->GetLiquidRevision();
    FLiquidComposition Snapshot = Cup->GetComposition();
    Snapshot.Add(FLiquidComposition::Pure(UnknownId, 10.0));
    CheckComposition(*this, TEXT("修改值快照不反写 Actor"), Cup, 100.0, 50.0);
    TestEqual(TEXT("修改外部快照不推进 Actor revision"), Cup->GetLiquidRevision(), MixedRevision);

    TestEqual(TEXT("消耗 30 mL"), Cup->ConsumeLiquid(30.f), 30.f);
    CheckComposition(*this, TEXT("消耗同比保留 2:1"), Cup, 80.0, 40.0);
    TestTrue(TEXT("消耗推进 revision"), Cup->GetLiquidRevision() > MixedRevision);
    const int32 ConsumedRevision = Cup->GetLiquidRevision();
    Cup->SetCapacityML(60.f);
    CheckComposition(*this, TEXT("容量缩减溢出也同比"), Cup, 40.0, 20.0);
    TestTrue(TEXT("溢出推进 revision"), Cup->GetLiquidRevision() > ConsumedRevision);
    const int32 FullRevision = Cup->GetLiquidRevision();
    TestEqual(TEXT("满杯拒收"), Cup->AddIngredientLiquid(10.f, MilkId, FLinearColor::White), 0.f);
    TestEqual(TEXT("拒收不推进 revision"), Cup->GetLiquidRevision(), FullRevision);

    Cup->SetCapacityML(80.f);
    const int32 BeforePartialAdd = Cup->GetLiquidRevision();
    TestEqual(TEXT("只接受剩余 20 mL"),
        Cup->AddIngredientLiquid(30.f, MilkId, FLinearColor::White), 20.f);
    CheckComposition(*this, TEXT("部分注入仅记实际接受量"), Cup, 40.0, 40.0);
    TestTrue(TEXT("部分注入推进 revision"), Cup->GetLiquidRevision() > BeforePartialAdd);
    const int32 BeforeReplacement = Cup->GetLiquidRevision();
    Cup->ConsumeLiquid(20.f);
    Cup->AddIngredientLiquid(20.f, CoffeeId, FLinearColor::Red);
    CheckComposition(*this, TEXT("总量仍为 80 但成分已改变"), Cup, 50.0, 30.0);
    TestTrue(TEXT("同总量的成分替换仍推进 revision"), Cup->GetLiquidRevision() > BeforeReplacement);
    const int32 BeforeRefresh = Cup->GetLiquidRevision();
    Cup->RefreshLiquidFX();
    TestEqual(TEXT("表现刷新不推进 revision"), Cup->GetLiquidRevision(), BeforeRefresh);
    TestEqual(TEXT("消耗被尾量封顶"), Cup->ConsumeLiquid(100.f), 80.f);
    CheckComposition(*this, TEXT("空杯成分归零"), Cup, 0.0, 0.0);
    const int32 EmptyRevision = Cup->GetLiquidRevision();
    TestEqual(TEXT("空杯再消耗为零"), Cup->ConsumeLiquid(1.f), 0.f);
    TestEqual(TEXT("零消耗不推进 revision"), Cup->GetLiquidRevision(), EmptyRevision);
    TestTrue(TEXT("成分操作体积账本闭合"), FMath::Abs(T.Transfers()->GetBalance().ErrorML) < VolumeTolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeMixedPacketSnapshotTest,
    "CyberCafe.Recipe.MixedPacketPartialOverflowSnapshotAndSourceDestruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRecipeMixedPacketSnapshotTest::RunTest(const FString&)
{
    // 目标已有 94 mL；容量 100 验证部分接收，容量 110 验证完整接收。
    for (float CapacityML : {100.f, 110.f})
    {
        FRecipeTestWorld T;
        const FString Label = FString::Printf(TEXT("容量 %.0f"), CapacityML);
        ACupActor* Source = T.Cup(60.f, 40.f, 200.f, FVector(100, 0, 0));
        ACupActor* Target = T.Cup(0.f, 94.f, CapacityML);
        Source->LiquidColor = FLinearColor::Red;
        Target->LiquidColor = FLinearColor::Blue;
        ULiquidTransferSubsystem* Transfers = T.Transfers();
        const int32 SourceRevision = Source->GetLiquidRevision();
        const int32 TargetRevision = Target->GetLiquidRevision();
        TestEqual(Label + TEXT("：发射 10 mL"),
            Transfers->EmitVolume(Source, 10.0, FVector(0, 0, 10),
                FVector(0, 0, -50), FVector::ZeroVector), 10.0);
        CheckComposition(*this, Label + TEXT("：源同比扣除"), Source, 54.0, 36.0);
        TestTrue(Label + TEXT("：发射推进源 revision"), Source->GetLiquidRevision() > SourceRevision);
        TestEqual(Label + TEXT("：到达前目标 revision 不变"), Target->GetLiquidRevision(), TargetRevision);
        TestEqual(Label + TEXT("：在途总量"), Transfers->GetBalance().InFlightML, 10.0);

        // 发射后的源液从 60/40 变成 30/70，再销毁源；包必须持有发射时的值快照。
        TestEqual(Label + TEXT("：源追加 Milk"),
            Source->AddIngredientLiquid(90.f, MilkId, FLinearColor::Green), 90.f);
        CheckComposition(*this, Label + TEXT("：源后续成分不同"), Source, 54.0, 126.0);
        TestTrue(Label + TEXT("：源销毁成功"), Source->Destroy());
        Source = nullptr;
        TestTrue(Label + TEXT("：销毁时账本闭合"),
            FMath::Abs(Transfers->GetBalance().ErrorML) < VolumeTolerance);
        TestEqual(Label + TEXT("：销毁不丢在途包"), Transfers->GetBalance().InFlightML, 10.0);
        Transfers->Simulate(.1);
        CheckComposition(*this, Label + TEXT("：尚未过杯口"), Target, 0.0, 94.0);
        Transfers->Simulate(.2);

        const double AcceptedML = FMath::Min(10.0, static_cast<double>(CapacityML) - 94.0);
        CheckComposition(*this, Label + TEXT("：接收发射时的 60/40 快照"),
            Target, AcceptedML * .6, 94.0 + AcceptedML * .4);
        TestTrue(Label + TEXT("：接收推进目标 revision"), Target->GetLiquidRevision() > TargetRevision);
        TestTrue(Label + TEXT("：溢出为包量减接受量"),
            FMath::Abs(Transfers->GetBalance().SpilledML - (10.0 - AcceptedML)) < VolumeTolerance);
        TestEqual(Label + TEXT("：包已结算"), Transfers->GetBalance().InFlightML, 0.0);
        const double TotalML = 94.0 + AcceptedML;
        const FLinearColor ExpectedColor(static_cast<float>(AcceptedML / TotalML),
            0.f, static_cast<float>(94.0 / TotalML), 1.f);
        TestTrue(Label + TEXT("：颜色也采用发射快照和实际接收量"),
            Target->LiquidColor.Equals(ExpectedColor, .00001f));
        const int32 SettledRevision = Target->GetLiquidRevision();
        Transfers->Simulate(3.5);
        CheckComposition(*this, Label + TEXT("：不重复记入成分"),
            Target, AcceptedML * .6, 94.0 + AcceptedML * .4);
        TestEqual(Label + TEXT("：结算一次后 revision 不变"), Target->GetLiquidRevision(), SettledRevision);
        TestTrue(Label + TEXT("：最终账本闭合"),
            FMath::Abs(Transfers->GetBalance().ErrorML) < VolumeTolerance);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeInclusiveBoundaryTest,
    "CyberCafe.Recipe.InclusiveVolumeAndRatioBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRecipeInclusiveBoundaryTest::RunTest(const FString&)
{
    FRecipeTestWorld T;
    UDrinkRecipe* Recipe = T.Recipe();
    TestEqual(TEXT("默认配方 ID"), Recipe->RecipeId, FName(TEXT("Latte")));
    TestTrue(TEXT("默认目标 200 mL"), FMath::Abs(Recipe->TargetVolumeML - 200.0) < VolumeTolerance);
    TestTrue(TEXT("默认下限 180 mL"), FMath::Abs(Recipe->MinVolumeML - 180.0) < VolumeTolerance);
    TestTrue(TEXT("默认上限 220 mL"), FMath::Abs(Recipe->MaxVolumeML - 220.0) < VolumeTolerance);
    TestEqual(TEXT("默认规则恰好两条"), Recipe->Rules.Num(), 2);
    const FDrinkIngredientRule* CoffeeRule = Recipe->Rules.FindByPredicate(
        [](const FDrinkIngredientRule& Rule) { return Rule.IngredientId == CoffeeId; });
    const FDrinkIngredientRule* MilkRule = Recipe->Rules.FindByPredicate(
        [](const FDrinkIngredientRule& Rule) { return Rule.IngredientId == MilkId; });
    if (!TestNotNull(TEXT("默认 Coffee 规则"), CoffeeRule) ||
        !TestNotNull(TEXT("默认 Milk 规则"), MilkRule))
    {
        return false;
    }
    TestTrue(TEXT("两种成分默认范围均为 45% 至 55%"),
        FMath::Abs(CoffeeRule->MinFraction - .45) < VolumeTolerance &&
        FMath::Abs(CoffeeRule->MaxFraction - .55) < VolumeTolerance &&
        FMath::Abs(MilkRule->MinFraction - .45) < VolumeTolerance &&
        FMath::Abs(MilkRule->MaxFraction - .55) < VolumeTolerance);

    struct FBoundaryCase
    {
        const TCHAR* Label;
        float CoffeeML;
        float MilkML;
        EDrinkValidationReason Reason;
    };
    const FBoundaryCase Cases[] = {
        {TEXT("180 mL / Coffee 45%"), 81.f, 99.f, EDrinkValidationReason::Accepted},
        {TEXT("180 mL / Coffee 55%"), 99.f, 81.f, EDrinkValidationReason::Accepted},
        {TEXT("220 mL / Coffee 45%"), 99.f, 121.f, EDrinkValidationReason::Accepted},
        {TEXT("220 mL / Coffee 55%"), 121.f, 99.f, EDrinkValidationReason::Accepted},
        {TEXT("200 mL / 50:50"), 100.f, 100.f, EDrinkValidationReason::Accepted},
        {TEXT("179 mL / 50:50"), 89.5f, 89.5f, EDrinkValidationReason::TooLittle},
        {TEXT("221 mL / 50:50"), 110.5f, 110.5f, EDrinkValidationReason::TooMuch},
        {TEXT("200 mL / Coffee 44.75%"), 89.5f, 110.5f, EDrinkValidationReason::WrongRatio},
        {TEXT("200 mL / Coffee 55.25%"), 110.5f, 89.5f, EDrinkValidationReason::WrongRatio}
    };
    TArray<FString> BoundaryRows;
    for (const FBoundaryCase& Case : Cases)
    {
        ACupActor* Cup = T.Cup(Case.CoffeeML, Case.MilkML);
        const int32 Revision = Cup->GetLiquidRevision();
        const FDrinkValidationResult Result = Recipe->Evaluate(Cup);
        const FString Label(Case.Label);
        TestEqual(Label + TEXT("：原因"), Result.Reason, Case.Reason);
        TestEqual(Label + TEXT("：接受状态"), Result.bAccepted,
            Case.Reason == EDrinkValidationReason::Accepted);
        const double TotalML = static_cast<double>(Case.CoffeeML) + Case.MilkML;
        TestTrue(Label + TEXT("：结果包含真实总量"), FMath::Abs(Result.TotalML - TotalML) < VolumeTolerance);
        TestTrue(Label + TEXT("：结果包含真实 Coffee 比例"),
            FMath::Abs(Result.CoffeeFraction - static_cast<double>(Case.CoffeeML) / TotalML) < VolumeTolerance);
        TestEqual(Label + TEXT("：Evaluate 不改 revision"), Cup->GetLiquidRevision(), Revision);
        TestFalse(Label + TEXT("：Evaluate 不冻结杯子"), Cup->IsLiquidFrozen());
        CheckComposition(*this, Label + TEXT("：Evaluate 不改液体"), Cup, Case.CoffeeML, Case.MilkML);
        BoundaryRows.Add(FString::Printf(
            TEXT("{\"label\":\"%s\",\"coffeeML\":%.17g,\"milkML\":%.17g,\"totalML\":%.17g,\"coffeeFraction\":%.17g,\"accepted\":%s,\"reason\":%d,\"feedback\":\"%s\"}"),
            *EscapeRecipeMeasurementJson(Label), Cup->GetIngredientVolumeML(CoffeeId),
            Cup->GetIngredientVolumeML(MilkId), Result.TotalML, Result.CoffeeFraction,
            Result.bAccepted ? TEXT("true") : TEXT("false"), static_cast<int32>(Result.Reason),
            *EscapeRecipeMeasurementJson(Result.Feedback.ToString())));
    }
    const FString MeasurementDir = FPaths::ProjectSavedDir() / TEXT("RecipeAcceptance");
    if (TestTrue(TEXT("创建边界测量导出目录"), IFileManager::Get().MakeDirectory(*MeasurementDir, true)))
    {
        const FString Json = TEXT("[\n") + FString::Join(BoundaryRows, TEXT(",\n")) + TEXT("\n]\n");
        TestTrue(TEXT("保存 boundaries.json"), FFileHelper::SaveStringToFile(Json,
            *(MeasurementDir / TEXT("boundaries.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeWrongAndUnknownIngredientTest,
    "CyberCafe.Recipe.WrongIngredientUnknownAndMissing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRecipeWrongAndUnknownIngredientTest::RunTest(const FString&)
{
    FRecipeTestWorld T;
    UDrinkRecipe* Recipe = T.Recipe();
    for (FName ExtraId : {FName(TEXT("Water")), UnknownId})
    {
        ACupActor* Cup = T.Cup(90.f, 90.f);
        Cup->AddIngredientLiquid(20.f, ExtraId, FLinearColor::White);
        const int32 Revision = Cup->GetLiquidRevision();
        const FDrinkValidationResult Result = Recipe->Evaluate(Cup);
        const FString Label = ExtraId.ToString();
        TestFalse(Label + TEXT("：即使 Coffee/Milk 均占 45% 也拒绝"), Result.bAccepted);
        TestEqual(Label + TEXT("：拒绝原因"), Result.Reason, EDrinkValidationReason::WrongIngredient);
        TestFalse(Label + TEXT("：提供拒绝反馈"), Result.Feedback.IsEmpty());
        TestTrue(Label + TEXT("：额外成分没有被忽略"),
            FMath::Abs(Cup->GetComposition().Get(ExtraId) - 20.0) < VolumeTolerance);
        TestTrue(Label + TEXT("：额外成分计入总量"), FMath::Abs(Result.TotalML - 200.0) < VolumeTolerance);
        TestEqual(Label + TEXT("：拒绝不改 revision"), Cup->GetLiquidRevision(), Revision);
    }

    ACupActor* LegacyCup = T.Cup(90.f, 90.f);
    TestEqual(TEXT("默认初始成分 ID 为 Unknown"), LegacyCup->InitialIngredientId, UnknownId);
    TestEqual(TEXT("旧 AddLiquid API 接收 20 mL"), LegacyCup->AddLiquid(20.f, FLinearColor::White), 20.f);
    CheckComposition(*this, TEXT("无成分 ID 的旧 API 记为 Unknown"), LegacyCup, 90.0, 90.0, 20.0);
    const FDrinkValidationResult LegacyResult = Recipe->Evaluate(LegacyCup);
    TestFalse(TEXT("旧 API 不能绕过配方检查"), LegacyResult.bAccepted);
    TestEqual(TEXT("旧 API 的 Unknown 被拒绝"), LegacyResult.Reason, EDrinkValidationReason::WrongIngredient);

    ACupActor* InitialUnknown = T.World->SpawnActor<ACupActor>();
    T.Configure(InitialUnknown, 400.f, .5f);
    InitialUnknown->InitializeLiquidState();
    CheckComposition(*this, TEXT("默认初始填充完整记为 Unknown"), InitialUnknown, 0.0, 0.0, 200.0);
    TestEqual(TEXT("初始 Unknown 不能被默认当作 Coffee"),
        Recipe->Evaluate(InitialUnknown).Reason, EDrinkValidationReason::WrongIngredient);

    ACupActor* MissingMilk = T.Cup(200.f, 0.f);
    const FDrinkValidationResult MissingResult = Recipe->Evaluate(MissingMilk);
    TestFalse(TEXT("缺少 Milk 拒绝"), MissingResult.bAccepted);
    TestEqual(TEXT("缺少必需成分的原因"), MissingResult.Reason, EDrinkValidationReason::MissingIngredient);
    ACupActor* Empty = T.Cup(0.f, 0.f);
    TestEqual(TEXT("空杯原因"), Recipe->Evaluate(Empty).Reason, EDrinkValidationReason::Empty);

    ABottleActor* Bottle = T.World->SpawnActor<ABottleActor>();
    T.Configure(Bottle, 300.f);
    Bottle->InitializeLiquidState();
    Bottle->AddIngredientLiquid(100.f, CoffeeId, FLinearColor::Red);
    Bottle->AddIngredientLiquid(100.f, MilkId, FLinearColor::White);
    const FDrinkValidationResult BottleResult = Recipe->Evaluate(Bottle);
    TestFalse(TEXT("有效配比的瓶子仍不能作为杯子"), BottleResult.bAccepted);
    TestEqual(TEXT("默认杯类检查"), BottleResult.Reason, EDrinkValidationReason::WrongCup);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeRejectRepairServeFreezeTest,
    "CyberCafe.Recipe.RejectRepairImmediateServeGrabLockAndFreeze",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRecipeRejectRepairServeFreezeTest::RunTest(const FString&)
{
    FRecipeTestWorld T;
    UDrinkRecipe* Recipe = T.Recipe();
    AServingSpotActor* Spot = T.Spot(Recipe);
    ACupActor* Cup = T.Cup(100.f, 0.f);
    const FTransform DetectionTransform = Spot->DetectBox->GetComponentTransform();
    const FVector Inside = DetectionTransform.TransformPosition(FVector(1, 1, 1));
    Cup->SetActorLocation(Inside);
    Cup->GrabComp->GrabType = EGrabType::Free;
    Cup->GrabComp->SetActive(true);
    Cup->GrabComp->bAllowRemoteGrab = true;
    Cup->GrabComp->bSimulateOnDrop = false;
    TestTrue(TEXT("直接提交杯确实位于 DetectBox 局部范围内"),
        FBox(-Spot->DetectBox->GetUnscaledBoxExtent(), Spot->DetectBox->GetUnscaledBoxExtent())
            .IsInside(DetectionTransform.InverseTransformPosition(Cup->GetActorLocation())));
    TestFalse(TEXT("提交前不持有"), Cup->IsHeld());
    const int32 BeforeReject = Cup->GetLiquidRevision();
    const FTransform BeforeRejectTransform = Cup->GetActorTransform();
    const bool BeforePhysics = Cup->ContainerMesh->IsSimulatingPhysics();
    const ECollisionEnabled::Type BeforeCollision = Cup->ContainerMesh->GetCollisionEnabled();
    const EGrabType BeforeGrabType = Cup->GrabComp->GrabType;
    const bool BeforeActive = Cup->GrabComp->IsActive();
    const bool BeforeRemote = Cup->GrabComp->bAllowRemoteGrab;
    const double BeforeCoffeeML = Cup->GetIngredientVolumeML(CoffeeId);
    const double BeforeMilkML = Cup->GetIngredientVolumeML(MilkId);
    const bool bBeforeCupLocked = !BeforeActive && !BeforeRemote;

    TestFalse(TEXT("只有 Coffee 的杯子提交被拒绝"), Spot->TrySubmitCup(Cup));
    const FDrinkValidationResult Rejected = Spot->GetLastValidation();
    TestFalse(TEXT("立即保存拒绝结果"), Rejected.bAccepted);
    TestFalse(TEXT("拒绝有反馈"), Rejected.Feedback.IsEmpty());
    TestFalse(TEXT("拒绝不占用 one-shot 完成状态"), Spot->HasBeenServed());
    TestNull(TEXT("拒绝不记录已上菜杯"), Spot->GetServedCup());
    TestTrue(TEXT("拒绝保留 one-shot 配置"), Spot->bOneShot);
    TestEqual(TEXT("拒绝保留物理状态"), Cup->ContainerMesh->IsSimulatingPhysics(), BeforePhysics);
    TestEqual(TEXT("拒绝保留碰撞状态"), Cup->ContainerMesh->GetCollisionEnabled(), BeforeCollision);
    TestEqual(TEXT("拒绝保留抓取类型"), Cup->GrabComp->GrabType, BeforeGrabType);
    TestEqual(TEXT("拒绝保留抓取组件激活状态"), Cup->GrabComp->IsActive(), BeforeActive);
    TestEqual(TEXT("拒绝保留远程抓取权限"), Cup->GrabComp->bAllowRemoteGrab, BeforeRemote);
    TestTrue(TEXT("拒绝不吸附或移动杯子"), Cup->GetActorTransform().Equals(BeforeRejectTransform));
    TestNull(TEXT("拒绝不新增附着"), Cup->GetRootComponent()->GetAttachParent());
    TestFalse(TEXT("拒绝不冻结液体"), Cup->IsLiquidFrozen());
    TestEqual(TEXT("拒绝不推进液体 revision"), Cup->GetLiquidRevision(), BeforeReject);
    CheckComposition(*this, TEXT("拒绝保留原液"), Cup, 100.0, 0.0);

    UMotionControllerComponent* Controller = T.Controller();
    Controller->SetWorldLocation(Inside);
    if (!TestTrue(TEXT("拒绝后仍能实际抓取"), Cup->GrabComp->TryGrab(Controller)))
    {
        return false;
    }
    TestTrue(TEXT("真实抓取设置 held 状态"), Cup->IsHeld());
    TestEqual(TEXT("补足 Milk 100 mL"), Cup->AddIngredientLiquid(100.f, MilkId, FLinearColor::White), 100.f);
    CheckComposition(*this, TEXT("补奶修正为 200 mL / 50:50"), Cup, 100.0, 100.0);
    TestTrue(TEXT("补奶推进 revision"), Cup->GetLiquidRevision() > BeforeReject);
    TestTrue(TEXT("补奶后配方本身立即合格"), Recipe->Evaluate(Cup).bAccepted);
    const int32 RepairedRevision = Cup->GetLiquidRevision();
    const FTransform HeldTransform = Cup->GetActorTransform();
    USceneComponent* HeldParent = Cup->GetRootComponent()->GetAttachParent();
    TestFalse(TEXT("合格但仍持有的杯子不得提交"), Spot->TrySubmitCup(Cup));
    TestTrue(TEXT("held 拒绝不强制释放"), Cup->IsHeld());
    TestTrue(TEXT("held 拒绝不改控制器"), Cup->GrabComp->GetHoldingController() == Controller);
    TestTrue(TEXT("held 拒绝不改附着"), Cup->GetRootComponent()->GetAttachParent() == HeldParent);
    TestTrue(TEXT("held 拒绝不移动杯子"), Cup->GetActorTransform().Equals(HeldTransform));
    TestFalse(TEXT("held 拒绝不完成上菜"), Spot->HasBeenServed());
    TestFalse(TEXT("held 拒绝不冻结"), Cup->IsLiquidFrozen());
    TestEqual(TEXT("held 拒绝不改 revision"), Cup->GetLiquidRevision(), RepairedRevision);
    TestTrue(TEXT("显式松手成功"), Cup->GrabComp->TryRelease());
    TestFalse(TEXT("松手后 held 清除"), Cup->IsHeld());

    Cup->SetActorLocation(DetectionTransform.TransformPosition(FVector(100, 0, 0)));
    const FTransform OutsideTransform = Cup->GetActorTransform();
    TestFalse(TEXT("合格但在 DetectBox 外不得提交"), Spot->TrySubmitCup(Cup));
    TestFalse(TEXT("范围外拒绝不完成上菜"), Spot->HasBeenServed());
    TestTrue(TEXT("范围外拒绝不吸附"), Cup->GetActorTransform().Equals(OutsideTransform));
    TestFalse(TEXT("范围外拒绝不冻结"), Cup->IsLiquidFrozen());
    TestEqual(TEXT("范围外拒绝不改 revision"), Cup->GetLiquidRevision(), RepairedRevision);
    Cup->SetActorLocation(Inside);

    // 不 Tick、不伪造 Candidate，也不等待 StabilitySeconds；此 API 是显式即时提交。
    if (!TestTrue(TEXT("回到范围内后直接提交立即成功"), Spot->TrySubmitCup(Cup)))
    {
        return false;
    }
    const FDrinkValidationResult Accepted = Spot->GetLastValidation();
    TestTrue(TEXT("完成状态已设置"), Spot->HasBeenServed());
    TestTrue(TEXT("记录的已上菜杯正确"), Spot->GetServedCup() == Cup);
    TestTrue(TEXT("立即保存 Accepted 结果"), Spot->GetLastValidation().bAccepted);
    TestEqual(TEXT("上菜结果原因"), Spot->GetLastValidation().Reason, EDrinkValidationReason::Accepted);
    TestTrue(TEXT("上菜结果为验收时的 200 mL"),
        FMath::Abs(Spot->GetLastValidation().TotalML - 200.0) < VolumeTolerance);
    TestTrue(TEXT("上菜结果为验收时的 50% Coffee"),
        FMath::Abs(Spot->GetLastValidation().CoffeeFraction - .5) < VolumeTolerance);
    TestTrue(TEXT("成功后附着到 Spot"), Cup->IsAttachedTo(Spot));
    TestTrue(TEXT("成功后位置吸附到 SceneRoot"),
        Cup->GetActorLocation().Equals(Spot->SceneRoot->GetComponentLocation(), .00001));
    TestFalse(TEXT("成功后关闭物理"), Cup->ContainerMesh->IsSimulatingPhysics());
    TestEqual(TEXT("成功后关闭碰撞"), Cup->ContainerMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("成功后抓取组件停用"), Cup->GrabComp->IsActive());
    TestFalse(TEXT("成功后远程抓取被禁止"), Cup->GrabComp->bAllowRemoteGrab);
    TestTrue(TEXT("成功后液体冻结"), Cup->IsLiquidFrozen());
    CheckComposition(*this, TEXT("冻结的是验收成分"), Cup, 100.0, 100.0);
    const int32 FrozenRevision = Cup->GetLiquidRevision();
    Cup->FreezeLiquidForServing();
    TestEqual(TEXT("重复冻结不推进 revision"), Cup->GetLiquidRevision(), FrozenRevision);
    TestFalse(TEXT("锁定后远程拉取 API 拒绝"), Cup->GrabComp->TryPull());

    // 不仅检查组件停用，还检查直接调用近距抓取 API 也无法绕过锁。
    if (!TestFalse(TEXT("锁定后直接近距抓取 API 拒绝"), Cup->GrabComp->TryGrab(Controller)))
    {
        return false;
    }
    TestFalse(TEXT("锁定后保持未持有"), Cup->IsHeld());

    TestEqual(TEXT("冻结杯拒收成分 API"), Cup->AddIngredientLiquid(10.f, MilkId, FLinearColor::Green), 0.f);
    TestEqual(TEXT("冻结杯拒收旧 AddLiquid API"), Cup->AddLiquid(10.f, FLinearColor::Green), 0.f);
    CheckComposition(*this, TEXT("外部加液不能修改冻结快照"), Cup, 100.0, 100.0);
    TestEqual(TEXT("冻结拒收不推进 revision"), Cup->GetLiquidRevision(), FrozenRevision);
    const double SpilledBefore = T.Transfers()->GetBalance().SpilledML;
    const FVector Mouth = Cup->PourEntryPoint->GetComponentLocation();
    ACupActor* Source = T.Cup(10.f, 0.f, 100.f, Mouth + FVector(100, 0, 0));
    TestEqual(TEXT("向冻结杯发射真实 10 mL 包"),
        T.Transfers()->EmitVolume(Source, 10.0, Mouth + FVector(0, 0, 10),
            FVector(0, 0, -50), FVector::ZeroVector), 10.0);
    T.Transfers()->Simulate(3.5);
    CheckComposition(*this, TEXT("真实在途包也不能修改冻结快照"), Cup, 100.0, 100.0);
    TestEqual(TEXT("冻结目标拒包不推进 revision"), Cup->GetLiquidRevision(), FrozenRevision);
    TestTrue(TEXT("拒收的整个包结算为溢出"),
        FMath::Abs(T.Transfers()->GetBalance().SpilledML - SpilledBefore - 10.0) < VolumeTolerance);
    TestEqual(TEXT("冻结拒包无在途残留"), T.Transfers()->GetBalance().InFlightML, 0.0);
    TestTrue(TEXT("冻结拒收账本闭合"), FMath::Abs(T.Transfers()->GetBalance().ErrorML) < VolumeTolerance);

    const FTransform ServedTransform = Cup->GetActorTransform();
    for (int32 Attempt = 0; Attempt < 3; ++Attempt)
    {
        TestFalse(TEXT("同杯重复提交不能再次成功"), Spot->TrySubmitCup(Cup));
    }
    ACupActor* SecondCup = T.Cup(100.f, 100.f);
    SecondCup->SetActorLocation(Inside);
    TestFalse(TEXT("one-shot 不接受第二只合格杯"), Spot->TrySubmitCup(SecondCup));
    TestTrue(TEXT("重复提交仍保持已完成状态"), Spot->HasBeenServed());
    TestTrue(TEXT("重复提交不替换已上菜杯"), Spot->GetServedCup() == Cup);
    TestTrue(TEXT("重复提交不移动已上菜杯"), Cup->GetActorTransform().Equals(ServedTransform));
    TestEqual(TEXT("重复提交不改冻结 revision"), Cup->GetLiquidRevision(), FrozenRevision);
    TestFalse(TEXT("第二杯被拒不冻结"), SecondCup->IsLiquidFrozen());
    TestNull(TEXT("第二杯被拒不吸附"), SecondCup->GetRootComponent()->GetAttachParent());

    const FString MeasurementDir = FPaths::ProjectSavedDir() / TEXT("RecipeAcceptance");
    if (TestTrue(TEXT("创建重试测量导出目录"), IFileManager::Get().MakeDirectory(*MeasurementDir, true)))
    {
        const FString Json = FString::Printf(
            TEXT("{\"beforeCoffee\":%.17g,\"beforeMilk\":%.17g,\"rejectedReason\":%d,\"rejectedFeedback\":\"%s\",\"beforeCupLocked\":%s,\"afterCoffee\":%.17g,\"afterMilk\":%.17g,\"acceptedResult\":%s,\"freeze\":%s}\n"),
            BeforeCoffeeML, BeforeMilkML, static_cast<int32>(Rejected.Reason),
            *EscapeRecipeMeasurementJson(Rejected.Feedback.ToString()),
            bBeforeCupLocked ? TEXT("true") : TEXT("false"),
            Cup->GetIngredientVolumeML(CoffeeId), Cup->GetIngredientVolumeML(MilkId),
            Accepted.bAccepted ? TEXT("true") : TEXT("false"),
            Cup->IsLiquidFrozen() ? TEXT("true") : TEXT("false"));
        TestTrue(TEXT("保存 retry_result.json"), FFileHelper::SaveStringToFile(Json,
            *(MeasurementDir / TEXT("retry_result.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
