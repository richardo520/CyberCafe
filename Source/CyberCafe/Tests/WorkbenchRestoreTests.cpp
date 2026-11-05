// @trace RS-01 RS-02 RS-03 RS-04
// UE5 原生合成 World 测试；仅执行时导出真实采样，不代表资产或硬件验收。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/BottleActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/IceInteraction.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/Steps/TutorialStep_Grab.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "GrabComponent.h"
#include "MotionControllerComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"

namespace WorkbenchRestoreTests
{
const FName MilkId(TEXT("Milk"));
constexpr double VolumeTolerance = 1.e-6;

FTransform SupplyPose(double X, double Y = 0.0)
{
    // 引擎 Cube 为 100 cm；缩到 6 cm，让供给和移开 50 cm 的交付杯不互占位置。
    return FTransform(FRotator::ZeroRotator, FVector(X, Y, 60), FVector(.06));
}

void ConfigureGrab(UGrabComponent* Grab)
{
    Grab->GrabType = EGrabType::Free;
    Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
    Grab->bSimulateOnDrop = false;
    Grab->bCaptureHand = false;
    Grab->OnGrabHapticEffect = nullptr;
}

void SuppressVesselSimulation(ALiquidContainerActor* A)
{
    A->ContainerMesh->SetSimulatePhysics(false);
    A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    A->ContainerMesh->SetGenerateOverlapEvents(false);
    A->bCanPour = false;
    A->bEnableSplash = false;
    A->LiquidFXTemplate = nullptr;
    A->SplashEffectTemplate = nullptr;
    A->LiquidFX->SetAutoActivate(false);
    A->LiquidFX->SetAsset(nullptr);
    A->LiquidFX->DeactivateImmediate();
    A->PourFX->SetAutoActivate(false);
    A->PourFX->SetAsset(nullptr);
    A->PourFX->DeactivateImmediate();
    ConfigureGrab(A->GrabComp.Get());
}

// 沿用 GlassBreakageTests 的真实 GI / WorldContext 生命周期。
struct FWorkbenchWorld
{
    UWorld* World;
    UGameInstance* GI;
    UStaticMesh* BasicCube;
    AGlassRecoveryActor* Manager;
    TArray<ALiquidContainerActor*> Vessels;

    FWorkbenchWorld()
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
        BasicCube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        // 经理先创建；BeginPlay 捕获初始填充，而不是容器刷新后的 FillAmount。
        Manager = World->SpawnActor<AGlassRecoveryActor>();
        Manager->bUnifiedRestore = true;
        Manager->HoldSeconds = 1.f;
    }

    ~FWorkbenchWorld()
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    template <typename T>
    T* Vessel(float Capacity, float Fill, const FTransform& Pose)
    {
        T* A = World->SpawnActorDeferred<T>(T::StaticClass(), Pose, nullptr,
            nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        SuppressVesselSimulation(A);
        A->ContainerMesh->SetStaticMesh(BasicCube);
        A->MaxVolumeML = Capacity;
        A->FillAmount = Fill;
        A->InitialIngredientId = MilkId;
        A->LiquidColor = FLinearColor::White;
        A->PourEntryRadius = 3.3f;
        // 视觉 Cube 的缩放不缩小冰球的厘米级入口。
        A->PourEntryPoint->SetAbsolute(false, false, true);
        A->PourEntryPoint->SetRelativeScale3D(FVector::OneVector);
        A->PourEntryPoint->SetRelativeLocation(FVector(0, 0, 5.0 / Pose.GetScale3D().Z));
        if (auto* Bottle = Cast<ABottleActor>(A))
        {
            Bottle->CapClass = nullptr;
            Bottle->bStartCapped = false;
            Bottle->PourHaptic = nullptr;
        }
        if (auto* Cup = Cast<ACupActor>(A))
        {
            Cup->VolumeWidgetClass = nullptr;
            Cup->bFaceCamera = false;
        }
        A->FinishSpawning(Pose);
        SuppressVesselSimulation(A);
        Vessels.Add(A);
        return A;
    }

    void Bind(ACupActor* Cup, ABottleActor* Bottle)
    {
        auto* CupProfile = NewObject<UGlassBreakProfile>(World);
        auto* BottleProfile = NewObject<UGlassBreakProfile>(World);
        CupProfile->BreakSound = nullptr;
        BottleProfile->BreakSound = nullptr;
        CupProfile->Shards.Empty();
        BottleProfile->Shards.Empty();
        Manager->BindPrototype(Cup, Bottle, CupProfile, BottleProfile);
    }

    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        for (auto* A : Vessels) SuppressVesselSimulation(A);
    }

    ULiquidTransferSubsystem* Transfers() const
    {
        return World->GetSubsystem<ULiquidTransferSubsystem>();
    }

    UMotionControllerComponent* Controller(EControllerHand Hand)
    {
        auto* Pawn = World->SpawnActor<APawn>();
        auto* Controller = NewObject<UMotionControllerComponent>(Pawn);
        Pawn->SetRootComponent(Controller);
        Pawn->AddInstanceComponent(Controller);
        Controller->SetTrackingSource(Hand);
        Controller->RegisterComponent();
        Pawn->SetActorLocation(FVector(400, Hand == EControllerHand::Left ? -100 : 100, 60));
        return Controller;
    }

    ATutorialDirector* Director(ALiquidContainerActor* Cup, ALiquidContainerActor* Bottle)
    {
        auto* D = World->SpawnActor<ATutorialDirector>();
        D->bAutoStart = false;
        D->NarrativeHUDWidgetClass = nullptr;
        D->bDialogueVoice = false;
        auto* GrabCup = NewObject<UTutorialStep_Grab>(D);
        auto* GrabBottle = NewObject<UTutorialStep_Grab>(D);
        GrabCup->RequiredActor = Cup;
        GrabBottle->RequiredActor = Bottle;
        // 首项为空，通过公开 StartTutorial 启动到非零步骤，防止重置到第零步漏检。
        D->Steps.Add(nullptr);
        D->Steps.Add(GrabCup);
        D->Steps.Add(GrabBottle);
        return D;
    }
};

bool ReadStepIndex(FAutomationTestBase& Test, ATutorialDirector* Director, int32& Out)
{
    // 只读 UObject 反射；不接触 Slots，也不写入受保护教学状态。
    auto* Property = FindFProperty<FIntProperty>(Director->GetClass(), TEXT("CurrentStepIndex"));
    if (!Test.TestNotNull(TEXT("教学步骤序号可通过只读反射采样"), Property)) return false;
    Out = Property->GetPropertyValue_InContainer(Director);
    return true;
}

int32 CountContainers(UWorld* World)
{
    int32 Count = 0;
    for (TActorIterator<ALiquidContainerActor> It(World); It; ++It)
        if (IsValid(*It) && !It->IsActorBeingDestroyed()) ++Count;
    return Count;
}

int32 CountSources(UWorld* World)
{
    int32 Count = 0;
    for (TActorIterator<ABottleActor> It(World); It; ++It)
        if (IsValid(*It) && !It->IsActorBeingDestroyed()) ++Count;
    return Count;
}

void CheckBalanced(FAutomationTestBase& Test, ULiquidTransferSubsystem* S)
{
    const auto B = S->GetBalance();
    Test.TestTrue(TEXT("真实账本守恒误差小于 1e-6 mL"),
        FMath::IsFinite(B.ErrorML) && FMath::Abs(B.ErrorML) < VolumeTolerance);
}

void CheckSameBalance(FAutomationTestBase& Test, const FLiquidBalance& Before,
    const FLiquidBalance& After)
{
    Test.TestEqual(TEXT("Initial 未变"), After.InitialML, Before.InitialML);
    Test.TestEqual(TEXT("Injected 未变"), After.InjectedML, Before.InjectedML);
    Test.TestEqual(TEXT("Removed 未变"), After.RemovedML, Before.RemovedML);
    Test.TestEqual(TEXT("Container 未变"), After.ContainerML, Before.ContainerML);
    Test.TestEqual(TEXT("InFlight 未变"), After.InFlightML, Before.InFlightML);
    Test.TestEqual(TEXT("Spilled 未变"), After.SpilledML, Before.SpilledML);
    Test.TestEqual(TEXT("Error 未变"), After.ErrorML, Before.ErrorML);
    Test.TestEqual(TEXT("在途包数量未变"), After.PacketCount, Before.PacketCount);
}

FString VesselJson(const ALiquidContainerActor* A)
{
    if (!IsValid(A) || A->IsActorBeingDestroyed()) return TEXT("null");
    const FVector Position = A->GetActorLocation();
    return FString::Printf(TEXT("{\"objectId\":%u,\"volumeML\":%.17g,\"milkML\":%.17g,\"compositionML\":%.17g,\"capacityML\":%.17g,\"fill\":%.17g,\"revision\":%d,\"broken\":%s,\"frozen\":%s,\"replacement\":%s,\"canPour\":%s,\"canReceive\":%s,\"grabActive\":%s,\"held\":%s,\"pulled\":%s,\"cooldownSeconds\":%.17g,\"xCM\":%.17g,\"yCM\":%.17g,\"zCM\":%.17g}"),
        A->GetUniqueID(), A->GetPreciseVolumeML(), A->GetIngredientVolumeML(MilkId),
        A->GetComposition().Total(), static_cast<double>(A->MaxVolumeML),
        static_cast<double>(A->FillAmount), A->GetLiquidRevision(),
        A->IsBroken() ? TEXT("true") : TEXT("false"),
        A->IsLiquidFrozen() ? TEXT("true") : TEXT("false"),
        A->bInitialVolumeIsReplacement ? TEXT("true") : TEXT("false"),
        A->bCanPour ? TEXT("true") : TEXT("false"),
        A->bAcceptLiquidFromOthers ? TEXT("true") : TEXT("false"),
        A->GrabComp->IsActive() ? TEXT("true") : TEXT("false"),
        A->GrabComp->IsHeld() ? TEXT("true") : TEXT("false"),
        A->GrabComp->IsPulled() ? TEXT("true") : TEXT("false"),
        static_cast<double>(A->RestoreReceiveCooldown), Position.X, Position.Y, Position.Z);
}

void Capture(TArray<FString>& Rows, const TCHAR* Stage, const FWorkbenchWorld& T,
    const ALiquidContainerActor* Observed = nullptr)
{
    const auto B = T.Transfers()->GetBalance();
    Rows.Add(FString::Printf(TEXT("{\"stage\":\"%s\",\"initialML\":%.17g,\"injectedML\":%.17g,\"removedML\":%.17g,\"containerML\":%.17g,\"inFlightML\":%.17g,\"spilledML\":%.17g,\"errorML\":%.17g,\"packetCount\":%d,\"sourceCount\":%d,\"containerCount\":%d,\"generation\":%d,\"slotCount\":%d,\"progress\":%.17g,\"flightHorizonSeconds\":%.17g,\"cup\":%s,\"bottle\":%s,\"observed\":%s}"),
        Stage, B.InitialML, B.InjectedML, B.RemovedML, B.ContainerML,
        B.InFlightML, B.SpilledML, B.ErrorML, B.PacketCount, CountSources(T.World),
        CountContainers(T.World), T.Manager->GetRestoreGeneration(),
        T.Manager->GetRestoreSlotCount(), static_cast<double>(T.Manager->GetRestoreProgress()),
        T.Transfers()->GetUnsettledFlightHorizon(), *VesselJson(T.Manager->GetCurrent(0)),
        *VesselJson(T.Manager->GetCurrent(1)), *VesselJson(Observed)));
}

bool SaveGroup(FAutomationTestBase& Test, const TCHAR* Group, const TArray<FString>& Rows)
{
    // 只保存本进程实际执行的组，不读取旧文件拼接未执行的结果。
    static TMap<FString, FString> CapturedGroups;
    CapturedGroups.Add(Group, TEXT("[") + FString::Join(Rows, TEXT(",\n")) + TEXT("]"));
    TArray<FString> Names;
    CapturedGroups.GetKeys(Names);
    Names.Sort();
    TArray<FString> Groups;
    for (const auto& Name : Names)
        Groups.Add(TEXT("\"") + Name + TEXT("\":") + CapturedGroups[Name]);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Workbench");
    if (!Test.TestTrue(TEXT("创建运行时采样目录"), IFileManager::Get().MakeDirectory(*Directory, true)))
        return false;
    const FString Json = TEXT("{\"sampleKind\":\"nativeWorldPublicAPI\",\"groups\":{\n")
        + FString::Join(Groups, TEXT(",\n")) + TEXT("\n}}\n");
    return Test.TestTrue(TEXT("写入真实 getter 和账本采样"),
        FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("behaviour.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

bool CheckNewSlot(FAutomationTestBase& Test, FWorkbenchWorld& T, int32 Index,
    const ALiquidContainerActor* Old, UClass* ExpectedClass, const FTransform& Pose,
    float Capacity, double MilkML)
{
    auto* Current = T.Manager->GetCurrent(Index);
    if (!Test.TestNotNull(TEXT("当前供给有效"), Current)) return false;
    Test.TestTrue(TEXT("恢复使用新身份和原 class"), Current != Old && Current->GetClass() == ExpectedClass);
    Test.TestTrue(TEXT("回到捕获的原始供给位姿"), Current->GetActorTransform().Equals(Pose));
    Test.TestEqual(TEXT("容量来自初始快照"), Current->MaxVolumeML, Capacity);
    Test.TestEqual(TEXT("Milk 成分使用初始供给"), Current->GetIngredientVolumeML(MilkId), MilkML);
    Test.TestEqual(TEXT("填充比例来自初始供给"), Current->FillAmount, static_cast<float>(MilkML / Capacity));
    Test.TestEqual(TEXT("杯可接液而瓶不接液的初始配置保留"),
        Current->bAcceptLiquidFromOthers, ExpectedClass == ACupActor::StaticClass());
    Test.TestEqual(TEXT("权威体积等于成分总量"), Current->GetPreciseVolumeML(), Current->GetComposition().Total());
    Test.TestEqual(TEXT("初始成分标识保持 Milk"), Current->InitialIngredientId, MilkId);
    Test.TestTrue(TEXT("颜色使用初始快照"), Current->LiquidColor.Equals(FLinearColor::White));
    Test.TestTrue(TEXT("仅新身份设置 replacement"), Current->bInitialVolumeIsReplacement);
    Test.TestFalse(TEXT("新供给不继承破碎"), Current->IsBroken());
    Test.TestFalse(TEXT("新供给不继承冻结"), Current->IsLiquidFrozen());
    Test.TestFalse(TEXT("新供给不带持有或拉取状态"), Current->GrabComp->IsHeld() || Current->GrabComp->IsPulled());
    Test.TestTrue(TEXT("新供给恢复可见与抓取激活"), Current->ContainerMesh->IsVisible() && Current->GrabComp->IsActive());
    Test.TestFalse(TEXT("恢复保留捕获的禁止自动倒液配置"), Current->bCanPour);
    Test.TestNull(TEXT("新供给不生成默认冰组件"),
        Current->FindComponentByClass<UCupIceComponent>());
    // 以上断言先采样恢复结果，再关闭 fixture 的物理和 FX；不修补恢复身份或业务状态。
    SuppressVesselSimulation(Current);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFullRestoreTest,
    "CyberCafe.Workbench.FullRestoreSpentIntactAndBroken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFullRestoreTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    if (!TestNotNull(TEXT("引擎 BasicCube 有效"), T.BasicCube)) return false;
    const auto CupPose = SupplyPose(30);
    const auto BottlePose = SupplyPose(70);
    const auto OtherCupPose = SupplyPose(110);
    const auto OtherBottlePose = SupplyPose(150);
    auto* Cup = T.Vessel<ACupActor>(200.f, 0.f, CupPose);
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, BottlePose);
    auto* OtherCup = T.Vessel<ACupActor>(250.f, 0.f, OtherCupPose);
    auto* OtherBottle = T.Vessel<ABottleActor>(300.f, .5f, OtherBottlePose);
    auto* Unrelated = T.Vessel<ABottleActor>(20.f, 1.f, SupplyPose(300));
    T.Bind(Cup, Bottle);
    T.Manager->RestoreTargets.Add(OtherCup);
    T.Manager->RestoreTargets.Add(OtherBottle);
    T.Manager->RestoreTargets.Add(Cup);
    T.BeginPlay();
    auto* S = T.Transfers();
    TArray<FString> Rows;
    TestEqual(TEXT("去重后四个供给槽"), T.Manager->GetRestoreSlotCount(), 4);
    TestTrue(TEXT("旧双槽及新增槽顺序保持"), T.Manager->GetCurrent(0) == Cup
        && T.Manager->GetCurrent(1) == Bottle && T.Manager->GetCurrent(2) == OtherCup
        && T.Manager->GetCurrent(3) == OtherBottle);
    TestEqual(TEXT("历史 Initial 为 270 mL"), S->GetBalance().InitialML, 270.0);
    TestEqual(TEXT("初始供给没有替换注入"), S->GetBalance().InjectedML, 0.0);
    TestEqual(TEXT("完整旧杯实际装入 40 mL"), Cup->AddIngredientLiquid(40.f, MilkId, FLinearColor::White), 40.f);
    TestEqual(TEXT("旧瓶真实消耗 10 mL"), Bottle->ConsumeLiquid(10.f), 10.f);
    TestEqual(TEXT("破碎前发射相关在途 20 mL"), S->EmitVolume(Bottle, 20.0,
        FVector(30, 0, 80), FVector(0, 0, -50), FVector::ZeroVector), 20.0);
    TestEqual(TEXT("无关源发射延迟在途 5 mL"), S->EmitVolume(Unrelated, 5.0,
        FVector(30, 0, 80), FVector(0, 0, -50), FVector::ZeroVector, .75), 5.0);
    TestTrue(TEXT("旧瓶通过公开业务 API 真正破碎"), Bottle->BreakContainerForImpact());
    TestEqual(TEXT("完整额外瓶耗尽"), OtherBottle->ConsumeLiquid(150.f), 150.f);
    TestFalse(TEXT("耗尽额外瓶仍完整"), OtherBottle->IsBroken());
    Bottle->InitialIngredientId = TEXT("Water");
    Bottle->LiquidColor = FLinearColor::Green;
    Cup->SetCapacityML(100.f);
    Cup->SetActorLocation(FVector(30, 50, 60));
    Capture(Rows, TEXT("beforeRestore"), T, Unrelated);
    const auto Before = S->GetBalance();
    const auto Horizon = S->GetUnsettledFlightHorizon();
    if (!TestTrue(TEXT("完整、耗尽、破碎供给可一键恢复"), T.Manager->RestoreWorkbench()))
    {
        Capture(Rows, TEXT("restoreRejected"), T, Unrelated);
        SaveGroup(*this, TEXT("FullRestoreSpentIntactAndBroken"), Rows);
        return false;
    }
    if (!CheckNewSlot(*this, T, 0, Cup, ACupActor::StaticClass(), CupPose, 200.f, 0.0)
        || !CheckNewSlot(*this, T, 1, Bottle, ABottleActor::StaticClass(), BottlePose, 200.f, 100.0)
        || !CheckNewSlot(*this, T, 2, OtherCup, ACupActor::StaticClass(), OtherCupPose, 250.f, 0.0)
        || !CheckNewSlot(*this, T, 3, OtherBottle, ABottleActor::StaticClass(), OtherBottlePose, 300.f, 150.0)) return false;
    auto* NewCup = T.Manager->GetCurrent(0);
    TestEqual(TEXT("新杯空 Fill"), NewCup->FillAmount, 0.f);
    TestEqual(TEXT("新瓶恢复 .5 Fill"), T.Manager->GetCurrent(1)->FillAmount, .5f);
    TestEqual(TEXT("恢复只推进一代"), T.Manager->GetRestoreGeneration(), 1);
    TestEqual(TEXT("不重置历史 Initial"), S->GetBalance().InitialML, Before.InitialML);
    TestEqual(TEXT("仅奶瓶原始供给累计注入 250 mL"), S->GetBalance().InjectedML, Before.InjectedML + 250.0);
    TestEqual(TEXT("相关旧尾流 20 mL 只洒一次"), S->GetBalance().SpilledML, Before.SpilledML + 20.0);
    TestEqual(TEXT("完整旧杯余液正常移除 40 mL"), S->GetBalance().RemovedML, Before.RemovedML + 40.0);
    TestEqual(TEXT("无关在途仍保留 5 mL"), S->GetBalance().InFlightML, 5.0);
    TestEqual(TEXT("只留下无关来源的一个包"), S->GetBalance().PacketCount, 1);
    TestTrue(TEXT("无关源不销毁且仍保留 15 mL"), IsValid(Unrelated)
        && !Unrelated->IsActorBeingDestroyed() && Unrelated->GetPreciseVolumeML() == 15.0);
    TestTrue(TEXT("新杯隔离时间覆盖延迟旧包的完整余寿命"),
        static_cast<double>(NewCup->RestoreReceiveCooldown) >= Horizon - .01);
    TestTrue(TEXT("公开原型引用均迁移"), T.Manager->PrototypeCup.Get() == NewCup
        && T.Manager->PrototypeBottle.Get() == T.Manager->GetCurrent(1));
    TestTrue(TEXT("额外供给引用迁移"), T.Manager->RestoreTargets[0].Get() == T.Manager->GetCurrent(2)
        && T.Manager->RestoreTargets[1].Get() == T.Manager->GetCurrent(3));
    Capture(Rows, TEXT("restoredUnrelatedTailRetained"), T, Unrelated);
    CheckBalanced(*this, S);
    const double Spilled = S->GetBalance().SpilledML;
    double Elapsed = 0;
    while (Elapsed < Horizon + .1)
    {
        const double Dt = FMath::Min(.1, Horizon + .1 - Elapsed);
        S->Simulate(Dt);
        NewCup->Tick(static_cast<float>(Dt));
        Elapsed += Dt;
    }
    TestEqual(TEXT("无关旧包不能进入新杯"), NewCup->GetPreciseVolumeML(), 0.0);
    TestEqual(TEXT("无关旧包独立结算 5 mL"), S->GetBalance().SpilledML, Spilled + 5.0);
    TestEqual(TEXT("模拟后无旧尾流残留"), S->GetBalance().InFlightML, 0.0);
    S->Simulate(Horizon + .1);
    TestEqual(TEXT("重复模拟不二次洒出"), S->GetBalance().SpilledML, Spilled + 5.0);
    TestEqual(TEXT("逐帧模拟和 Tick 后冷却自然归零"), NewCup->RestoreReceiveCooldown, 0.f);
    const FVector Mouth = NewCup->PourEntryPoint->GetComponentLocation();
    TestEqual(TEXT("冷却到期后新发射 5 mL"), S->EmitVolume(Unrelated, 5.0,
        Mouth + FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 5.0);
    S->Simulate(.3);
    TestEqual(TEXT("恢复杯不是永久禁止接液"), NewCup->GetIngredientVolumeML(MilkId), 5.0);
    Capture(Rows, TEXT("newTailAcceptedAfterCooldown"), T, Unrelated);
    CheckBalanced(*this, S);
    return SaveGroup(*this, TEXT("FullRestoreSpentIntactAndBroken"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrozenCupOrderTest,
    "CyberCafe.Workbench.FrozenCupPreservedAndSameOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFrozenCupOrderTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    const auto CupPose = SupplyPose(30);
    auto* Cup = T.Vessel<ACupActor>(250.f, 0.f, CupPose);
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, SupplyPose(70));
    auto* OtherCup = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(110));
    T.Bind(Cup, Bottle);
    T.Manager->RestoreTargets.Add(OtherCup);
    auto* Director = T.Director(OtherCup, Bottle);
    auto* Recipe = NewObject<UDrinkRecipe>(T.World);
    Recipe->RecipeId = TEXT("WorkbenchMilk");
    Recipe->MinVolumeML = 200;
    Recipe->MaxVolumeML = 200;
    Recipe->TargetVolumeML = 200;
    Recipe->AcceptedCupClass = ACupActor::StaticClass();
    Recipe->Rules.Empty();
    FDrinkIngredientRule Rule;
    Rule.IngredientId = MilkId;
    Rule.MinFraction = 1;
    Rule.MaxFraction = 1;
    Recipe->Rules.Add(Rule);
    auto* Spot = T.World->SpawnActor<AServingSpotActor>();
    Spot->SetActorLocation(FVector(30, 50, 60));
    Spot->DetectBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Spot->SetActiveRecipe(Recipe);
    T.BeginPlay();
    Director->StartTutorial();
    int32 StepBefore = -1;
    if (!ReadStepIndex(*this, Director, StepBefore)) return false;
    TestEqual(TEXT("公开启动教学到非零步骤"), StepBefore, 1);
    TestEqual(TEXT("经理捕获后才装入交付液 200 mL"), Cup->AddIngredientLiquid(200.f, MilkId, FLinearColor::White), 200.f);
    Cup->SetActorLocation(Spot->GetActorLocation());
    if (!TestTrue(TEXT("真实配方提交成功并冻结"), Spot->TrySubmitCup(Cup))) return false;
    Director->LastServedCup = Spot->GetServedCup();
    const auto FrozenPose = Cup->GetActorTransform();
    const auto Revision = Cup->GetLiquidRevision();
    const bool FrozenGrabActive = Cup->GrabComp->IsActive();
    const bool FrozenVisible = Cup->ContainerMesh->IsVisible();
    const auto FrozenGrabType = Cup->GrabComp->GrabType;
    const auto Validation = Spot->GetLastValidation();
    const auto OrderSerial = Spot->GetOrderSerial();
    const auto AcceptedSerial = Spot->GetAcceptedOrderSerial();
    const auto Steps = Director->Steps;
    TArray<FString> Rows;
    Capture(Rows, TEXT("submittedFrozenCup"), T, Cup);
    const auto Before = T.Transfers()->GetBalance();
    if (!TestTrue(TEXT("交付杯移开 50 cm 后材料恢复成功"), T.Manager->RestoreWorkbench())) return false;
    if (!TestTrue(TEXT("原交付杯身份仍存活且冻结"), IsValid(Cup)
        && !Cup->IsActorBeingDestroyed() && Cup->IsLiquidFrozen()))
    {
        Capture(Rows, TEXT("frozenCupUnexpectedlyRemoved"), T);
        SaveGroup(*this, TEXT("FrozenCupPreservedAndSameOrder"), Rows);
        return false;
    }
    TestEqual(TEXT("交付液和成分保留 200 mL"), Cup->GetPreciseVolumeML(), 200.0);
    TestEqual(TEXT("交付 Milk 快照不变"), Cup->GetIngredientVolumeML(MilkId), 200.0);
    TestEqual(TEXT("冻结 revision 不变"), Cup->GetLiquidRevision(), Revision);
    TestEqual(TEXT("交付杯抓取激活状态不被解锁"), Cup->GrabComp->IsActive(), FrozenGrabActive);
    TestEqual(TEXT("交付杯抓取类型不被解锁"), Cup->GrabComp->GrabType, FrozenGrabType);
    TestEqual(TEXT("交付杯可见状态不变"), Cup->ContainerMesh->IsVisible(), FrozenVisible);
    TestFalse(TEXT("原交付杯不误标成替换供给"), Cup->bInitialVolumeIsReplacement);
    TestTrue(TEXT("冻结位姿及吸附关系不变"), Cup->GetActorTransform().Equals(FrozenPose) && Cup->IsAttachedTo(Spot));
    TestTrue(TEXT("交付引用完全保留"), Spot->GetServedCup() == Cup && Director->LastServedCup.Get() == Cup);
    TestTrue(TEXT("保持同一配方和已交付订单"), Spot->ActiveRecipe.Get() == Recipe
        && Spot->HasBeenServed() && Spot->HasAcceptedCup(Cup, Recipe->RecipeId));
    TestEqual(TEXT("订单序号不重置"), Spot->GetOrderSerial(), OrderSerial);
    TestEqual(TEXT("已验收订单序号不变"), Spot->GetAcceptedOrderSerial(), AcceptedSerial);
    TestEqual(TEXT("配方验收结果体积不变"), Spot->GetLastValidation().TotalML, Validation.TotalML);
    TestEqual(TEXT("配方验收结果 revision 不变"), Spot->GetLastValidation().LiquidRevision, Validation.LiquidRevision);
    TestEqual(TEXT("配方验收结果标识不变"), Spot->GetLastValidation().RecipeId, Validation.RecipeId);
    TestEqual(TEXT("配方验收接受状态不变"), Spot->GetLastValidation().bAccepted, Validation.bAccepted);
    TestEqual(TEXT("已验收成分快照不变"), Spot->GetLastValidation().Composition.Get(MilkId), Validation.Composition.Get(MilkId));
    TestEqual(TEXT("冻结杯不作为移除量"), T.Transfers()->GetBalance().RemovedML, Before.RemovedML + 100.0);
    TestEqual(TEXT("历史 Initial 保留"), T.Transfers()->GetBalance().InitialML, Before.InitialML);
    TestEqual(TEXT("仅原始奶瓶注入 100 mL"), T.Transfers()->GetBalance().InjectedML, Before.InjectedML + 100.0);
    TestTrue(TEXT("Steps 对象和顺序保留"), Director->Steps == Steps);
    TestTrue(TEXT("未交付杯教学 RequiredActor 迁移"), Cast<UTutorialStep_Grab>(Director->Steps[1].Get())->RequiredActor.Get() == T.Manager->GetCurrent(2));
    int32 StepAfter = -1;
    if (!ReadStepIndex(*this, Director, StepAfter)) return false;
    TestEqual(TEXT("材料恢复不推进或重启教学步骤"), StepAfter, StepBefore);
    TestFalse(TEXT("当前抓杯步骤不被材料恢复自动完成"), Director->Steps[1]->IsCompleted());
    if (!CheckNewSlot(*this, T, 0, Cup, ACupActor::StaticClass(), CupPose, 250.f, 0.0)) return false;
    TestEqual(TEXT("没有无关尾流时新杯无需冷却"), T.Manager->GetCurrent(0)->RestoreReceiveCooldown, 0.f);
    Rows.Add(FString::Printf(TEXT("{\"orderSerial\":%u,\"acceptedOrderSerial\":%u,\"stepBefore\":%d,\"stepAfter\":%d,\"servedCupId\":%u,\"validationML\":%.17g}"),
        Spot->GetOrderSerial(), Spot->GetAcceptedOrderSerial(), StepBefore, StepAfter,
        Spot->GetServedCup()->GetUniqueID(), Spot->GetLastValidation().TotalML));
    Capture(Rows, TEXT("sameFrozenCupSameOrder"), T, Cup);
    CheckBalanced(*this, T.Transfers());
    return SaveGroup(*this, TEXT("FrozenCupPreservedAndSameOrder"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeldPullGuardTest,
    "CyberCafe.Workbench.HeldPullGuardAndFailureNoLedgerMutation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeldPullGuardTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    auto* Cup = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(30));
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, SupplyPose(70));
    auto* Unrelated = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(300));
    T.Bind(Cup, Bottle);
    T.BeginPlay();
    auto* Left = T.Controller(EControllerHand::Left);
    auto* Right = T.Controller(EControllerHand::Right);
    if (!TestTrue(TEXT("指定杯通过真实近距 API 拿起"), Cup->GrabComp->TryGrab(Left))) return false;
    TestTrue(TEXT("近距抓取确实处于 Held"), Cup->GrabComp->IsHeld());
    TArray<FString> Rows;
    const auto Before = T.Transfers()->GetBalance();
    const int32 Containers = CountContainers(T.World);
    const auto HeldPose = Cup->GetActorTransform();
    Capture(Rows, TEXT("heldBeforeReject"), T, Cup);
    TestFalse(TEXT("指定供给被持有时拒绝恢复"), T.Manager->RestoreWorkbench());
    CheckSameBalance(*this, Before, T.Transfers()->GetBalance());
    TestTrue(TEXT("拒绝不拉走手中杯"), Cup->GrabComp->IsHeld()
        && Cup->GrabComp->GetHoldingController() == Left && Cup->GetActorTransform().Equals(HeldPose));
    TestTrue(TEXT("拒绝不替换旧杯瓶"), T.Manager->GetCurrent(0) == Cup && T.Manager->GetCurrent(1) == Bottle);
    TestEqual(TEXT("拒绝没有创建新供给"), CountContainers(T.World), Containers);
    TestEqual(TEXT("拒绝恢复代数为零"), T.Manager->GetRestoreGeneration(), 0);
    TestFalse(TEXT("拒绝状态文案非空"), T.Manager->RestoreStatus.IsEmpty());
    TestFalse(TEXT("旧杯未被强制破碎"), Cup->IsBroken());
    Capture(Rows, TEXT("heldRejected"), T, Cup);
    Cup->GrabComp->ReleaseForBreak();
    TestFalse(TEXT("公开释放确实清理 Held"), Cup->GrabComp->IsHeld());
    if (!TestTrue(TEXT("指定瓶通过公开 API 进入拉取"), Bottle->GrabComp->TryPull(EControllerHand::Left))) return false;
    TestTrue(TEXT("拉取状态可公开查询"), Bottle->GrabComp->IsPulled());
    TestFalse(TEXT("指定供给正在拉取时拒绝恢复"), T.Manager->RestoreWorkbench());
    CheckSameBalance(*this, Before, T.Transfers()->GetBalance());
    TestTrue(TEXT("失败不强制停止拉取"), Bottle->GrabComp->IsPulled());
    TestEqual(TEXT("拉取拒绝不创建供给"), CountContainers(T.World), Containers);
    for (int32 I = 0; I < 5; ++I)
        TestFalse(TEXT("拉取期间长按达到阈值仍失败"), T.Manager->AdvanceRestoreHold(.2f, true));
    TestEqual(TEXT("失败长按不推进代数"), T.Manager->GetRestoreGeneration(), 0);
    CheckSameBalance(*this, Before, T.Transfers()->GetBalance());
    Bottle->GrabComp->ReleaseForBreak();
    TestFalse(TEXT("公开释放确实清理 Pull"), Bottle->GrabComp->IsPulled());
    for (int32 I = 0; I < 5; ++I)
        TestFalse(TEXT("失败后持续接触也锁存，放下瓶不自动重试"), T.Manager->AdvanceRestoreHold(.2f, true));
    CheckSameBalance(*this, Before, T.Transfers()->GetBalance());
    T.Manager->AdvanceRestoreHold(0.f, false);
    auto* Occupant = T.Vessel<ACupActor>(100.f, 0.f, SupplyPose(30));
    const auto OccupiedBalance = T.Transfers()->GetBalance();
    TestFalse(TEXT("供给位被另一原生容器占据时预检失败"), T.Manager->RestoreWorkbench());
    CheckSameBalance(*this, OccupiedBalance, T.Transfers()->GetBalance());
    TestEqual(TEXT("占位拒绝没有额外预建供给"), CountContainers(T.World), Containers + 1);
    TestEqual(TEXT("占位拒绝不推进代数"), T.Manager->GetRestoreGeneration(), 0);
    TestTrue(TEXT("占位拒绝保持完整旧供给身份"), T.Manager->GetCurrent(0) == Cup
        && T.Manager->GetCurrent(1) == Bottle && !Cup->IsBroken() && !Bottle->IsBroken());
    Capture(Rows, TEXT("occupiedSupplyPositionRejected"), T, Occupant);
    Occupant->SetActorLocation(FVector(30, -50, 60));
    if (!TestTrue(TEXT("另一手真实持有不在供给列表内的物体"), Unrelated->GrabComp->TryGrab(Right))) return false;
    const auto UnrelatedPose = Unrelated->GetActorTransform();
    Capture(Rows, TEXT("unrelatedHeldBeforeRestore"), T, Unrelated);
    if (!TestTrue(TEXT("无关手持物不阻断材料恢复"), T.Manager->RestoreWorkbench())) return false;
    TestTrue(TEXT("无关手持物未释放、未移位、未删除"), IsValid(Unrelated)
        && !Unrelated->IsActorBeingDestroyed() && Unrelated->GrabComp->IsHeld()
        && Unrelated->GrabComp->GetHoldingController() == Right
        && Unrelated->GetActorTransform().Equals(UnrelatedPose));
    TestEqual(TEXT("成功才推进一代"), T.Manager->GetRestoreGeneration(), 1);
    TestEqual(TEXT("此前失败没有额外注入"), T.Transfers()->GetBalance().InjectedML, 100.0);
    Capture(Rows, TEXT("unrelatedStillHeld"), T, Unrelated);
    Unrelated->GrabComp->ReleaseForBreak();
    CheckBalanced(*this, T.Transfers());
    return SaveGroup(*this, TEXT("HeldPullGuardAndFailureNoLedgerMutation"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLongHoldTest,
    "CyberCafe.Workbench.LongHoldLatchesAndCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLongHoldTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    auto* Cup = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(30));
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, SupplyPose(70));
    T.Bind(Cup, Bottle);
    auto* Director = T.Director(Cup, Bottle);
    T.BeginPlay();
    Director->StartTutorial();
    const auto Steps = Director->Steps;
    int32 StepBefore = -1;
    if (!ReadStepIndex(*this, Director, StepBefore)) return false;
    TArray<FString> Rows;
    const auto Initial = T.Transfers()->GetBalance();
    TestFalse(TEXT("触碰 .4 秒不足一秒"), T.Manager->AdvanceRestoreHold(.4f, true));
    TestTrue(TEXT("真实进度为 .4"), FMath::IsNearlyEqual(T.Manager->GetRestoreProgress(), .4f));
    Capture(Rows, TEXT("partialHold"), T);
    TestFalse(TEXT("离开接触只取消不恢复"), T.Manager->AdvanceRestoreHold(0.f, false));
    TestEqual(TEXT("取消清空进度"), T.Manager->GetRestoreProgress(), 0.f);
    TestEqual(TEXT("取消保持零代"), T.Manager->GetRestoreGeneration(), 0);
    CheckSameBalance(*this, Initial, T.Transfers()->GetBalance());
    int32 SuccessfulTriggers = 0;
    for (int32 I = 0; I < 5; ++I)
        SuccessfulTriggers += T.Manager->AdvanceRestoreHold(.2f, true) ? 1 : 0;
    TestEqual(TEXT("连续五个有效 .2 秒只触发一次"), SuccessfulTriggers, 1);
    TestEqual(TEXT("第一次长按代数为一"), T.Manager->GetRestoreGeneration(), 1);
    TestEqual(TEXT("第一次长按只注入 100 mL"), T.Transfers()->GetBalance().InjectedML, 100.0);
    auto* FirstCup = T.Manager->GetCurrent(0);
    const auto AfterFirst = T.Transfers()->GetBalance();
    for (int32 I = 0; I < 5; ++I)
        TestFalse(TEXT("持续触碰锁存不再创建供给"), T.Manager->AdvanceRestoreHold(.2f, true));
    TestTrue(TEXT("锁存保留首次新杯身份"), T.Manager->GetCurrent(0) == FirstCup);
    TestEqual(TEXT("持续触碰代数仍为一"), T.Manager->GetRestoreGeneration(), 1);
    CheckSameBalance(*this, AfterFirst, T.Transfers()->GetBalance());
    Capture(Rows, TEXT("latchedFirstGeneration"), T);
    T.Manager->AdvanceRestoreHold(0.f, false);
    SuccessfulTriggers = 0;
    for (int32 I = 0; I < 5; ++I)
        SuccessfulTriggers += T.Manager->AdvanceRestoreHold(.2f, true) ? 1 : 0;
    TestEqual(TEXT("松开后再次长按只触发一次"), SuccessfulTriggers, 1);
    TestEqual(TEXT("第二次长按代数为二"), T.Manager->GetRestoreGeneration(), 2);
    TestEqual(TEXT("第二次长按累计注入 200 mL"), T.Transfers()->GetBalance().InjectedML, 200.0);
    TestEqual(TEXT("多次恢复不重置 Initial"), T.Transfers()->GetBalance().InitialML, Initial.InitialML);
    TestTrue(TEXT("第二代使用另一只新杯"), T.Manager->GetCurrent(0) != FirstCup);
    TestTrue(TEXT("教学 Steps 对象和顺序未改"), Director->Steps == Steps);
    TestTrue(TEXT("当前教学引用迁移到第二代"), Cast<UTutorialStep_Grab>(Director->Steps[1].Get())->RequiredActor.Get() == T.Manager->GetCurrent(0));
    int32 StepAfter = -1;
    if (!ReadStepIndex(*this, Director, StepAfter)) return false;
    TestEqual(TEXT("两次恢复不重启或推进原步骤号"), StepAfter, StepBefore);
    TestFalse(TEXT("材料恢复不是抓取完成事件"), Director->Steps[1]->IsCompleted());
    Capture(Rows, TEXT("secondGenerationSameStep"), T);
    CheckBalanced(*this, T.Transfers());
    return SaveGroup(*this, TEXT("LongHoldLatchesAndCancel"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCupIceScoopTest,
    "CyberCafe.Workbench.CupIceAndScoopResetIdentityReferenceMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCupIceScoopTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    const auto CupPose = SupplyPose(30);
    const auto ScoopPose = SupplyPose(200);
    auto* Cup = T.Vessel<ACupActor>(200.f, 0.f, CupPose);
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, SupplyPose(70));
    T.Bind(Cup, Bottle);
    auto* Scoop = T.World->SpawnActorDeferred<AIceScoopActor>(AIceScoopActor::StaticClass(), ScoopPose,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Scoop->Mesh->SetStaticMesh(T.BasicCube);
    Scoop->Mesh->SetSimulatePhysics(false);
    // 瞬态原生音效仅验证引用保留；World 已禁止音频播放，不加载项目音效资产。
    Scoop->ScoopSound = NewObject<USoundWave>(Scoop);
    ConfigureGrab(Scoop->Grab.Get());
    Scoop->FinishSpawning(ScoopPose);
    auto* Bucket = T.World->SpawnActor<AIceBucketActor>();
    Bucket->SetActorLocation(Scoop->Mesh->GetComponentTransform().TransformPosition(Scoop->BowlCenter) - FVector(0, 0, 12));
    auto* Station = T.World->SpawnActor<AIceStationActor>();
    Station->Cup = Cup;
    Station->Scoop = Scoop;
    Station->Bucket = Bucket;
    Station->CupInteriorRadiusCM = 3.3f;
    Station->CupInteriorDepthCM = 8.f;
    Station->ClinkSound = NewObject<USoundWave>(Station);
    T.Manager->RestoreScoop = Scoop;
    auto* Director = T.Director(Cup, Bottle);
    T.BeginPlay();
    Scoop->Mesh->SetSimulatePhysics(false);
    Scoop->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ConfigureGrab(Scoop->Grab.Get());
    auto* OldIce = Cup->FindComponentByClass<UCupIceComponent>();
    if (!TestNotNull(TEXT("原生冰站 BeginPlay 安装杯冰组件"), OldIce)) return false;
    auto* OwnedCube = T.World->SpawnActor<AIceCubeActor>();
    OwnedCube->Mesh->SetSimulatePhysics(false);
    OwnedCube->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    const auto Entry = Cup->PourEntryPoint->GetComponentTransform();
    if (!TestTrue(TEXT("真实穿过厘米级入口接受杯内冰"), OldIce->TryAccept(OwnedCube,
        Entry.TransformPosition(FVector(0, 0, 3)), Entry.TransformPosition(FVector(0, 0, -2))))) return false;
    TestEqual(TEXT("旧杯有一块真实杯内冰"), OldIce->GetIceCount(), 1);
    if (!TestTrue(TEXT("冰铲通过真实桶接口装载"), Scoop->TryLoadFromBucket())) return false;
    TestEqual(TEXT("原生冰铲装载三块"), Scoop->GetLoadedCount(), 3);
    TArray<TWeakObjectPtr<AIceCubeActor>> Loaded;
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
        if (It->State == EIceState::InScoop && It->Scoop.Get() == Scoop) Loaded.Add(*It);
    TestEqual(TEXT("三块装载冰都能按公开来源身份找到"), Loaded.Num(), 3);
    auto* FreeCube = T.World->SpawnActor<AIceCubeActor>();
    FreeCube->SetActorLocation(FVector(300, 100, 60));
    FreeCube->Mesh->SetSimulatePhysics(false);
    FreeCube->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    const auto FreePose = FreeCube->GetActorTransform();
    const auto BucketPose = Bucket->GetActorTransform();
    const auto IceClass = Scoop->IceClass;
    auto* ScoopSound = Scoop->ScoopSound.Get();
    auto* GrabIdentity = Scoop->Grab.Get();
    Scoop->SetActorLocation(FVector(200, 50, 60));
    TArray<FString> Rows;
    Capture(Rows, TEXT("cupIceAndLoadedScoop"), T);
    const auto LiveBefore = T.World->GetSubsystem<UIceWorldSubsystem>()->GetLiveCount();
    const TWeakObjectPtr<AIceCubeActor> OwnedIdentity(OwnedCube);
    const auto BeforeGuard = T.Transfers()->GetBalance();
    if (!TestTrue(TEXT("冰铲通过真实 API 进入拉取"), Scoop->Grab->TryPull(EControllerHand::Left))) return false;
    TestFalse(TEXT("指定冰铲拉取时拒绝整批恢复"), T.Manager->RestoreWorkbench());
    CheckSameBalance(*this, BeforeGuard, T.Transfers()->GetBalance());
    TestEqual(TEXT("拉取拒绝保留三块装载冰"), Scoop->GetLoadedCount(), 3);
    TestEqual(TEXT("拉取拒绝保留原杯冰"), OldIce->GetIceCount(), 1);
    TestEqual(TEXT("冰铲拉取拒绝不推进代数"), T.Manager->GetRestoreGeneration(), 0);
    TestTrue(TEXT("拒绝不强制释放铲"), Scoop->Grab->IsPulled());
    Capture(Rows, TEXT("pulledScoopRejectedWithoutDisposal"), T);
    Scoop->Grab->ReleaseForBreak();
    if (!TestTrue(TEXT("有杯内冰和装载冰时一键恢复成功"), T.Manager->RestoreWorkbench())) return false;
    auto* NewCup = Cast<ACupActor>(T.Manager->GetCurrent(0));
    if (!TestNotNull(TEXT("新杯身份有效"), NewCup)) return false;
    auto* NewIce = NewCup->FindComponentByClass<UCupIceComponent>();
    if (!TestNotNull(TEXT("新杯重新安装杯冰支持"), NewIce)) return false;
    TestTrue(TEXT("新杯身份、组件及冰站引用迁移"), NewCup != Cup && NewIce != OldIce && Station->Cup.Get() == NewCup && NewIce->GetCup() == NewCup);
    TestEqual(TEXT("新杯清空权威液量"), NewCup->GetPreciseVolumeML(), 0.0);
    TestEqual(TEXT("新杯无冰"), NewIce->GetIceCount(), 0);
    TestEqual(TEXT("杯冰半径保持 3.3 cm"), NewIce->InteriorRadiusCM, 3.3f);
    TestEqual(TEXT("杯冰深度保持 8 cm"), NewIce->InteriorDepthCM, 8.f);
    TestEqual(TEXT("杯冰上限保持六块"), NewIce->MaxCupIce, 6);
    TestNotNull(TEXT("非空原生碰冰音效引用保留"), NewIce->ClinkSound.Get());
    TestTrue(TEXT("保留冰站音效引用"), NewIce->ClinkSound == Station->ClinkSound);
    TestTrue(TEXT("新杯恢复捕获位姿"), NewCup->GetActorTransform().Equals(CupPose));
    TestTrue(TEXT("新杯可抓取且非冻结破碎"), NewCup->GrabComp->IsActive() && !NewCup->IsBroken() && !NewCup->IsLiquidFrozen());
    TestTrue(TEXT("教学 RequiredActor 同步迁移"), Cast<UTutorialStep_Grab>(Director->Steps[1].Get())->RequiredActor.Get() == NewCup);
    TestTrue(TEXT("冰铲自身及抓取组件身份保留"), T.Manager->RestoreScoop.Get() == Scoop && Scoop->Grab.Get() == GrabIdentity);
    TestTrue(TEXT("冰铲回到经理初始捕获位姿"), Scoop->GetActorTransform().Equals(ScoopPose));
    TestEqual(TEXT("冰铲装载清零"), Scoop->GetLoadedCount(), 0);
    TestTrue(TEXT("桶及冰类引用保留"), Scoop->Bucket.Get() == Bucket && Scoop->IceClass == IceClass);
    TestTrue(TEXT("非空原生铲冰音效引用保留"), ScoopSound && Scoop->ScoopSound.Get() == ScoopSound);
    TestTrue(TEXT("桶的位姿不改"), Bucket->GetActorTransform().Equals(BucketPose));
    // Destroy 与 GC 不是同一时刻；检查销毁状态，不要求弱指针立即失效。
    TestTrue(TEXT("旧杯所属冰已销毁"), !OwnedIdentity.IsValid() || OwnedIdentity->IsActorBeingDestroyed());
    for (const auto& Ice : Loaded)
        TestTrue(TEXT("旧铲装载冰已销毁而非散落"), !Ice.IsValid() || Ice->IsActorBeingDestroyed());
    TestTrue(TEXT("无关散冰原身份、位置和自由态保留"), IsValid(FreeCube) && !FreeCube->IsActorBeingDestroyed()
        && FreeCube->State == EIceState::Free && FreeCube->GetActorTransform().Equals(FreePose));
    TestEqual(TEXT("只清理一块杯内冰和三块装载冰"), T.World->GetSubsystem<UIceWorldSubsystem>()->GetLiveCount(), LiveBefore - 4);
    Rows.Add(FString::Printf(TEXT("{\"cupIceCount\":%d,\"scoopLoadedCount\":%d,\"liveIceBefore\":%d,\"liveIceAfter\":%d,\"scoopId\":%u,\"freeIceId\":%u}"),
        NewIce->GetIceCount(), Scoop->GetLoadedCount(), LiveBefore,
        T.World->GetSubsystem<UIceWorldSubsystem>()->GetLiveCount(), Scoop->GetUniqueID(), FreeCube->GetUniqueID()));
    Capture(Rows, TEXT("emptyCupSameScoopUnrelatedIceRetained"), T);
    SuppressVesselSimulation(NewCup);
    CheckBalanced(*this, T.Transfers());
    return SaveGroup(*this, TEXT("CupIceAndScoopResetIdentityReferenceMigration"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrepareAtomicRejectTest,
    "CyberCafe.Workbench.PrepareRejectFrozenNullForeignBatchAtomic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPrepareAtomicRejectTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    FWorkbenchWorld ForeignWorld;
    auto* Cup = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(30));
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, SupplyPose(70));
    auto* Frozen = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(150));
    auto* Foreign = ForeignWorld.Vessel<ABottleActor>(50.f, 1.f, SupplyPose(30));
    T.Bind(Cup, Bottle);
    T.BeginPlay();
    ForeignWorld.BeginPlay();
    Frozen->AddIngredientLiquid(20.f, MilkId, FLinearColor::White);
    Frozen->FreezeLiquidForServing();
    Bottle->bCanPour = true;
    Bottle->bAcceptLiquidFromOthers = true;
    Cup->bCanPour = true;
    TestEqual(TEXT("原有效源发射真实在途 20 mL"), T.Transfers()->EmitVolume(Bottle, 20.0,
        FVector(30, 0, 80), FVector(0, 0, -50), FVector::ZeroVector, .5), 20.0);
    const auto Before = T.Transfers()->GetBalance();
    const auto ForeignBefore = ForeignWorld.Transfers()->GetBalance();
    const auto BottlePose = Bottle->GetActorTransform();
    const int32 BottleRevision = Bottle->GetLiquidRevision();
    const int32 CupRevision = Cup->GetLiquidRevision();
    const int32 FrozenRevision = Frozen->GetLiquidRevision();
    const auto Containers = CountContainers(T.World);
    TArray<FString> Rows;
    Capture(Rows, TEXT("beforeInvalidBatches"), T, Frozen);
    const TArray<TArray<ALiquidContainerActor*>> InvalidBatches = {
        { Bottle, nullptr }, { Bottle, Frozen }, { Bottle, Foreign },
        { Cup, Bottle, nullptr }, { Cup, Bottle, Frozen }, { Cup, Bottle, Foreign }
    };
    for (const auto& Batch : InvalidBatches)
    {
        TestTrue(TEXT("预检可调用且不修改账本"), T.Transfers()->CanPrepareWorkbenchRestore());
        CheckSameBalance(*this, Before, T.Transfers()->GetBalance());
        TestFalse(TEXT("含无效、冻结或跨世界成员的批次全量拒绝"), T.Transfers()->PrepareWorkbenchRestore(Batch));
        CheckSameBalance(*this, Before, T.Transfers()->GetBalance());
        CheckSameBalance(*this, ForeignBefore, ForeignWorld.Transfers()->GetBalance());
        TestTrue(TEXT("晚发现无效成员不提前停掉有效源"), Bottle->bCanPour && Bottle->bAcceptLiquidFromOthers && Cup->bCanPour);
        TestFalse(TEXT("批次拒绝不产生 pouring"), Bottle->bIsPouring);
        TestEqual(TEXT("有效瓶余液保持 80 mL"), Bottle->GetPreciseVolumeML(), 80.0);
        TestEqual(TEXT("有效瓶成分保持 80 mL"), Bottle->GetIngredientVolumeML(MilkId), 80.0);
        TestEqual(TEXT("有效瓶 revision 不变"), Bottle->GetLiquidRevision(), BottleRevision);
        TestEqual(TEXT("有效杯 revision 不变"), Cup->GetLiquidRevision(), CupRevision);
        TestEqual(TEXT("冻结成员 revision 不变"), Frozen->GetLiquidRevision(), FrozenRevision);
        TestEqual(TEXT("冻结成员仍为 20 mL"), Frozen->GetPreciseVolumeML(), 20.0);
        TestTrue(TEXT("有效源位姿不变"), Bottle->GetActorTransform().Equals(BottlePose));
        TestEqual(TEXT("无新供给创建"), CountContainers(T.World), Containers);
        TestEqual(TEXT("经理恢复代数仍为零"), T.Manager->GetRestoreGeneration(), 0);
        TestTrue(TEXT("公开当前供给未替换"), T.Manager->GetCurrent(0) == Cup && T.Manager->GetCurrent(1) == Bottle);
        Capture(Rows, TEXT("invalidBatchRejectedAtomically"), T, Frozen);
    }
    CheckBalanced(*this, T.Transfers());
    return SaveGroup(*this, TEXT("PrepareRejectFrozenNullForeignBatchAtomic"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestroyedOldSourceTest,
    "CyberCafe.Workbench.DestroyedNativeOldNoDoubleRemoved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestroyedOldSourceTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    auto* Cup = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(30));
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, SupplyPose(70));
    T.Bind(Cup, Bottle);
    T.BeginPlay();
    TArray<FString> Rows;
    TestEqual(TEXT("旧原生源发射真实延迟尾流 20 mL"), T.Transfers()->EmitVolume(Bottle, 20.0,
        FVector(30, 0, 80), FVector(0, 0, -50), FVector::ZeroVector, .25), 20.0);
    Capture(Rows, TEXT("beforeOldNativeDestroy"), T);
    const auto Before = T.Transfers()->GetBalance();
    if (!TestTrue(TEXT("原生旧瓶通过真实 Destroy 结束生命周期"), Bottle->Destroy())) return false;
    TestNull(TEXT("已销毁瓶不再作为当前有效容器"), T.Manager->GetCurrent(1));
    TestEqual(TEXT("EndPlay 和 Destroyed 只移除一次剩余 80 mL"), T.Transfers()->GetBalance().RemovedML, Before.RemovedML + 80.0);
    TestEqual(TEXT("Destroy 不销毁已经发射的包"), T.Transfers()->GetBalance().InFlightML, 20.0);
    Capture(Rows, TEXT("oldNativeDestroyed"), T);
    const auto Removed = T.Transfers()->GetBalance().RemovedML;
    if (!TestTrue(TEXT("旧源已销毁仍可恢复捕获的供给"), T.Manager->RestoreWorkbench())) return false;
    TestEqual(TEXT("恢复已销毁旧源不重复记 Removed"), T.Transfers()->GetBalance().RemovedML, Removed);
    TestEqual(TEXT("原始 Initial 不被重置"), T.Transfers()->GetBalance().InitialML, Before.InitialML);
    TestEqual(TEXT("新原生瓶只注入一次 100 mL"), T.Transfers()->GetBalance().InjectedML, Before.InjectedML + 100.0);
    TestEqual(TEXT("新源数量为一个"), CountSources(T.World), 1);
    TestEqual(TEXT("槽位数量仍为两个"), T.Manager->GetRestoreSlotCount(), 2);
    TestEqual(TEXT("恢复推进一代"), T.Manager->GetRestoreGeneration(), 1);
    TestEqual(TEXT("失效源包不被误当作当前新源包清理"), T.Transfers()->GetBalance().InFlightML, 20.0);
    TestEqual(TEXT("失效源包身份仍独立保留"), T.Transfers()->GetBalance().PacketCount, 1);
    Capture(Rows, TEXT("restoredAfterDestroyedOld"), T);
    CheckBalanced(*this, T.Transfers());
    auto* NewCup = T.Manager->GetCurrent(0);
    if (!TestNotNull(TEXT("尾流隔离目标新杯有效"), NewCup)) return false;
    SuppressVesselSimulation(NewCup);
    SuppressVesselSimulation(T.Manager->GetCurrent(1));
    const double Horizon = T.Transfers()->GetUnsettledFlightHorizon();
    TestTrue(TEXT("失效旧源尾流仍有完整冷却保护"), NewCup->RestoreReceiveCooldown >= Horizon - .01);
    T.Transfers()->Simulate(Horizon + .1);
    TestEqual(TEXT("失效旧源尾流不污染新杯"), NewCup->GetPreciseVolumeML(), 0.0);
    TestEqual(TEXT("失效源尾流仅洒出一次"), T.Transfers()->GetBalance().SpilledML, Before.SpilledML + 20.0);
    TestEqual(TEXT("尾流结算不重复移除旧液"), T.Transfers()->GetBalance().RemovedML, Removed);
    T.Transfers()->Simulate(Horizon + .1);
    TestEqual(TEXT("重复模拟仍只洒出 20 mL"), T.Transfers()->GetBalance().SpilledML, Before.SpilledML + 20.0);
    Capture(Rows, TEXT("destroyedSourceTailSettledOnce"), T);
    CheckBalanced(*this, T.Transfers());
    return SaveGroup(*this, TEXT("DestroyedNativeOldNoDoubleRemoved"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapCleanupGuardTest,
    "CyberCafe.Workbench.RelatedCapHeldGuardAndCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCapCleanupGuardTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FWorkbenchWorld T;
    auto* Cup = T.Vessel<ACupActor>(200.f, 0.f, SupplyPose(30));
    auto* Bottle = T.Vessel<ABottleActor>(200.f, .5f, SupplyPose(70));
    T.Bind(Cup, Bottle);
    auto* Cap = T.World->SpawnActor<ABottleCapActor>();
    ConfigureGrab(Cap->GrabComp.Get());
    Cap->AttachToBottle(Bottle, NAME_None);
    T.BeginPlay();
    ConfigureGrab(Cap->GrabComp.Get());
    // 通过公开 Attach/Detach 建立瓶盖归属；不假造私有 held 状态。
    auto* Left = T.Controller(EControllerHand::Left);
    if (!TestTrue(TEXT("相关盖通过真实 TryGrab 拿起"), Cap->GrabComp->TryGrab(Left))) return false;
    Cap->DetachFromBottle(Left);
    TestFalse(TEXT("相关盖通过业务 API 真正脱离瓶口"), Cap->bIsAttached);
    TestTrue(TEXT("脱离后公开来源仍为旧瓶"), Cap->OwnerBottle.Get() == Bottle);
    Cap->SetActorLocation(FVector(70, 50, 60));
    auto* UnrelatedCap = T.World->SpawnActor<ABottleCapActor>();
    ConfigureGrab(UnrelatedCap->GrabComp.Get());
    UnrelatedCap->SetActorLocation(FVector(300, 100, 60));
    auto* Right = T.Controller(EControllerHand::Right);
    if (!TestTrue(TEXT("另一手抓取无关盖"), UnrelatedCap->GrabComp->TryGrab(Right))) return false;
    TArray<FString> Rows;
    const auto Before = T.Transfers()->GetBalance();
    TestFalse(TEXT("相关盖手持时拒绝一键恢复"), T.Manager->RestoreWorkbench());
    CheckSameBalance(*this, Before, T.Transfers()->GetBalance());
    TestTrue(TEXT("拒绝不释放相关盖"), Cap->GrabComp->IsHeld());
    TestEqual(TEXT("相关盖拒绝代数不变"), T.Manager->GetRestoreGeneration(), 0);
    Capture(Rows, TEXT("relatedCapHeldRejected"), T);
    Cap->GrabComp->ReleaseForBreak();
    const TWeakObjectPtr<ABottleCapActor> CapIdentity(Cap);
    const auto UnrelatedPose = UnrelatedCap->GetActorTransform();
    if (!TestTrue(TEXT("放下相关盖后可恢复"), T.Manager->RestoreWorkbench())) return false;
    TestTrue(TEXT("清理仍归旧瓶的独立盖"), !CapIdentity.IsValid() || CapIdentity->IsActorBeingDestroyed());
    TestTrue(TEXT("无关盖身份与另一手持有关系不变"), IsValid(UnrelatedCap)
        && !UnrelatedCap->IsActorBeingDestroyed() && UnrelatedCap->GrabComp->IsHeld()
        && UnrelatedCap->GrabComp->GetHoldingController() == Right
        && UnrelatedCap->GetActorTransform().Equals(UnrelatedPose));
    auto* NewBottle = Cast<ABottleActor>(T.Manager->GetCurrent(1));
    if (!TestNotNull(TEXT("新瓶身份有效"), NewBottle)) return false;
    TestNull(TEXT("CapClass 为空的新原生瓶不凭空生盖"), NewBottle->CapActor.Get());
    TestEqual(TEXT("只注入一次初始奶量"), T.Transfers()->GetBalance().InjectedML, 100.0);
    Capture(Rows, TEXT("relatedCapRemovedUnrelatedStillHeld"), T);
    UnrelatedCap->GrabComp->ReleaseForBreak();
    CheckBalanced(*this, T.Transfers());
    return SaveGroup(*this, TEXT("RelatedCapHeldGuardAndCleanup"), Rows);
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
