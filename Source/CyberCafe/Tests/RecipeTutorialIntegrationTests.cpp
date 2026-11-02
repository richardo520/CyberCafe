// 仅使用真实 World、GI 总线和公开交互 API；不改写候选杯、验收或步骤完成状态。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Liquid/CupActor.h"
#include "Liquid/LiquidContainerActor.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "UI/Narrative/NarrativeHUDActor.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "GrabComponent.h"
#include "MotionControllerComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"

namespace RecipeTutorialIntegrationTests
{
const FName CoffeeId(TEXT("Coffee"));
const FName MilkId(TEXT("Milk"));
constexpr double VolumeTolerance = 1.e-6;

struct FRecipeTutorialWorld
{
    UWorld* World;
    UGameInstance* GI;
    UCyberCafeEventSubsystem* Events;
    UNarrativeSubsystem* Narrative;
    ANarrativeHUDActor* HUD;
    UNarrativeWidget* Widget;
    ATutorialDirector* Director;
    ATutorialNPC* NPC;
    TArray<ACupActor*> Cups;

    FRecipeTutorialWorld()
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
        Narrative = World->GetSubsystem<UNarrativeSubsystem>();

        HUD = World->SpawnActor<ANarrativeHUDActor>();
        Widget = NewObject<UNarrativeWidget>(World);
        Widget->bEnableVoice = false;
        HUD->FindComponentByClass<UWidgetComponent>()->SetWidget(Widget);
        NPC = World->SpawnActor<ATutorialNPC>(FVector(-300, 0, 100), FRotator::ZeroRotator);
        Director = World->SpawnActor<ATutorialDirector>();
        Director->bAutoStart = false;
        Director->NPCRef = NPC;
        Director->bDialogueVoice = false;
        Director->CheerDuration = 0.f;
        Director->FinishedTextAutoHideSeconds = 0.f;
    }

    ~FRecipeTutorialWorld()
    {
        // 先让 Director 在 GI 总线仍有效时执行 EndPlay，清理活动 Step 和定时器。
        if (IsValid(Director)) Director->Destroy();
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        // GrabComponent::BeginPlay 会恢复根组件的 PhysicsActor profile；检测只用测试 Box。
        for (ACupActor* Cup : Cups)
        {
            Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }

    UDrinkRecipe* Recipe()
    {
        return NewObject<UDrinkRecipe>(World);
    }

    AServingSpotActor* Spot(const FVector& Position, UDrinkRecipe* Recipe = nullptr)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AServingSpotActor* Result = World->SpawnActor<AServingSpotActor>(
            Position, FRotator(0, 35, 0), Params);
        Result->DetectBox->SetRelativeLocation(FVector(5, -3, 10));
        Result->DetectBox->SetBoxExtent(FVector(20, 15, 20));
        Result->StabilitySeconds = .4f;
        Result->bOneShot = true;
        Result->bLockCupAfterServing = true;
        Result->SetActiveRecipe(Recipe);
        return Result;
    }

    ACupActor* Cup(float CoffeeML, float MilkML, const FVector& Position)
    {
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
        if (CoffeeML > 0.f)
        {
            Result->AddIngredientLiquid(CoffeeML, CoffeeId, FLinearColor::Red);
        }
        if (MilkML > 0.f)
        {
            Result->AddIngredientLiquid(MilkML, MilkId, FLinearColor::White);
        }
        Cups.Add(Result);
        return Result;
    }

    UTutorialStep_WaitForServe* WaitStep(AServingSpotActor* RequiredSpot, UDrinkRecipe* RequiredRecipe)
    {
        UTutorialStep_WaitForServe* Step = NewObject<UTutorialStep_WaitForServe>(Director);
        Step->RequiredSpot = RequiredSpot;
        Step->RequiredRecipe = RequiredRecipe;
        Step->HintText = FText::FromString(TEXT("请做好拿铁，放下杯子后提交。"));
        Step->CompletedText = FText::GetEmpty();
        Director->Steps = {Step};
        return Step;
    }

    UMotionControllerComponent* Controller(const FVector& Position)
    {
        AActor* Owner = World->SpawnActor<AActor>();
        UMotionControllerComponent* Result = NewObject<UMotionControllerComponent>(Owner);
        Owner->SetRootComponent(Result);
        Owner->AddInstanceComponent(Result);
        Result->RegisterComponent();
        Result->SetWorldLocation(Position);
        return Result;
    }

    void TickWorld(float DeltaSeconds)
    {
        ++GFrameCounter;
        World->Tick(LEVELTICK_All, DeltaSeconds);
    }
};

FVector Inside(const AServingSpotActor* Spot)
{
    return Spot->DetectBox->GetComponentTransform().TransformPosition(FVector(1, 1, 1));
}

FVector Outside(const AServingSpotActor* Spot)
{
    return Spot->DetectBox->GetComponentTransform().TransformPosition(FVector(100, 0, 0));
}

UBoxComponent* RegisterCupOverlapBox(ACupActor* Cup)
{
    UBoxComponent* Box = NewObject<UBoxComponent>(Cup);
    Cup->AddInstanceComponent(Box);
    Box->SetupAttachment(Cup->GetRootComponent());
    Box->SetBoxExtent(FVector(3.f));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_PhysicsBody);
    Box->SetCollisionResponseToAllChannels(ECR_Overlap);
    Box->SetGenerateOverlapEvents(true);
    Box->RegisterComponent();
    return Box;
}

bool CheckRealOverlap(FAutomationTestBase& Test, const FString& Label,
    AServingSpotActor* Spot, ACupActor* Cup, UBoxComponent* Box)
{
    // 只更新真实场景查询；不手动广播 overlap，也不调用受保护的 overlap handler。
    Box->UpdateOverlaps();
    Spot->DetectBox->UpdateOverlaps();
    TArray<AActor*> Overlapping;
    Spot->DetectBox->GetOverlappingActors(Overlapping, ACupActor::StaticClass());
    const bool bComponent = Test.TestTrue(Label + TEXT("：DetectBox 与注册 Box 实际重叠"),
        Spot->DetectBox->IsOverlappingComponent(Box));
    const bool bActor = Test.TestTrue(Label + TEXT("：真实 overlap 查询包含杯子"), Overlapping.Contains(Cup));
    const bool bArea = Test.TestTrue(Label + TEXT("：杯子原点也在检测范围内"), Spot->IsInServingArea(Cup));
    const bool bCandidate = Test.TestTrue(Label + TEXT("：真实 overlap 已记录 Candidate"),
        Spot->GetCandidateCup() == Cup);
    return bComponent && bActor && bArea && bCandidate;
}

void TickSpot(AServingSpotActor* Spot, int32 Count)
{
    for (int32 Index = 0; Index < Count; ++Index)
    {
        Spot->Tick(.05f);
    }
}

void CheckPending(FAutomationTestBase& Test, const FString& Label,
    const FRecipeTutorialWorld& T, AServingSpotActor* Spot, UTutorialStep_WaitForServe* Step)
{
    Test.TestFalse(Label + TEXT("：Spot 未上菜"), Spot->HasBeenServed());
    Test.TestNull(Label + TEXT("：Spot 未保存上菜杯"), Spot->GetServedCup());
    Test.TestFalse(Label + TEXT("：教学步骤未完成"), Step->IsCompleted());
    Test.TestNull(Label + TEXT("：Director.LastServedCup 为空"), T.Director->LastServedCup.Get());
}

void CheckAccepted(FAutomationTestBase& Test, const FString& Label,
    const FRecipeTutorialWorld& T, AServingSpotActor* Spot, ACupActor* Cup,
    UTutorialStep_WaitForServe* Step, UDrinkRecipe* Recipe)
{
    Test.TestTrue(Label + TEXT("：已提交 Accepted 记录"), Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
    Test.TestTrue(Label + TEXT("：LastValidation accepted"), Spot->GetLastValidation().bAccepted);
    Test.TestEqual(Label + TEXT("：LastValidation 原因"), Spot->GetLastValidation().Reason,
        EDrinkValidationReason::Accepted);
    Test.TestEqual(Label + TEXT("：验收 revision 为当前杯的 revision"),
        Spot->GetLastValidation().LiquidRevision, Cup->GetLiquidRevision());
    Test.TestTrue(Label + TEXT("：步骤已完成"), Step->IsCompleted());
    Test.TestTrue(Label + TEXT("：Director 保存同一只验收杯"), T.Director->LastServedCup.Get() == Cup);
    Test.TestTrue(Label + TEXT("：液体已冻结"), Cup->IsLiquidFrozen());
    Test.TestTrue(Label + TEXT("：杯子已吸附"), Cup->IsAttachedTo(Spot));
    Test.TestFalse(Label + TEXT("：关闭根组件物理"), Cup->ContainerMesh->IsSimulatingPhysics());
    Test.TestEqual(Label + TEXT("：关闭根组件碰撞"), Cup->ContainerMesh->GetCollisionEnabled(),
        ECollisionEnabled::NoCollision);
    Test.TestFalse(Label + TEXT("：停用抓取组件"), Cup->GrabComp->IsActive());
    Test.TestEqual(Label + TEXT("：禁止近距抓取"), Cup->GrabComp->GrabType, EGrabType::None);
    Test.TestFalse(Label + TEXT("：禁止远程抓取"), Cup->GrabComp->bAllowRemoteGrab);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeTutorialBusIntegrationTest,
    "CyberCafe.Recipe.TutorialRealBusRejectRepairAndFeedbackOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRecipeTutorialBusIntegrationTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    {
        FRecipeTutorialWorld T;
        if (!TestNotNull(TEXT("真实 GI 事件总线"), T.Events) ||
            !TestNotNull(TEXT("真实 World NarrativeSubsystem"), T.Narrative)) return false;
        UDrinkRecipe* Recipe = T.Recipe();
        UDrinkRecipe* PreviousRecipe = T.Recipe();
        PreviousRecipe->RecipeId = TEXT("PreviousOrder");
        AServingSpotActor* Spot = T.Spot(FVector(300, 40, 20), PreviousRecipe);
        Spot->SetSubmissionEnabled(false);
        AServingSpotActor* WrongSpot = T.Spot(FVector(700, 40, 20), Recipe);
        ACupActor* Cup = T.Cup(100.f, 0.f, Inside(Spot));
        ACupActor* OtherCup = T.Cup(100.f, 100.f, Inside(WrongSpot));
        UTutorialStep_WaitForServe* Step = T.WaitStep(Spot, Recipe);
        T.BeginPlay();
        T.Director->StartTutorial();
        TestTrue(TEXT("真实游戏世界已 BeginPlay"), T.World->HasBegunPlay());
        TestTrue(TEXT("Spot 已通过世界生命周期 BeginPlay"), Spot->HasActorBegunPlay());
        TestTrue(TEXT("EnterStep 安装 RequiredRecipe"), Spot->ActiveRecipe.Get() == Recipe);
        TestTrue(TEXT("EnterStep 启用原本禁用的 Spot"), Spot->bAcceptSubmissions);
        TestTrue(TEXT("EnterStep 绑定 OnDrinkValidated"), Spot->OnDrinkValidated.IsBound());
        TestTrue(TEXT("Step 使用当前 GI 总线"), UCyberCafeEventSubsystem::Get(Step) == T.Events);
        TestTrue(TEXT("Spot 使用同一 GI 总线"), UCyberCafeEventSubsystem::Get(Spot) == T.Events);
        TestTrue(TEXT("真实 NPC 已配置给 Director"), IsValid(T.NPC) && T.Director->NPCRef == T.NPC);
        CheckPending(*this, TEXT("步骤进入后"), T, Spot, Step);

        const int32 RejectedRevision = Cup->GetLiquidRevision();
        const uint32 BeforeFeedback = T.Narrative->GetPresentationSerial();
        TestFalse(TEXT("OnlyCoffee 100 mL 真实提交被拒"), Spot->TrySubmitCup(Cup));
        const FDrinkValidationResult Rejected = Spot->GetLastValidation();
        TestFalse(TEXT("LastValidation 为拒收"), Rejected.bAccepted);
        TestEqual(TEXT("缺奶拒收原因"), Rejected.Reason, EDrinkValidationReason::MissingIngredient);
        TestEqual(TEXT("拒收记录当前 revision"), Rejected.LiquidRevision, RejectedRevision);
        TestFalse(TEXT("拒收反馈非空"), Rejected.Feedback.IsEmpty());
        TestTrue(TEXT("拒收通过 Narrative 新建表现"), T.Narrative->GetPresentationSerial() > BeforeFeedback);
        TestFalse(TEXT("拒收不冻结"), Cup->IsLiquidFrozen());
        CheckPending(*this, TEXT("OnlyCoffee 拒收"), T, Spot, Step);

        const uint32 FeedbackSerial = T.Narrative->GetPresentationSerial();
        for (int32 Attempt = 0; Attempt < 3; ++Attempt)
        {
            TestFalse(TEXT("同杯同 revision 重复拒收"), Spot->TrySubmitCup(Cup));
            TestEqual(TEXT("同杯同 revision 不刷新表现 serial"),
                T.Narrative->GetPresentationSerial(), FeedbackSerial);
        }
        if (!T.Widget->GetVisibleText().Contains(Rejected.Feedback.ToString()))
        {
            TestTrue(TEXT("SkipTypewriter 只补全拒收全文"), T.Narrative->SkipTypewriter());
        }
        TestTrue(TEXT("Narrative 发现真实 HUD"), T.Narrative->GetHUDActor() == T.HUD);
        TestTrue(TEXT("HUD 宿主使用同一 Widget"), T.HUD->GetNarrativeWidget() == T.Widget);
        TestTrue(TEXT("NPC 拒收文本在 HUD 可见"), T.Narrative->IsVisible());
        TestTrue(TEXT("HUD 全文包含配方返回的反馈"),
            T.Widget->GetVisibleText().Contains(Rejected.Feedback.ToString()));
        CheckPending(*this, TEXT("补全反馈不能完成操作步骤"), T, Spot, Step);
        T.Events->OnPlayerSkipRequested.Broadcast();
        CheckPending(*this, TEXT("真实跳过输入不能完成上菜任务"), T, Spot, Step);
        TestFalse(TEXT("全文显示后同 revision 仍拒收"), Spot->TrySubmitCup(Cup));
        TestEqual(TEXT("全文显示后不重启拒收表现"), T.Narrative->GetPresentationSerial(), FeedbackSerial);

        // 仅负向测试故意广播旧成功事件；所有成功状态仍由 TrySubmitCup 产生。
        T.Events->OnDrinkServed.Broadcast(Spot, Cup);
        CheckPending(*this, TEXT("伪造旧 OnDrinkServed 不能绕过配方"), T, Spot, Step);
        TestTrue(TEXT("另一个 Spot 的真实合格提交成功"), WrongSpot->TrySubmitCup(OtherCup));
        TestTrue(TEXT("wrongSpot 确实有 Accepted 记录"), WrongSpot->HasAcceptedCup(OtherCup, Recipe->RecipeId));
        CheckPending(*this, TEXT("其他 Spot 的真实总线事件不能推进"), T, Spot, Step);
        T.Events->OnDrinkServed.Broadcast(Spot, OtherCup);
        CheckPending(*this, TEXT("把他处验收杯冒充本 Spot 的杯也不能推进"), T, Spot, Step);
        TestFalse(TEXT("真实提交 API 拒绝空杯指针"), Spot->TrySubmitCup(nullptr));
        T.Events->OnDrinkServed.Broadcast(Spot, nullptr);
        CheckPending(*this, TEXT("无效空杯事件不能推进"), T, Spot, Step);
        ACupActor* DestroyedCup = T.Cup(100.f, 100.f, Outside(Spot));
        TestTrue(TEXT("测试无效杯由真实 Destroy 产生"), DestroyedCup->Destroy());
        TestFalse(TEXT("已销毁杯不再有效"), IsValid(DestroyedCup));
        T.Events->OnDrinkServed.Broadcast(Spot, DestroyedCup);
        CheckPending(*this, TEXT("已销毁杯事件不能推进"), T, Spot, Step);

        TestEqual(TEXT("补足 Milk 100 mL"), Cup->AddIngredientLiquid(100.f, MilkId, FLinearColor::White), 100.f);
        TestTrue(TEXT("补奶产生新 revision"), Cup->GetLiquidRevision() > RejectedRevision);
        TestTrue(TEXT("补奶后配方本身合格"), Recipe->Evaluate(Cup).bAccepted);
        TestFalse(TEXT("合格但未经提交还没有 Accepted 记录"), Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
        T.Events->OnDrinkServed.Broadcast(Spot, Cup);
        CheckPending(*this, TEXT("仅 Evaluate 合格不能冒充已上菜"), T, Spot, Step);
        T.Events->OnDrinkServed.Broadcast(nullptr, Cup);
        CheckPending(*this, TEXT("无效 Spot 不能推进"), T, Spot, Step);

        // 借现有原生叙事回调观察真实 OnDrinkServed -> MarkCompleted 调用栈，不增加 UCLASS 探针。
        Step->CompletedText = FText::FromString(TEXT("配方验收完成。"));
        T.Director->bDialogueTypewriter = false;
        int32 CompletionPresentations = 0;
        int32 FinishedPresentations = 0;
        const FDelegateHandle Observation = T.Narrative->OnTextPresentationFinishedNative.AddLambda(
            [&](uint32)
            {
                if (T.Widget->GetVisibleText() == Step->CompletedText.ToString())
                {
                    ++CompletionPresentations;
                    CheckAccepted(*this, TEXT("完成回调内部已有冻结、锁杯与验收记录"), T, Spot, Cup, Step, Recipe);
                }
                if (T.Widget->GetVisibleText() == T.Director->FinishedText.ToString())
                {
                    ++FinishedPresentations;
                }
            });
        TestTrue(TEXT("同杯补奶后真实再次提交成功"), Spot->TrySubmitCup(Cup));
        TestEqual(TEXT("真实 MarkCompleted 只触发一次完成表现"), CompletionPresentations, 1);
        CheckAccepted(*this, TEXT("真实总线成功后"), T, Spot, Cup, Step, Recipe);
        T.Events->OnDrinkServed.Broadcast(WrongSpot, OtherCup);
        T.Events->OnDrinkServed.Broadcast(Spot, OtherCup);
        T.Events->OnDrinkServed.Broadcast(Spot, nullptr);
        TestTrue(TEXT("完成后旧事件不能替换 LastServedCup"), T.Director->LastServedCup.Get() == Cup);
        TestFalse(TEXT("同杯成功后 one-shot 拒绝重交"), Spot->TrySubmitCup(Cup));
        TestEqual(TEXT("重复事件不重复完成表现"), CompletionPresentations, 1);
        T.TickWorld(.02f);
        TestEqual(TEXT("Director 正常推进至教学结束"), FinishedPresentations, 1);
        TestNull(TEXT("正常推进调用了 Step.ExitStep"), Step->GetDirector());
        TestFalse(TEXT("正常 Exit 解绑配方拒收事件"), Spot->OnDrinkValidated.IsBound());
        T.Narrative->OnTextPresentationFinishedNative.Remove(Observation);
    }

    for (bool bNextPresentation : {false, true})
    {
        FRecipeTutorialWorld T;
        if (!TestNotNull(TEXT("反馈退出场景的真实 GI 总线"), T.Events) ||
            !TestNotNull(TEXT("反馈退出场景的 NarrativeSubsystem"), T.Narrative)) return false;
        const FString Label = bNextPresentation ? TEXT("旧步骤退出保留下一段") : TEXT("步骤退出取消拥有的拒收");
        UDrinkRecipe* PreviousRecipe = T.Recipe();
        PreviousRecipe->RecipeId = TEXT("PreviousOrder");
        UDrinkRecipe* Recipe = T.Recipe();
        AServingSpotActor* Spot = T.Spot(FVector(300, 40, 20), PreviousRecipe);
        Spot->SetSubmissionEnabled(false);
        ACupActor* Cup = T.Cup(100.f, 0.f, Inside(Spot));
        UTutorialStep_WaitForServe* Step = T.WaitStep(Spot, Recipe);
        T.BeginPlay();
        T.Director->StartTutorial();
        TestFalse(Label + TEXT("：真实提交产生拒收"), Spot->TrySubmitCup(Cup));
        TestTrue(Label + TEXT("：拒收全文尚在逐字显示"), T.Narrative->IsTyping());
        const uint32 OwnedSerial = T.Narrative->GetPresentationSerial();
        int32 OwnedCancellations = 0;
        const FDelegateHandle CancelObservation = T.Narrative->OnTextPresentationCancelledNative.AddLambda(
            [&](uint32 Serial) { if (Serial == OwnedSerial) ++OwnedCancellations; });
        UTutorialStep_ShowMessage* Next = nullptr;
        if (bNextPresentation)
        {
            Next = NewObject<UTutorialStep_ShowMessage>(T.Director);
            Next->HintText = FText::FromString(TEXT("接下来请认识新的操作区域，这段文字不能被旧拒收步骤取消。"));
            Next->Duration = 0.f;
            Next->EnterStep(T.Director);
            T.Narrative->PlayTypewriter(Next->HintText, 30.f);
        }
        const uint32 CurrentSerial = T.Narrative->GetPresentationSerial();
        Step->ExitStep();
        TestNull(Label + TEXT("：Step 已解除 Director"), Step->GetDirector());
        TestFalse(Label + TEXT("：解除 Spot 验收订阅"), Spot->OnDrinkValidated.IsBound());
        TestTrue(Label + TEXT("：未上菜时恢复之前的配方"), Spot->ActiveRecipe.Get() == PreviousRecipe);
        TestFalse(Label + TEXT("：恢复之前禁用状态"), Spot->bAcceptSubmissions);
        CheckPending(*this, Label, T, Spot, Step);
        if (bNextPresentation)
        {
            TestTrue(Label + TEXT("：下一段有新的 serial"), CurrentSerial > OwnedSerial);
            TestEqual(Label + TEXT("：旧 Exit 不改下一段 serial"), T.Narrative->GetPresentationSerial(), CurrentSerial);
            TestTrue(Label + TEXT("：下一段仍在逐字显示"), T.Narrative->IsTyping());
            TestTrue(Label + TEXT("：下一段面板仍可见"), T.Narrative->IsVisible());
            TestEqual(Label + TEXT("：旧 Exit 未取消下一段"), OwnedCancellations, 0);
            TestTrue(Label + TEXT("：下一段可正常补全"), T.Narrative->SkipTypewriter());
            TestEqual(Label + TEXT("：保留的全文正确"), T.Widget->GetVisibleText(), Next->HintText.ToString());
            TestFalse(Label + TEXT("：补全下一段也不完成旧上菜步骤"), Step->IsCompleted());
            Next->ExitStep();
        }
        else
        {
            TestFalse(Label + TEXT("：停止拥有的拒收逐字显示"), T.Narrative->IsTyping());
            TestFalse(Label + TEXT("：隐藏拥有的拒收面板"), T.Narrative->IsVisible());
            TestEqual(Label + TEXT("：真实取消回调恰好一次"), OwnedCancellations, 1);
            TestTrue(Label + TEXT("：取消后 serial 已失效"), T.Narrative->GetPresentationSerial() > OwnedSerial);
        }
        T.Narrative->OnTextPresentationCancelledNative.Remove(CancelObservation);
        const uint32 AfterExitSerial = T.Narrative->GetPresentationSerial();
        Spot->SetSubmissionEnabled(true);
        TestFalse(Label + TEXT("：退出后真实拒收仍可发生"), Spot->TrySubmitCup(Cup));
        TestEqual(Label + TEXT("：已退出步骤不再刷新拒收 HUD"),
            T.Narrative->GetPresentationSerial(), AfterExitSerial);
        T.Events->OnDrinkServed.Broadcast(Spot, Cup);
        CheckPending(*this, Label + TEXT("：退出后总线事件不推进"), T, Spot, Step);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeAutomaticStabilityIntegrationTest,
    "CyberCafe.Recipe.AutomaticRealOverlapRevisionStabilityAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRecipeAutomaticStabilityIntegrationTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("测试需要 Engine WorldContext"), GEngine)) return false;
    enum class EInterruption { None, Pickup, MoveOutside };
    for (EInterruption Interruption : {EInterruption::None, EInterruption::Pickup, EInterruption::MoveOutside})
    {
        FRecipeTutorialWorld T;
        if (!TestNotNull(TEXT("自动验收的真实 GI 总线"), T.Events) ||
            !TestNotNull(TEXT("自动验收的 NarrativeSubsystem"), T.Narrative)) return false;
        const FString Label = Interruption == EInterruption::None ? TEXT("Revision 稳定窗") :
            Interruption == EInterruption::Pickup ? TEXT("拿起取消稳定窗") : TEXT("移出取消稳定窗");
        UDrinkRecipe* Recipe = T.Recipe();
        AServingSpotActor* Spot = T.Spot(FVector(300, 40, 20), Recipe);
        ACupActor* Cup = T.Cup(90.f, 90.f, Outside(Spot));
        UBoxComponent* CupBox = RegisterCupOverlapBox(Cup);
        UMotionControllerComponent* Controller = T.Controller(Inside(Spot));
        UTutorialStep_WaitForServe* Step = T.WaitStep(Spot, Recipe);
        T.BeginPlay();
        T.Director->StartTutorial();
        TestTrue(Label + TEXT("：真实 Game World 已 BeginPlay"), T.World->HasBegunPlay());
        TestTrue(Label + TEXT("：Spot 已 BeginPlay"), Spot->HasActorBegunPlay());
        TestTrue(Label + TEXT("：Cup 已 BeginPlay"), Cup->HasActorBegunPlay());
        TestTrue(Label + TEXT("：活动配方正确"), Spot->ActiveRecipe.Get() == Recipe);
        TestEqual(Label + TEXT("：稳定窗为 0.4 秒"), Spot->StabilitySeconds, .4f);
        TestTrue(Label + TEXT("：注册碰撞体属于真实 Cup"), CupBox->IsRegistered() && CupBox->GetOwner() == Cup);
        TestEqual(Label + TEXT("：碰撞体 QueryOnly"), CupBox->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
        TestEqual(Label + TEXT("：碰撞体 PhysicsBody"), CupBox->GetCollisionObjectType(), ECC_PhysicsBody);
        TestFalse(Label + TEXT("：禁用默认杯漏液"), Cup->bCanPour);
        TestNull(Label + TEXT("：原液 FX asset 为空"), Cup->LiquidFX->GetAsset());
        TestNull(Label + TEXT("：出液 FX asset 为空"), Cup->PourFX->GetAsset());
        TestTrue(Label + TEXT("：90 Coffee + 90 Milk 的 180 mL 真实合格"), Recipe->Evaluate(Cup).bAccepted);
        TestFalse(Label + TEXT("：初始杯在实际检测区外"), Spot->DetectBox->IsOverlappingActor(Cup));
        TestNull(Label + TEXT("：初始无 Candidate"), Spot->GetCandidateCup());

        Cup->SetActorLocation(Inside(Spot), false, nullptr, ETeleportType::TeleportPhysics);
        if (!CheckRealOverlap(*this, Label + TEXT("首次进入"), Spot, Cup, CupBox)) return false;
        // 首次 Tick 仅观察 revision；之后才累计稳定时间。
        Spot->Tick(0.f);
        TickSpot(Spot, 4);
        CheckPending(*this, Label + TEXT("：0.2 秒不足以自动提交"), T, Spot, Step);
        const int32 InitialRevision = Cup->GetLiquidRevision();
        TestEqual(Label + TEXT("：实际补入 Milk 5 mL"),
            Cup->AddIngredientLiquid(5.f, MilkId, FLinearColor::White), 5.f);
        const int32 RevisedRevision = Cup->GetLiquidRevision();
        TestTrue(Label + TEXT("：补奶确实产生新 revision"), RevisedRevision > InitialRevision);
        TestTrue(Label + TEXT("：90 Coffee + 95 Milk 仍然合格"), Recipe->Evaluate(Cup).bAccepted);
        TestTrue(Label + TEXT("：权威总量为 185 mL"), FMath::Abs(Cup->GetPreciseVolumeML() - 185.0) < VolumeTolerance);
        Spot->Tick(0.f);
        TickSpot(Spot, 5);
        // 如果继续使用旧 revision 的 0.2 秒，这里累计 0.45 秒就会错误上菜。
        CheckPending(*this, Label + TEXT("：新 revision 0.25 秒必须重新计时"), T, Spot, Step);
        TestFalse(Label + TEXT("：稳定窗未满不冻结"), Cup->IsLiquidFrozen());

        if (Interruption == EInterruption::Pickup)
        {
            if (!TestTrue(Label + TEXT("：通过真实 TryGrab 拿起"), Cup->GrabComp->TryGrab(Controller))) return false;
            TestTrue(Label + TEXT("：真实 held 状态"), Cup->IsHeld());
            TickSpot(Spot, 10);
            CheckPending(*this, Label + TEXT("：持有超过稳定窗仍不得上菜"), T, Spot, Step);
            TestTrue(Label + TEXT("：Tick 不强制松手"), Cup->IsHeld());
            TestTrue(Label + TEXT("：仍由原控制器持有"), Cup->GrabComp->GetHoldingController() == Controller);
            if (!TestTrue(Label + TEXT("：通过真实 TryRelease 松手"), Cup->GrabComp->TryRelease())) return false;
            TestFalse(Label + TEXT("：释放后不再 held"), Cup->IsHeld());
            if (!CheckRealOverlap(*this, Label + TEXT("松手后"), Spot, Cup, CupBox)) return false;
            Spot->Tick(0.f);
            TickSpot(Spot, 5);
            CheckPending(*this, Label + TEXT("：松手后重新稳定 0.25 秒仍未提交"), T, Spot, Step);
        }
        else if (Interruption == EInterruption::MoveOutside)
        {
            Cup->SetActorLocation(Outside(Spot), false, nullptr, ETeleportType::TeleportPhysics);
            CupBox->UpdateOverlaps();
            Spot->DetectBox->UpdateOverlaps();
            TestFalse(Label + TEXT("：真实 DetectBox overlap 已结束"), Spot->DetectBox->IsOverlappingActor(Cup));
            TestFalse(Label + TEXT("：杯子原点已移出检测区"), Spot->IsInServingArea(Cup));
            TestNull(Label + TEXT("：真实 EndOverlap 清除 Candidate"), Spot->GetCandidateCup());
            TickSpot(Spot, 10);
            CheckPending(*this, Label + TEXT("：移出后超过稳定窗不得上菜"), T, Spot, Step);
            Cup->SetActorLocation(Inside(Spot), false, nullptr, ETeleportType::TeleportPhysics);
            if (!CheckRealOverlap(*this, Label + TEXT("重新进入"), Spot, Cup, CupBox)) return false;
            Spot->Tick(0.f);
            TickSpot(Spot, 5);
            CheckPending(*this, Label + TEXT("：重新进入后 0.25 秒仍未提交"), T, Spot, Step);
        }

        TickSpot(Spot, 2);
        CheckPending(*this, Label + TEXT("：连续稳定 0.35 秒仍未提交"), T, Spot, Step);
        TickSpot(Spot, 2);
        CheckAccepted(*this, Label + TEXT("：连续稳定 0.45 秒自动成功"), T, Spot, Cup, Step, Recipe);
        TestEqual(Label + TEXT("：自动成功保存最新 revision"), Spot->GetLastValidation().LiquidRevision, RevisedRevision);
        TestTrue(Label + TEXT("：验收总量没有默认漏液"), FMath::Abs(Spot->GetLastValidation().TotalML - 185.0) < VolumeTolerance);
        TestTrue(Label + TEXT("：验收 Coffee 成分为 90 mL"),
            FMath::Abs(Spot->GetLastValidation().Composition.Get(CoffeeId) - 90.0) < VolumeTolerance);
        TestTrue(Label + TEXT("：验收 Milk 成分为 95 mL"),
            FMath::Abs(Spot->GetLastValidation().Composition.Get(MilkId) - 95.0) < VolumeTolerance);
        TestNull(Label + TEXT("：上菜后清除 Candidate"), Spot->GetCandidateCup());
        const uint32 ServedSerial = T.Narrative->GetPresentationSerial();
        TickSpot(Spot, 10);
        TestEqual(Label + TEXT("：one-shot 后 Tick 不再刷新叙事"), T.Narrative->GetPresentationSerial(), ServedSerial);
        TestTrue(Label + TEXT("：后续 Tick 不替换上菜杯"), Spot->GetServedCup() == Cup);
        TestEqual(Label + TEXT("：后续 Tick 不修改冻结 revision"), Cup->GetLiquidRevision(), RevisedRevision);
    }
    return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
