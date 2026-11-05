// @trace CF-01 CF-02 CF-03 CF-04
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/WidgetComponent.h"
#include "MotionControllerComponent.h"
#include "GrabComponent.h"
#include "NiagaraComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <limits>

// 仅通过已声明的公共接口驱动；不修改生产代码、旧测试或构建配置。
namespace CyberCafeCoffeeBrewTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double Tolerance = 1.e-4;
const FName Water(TEXT("Water"));
const FName Coffee(TEXT("Coffee"));
const FName Milk(TEXT("Milk"));
const FName Unknown(TEXT("Unknown"));

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual,
    double Expected, double AllowedError = Tolerance)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"),
        *Label, Actual, Expected), FMath::IsFinite(Actual)
        && FMath::Abs(Actual - Expected) <= AllowedError);
}

void CheckBalance(FAutomationTestBase& Test, ULiquidTransferSubsystem* Transfer)
{
    const FLiquidBalance B = Transfer->GetBalance();
    Near(Test, TEXT("真实转移账本误差为零"), B.ErrorML, 0.0);
    Test.TestTrue(TEXT("账本各项有限且非负"), FMath::IsFinite(B.InitialML)
        && FMath::IsFinite(B.InjectedML) && FMath::IsFinite(B.RemovedML)
        && FMath::IsFinite(B.ContainerML) && FMath::IsFinite(B.InFlightML)
        && FMath::IsFinite(B.SpilledML) && B.InitialML >= 0 && B.InjectedML >= 0
        && B.RemovedML >= 0 && B.ContainerML >= 0 && B.InFlightML >= 0 && B.SpilledML >= 0);
}

// JSON 数值来自运行时读数；失败产生的非有限值记为 null，不伪装成零。
struct FMeasurement
{
    TArray<FString> Fields;
    explicit FMeasurement(const TCHAR* Case)
    {
        Fields.Add(FString::Printf(TEXT("\"case\":\"%s\""), Case));
    }
    void Number(const TCHAR* Key, double Value)
    {
        Fields.Add(FString::Printf(TEXT("\"%s\":%s"), Key,
            FMath::IsFinite(Value) ? *FString::Printf(TEXT("%.12g"), Value) : TEXT("null")));
    }
    void Boolean(const TCHAR* Key, bool Value)
    {
        Fields.Add(FString::Printf(TEXT("\"%s\":%s"), Key, Value ? TEXT("true") : TEXT("false")));
    }
    FString JSON() const { return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}"); }
};

struct FGroupEvidence
{
    FAutomationTestBase& Test;
    FString Group;
    TArray<FString> Samples;
    FGroupEvidence(FAutomationTestBase& InTest, const TCHAR* InGroup)
        : Test(InTest), Group(InGroup) {}
    ~FGroupEvidence()
    {
        // 只在测试实际执行时写入；文件仅汇总本进程执行过的组，不读取旧证据补齐。
        static TMap<FString, FString> ExecutedGroups;
        const FString Status = Test.HasAnyErrors() ? TEXT("Failed") : TEXT("Passed");
        ExecutedGroups.Add(Group, FString::Printf(
            TEXT("{\"group\":\"%s\",\"status\":\"%s\",\"sampleCount\":%d,\"samples\":[%s]}"),
            *Group, *Status, Samples.Num(), *FString::Join(Samples, TEXT(","))));
        TArray<FString> Keys;
        ExecutedGroups.GetKeys(Keys);
        Keys.Sort();
        TArray<FString> Groups;
        for (const FString& Key : Keys) Groups.Add(ExecutedGroups.FindChecked(Key));
        const FString JSON = FString::Printf(
            TEXT("{\"schemaVersion\":1,\"recordedAtUTC\":\"%s\",\"scope\":\"native_game_world_public_api\",\"realBlueprintsVerified\":false,\"groups\":[%s]}"),
            *FDateTime::UtcNow().ToIso8601(), *FString::Join(Groups, TEXT(",")));
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Coffee");
        const bool bDirectory = IFileManager::Get().MakeDirectory(*Directory, true);
        Test.TestTrue(TEXT("写出本次真实行为证据"), bDirectory
            && FFileHelper::SaveStringToFile(JSON, *(Directory / TEXT("behaviour.json")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    void Add(const FMeasurement& M) { Samples.Add(M.JSON()); }
    void Liquid(const TCHAR* Case, ULiquidTransferSubsystem* Transfer,
        AHandDripperActor* Source, ACupActor* Target = nullptr)
    {
        const FLiquidBalance B = Transfer->GetBalance();
        FMeasurement M(Case);
        M.Number(TEXT("initialML"), B.InitialML);
        M.Number(TEXT("injectedML"), B.InjectedML);
        M.Number(TEXT("removedML"), B.RemovedML);
        M.Number(TEXT("containerML"), B.ContainerML);
        M.Number(TEXT("inFlightML"), B.InFlightML);
        M.Number(TEXT("spilledML"), B.SpilledML);
        M.Number(TEXT("errorML"), B.ErrorML);
        M.Number(TEXT("packetCount"), B.PacketCount);
        M.Number(TEXT("sourceML"), Source->GetPreciseVolumeML());
        const FLiquidComposition Raw = Source->ALiquidContainerActor::GetComposition();
        M.Number(TEXT("sourceRawWaterML"), Raw.Get(Water));
        M.Number(TEXT("sourceRawCoffeeML"), Raw.Get(Coffee));
        M.Number(TEXT("sourceRawMilkML"), Raw.Get(Milk));
        M.Number(TEXT("groundsGrams"), Source->GetGroundsGrams());
        M.Number(TEXT("coffeeBudgetML"), Source->GetCoffeeBudgetML());
        M.Number(TEXT("drippedML"), Source->DrippedML);
        M.Number(TEXT("coffeeProducedML"), Source->CoffeeProducedML);
        M.Number(TEXT("brewGeneration"), Source->BrewGeneration);
        M.Boolean(TEXT("sourceFrozen"), Source->IsLiquidFrozen());
        M.Boolean(TEXT("sourceBroken"), Source->IsBroken());
        if (Target)
        {
            const FLiquidComposition Received = Target->ALiquidContainerActor::GetComposition();
            M.Number(TEXT("targetML"), Target->GetPreciseVolumeML());
            M.Number(TEXT("targetCoffeeML"), Received.Get(Coffee));
            M.Number(TEXT("targetWaterML"), Received.Get(Water));
            M.Number(TEXT("targetMilkML"), Received.Get(Milk));
            M.Number(TEXT("targetUnknownML"), Received.Get(Unknown));
            M.Boolean(TEXT("targetFrozen"), Target->IsLiquidFrozen());
        }
        Add(M);
    }
};

struct FNativeCoffeeWorld
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    explicit FNativeCoffeeWorld(FAutomationTestBase& InTest) : Test(InTest)
    {
        if (!Test.TestNotNull(TEXT("引擎上下文可用"), GEngine)) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
            true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("创建原生 Game World"), World)) return;
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FNativeCoffeeWorld()
    {
        if (!World) return;
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
    ULiquidTransferSubsystem* Transfer() const
    {
        return World ? World->GetSubsystem<ULiquidTransferSubsystem>() : nullptr;
    }
    bool Ready() { return Test.TestNotNull(TEXT("真实液体转移系统可用"), Transfer()); }
    static void QuietGrab(UGrabComponent* Grab)
    {
        Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
        Grab->bSimulateOnDrop = false;
        Grab->bCaptureHand = false;
        Grab->bHideHandWhileHeld = false;
        Grab->OnGrabHapticEffect = nullptr;
    }
    static void QuietFX(UNiagaraComponent* FX)
    {
        FX->SetAutoActivate(false);
        FX->SetAsset(nullptr);
        FX->DeactivateImmediate();
    }
    static void QuietActor(AActor* A)
    {
        A->SetActorTickEnabled(false);
        TArray<UStaticMeshComponent*> Meshes;
        A->GetComponents<UStaticMeshComponent>(Meshes);
        for (UStaticMeshComponent* Mesh : Meshes)
        {
            Mesh->SetSimulatePhysics(false);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetGenerateOverlapEvents(false);
        }
    }
    template <typename T>
    T* Part(const FVector& Position = FVector::ZeroVector)
    {
        const FTransform Transform(Position);
        T* A = World->SpawnActorDeferred<T>(T::StaticClass(), Transform, nullptr,
            nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Test.TestNotNull(TEXT("创建原生研磨组件 Actor"), A)) return nullptr;
        QuietActor(A);
        QuietGrab(A->GrabComp);
        if (ACoffeeGrinderActor* Grinder = Cast<ACoffeeGrinderActor>(A))
        {
            Grinder->HandleClass = nullptr;
            Grinder->DrawerClass = nullptr;
            Grinder->LoadedGrindSound = nullptr;
            Grinder->EmptyGrindSound = nullptr;
            Grinder->GrindSFX->SetSound(nullptr);
            Grinder->GrindSFX->SetAutoActivate(false);
            QuietFX(Grinder->GrindFX);
        }
        if (AGrinderHandleActor* Handle = Cast<AGrinderHandleActor>(A)) Handle->TurnHaptic = nullptr;
        if (AGrinderDrawerActor* Drawer = Cast<AGrinderDrawerActor>(A))
        {
            Drawer->DetachSound = nullptr;
            Drawer->ReattachSound = nullptr;
            Drawer->DetachHaptic = nullptr;
            Drawer->GrabComp->bGrabInPlace = false;
        }
        A->FinishSpawning(Transform);
        QuietActor(A);
        return A;
    }
    template <typename T>
    T* Vessel(float Capacity, const FVector& Position)
    {
        const FTransform Transform(Position);
        T* A = World->SpawnActorDeferred<T>(T::StaticClass(), Transform, nullptr,
            nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Test.TestNotNull(TEXT("创建原生液体容器"), A)) return nullptr;
        QuietActor(A);
        QuietGrab(A->GrabComp);
        QuietFX(A->LiquidFX);
        QuietFX(A->PourFX);
        A->LiquidFXTemplate = nullptr;
        A->SplashEffectTemplate = nullptr;
        A->GlassBreakProfile = nullptr;
        A->LiquidMaterialAsset = nullptr;
        A->bEnableSplash = false;
        A->bEnableDecal = false;
        A->bCanPour = false;
        A->bAcceptLiquidFromOthers = true;
        A->MaxVolumeML = Capacity;
        A->FillAmount = 0.f;
        A->InitialIngredientId = Water;
        A->LiquidColor = FLinearColor::White;
        A->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector);
        A->PourTargetPoint->SetRelativeLocation(FVector(0, 0, -5));
        A->PourEntryRadius = 4.f;
        if (AHandDripperActor* Dripper = Cast<AHandDripperActor>(A))
        {
            Dripper->DripSound = nullptr;
            Dripper->OutletLocal = FVector::ZeroVector;
        }
        if (ACupActor* Cup = Cast<ACupActor>(A))
        {
            Cup->VolumeWidgetClass = nullptr;
            Cup->bEditorPreviewWidget = false;
            Cup->bFaceCamera = false;
            Cup->VolumeWidgetComp->SetWidgetClass(nullptr);
            Cup->VolumeWidgetComp->SetHiddenInGame(true);
            Cup->VolumeWidgetComp->SetComponentTickEnabled(false);
        }
        A->FinishSpawning(Transform);
        QuietActor(A);
        return A;
    }
    AHandDripperActor* Dripper(const FVector& Position = FVector(0, 0, 10))
    {
        return Vessel<AHandDripperActor>(120.f, Position);
    }
    ACupActor* Cup(float Capacity = 200.f, const FVector& Position = FVector::ZeroVector)
    {
        return Vessel<ACupActor>(Capacity, Position);
    }
    void Step(AHandDripperActor* Source, float Dt)
    {
        Source->AdvanceBrew(Dt);
        CheckBalance(Test, Transfer());
        Transfer()->Simulate(Dt);
        CheckBalance(Test, Transfer());
    }
    void Drain(AHandDripperActor* Source, float Dt = .1f, double Duration = 30.0)
    {
        for (double Elapsed = 0; Elapsed < Duration; Elapsed += Dt) Step(Source, Dt);
        Transfer()->Simulate(Transfer()->GetUnsettledFlightHorizon() + .05);
        CheckBalance(Test, Transfer());
    }
};

FVector HandAt(double Degrees)
{
    const double Radians = FMath::DegreesToRadians(Degrees);
    return FVector(5.0 * FMath::Cos(Radians), 5.0 * FMath::Sin(Radians), 0);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewGrindMassAndClampTest,
    "CyberCafe.Coffee.GrindMassAndClamp", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewGrindMassAndClampTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("GrindMassAndClamp"));
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    for (float Efficiency : { -1.f, 0.f, .65f, 1.f, 2.f, NaN })
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        ACoffeeGrinderActor* G = T.Part<ACoffeeGrinderActor>();
        if (!G) return false;
        G->GrindEfficiency = Efficiency;
        Near(*this, TEXT("超额投豆仅接纳默认满仓 15g"), G->TryAddBeans(20), 15);
        Near(*this, TEXT("满仓继续投豆拒收"), G->TryAddBeans(1), 0);
        Near(*this, TEXT("满仓粉堆位于原始顶部而不拉伸"), G->BeanPileMesh->GetRelativeLocation().Z,
            G->BeanPileInitialLocation.Z);
        TestTrue(TEXT("满仓网格缩放保持原值"), G->BeanPileMesh->GetRelativeScale3D().Equals(G->BeanPileInitialScale));
        Near(*this, TEXT("反向不消耗"), G->AdvanceGrinding(-360, 1), 0);
        Near(*this, TEXT("NaN 角拒止"), G->AdvanceGrinding(NaN, 1), 0);
        Near(*this, TEXT("NaN 时间拒止"), G->AdvanceGrinding(360, NaN), 0);
        Near(*this, TEXT("零时间拒止"), G->AdvanceGrinding(360, 0), 0);
        Near(*this, TEXT("非法投豆拒收"), G->TryAddBeans(NaN), 0);
        Near(*this, TEXT("负投豆拒收"), G->TryAddBeans(-1), 0);
        Near(*this, TEXT("保护未改变豆量"), G->GetCurrentBeanGrams(), 15);
        double Consumed = 0;
        for (int32 Circle = 0; Circle < 6; ++Circle) Consumed += G->AdvanceGrinding(360, 1);
        const double Clamped = FMath::IsFinite(Efficiency) ? FMath::Clamp(Efficiency, 0.f, 1.f) : 0.0;
        Near(*this, TEXT("效率钳制到零至一"), G->GrindEfficiency, Clamped);
        Near(*this, TEXT("六圈实际消耗 15g"), Consumed, 15);
        Near(*this, TEXT("六圈后没有豆"), G->GetCurrentBeanGrams(), 0);
        Near(*this, TEXT("粉产出符合实际效率"), G->GetCurrentGroundGrams(), 15 * Clamped);
        Near(*this, TEXT("损耗符合效率"), G->GetGrindLossGrams(), 15 * (1 - Clamped));
        Near(*this, TEXT("真实累计质量守恒"), G->GetTotalBeanConsumedGrams(),
            G->GetTotalGroundOutputGrams() + G->GetGrindLossGrams());
        const double Before = G->GetCurrentGroundGrams();
        Near(*this, TEXT("无豆再摇不消耗"), G->AdvanceGrinding(2160, 6), 0);
        Near(*this, TEXT("无豆再摇不产粉"), G->GetCurrentGroundGrams(), Before);
        TestFalse(TEXT("磨空及非法输入不保留磨豆音效状态"), G->bGrindSFXActive);
        FMeasurement M(TEXT("six_turns"));
        M.Number(TEXT("requestedEfficiency"), Efficiency);
        M.Number(TEXT("clampedEfficiency"), G->GrindEfficiency);
        M.Number(TEXT("consumedGrams"), G->GetTotalBeanConsumedGrams());
        M.Number(TEXT("outputGrams"), G->GetTotalGroundOutputGrams());
        M.Number(TEXT("lossGrams"), G->GetGrindLossGrams());
        M.Number(TEXT("beansLeftGrams"), G->GetCurrentBeanGrams());
        M.Number(TEXT("groundsLeftGrams"), G->GetCurrentGroundGrams());
        Evidence.Add(M);
        Near(*this, TEXT("非法 Drain 不扣粉"), G->DrainGround(NaN), 0);
        const double Drained = G->DrainGround(100);
        Near(*this, TEXT("Drain 返回真实留存"), Drained, Before);
        Near(*this, TEXT("搬粉不改变累计产出"), G->GetTotalGroundOutputGrams(), 15 * Clamped);
        CheckBalance(*this, T.Transfer());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewHandleWrapPauseJitterTest,
    "CyberCafe.Coffee.HandleWrapPauseJitter", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewHandleWrapPauseJitterTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("HandleWrapPauseJitter"));
    FNativeCoffeeWorld T(*this);
    if (!T.Ready()) return false;
    ACoffeeGrinderActor* G = T.Part<ACoffeeGrinderActor>();
    AGrinderHandleActor* H = T.Part<AGrinderHandleActor>();
    if (!G || !H) return false;
    G->TryAddBeans(15);
    H->AttachToGrinder(G, G->HandleMountPoint);
    TestTrue(TEXT("178 度样本只立基准"), H->ApplyHandSample(HandAt(178), .01f));
    Near(*this, TEXT("首样本不产粉"), G->GetCurrentGroundGrams(), 0);
    TestTrue(TEXT("负 178 度样本跨环绕有效"), H->ApplyHandSample(HandAt(-178), .01f));
    Near(*this, TEXT("环绕累计仅正四度"), H->CurrentAngleDeg, 4);
    Near(*this, TEXT("环绕仅消耗四度对应豆量"), G->GetTotalBeanConsumedGrams(), 4 * G->GramsPerDegree);
    H->GrabComp->OnDropped.Broadcast();
    H->GrabComp->OnGrabbed.Broadcast();
    const double AfterWrap = G->GetTotalBeanConsumedGrams();
    H->ApplyHandSample(HandAt(0), .01f);
    for (int32 I = 0; I < 300; ++I)
    {
        H->ApplyHandSample(HandAt(.3), .01f);
        H->ApplyHandSample(HandAt(-.3), .01f);
    }
    Near(*this, TEXT("静止附近正负 0.3 度反复抖动不产粉"), G->GetTotalBeanConsumedGrams(), AfterWrap);
    TestFalse(TEXT("跳到 x100 被忽略"), H->ApplyHandSample(FVector(100, 0, 0), .01f));
    H->ApplyHandSample(HandAt(60), .01f);
    Near(*this, TEXT("失踪恢复首样本不追补角度"), G->GetTotalBeanConsumedGrams(), AfterWrap);
    H->GrabComp->OnDropped.Broadcast();
    H->GrabComp->OnGrabbed.Broadcast();
    H->ApplyHandSample(HandAt(100), .01f);
    Near(*this, TEXT("重新抓取首样本不兑换旧角"), G->GetTotalBeanConsumedGrams(), AfterWrap);
    H->ApplyHandSample(HandAt(100.6), .01f);
    H->ApplyHandSample(HandAt(100.6), .01f);
    H->ApplyHandSample(HandAt(110), .01f);
    Near(*this, TEXT("暂停清预算，恢复首样本只立基准"), G->GetTotalBeanConsumedGrams(), AfterWrap);
    H->ApplyHandSample(HandAt(112), .01f);
    Near(*this, TEXT("恢复后仅新两度研磨"), G->GetTotalBeanConsumedGrams(), AfterWrap + 2 * G->GramsPerDegree);
    const double BeforeInvalid = G->GetTotalBeanConsumedGrams();
    TestFalse(TEXT("长暂停样本拒止"), H->ApplyHandSample(HandAt(113), .4f));
    TestFalse(TEXT("NaN 手位置拒止"), H->ApplyHandSample(
        FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), .01f));
    H->ApplyHandSample(HandAt(-60), .01f);
    H->ApplyHandSample(HandAt(-62), .01f);
    Near(*this, TEXT("反向样本不产粉"), G->GetTotalBeanConsumedGrams(), BeforeInvalid);
    FMeasurement M(TEXT("wrap_jitter_pause_public_delegates"));
    M.Number(TEXT("currentAngleDeg"), H->CurrentAngleDeg);
    M.Number(TEXT("consumedAfterWrapGrams"), AfterWrap);
    M.Number(TEXT("consumedAfterResumeGrams"), BeforeInvalid);
    M.Number(TEXT("finalConsumedGrams"), G->GetTotalBeanConsumedGrams());
    M.Number(TEXT("groundGrams"), G->GetCurrentGroundGrams());
    M.Number(TEXT("lossGrams"), G->GetGrindLossGrams());
    Evidence.Add(M);
    CheckBalance(*this, T.Transfer());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewDrawerRetainedPowderTest,
    "CyberCafe.Coffee.DrawerRetainedPowderOnReattach", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewDrawerRetainedPowderTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("DrawerRetainedPowderOnReattach"));
    FNativeCoffeeWorld T(*this);
    if (!T.Ready()) return false;
    ACoffeeGrinderActor* G = T.Part<ACoffeeGrinderActor>();
    AGrinderDrawerActor* D = T.Part<AGrinderDrawerActor>();
    if (!G || !D) return false;
    G->TryAddBeans(10);
    G->AdvanceGrinding(1440, 4);
    Near(*this, TEXT("没有抽屉仍留存 10g 真实研磨粉"), G->GetCurrentGroundGrams(), 10);
    D->AttachToGrinder(G, G->DrawerMountPoint);
    Near(*this, TEXT("初次挂回立即搬运留存粉"), D->GetGroundsGrams(), 10);
    Near(*this, TEXT("初次搬运主体归零"), G->GetCurrentGroundGrams(), 0);
    Near(*this, TEXT("公开补粉接纳 45g"), D->AddGrounds(45), 45);
    UMotionControllerComponent* Controller = NewObject<UMotionControllerComponent>(G);
    Controller->SetupAttachment(G->BodyMesh);
    G->AddInstanceComponent(Controller);
    Controller->RegisterComponent();
    D->DetachFromGrinder(Controller);
    TestTrue(TEXT("公开拔出切换为 Detached"), D->State == AGrinderDrawerActor::EDrawerState::Detached);
    G->TryAddBeans(10);
    G->AdvanceGrinding(1440, 4);
    Near(*this, TEXT("拔出时主体保留新粉"), G->GetCurrentGroundGrams(), 10);
    Near(*this, TEXT("拔出时抽屉不自动吸粉"), D->GetGroundsGrams(), 55);
    const double AngleBefore = G->AccumulatedHandleAngleDeg;
    D->ReattachToGrinder();
    Near(*this, TEXT("挂回立即填到 60g"), D->GetGroundsGrams(), 60);
    Near(*this, TEXT("满抽屉溢出量仍在主体"), G->GetCurrentGroundGrams(), 5);
    D->OnGrinderGrindProgress(G->GetCurrentBeanGrams(), G->GetCurrentGroundGrams());
    D->ReattachToGrinder();
    Near(*this, TEXT("满抽屉重复同步不 Drain 溢出"), G->GetCurrentGroundGrams(), 5);
    Near(*this, TEXT("公开移除返回真实 4g"), D->RemoveGrounds(4), 4);
    D->ReattachToGrinder();
    Near(*this, TEXT("不转手柄挂回仍同步"), D->GetGroundsGrams(), 60);
    Near(*this, TEXT("补回后主体仍保留 1g"), G->GetCurrentGroundGrams(), 1);
    Near(*this, TEXT("同步没有伪造手柄转角"), G->AccumulatedHandleAngleDeg, AngleBefore);
    Near(*this, TEXT("研磨粉及补粉总量守恒"), G->GetCurrentGroundGrams() + D->GetGroundsGrams() + 4,
        G->GetTotalGroundOutputGrams() + 45);
    Near(*this, TEXT("负移除不扣粉"), D->RemoveGrounds(-1), 0);
    Near(*this, TEXT("满抽屉追加不溢出"), D->AddGrounds(1), 0);
    FMeasurement M(TEXT("reattach_without_handle_turn"));
    M.Number(TEXT("drawerGrams"), D->GetGroundsGrams());
    M.Number(TEXT("retainedGrinderGrams"), G->GetCurrentGroundGrams());
    M.Number(TEXT("groundOutputGrams"), G->GetTotalGroundOutputGrams());
    M.Number(TEXT("groundSpilledGrams"), D->SpilledGroundGrams);
    M.Number(TEXT("handleAngleDeg"), G->AccumulatedHandleAngleDeg);
    Evidence.Add(M);
    CheckBalance(*this, T.Transfer());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewDrawerPourToDripperTest,
    "CyberCafe.Coffee.DrawerPourToDripper", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewDrawerPourToDripperTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("DrawerPourToDripper"));
    FNativeCoffeeWorld T(*this);
    if (!T.Ready()) return false;
    AGrinderDrawerActor* D = T.Part<AGrinderDrawerActor>(FVector(0, 0, 20));
    AHandDripperActor* Top = T.Dripper(FVector(0, 0, 10));
    AHandDripperActor* Below = T.Dripper(FVector::ZeroVector);
    if (!D || !Top || !Below) return false;
    D->GroundPourPoint->SetRelativeLocation(FVector::ZeroVector);
    D->AddGrounds(10);
    Near(*this, TEXT("直立不出粉"), D->PourGrounds(.5f), 0);
    D->SetActorRotation(FQuat(FVector::ForwardVector, -PI / 2));
    auto PourAndMeasure = [&](const TCHAR* Case)
    {
        const double DrawerBefore = D->GetGroundsGrams();
        const double TargetBefore = Top->GetGroundsGrams();
        const double SpilledBefore = D->SpilledGroundGrams;
        const double Poured = D->PourGrounds(.5f);
        const double Accepted = Top->GetGroundsGrams() - TargetBefore;
        const double Spilled = D->SpilledGroundGrams - SpilledBefore;
        Near(*this, TEXT("公开 Pour 返回等于抽屉真实扣量"), Poured, DrawerBefore - D->GetGroundsGrams());
        Near(*this, TEXT("每次实际扣粉等于目标增粉加撒粉"), Poured, Accepted + Spilled);
        Near(*this, TEXT("公开本次接纳读数等于目标真实增量"), D->LastAcceptedGroundGrams, Accepted);
        FMeasurement M(Case);
        M.Number(TEXT("pouredGrams"), D->LastPouredGroundGrams);
        M.Number(TEXT("acceptedGrams"), D->LastAcceptedGroundGrams);
        M.Number(TEXT("drawerGrams"), D->GetGroundsGrams());
        M.Number(TEXT("topGroundGrams"), Top->GetGroundsGrams());
        M.Number(TEXT("belowGroundGrams"), Below->GetGroundsGrams());
        M.Number(TEXT("spilledGroundGrams"), D->SpilledGroundGrams);
        M.Number(TEXT("originZ"), D->LastGroundPourOrigin.Z);
        M.Number(TEXT("entryHitZ"), D->LastGroundPourEnd.Z);
        Evidence.Add(M);
        CheckBalance(*this, T.Transfer());
        return Poured;
    };
    Near(*this, TEXT("倾倒沿真实口点穿过顶部平面接纳 4g"), PourAndMeasure(TEXT("cross_top_entry")), 4);
    Near(*this, TEXT("顶部目标接纳真实粉量"), Top->GetGroundsGrams(), 4);
    Near(*this, TEXT("路径终点是顶部入口"), D->LastGroundPourEnd.Z, Top->PourEntryPoint->GetComponentLocation().Z);
    Near(*this, TEXT("粉未穿透到第二个滤杯"), Below->GetGroundsGrams(), 0);
    Near(*this, TEXT("顶部补满仅接纳剩余 16g"), Top->AddGrounds(100, Top->PourEntryPoint->GetComponentLocation()), 16);
    Near(*this, TEXT("顶部满杯仍实际倒出 4g"), PourAndMeasure(TEXT("first_full_entry_refuses")), 4);
    Near(*this, TEXT("最早入口拒收记为撒粉"), D->SpilledGroundGrams, 4);
    Near(*this, TEXT("满杯拒收不重定向第二目标"), Below->GetGroundsGrams(), 0);
    D->SetActorLocation(FVector(100, 0, 20));
    Near(*this, TEXT("未命中只倒出真实剩余 2g"), PourAndMeasure(TEXT("miss_spills_tail")), 2);
    Near(*this, TEXT("原 10g 等于接纳 4g 加撒粉 6g"), D->SpilledGroundGrams, 6);
    Near(*this, TEXT("抽屉倾倒后归零"), D->GetGroundsGrams(), 0);
    Near(*this, TEXT("空抽屉不产生粉"), D->PourGrounds(1), 0);
    Near(*this, TEXT("入口外点不接粉"), Below->AddGrounds(1, FVector(100, 0, 0)), 0);
    Near(*this, TEXT("不在入口平面的点不接粉"), Below->AddGrounds(1, FVector(0, 0, 10)), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewBloomPacketConservationTest,
    "CyberCafe.Coffee.BloomStorageThenCoffeePacketConservation", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewBloomPacketConservationTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("BloomStorageThenCoffeePacketConservation"));
    for (bool bCatch : { false, true })
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Cup = bCatch ? T.Cup() : nullptr;
        if (!D || (bCatch && !Cup)) return false;
        Near(*this, TEXT("默认闷蒸八秒"), D->BloomSeconds, 8);
        Near(*this, TEXT("默认滴流十二毫升每秒"), D->DrainRateMLPerSecond, 12);
        Near(*this, TEXT("默认产率十六毫升每克"), D->CoffeeYieldMLPerGram, 16);
        Near(*this, TEXT("通过实际入口放入 15g"), D->AddGrounds(15, D->PourEntryPoint->GetComponentLocation()), 15);
        Near(*this, TEXT("初始咖啡预算 240mL"), D->GetCoffeeBudgetML(), 240);
        Near(*this, TEXT("实际注入 100mL Water"), D->AddIngredientLiquid(100, Water, FLinearColor::White), 100);
        Near(*this, TEXT("源原始成分仅是 Water"), D->ALiquidContainerActor::GetComposition().Get(Water), 100);
        T.Step(D, 7.5f);
        Near(*this, TEXT("闷蒸期间库存不下降"), D->GetPreciseVolumeML(), 100);
        Near(*this, TEXT("闷蒸期间无液包"), T.Transfer()->GetBalance().PacketCount, 0);
        Near(*this, TEXT("闷蒸期间无咖啡转换"), D->CoffeeProducedML, 0);
        Evidence.Liquid(bCatch ? TEXT("bloom_with_cup") : TEXT("bloom_without_cup"), T.Transfer(), D, Cup);
        T.Step(D, .5f);
        Near(*this, TEXT("恰到八秒仍未使用滴流时间"), D->GetPreciseVolumeML(), 100);
        D->AdvanceBrew(.1f);
        CheckBalance(*this, T.Transfer());
        Near(*this, TEXT("闷蒸后真实出液包 1.2mL"), T.Transfer()->GetBalance().InFlightML, 1.2);
        TestTrue(TEXT("真实在途包已产生"), T.Transfer()->GetBalance().PacketCount > 0);
        Near(*this, TEXT("出液转换不回注源库存"), D->ALiquidContainerActor::GetComposition().Get(Coffee), 0);
        Near(*this, TEXT("源 Water 随真实包扣除"), D->GetIngredientVolumeML(Water), 98.8);
        if (Cup) Near(*this, TEXT("模拟跨杯口前目标仍为空"), Cup->GetPreciseVolumeML(), 0);
        Evidence.Liquid(bCatch ? TEXT("real_packet_before_crossing") : TEXT("real_packet_no_cup"), T.Transfer(), D, Cup);
        T.Transfer()->Simulate(.1);
        CheckBalance(*this, T.Transfer());
        T.Drain(D);
        Near(*this, TEXT("滤杯最终滴空"), D->GetPreciseVolumeML(), 0);
        Near(*this, TEXT("实际总滴量等于注水量"), D->DrippedML, 100);
        Near(*this, TEXT("实际咖啡转换 100mL"), D->CoffeeProducedML, 100);
        Near(*this, TEXT("转换不做第二次注入"), T.Transfer()->GetBalance().InjectedML, 100);
        Near(*this, TEXT("所有液包已结算"), T.Transfer()->GetBalance().InFlightML, 0);
        Near(*this, TEXT("所有液包计数归零"), T.Transfer()->GetBalance().PacketCount, 0);
        Near(*this, TEXT("原料初始加注总额仅为 100mL"),
            T.Transfer()->GetBalance().InitialML + T.Transfer()->GetBalance().InjectedML, 100);
        Near(*this, TEXT("滤纸批次保留原 15g"), D->GetGroundsGrams(), 15);
        Near(*this, TEXT("滴空不恢复预算"), D->GetCoffeeBudgetML(), 140, .01);
        if (Cup)
        {
            Near(*this, TEXT("从 z10 出口物理跨入 z0 杯口接满 100mL"), Cup->GetPreciseVolumeML(), 100);
            Near(*this, TEXT("真实目标原始成分是 Coffee100"), Cup->ALiquidContainerActor::GetComposition().Get(Coffee), 100);
            Near(*this, TEXT("未保留 Water 标签"), Cup->GetIngredientVolumeML(Water), 0);
            Near(*this, TEXT("未凭空混入 Milk"), Cup->GetIngredientVolumeML(Milk), 0);
            Near(*this, TEXT("未凭空混入 Unknown"), Cup->GetIngredientVolumeML(Unknown), 0);
            Near(*this, TEXT("正确跨口无撒漏"), T.Transfer()->GetBalance().SpilledML, 0);
        }
        else Near(*this, TEXT("无杯时真实 100mL 全部记为撒漏"), T.Transfer()->GetBalance().SpilledML, 100);
        Evidence.Liquid(bCatch ? TEXT("caught_coffee_final") : TEXT("uncaught_spill_final"), T.Transfer(), D, Cup);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewEmptyFilterFiniteBatchTest,
    "CyberCafe.Coffee.EmptyFilterPassesWaterAndFiniteBatch", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewEmptyFilterFiniteBatchTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("EmptyFilterPassesWaterAndFiniteBatch"));
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Cup = T.Cup();
        if (!D || !Cup) return false;
        D->AddIngredientLiquid(10, Water, FLinearColor::White);
        T.Step(D, .1f);
        TestTrue(TEXT("无粉滤纸不闷蒸，立即产生在途水包"), T.Transfer()->GetBalance().PacketCount > 0);
        T.Drain(D);
        Near(*this, TEXT("无粉 Water10 原标签穿透"), Cup->GetIngredientVolumeML(Water), 10);
        Near(*this, TEXT("无粉不生产 Coffee"), Cup->GetIngredientVolumeML(Coffee), 0);
        Near(*this, TEXT("无粉转换统计为零"), D->CoffeeProducedML, 0);
        Evidence.Liquid(TEXT("empty_filter_water_pass"), T.Transfer(), D, Cup);
    }
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Cup = T.Cup();
        if (!D || !Cup) return false;
        D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation());
        const int32 Generation = D->BrewGeneration;
        D->AddIngredientLiquid(40, Water, FLinearColor::White);
        T.Drain(D);
        Near(*this, TEXT("1g 批次最多 Coffee16"), Cup->GetIngredientVolumeML(Coffee), 16);
        Near(*this, TEXT("超额 24mL 保持 Water"), Cup->GetIngredientVolumeML(Water), 24);
        Near(*this, TEXT("整批总量 40mL"), Cup->GetPreciseVolumeML(), 40);
        Near(*this, TEXT("有限预算耗尽"), D->GetCoffeeBudgetML(), 0);
        Near(*this, TEXT("滴空不清粉"), D->GetGroundsGrams(), 1);
        Near(*this, TEXT("用过滤纸拒绝追加干粉"), D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation()), 0);
        Evidence.Liquid(TEXT("finite_batch_first_drain"), T.Transfer(), D, Cup);
        D->AddIngredientLiquid(10, Water, FLinearColor::White);
        T.Step(D, .1f);
        TestTrue(TEXT("同批补水不重新闷蒸"), D->GetPreciseVolumeML() < 10);
        T.Drain(D);
        Near(*this, TEXT("同批补水仍只 Coffee16"), Cup->GetIngredientVolumeML(Coffee), 16);
        Near(*this, TEXT("同批补水后 Water34"), Cup->GetIngredientVolumeML(Water), 34);
        Near(*this, TEXT("同批咖啡累计不重置"), D->CoffeeProducedML, 16);
        TestEqual(TEXT("补水未新建滤纸批次"), D->BrewGeneration, Generation);
        Near(*this, TEXT("补水实际注入累计 50"), T.Transfer()->GetBalance().InjectedML, 50);
        Near(*this, TEXT("同批无撒漏"), T.Transfer()->GetBalance().SpilledML, 0);
        Evidence.Liquid(TEXT("same_batch_refill_no_budget_reset"), T.Transfer(), D, Cup);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewFrozenBrokenPartialCapacityTest,
    "CyberCafe.Coffee.FrozenBrokenAndPartialCapacity", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewFrozenBrokenPartialCapacityTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("FrozenBrokenAndPartialCapacity"));
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        if (!D) return false;
        D->FreezeLiquidForServing();
        Near(*this, TEXT("同一冻结滤杯拒收粉"), D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation()), 0);
        Near(*this, TEXT("冻结滤杯拒收外部注水"), D->AddIngredientLiquid(10, Water, FLinearColor::White), 0);
        D->AdvanceBrew(100);
        Near(*this, TEXT("冻结滤杯不产生包"), T.Transfer()->GetBalance().PacketCount, 0);
        Evidence.Liquid(TEXT("frozen_empty_rejects_ground"), T.Transfer(), D);
        CheckBalance(*this, T.Transfer());
    }
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        if (!D) return false;
        D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation());
        D->AddIngredientLiquid(10, Water, FLinearColor::White);
        D->FreezeLiquidForServing();
        D->AdvanceBrew(100);
        Near(*this, TEXT("带库存冻结后保持十毫升"), D->GetPreciseVolumeML(), 10);
        Near(*this, TEXT("冻结不消耗预算"), D->GetCoffeeBudgetML(), 16);
        Near(*this, TEXT("冻结源 EmitVolume 拒绝扣量"), T.Transfer()->EmitVolume(D, 10,
            FVector(0, 0, 10), FVector(0, 0, -20), FVector(0, 0, -980)), 0);
        Near(*this, TEXT("冻结源无在途量"), T.Transfer()->GetBalance().InFlightML, 0);
        Near(*this, TEXT("冻结不转为撒漏"), T.Transfer()->GetBalance().SpilledML, 0);
        Evidence.Liquid(TEXT("frozen_storage_no_emit"), T.Transfer(), D);
        CheckBalance(*this, T.Transfer());
    }
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        if (!D) return false;
        D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation());
        D->AddIngredientLiquid(10, Water, FLinearColor::White);
        TestTrue(TEXT("公开撞击破损成功"), D->BreakContainerForImpact());
        TestTrue(TEXT("同一滤杯处于破损状态"), D->IsBroken());
        Near(*this, TEXT("破损滤杯拒粉"), D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation()), 0);
        Near(*this, TEXT("破损滤杯拒水"), D->AddIngredientLiquid(10, Water, FLinearColor::White), 0);
        D->AdvanceBrew(100);
        Near(*this, TEXT("破损残液只撒漏一次"), T.Transfer()->GetBalance().SpilledML, 10);
        Near(*this, TEXT("破损不再滴流"), D->DrippedML, 0);
        Near(*this, TEXT("破损无包"), T.Transfer()->GetBalance().PacketCount, 0);
        Evidence.Liquid(TEXT("broken_storage_spills_once"), T.Transfer(), D);
        CheckBalance(*this, T.Transfer());
    }
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Cup = T.Cup();
        if (!D || !Cup) return false;
        D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation());
        D->AddIngredientLiquid(10, Water, FLinearColor::White);
        Cup->FreezeLiquidForServing();
        Near(*this, TEXT("冻结目标拒外部加液"), Cup->AddIngredientLiquid(1, Milk, FLinearColor::White), 0);
        T.Drain(D);
        Near(*this, TEXT("滴向冻结杯不接液"), Cup->GetPreciseVolumeML(), 0);
        Near(*this, TEXT("滴向冻结杯全部撒漏"), T.Transfer()->GetBalance().SpilledML, 10);
        Near(*this, TEXT("冻结目标未追加注入账"), T.Transfer()->GetBalance().InjectedML, 10);
        Evidence.Liquid(TEXT("frozen_destination_real_packets_spill"), T.Transfer(), D, Cup);
    }
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Cup = T.Cup(6);
        if (!D || !Cup) return false;
        D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation());
        const double Accepted = D->AddIngredientLiquid(200, Water, FLinearColor::White);
        Near(*this, TEXT("容量 120 的外部请求 200 仅接 120"), Accepted, 120);
        Near(*this, TEXT("拒收 80 不算真实撒漏"), T.Transfer()->GetBalance().SpilledML, 0);
        Near(*this, TEXT("外部注入账只记实际 120"), T.Transfer()->GetBalance().InjectedML, 120);
        FMeasurement Input(TEXT("external_input_partial_capacity"));
        Input.Number(TEXT("acceptedInputML"), Accepted);
        Input.Number(TEXT("rejectedInputML"), 200 - Accepted);
        Input.Number(TEXT("actualInjectedML"), T.Transfer()->GetBalance().InjectedML);
        Input.Number(TEXT("actualSpilledML"), T.Transfer()->GetBalance().SpilledML);
        Evidence.Add(Input);
        CheckBalance(*this, T.Transfer());
        D->AdvanceBrew(8);
        Near(*this, TEXT("真实单包出液 120mL"), T.Transfer()->EmitVolume(D, 120,
            FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 120);
        Near(*this, TEXT("单包转换仅使用有限 16mL 预算"), D->CoffeeProducedML, 16);
        Near(*this, TEXT("真实单包全部在途"), T.Transfer()->GetBalance().InFlightML, 120);
        Evidence.Liquid(TEXT("real_120ml_packet_before_capacity_credit"), T.Transfer(), D, Cup);
        CheckBalance(*this, T.Transfer());
        T.Transfer()->Simulate(.3);
        Near(*this, TEXT("部分容量杯仅接纳六毫升"), Cup->GetPreciseVolumeML(), 6);
        Near(*this, TEXT("单包余量实际撒漏 114"), T.Transfer()->GetBalance().SpilledML, 114);
        Near(*this, TEXT("按真实混合包比例接纳 Coffee0.8"), Cup->GetIngredientVolumeML(Coffee), .8);
        Near(*this, TEXT("按真实混合包比例接纳 Water5.2"), Cup->GetIngredientVolumeML(Water), 5.2);
        Near(*this, TEXT("接液及转换不二次注入"), T.Transfer()->GetBalance().InjectedML, 120);
        Evidence.Liquid(TEXT("partial_cup_actual_credit_and_spill"), T.Transfer(), D, Cup);
        CheckBalance(*this, T.Transfer());
    }
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Donor = T.Cup(200, FVector(100, 0, 20));
        if (!D || !Donor) return false;
        Donor->AddIngredientLiquid(200, Water, FLinearColor::White);
        Near(*this, TEXT("另一真实源实际发出 200mL"), T.Transfer()->EmitVolume(Donor, 200,
            FVector(0, 0, 20), FVector(0, 0, -50), FVector::ZeroVector), 200);
        CheckBalance(*this, T.Transfer());
        T.Transfer()->Simulate(.3);
        Near(*this, TEXT("真实入液包向滤杯只计接收 120"), D->GetPreciseVolumeML(), 120);
        Near(*this, TEXT("真实入液包拒收 80 记为撒漏"), T.Transfer()->GetBalance().SpilledML, 80);
        Near(*this, TEXT("入液不再额外注入 120"), T.Transfer()->GetBalance().InjectedML, 200);
        Evidence.Liquid(TEXT("incoming_real_packet_overflow_not_user_rejection"), T.Transfer(), D, Donor);
        CheckBalance(*this, T.Transfer());
    }
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Cup = T.Cup();
        if (!D || !Cup) return false;
        D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation());
        D->AddIngredientLiquid(40, Water, FLinearColor::White);
        D->AddIngredientLiquid(20, Milk, FLinearColor::White);
        D->AdvanceBrew(8);
        Near(*this, TEXT("混合液首包实际发出 30"), T.Transfer()->EmitVolume(D, 30,
            FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 30);
        Near(*this, TEXT("首包只将 Water16 转为 Coffee"), D->CoffeeProducedML, 16);
        Near(*this, TEXT("源剩余 Water20 不被原地改标签"), D->GetIngredientVolumeML(Water), 20);
        Near(*this, TEXT("源剩余 Milk10 保留"), D->GetIngredientVolumeML(Milk), 10);
        Near(*this, TEXT("源未回注 Coffee"), D->GetIngredientVolumeML(Coffee), 0);
        CheckBalance(*this, T.Transfer());
        T.Transfer()->Simulate(.3);
        Near(*this, TEXT("实际首包目标 Coffee16"), Cup->GetIngredientVolumeML(Coffee), 16);
        Near(*this, TEXT("实际首包目标 Water4"), Cup->GetIngredientVolumeML(Water), 4);
        Near(*this, TEXT("实际首包目标 Milk10"), Cup->GetIngredientVolumeML(Milk), 10);
        Evidence.Liquid(TEXT("mixed_first_real_packet"), T.Transfer(), D, Cup);
        Near(*this, TEXT("混合液尾包实际发出 30"), T.Transfer()->EmitVolume(D, 30,
            FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 30);
        CheckBalance(*this, T.Transfer());
        T.Transfer()->Simulate(.3);
        Near(*this, TEXT("整批仅 Coffee16"), Cup->GetIngredientVolumeML(Coffee), 16);
        Near(*this, TEXT("剩余 Water24 保留"), Cup->GetIngredientVolumeML(Water), 24);
        Near(*this, TEXT("整批 Milk20 原量保留"), Cup->GetIngredientVolumeML(Milk), 20);
        Near(*this, TEXT("混合总量守恒 60"), Cup->GetPreciseVolumeML(), 60);
        Near(*this, TEXT("混合包无 Unknown"), Cup->GetIngredientVolumeML(Unknown), 0);
        Near(*this, TEXT("混合转换只注入最初 60"), T.Transfer()->GetBalance().InjectedML, 60);
        Evidence.Liquid(TEXT("mixed_final_no_second_coffee_injection"), T.Transfer(), D, Cup);
        CheckBalance(*this, T.Transfer());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeBrewFrameRateEquivalenceTest,
    "CyberCafe.Coffee.FrameRateEquivalence", CyberCafeCoffeeBrewTests::Flags)
bool FCoffeeBrewFrameRateEquivalenceTest::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeBrewTests;
    FGroupEvidence Evidence(*this, TEXT("FrameRateEquivalence"));
    double ReferenceReceived = 0;
    double ReferenceCoffee = 0;
    for (int32 Run = 0; Run < 4; ++Run)
    {
        FNativeCoffeeWorld T(*this);
        if (!T.Ready()) return false;
        AHandDripperActor* D = T.Dripper();
        ACupActor* Cup = T.Cup();
        if (!D || !Cup) return false;
        D->AddGrounds(15, D->PourEntryPoint->GetComponentLocation());
        D->AddIngredientLiquid(100, Water, FLinearColor::White);
        const float Dt = Run == 0 ? .01f : (Run == 1 ? .1f : .4f);
        if (Run == 3)
        {
            D->AdvanceBrew(10.f);
            Near(*this, TEXT("长帧先完整闷蒸八秒，再积分两秒滴流"), D->DrippedML, 24);
            Near(*this, TEXT("长帧真实在途为 24mL"), T.Transfer()->GetBalance().InFlightML, 24);
            Evidence.Liquid(TEXT("ten_second_hitch_before_simulate"), T.Transfer(), D, Cup);
            CheckBalance(*this, T.Transfer());
            T.Transfer()->Simulate(10.0);
            CheckBalance(*this, T.Transfer());
        }
        // 比较充分滴空后的总量，不把浮点闷蒸边界或分片出生时刻误当成同帧验收。
        T.Drain(D, Dt, 40.0);
        Near(*this, TEXT("各种帧长源最终滴空"), D->GetPreciseVolumeML(), 0);
        Near(*this, TEXT("各种帧长累计真实滴量 100"), D->DrippedML, 100);
        Near(*this, TEXT("各种帧长实际接纳 100"), Cup->GetPreciseVolumeML(), 100);
        Near(*this, TEXT("各种帧长实际 Coffee100"), Cup->GetIngredientVolumeML(Coffee), 100);
        Near(*this, TEXT("各种帧长无未结算尾包"), T.Transfer()->GetBalance().InFlightML, 0);
        Near(*this, TEXT("各种帧长无撒漏"), T.Transfer()->GetBalance().SpilledML, 0);
        Near(*this, TEXT("float 预算扣减在有限舍入范围内"), D->GetCoffeeBudgetML(), 140, .05);
        if (Run == 0)
        {
            ReferenceReceived = Cup->GetPreciseVolumeML();
            ReferenceCoffee = Cup->GetIngredientVolumeML(Coffee);
        }
        else
        {
            Near(*this, TEXT("与 0.01 秒基线接纳量等价"), Cup->GetPreciseVolumeML(), ReferenceReceived);
            Near(*this, TEXT("与 0.01 秒基线咖啡成分等价"), Cup->GetIngredientVolumeML(Coffee), ReferenceCoffee);
        }
        FMeasurement Timing(TEXT("fully_drained_frame_variant"));
        Timing.Number(TEXT("stepSeconds"), Dt);
        Timing.Boolean(TEXT("initialTenSecondHitch"), Run == 3);
        Timing.Number(TEXT("actualReceivedML"), Cup->GetPreciseVolumeML());
        Timing.Number(TEXT("actualCoffeeML"), Cup->GetIngredientVolumeML(Coffee));
        Timing.Number(TEXT("actualDrippedML"), D->DrippedML);
        Evidence.Add(Timing);
        Evidence.Liquid(TEXT("frame_variant_final"), T.Transfer(), D, Cup);
    }
    return !HasAnyErrors();
}
#endif
