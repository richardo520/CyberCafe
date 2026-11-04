// @trace GB-01 GB-02 GB-03 GB-04
// 合成 World 行为测试；不代表硬件、碎片几何或音效验收。仅执行测试时导出真实采样。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/CupActor.h"
#include "Liquid/BottleActor.h"
#include "GrabComponent.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include <limits>

namespace GlassBreakageTests
{
const FName CoffeeId(TEXT("Coffee"));
const FName MilkId(TEXT("Milk"));
constexpr double VolumeTolerance = 1.e-6;

// 沿用 Recipe/全息 fixture 的真实 GI 生命周期；关闭 FX 和物理，不加载项目资产。
struct FGlassTestWorld
{
    UWorld* World;
    UGameInstance* GI;
    TArray<ALiquidContainerActor*> Containers;

    FGlassTestWorld()
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
    }

    ~FGlassTestWorld()
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    static void SuppressSimulation(ALiquidContainerActor* A)
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
        A->GrabComp->PhysicsMode = EGrabPhysicsMode::Kinematic;
        A->GrabComp->bSimulateOnDrop = false;
        A->GrabComp->bCaptureHand = false;
        A->GrabComp->OnGrabHapticEffect = nullptr;
    }

    template <typename T>
    T* Vessel(float CapacityML, float InitialFill, FName Ingredient,
        const FTransform& Transform = FTransform::Identity)
    {
        T* A = World->SpawnActorDeferred<T>(T::StaticClass(), Transform, nullptr,
            nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        SuppressSimulation(A);
        A->MaxVolumeML = CapacityML;
        A->FillAmount = InitialFill;
        A->InitialIngredientId = Ingredient;
        A->LiquidColor = FLinearColor::White;
        A->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector);
        A->PourEntryRadius = 3.f;
        A->GrabComp->GrabType = EGrabType::Free;
        if (ABottleActor* Bottle = Cast<ABottleActor>(A))
        {
            Bottle->CapClass = nullptr;
            Bottle->bStartCapped = false;
            Bottle->PourHaptic = nullptr;
        }
        if (ACupActor* Cup = Cast<ACupActor>(A))
        {
            Cup->VolumeWidgetClass = nullptr;
            Cup->bFaceCamera = false;
        }
        A->FinishSpawning(Transform);
        SuppressSimulation(A);
        Containers.Add(A);
        return A;
    }

    UGlassBreakProfile* Profile(float BreakSpeedCms = 260.f)
    {
        UGlassBreakProfile* P = NewObject<UGlassBreakProfile>(World);
        P->BreakSpeedCms = BreakSpeedCms;
        P->BreakSound = nullptr;
        P->Shards.Empty();
        return P;
    }

    UGlassBreakableComponent* AttachBreakable(ALiquidContainerActor* A,
        UGlassBreakProfile* P, bool bSampleMesh = false)
    {
        // 瞬态空网格只使 SM 非空采样分支可达；不伪装成 Cube 或验证几何 sweep。
        if (bSampleMesh) A->ContainerMesh->SetStaticMesh(NewObject<UStaticMesh>(A));
        UGlassBreakableComponent* Comp = NewObject<UGlassBreakableComponent>(A);
        Comp->Profile = P;
        A->AddInstanceComponent(Comp);
        Comp->RegisterComponent();
        return Comp;
    }

    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        // GrabComponent::BeginPlay 会恢复 PhysicsActor 碰撞配置。
        for (ALiquidContainerActor* A : Containers) SuppressSimulation(A);
    }

    ULiquidTransferSubsystem* Transfers() const
    {
        return World->GetSubsystem<ULiquidTransferSubsystem>();
    }
};

void AdvanceSamples(UGlassBreakableComponent* Comp, int32 Count = 40)
{
    // .02 x 40 同时越过 .6 启动保护及首帧 .12 保护，不用大 Dt 假装热身。
    for (int32 I = 0; I < Count; ++I) Comp->TickComponent(.02f, LEVELTICK_All, nullptr);
}

void CheckBalanced(FAutomationTestBase& Test, const TCHAR* Label,
    ULiquidTransferSubsystem* Transfers)
{
    const FLiquidBalance B = Transfers->GetBalance();
    Test.TestTrue(Label, FMath::IsFinite(B.ErrorML) && FMath::Abs(B.ErrorML) < VolumeTolerance);
}

void CaptureBalance(TArray<FString>& Rows, const TCHAR* Stage,
    ULiquidTransferSubsystem* Transfers, const ALiquidContainerActor* Source,
    const ALiquidContainerActor* Target)
{
    const FLiquidBalance B = Transfers->GetBalance();
    const auto VesselJson = [](const ALiquidContainerActor* A)
    {
        if (!A) return FString(TEXT("null"));
        return FString::Printf(
            TEXT("{\"volumeML\":%.17g,\"coffeeML\":%.17g,\"milkML\":%.17g,\"compositionML\":%.17g,\"capacityML\":%.17g,\"fill\":%.17g,\"revision\":%d,\"broken\":%s,\"frozen\":%s,\"replacement\":%s,\"canPour\":%s,\"canReceive\":%s,\"visible\":%s,\"grabActive\":%s,\"actorTick\":%s,\"xCM\":%.17g,\"yCM\":%.17g,\"zCM\":%.17g}"),
            A->GetPreciseVolumeML(), A->GetIngredientVolumeML(CoffeeId),
            A->GetIngredientVolumeML(MilkId), A->GetComposition().Total(),
            static_cast<double>(A->MaxVolumeML), static_cast<double>(A->FillAmount),
            A->GetLiquidRevision(), A->IsBroken() ? TEXT("true") : TEXT("false"),
            A->IsLiquidFrozen() ? TEXT("true") : TEXT("false"),
            A->bInitialVolumeIsReplacement ? TEXT("true") : TEXT("false"),
            A->bCanPour ? TEXT("true") : TEXT("false"),
            A->bAcceptLiquidFromOthers ? TEXT("true") : TEXT("false"),
            A->ContainerMesh->IsVisible() ? TEXT("true") : TEXT("false"),
            A->GrabComp->IsActive() ? TEXT("true") : TEXT("false"),
            A->IsActorTickEnabled() ? TEXT("true") : TEXT("false"),
            A->GetActorLocation().X, A->GetActorLocation().Y, A->GetActorLocation().Z);
    };
    Rows.Add(FString::Printf(
        TEXT("{\"stage\":\"%s\",\"initialML\":%.17g,\"injectedML\":%.17g,\"removedML\":%.17g,\"containerML\":%.17g,\"inFlightML\":%.17g,\"spilledML\":%.17g,\"errorML\":%.17g,\"packetCount\":%d,\"source\":%s,\"target\":%s}"),
        Stage, B.InitialML, B.InjectedML, B.RemovedML, B.ContainerML,
        B.InFlightML, B.SpilledML, B.ErrorML, B.PacketCount,
        *VesselJson(Source), *VesselJson(Target)));
}

bool SaveGroup(FAutomationTestBase& Test, const TCHAR* Group, const TArray<FString>& Rows)
{
    // 仅聚合本进程实际执行的组；单组运行也可导出，不读取旧文件补造未执行结果。
    static TMap<FString, FString> CapturedGroups;
    CapturedGroups.Add(Group, TEXT("[") + FString::Join(Rows, TEXT(",\n")) + TEXT("]"));
    TArray<FString> Names;
    CapturedGroups.GetKeys(Names);
    Names.Sort();
    TArray<FString> Groups;
    for (const FString& Name : Names)
        Groups.Add(TEXT("\"") + Name + TEXT("\":") + CapturedGroups[Name]);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("GlassBreakage");
    if (!Test.TestTrue(TEXT("创建运行时行为采样目录"),
        IFileManager::Get().MakeDirectory(*Directory, true))) return false;
    const FString Json = TEXT("{\"sampleKind\":\"syntheticWorldPublicAPI\",\"groups\":{\n")
        + FString::Join(Groups, TEXT(",\n")) + TEXT("\n}}\n");
    return Test.TestTrue(TEXT("保存实测 getter/账本采样到 behaviour.json"),
        FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("behaviour.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassClosingMathTest,
    "CyberCafe.Glass.ClosingMathThresholds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassClosingMathTest::RunTest(const FString&)
{
    UGlassBreakProfile* P = NewObject<UGlassBreakProfile>();
    TestEqual(TEXT("默认破碎阈值为 260 cm/s"), P->BreakSpeedCms, 260.f);
    const FVector Normal = FVector::UpVector;
    TArray<FString> Rows;
    for (float Speed : {0.f, 259.99f, 260.f, 260.01f, 1000.f})
    {
        const float Measured = UGlassBreakableComponent::ClosingSpeed(FVector(0, 0, -Speed), Normal);
        TestEqual(TEXT("正向闭合速度保留阈值两侧与等值边界"), Measured, Speed);
        TestEqual(TEXT("法线长度不改变闭合速度"),
            UGlassBreakableComponent::ClosingSpeed(FVector(0, 0, -Speed), Normal * 10), Speed);
        Rows.Add(FString::Printf(TEXT("{\"inputCms\":%.17g,\"closingCms\":%.17g,\"thresholdCms\":%.17g}"),
            static_cast<double>(Speed), static_cast<double>(Measured), static_cast<double>(P->BreakSpeedCms)));
    }
    TestEqual(TEXT("高速切向不视为命中"),
        UGlassBreakableComponent::ClosingSpeed(FVector(5000, 0, 0), Normal), 0.f);
    TestEqual(TEXT("离开接触面不视为闭合"),
        UGlassBreakableComponent::ClosingSpeed(FVector(0, 0, 1000), Normal), 0.f);
    TestEqual(TEXT("零法线不视为命中"),
        UGlassBreakableComponent::ClosingSpeed(FVector(0, 0, -1000), FVector::ZeroVector), 0.f);
    FVector Invalid = FVector::ZeroVector;
    Invalid.X = std::numeric_limits<double>::quiet_NaN();
    TestEqual(TEXT("NaN 速度拒绝"), UGlassBreakableComponent::ClosingSpeed(Invalid, Normal), 0.f);
    TestEqual(TEXT("NaN 法线拒绝"), UGlassBreakableComponent::ClosingSpeed(Normal, Invalid), 0.f);
    Invalid.X = std::numeric_limits<double>::infinity();
    TestEqual(TEXT("无穷速度拒绝"), UGlassBreakableComponent::ClosingSpeed(Invalid, Normal), 0.f);
    TestEqual(TEXT("无穷法线拒绝"), UGlassBreakableComponent::ClosingSpeed(Normal, Invalid), 0.f);
    return SaveGroup(*this, TEXT("ClosingMathThresholds"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassSourceInFlightTest,
    "CyberCafe.Glass.SourceInFlightSpillAndBrokenCredit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassSourceInFlightTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FGlassTestWorld T;
    ABottleActor* Source = T.Vessel<ABottleActor>(100.f, 1.f, MilkId, FTransform(FVector(100, 0, 0)));
    ACupActor* Target = T.Vessel<ACupActor>(100.f, 0.f, CoffeeId);
    T.BeginPlay();
    ULiquidTransferSubsystem* S = T.Transfers();
    TArray<FString> Rows;
    CaptureBalance(Rows, TEXT("initial"), S, Source, Target);
    Source->SetCapacityML(80.f);
    TestEqual(TEXT("缩容只洒出 20 mL"), S->GetBalance().SpilledML, 20.0);
    TestEqual(TEXT("破碎前真实消耗 5 mL"), Source->ConsumeLiquid(5.f), 5.f);
    const double Emitted = S->EmitVolume(Source, 15.0, FVector(0, 0, 10),
        FVector(0, 0, -50), FVector::ZeroVector);
    TestEqual(TEXT("真实发射 15 mL"), Emitted, 15.0);
    TestEqual(TEXT("发射后权威余量 60 mL"), Source->GetPreciseVolumeML(), 60.0);
    CaptureBalance(Rows, TEXT("emitted"), S, Source, Target);
    TestTrue(TEXT("源瓶首次破碎成功"), Source->BreakContainerForImpact());
    TestTrue(TEXT("源瓶 broken 标记置位"), Source->IsBroken());
    TestEqual(TEXT("仅余量洒出，累计 80 mL"), S->GetBalance().SpilledML, 80.0);
    TestEqual(TEXT("破碎不重复结算在途 15 mL"), S->GetBalance().InFlightML, 15.0);
    TestEqual(TEXT("破碎不记为额外消耗"), S->GetBalance().RemovedML, 5.0);
    TestEqual(TEXT("源成分与权威余量均归零"), Source->GetComposition().Total(), 0.0);
    TestFalse(TEXT("破碎关闭倒液"), Source->bCanPour);
    TestFalse(TEXT("破碎关闭接液"), Source->bAcceptLiquidFromOthers);
    TestFalse(TEXT("破碎立即停止 pouring"), Source->bIsPouring);
    const int32 BrokenRevision = Source->GetLiquidRevision();
    TestFalse(TEXT("重复破碎拒绝"), Source->BreakContainerForImpact());
    TestEqual(TEXT("破损源旧加液 API 拒绝"), Source->AddLiquid(10.f, FLinearColor::Red), 0.f);
    TestEqual(TEXT("破损源成分加液 API 拒绝"), Source->AddIngredientLiquid(10.f, MilkId, FLinearColor::White), 0.f);
    TestEqual(TEXT("破损源消耗 API 拒绝"), Source->ConsumeLiquid(10.f), 0.f);
    Source->SetCapacityML(200.f);
    TestEqual(TEXT("破损源拒绝容量改写"), Source->MaxVolumeML, 80.f);
    Source->bCanPour = true;
    TestEqual(TEXT("即使倒液标志被外部打开，破损源仍拒绝发射"),
        S->EmitVolume(Source, 5.0, FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 0.0);
    S->EmitPour(Source, .1, FTransform(FVector(0, 0, 10)));
    Source->bCanPour = false;
    TestEqual(TEXT("所有拒绝不推进 revision"), Source->GetLiquidRevision(), BrokenRevision);
    TestEqual(TEXT("拒绝发射不增加在途包"), S->GetBalance().InFlightML, 15.0);
    CaptureBalance(Rows, TEXT("brokenSource"), S, Source, Target);
    CheckBalanced(*this, TEXT("破碎结算立即闭合"), S);
    TestTrue(TEXT("破损源正常 Destroy"), Source->Destroy());
    Source = nullptr;
    CaptureBalance(Rows, TEXT("sourceDestroyed"), S, Source, Target);
    S->Simulate(.3);
    TestEqual(TEXT("被销毁源的在途奶仍到达"), Target->GetIngredientVolumeML(MilkId), 15.0);
    TestEqual(TEXT("在途已结算"), S->GetBalance().InFlightML, 0.0);
    TestEqual(TEXT("源销毁不重复扣除破碎余量"), S->GetBalance().RemovedML, 5.0);
    CaptureBalance(Rows, TEXT("tailReceived"), S, Source, Target);

    ACupActor* Donor = T.Vessel<ACupActor>(10.f, 1.f, CoffeeId, FTransform(FVector(100, 0, 0)));
    TestTrue(TEXT("目标杯破碎成功"), Target->BreakContainerForImpact());
    const int32 TargetRevision = Target->GetLiquidRevision();
    Target->bAcceptLiquidFromOthers = true;
    TestEqual(TEXT("破损目标直接加液拒绝"), Target->AddLiquid(10.f, FLinearColor::White), 0.f);
    TestEqual(TEXT("破损目标成分加液拒绝"), Target->AddIngredientLiquid(10.f, CoffeeId, FLinearColor::Red), 0.f);
    TestEqual(TEXT("破损目标消耗拒绝"), Target->ConsumeLiquid(1.f), 0.f);
    Target->SetCapacityML(200.f);
    TestEqual(TEXT("破损目标容量仍为 100"), Target->MaxVolumeML, 100.f);
    TestEqual(TEXT("向破损目标发射真实 10 mL 包"),
        S->EmitVolume(Donor, 10.0, FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 10.0);
    CaptureBalance(Rows, TEXT("packetToBrokenTarget"), S, Donor, Target);
    S->Simulate(3.5);
    TestEqual(TEXT("打开接液标志也不能给破损目标记账"), Target->GetPreciseVolumeML(), 0.0);
    TestEqual(TEXT("破损目标不推进 revision"), Target->GetLiquidRevision(), TargetRevision);
    TestEqual(TEXT("原余量和拒收包各结算一次"), S->GetBalance().SpilledML, 105.0);
    TestEqual(TEXT("无在途残留"), S->GetBalance().InFlightML, 0.0);
    S->Simulate(3.5);
    TestEqual(TEXT("重复模拟不重洒"), S->GetBalance().SpilledML, 105.0);
    CaptureBalance(Rows, TEXT("brokenTargetSettled"), S, Donor, Target);
    CheckBalanced(*this, TEXT("源尾流与破损目标账本闭合"), S);
    return SaveGroup(*this, TEXT("SourceInFlightSpillAndBrokenCredit"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassFrozenCupTest,
    "CyberCafe.Glass.FrozenCupRefusesBreak",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassFrozenCupTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FGlassTestWorld T;
    ACupActor* Cup = T.Vessel<ACupActor>(200.f, .5f, CoffeeId);
    UGlassBreakableComponent* Comp = T.AttachBreakable(Cup, T.Profile(), true);
    T.BeginPlay();
    TestEqual(TEXT("加入真实 50 mL 奶"), Cup->AddIngredientLiquid(50.f, MilkId, FLinearColor::White), 50.f);
    Cup->FreezeLiquidForServing();
    AdvanceSamples(Comp);
    const int32 Revision = Cup->GetLiquidRevision();
    const FTransform Transform = Cup->GetActorTransform();
    const bool bVisible = Cup->ContainerMesh->IsVisible();
    const bool bActive = Cup->GrabComp->IsActive();
    TArray<FString> Rows;
    CaptureBalance(Rows, TEXT("frozen"), T.Transfers(), Cup, nullptr);
    TestFalse(TEXT("冻结杯业务破碎 API 拒绝"), Cup->BreakContainerForImpact());
    TestFalse(TEXT("热身后的高速 impact 仍不能砸碎冻结杯"),
        Comp->TryImpact(Cup->GetActorLocation(), FVector::UpVector, FVector(0, 0, -1000)));
    TestFalse(TEXT("冻结杯未置 broken"), Cup->IsBroken());
    TestTrue(TEXT("保持 frozen"), Cup->IsLiquidFrozen());
    TestEqual(TEXT("冻结拒绝保留 150 mL"), Cup->GetPreciseVolumeML(), 150.0);
    TestEqual(TEXT("冻结拒绝保留 Coffee 100 mL"), Cup->GetIngredientVolumeML(CoffeeId), 100.0);
    TestEqual(TEXT("冻结拒绝保留 Milk 50 mL"), Cup->GetIngredientVolumeML(MilkId), 50.0);
    TestEqual(TEXT("冻结拒绝不推进 revision"), Cup->GetLiquidRevision(), Revision);
    TestEqual(TEXT("拒绝不隐藏杯身"), Cup->ContainerMesh->IsVisible(), bVisible);
    TestEqual(TEXT("拒绝不停用抓取"), Cup->GrabComp->IsActive(), bActive);
    TestTrue(TEXT("拒绝不移动冻结杯"), Cup->GetActorTransform().Equals(Transform));
    TestEqual(TEXT("冻结拒绝不洒出"), T.Transfers()->GetBalance().SpilledML, 0.0);
    ACupActor* Donor = T.Vessel<ACupActor>(10.f, 1.f, MilkId, FTransform(FVector(100, 0, 0)));
    TestEqual(TEXT("对冻结杯发射真实包"), T.Transfers()->EmitVolume(Donor, 10.0,
        FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 10.0);
    T.Transfers()->Simulate(3.5);
    TestEqual(TEXT("冻结快照不受尾流影响"), Cup->GetPreciseVolumeML(), 150.0);
    TestEqual(TEXT("冻结快照 revision 不变"), Cup->GetLiquidRevision(), Revision);
    TestEqual(TEXT("未接受的包独立洒出"), T.Transfers()->GetBalance().SpilledML, 10.0);
    CaptureBalance(Rows, TEXT("frozenPacketRejected"), T.Transfers(), Donor, Cup);
    CheckBalanced(*this, TEXT("冻结拒绝后账本闭合"), T.Transfers());
    return SaveGroup(*this, TEXT("FrozenCupRefusesBreak"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassTriggerOnceTest,
    "CyberCafe.Glass.TriggerOnceGentleTangentialAndGrace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassTriggerOnceTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FGlassTestWorld T;
    ACupActor* Cup = T.Vessel<ACupActor>(100.f, 1.f, MilkId);
    UGlassBreakProfile* P = T.Profile();
    UGlassBreakableComponent* Comp = T.AttachBreakable(Cup, P, true);
    T.BeginPlay();
    TestTrue(TEXT("组件先 AddInstance/Register，再随世界 BeginPlay"), Comp->HasBegunPlay());
    TestNotNull(TEXT("瞬态采样网格有效，未走 Missing SM 早退"), Cup->ContainerMesh->GetStaticMesh().Get());
    const FVector Normal = FVector::UpVector;
    const FVector Fast(0, 0, -1000);
    TestFalse(TEXT("启动保护拒绝高速冲击"), Comp->TryImpact(FVector::ZeroVector, Normal, Fast));
    AdvanceSamples(Comp);
    Cup->SetActorLocation(FVector(Comp->MaxTrackingJumpCM + 1.f, 0, 0));
    Comp->TickComponent(.02f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("追踪跳变重新设置保护"), Comp->TryImpact(Cup->GetActorLocation(), Normal, Fast));
    AdvanceSamples(Comp, 8);
    Comp->TickComponent(.2f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Dt 超过 .12 会重新设置保护，不能用于热身"),
        Comp->TryImpact(Cup->GetActorLocation(), Normal, Fast));
    AdvanceSamples(Comp, 8);
    const FVector Contact = Cup->GetActorLocation();
    TestFalse(TEXT("轻放低于阈值不破碎"), Comp->TryImpact(Contact, Normal, FVector(0, 0, -259.99)));
    TestFalse(TEXT("高速切向擦过不破碎"), Comp->TryImpact(Contact, Normal, FVector(5000, 0, 0)));
    TestFalse(TEXT("高速离开表面不破碎"), Comp->TryImpact(Contact, Normal, -Fast));
    TestFalse(TEXT("零法线不视为命中"), Comp->TryImpact(Contact, FVector::ZeroVector, Fast));
    TestFalse(TEXT("自身不能作为冲击目标"), Comp->TryImpact(Contact, Normal, Fast, Cup));
    FVector Invalid = FVector::ZeroVector;
    Invalid.X = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("无效 contact 拒绝"), Comp->TryImpact(Invalid, Normal, Fast));
    TestFalse(TEXT("无效 normal 拒绝"), Comp->TryImpact(Contact, Invalid, Fast));
    TestFalse(TEXT("无效 velocity 拒绝"), Comp->TryImpact(Contact, Normal, Invalid));
    Invalid.X = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("无穷 contact 拒绝"), Comp->TryImpact(Invalid, Normal, Fast));
    TestFalse(TEXT("无穷 normal 拒绝"), Comp->TryImpact(Contact, Invalid, Fast));
    TestFalse(TEXT("无穷 velocity 拒绝"), Comp->TryImpact(Contact, Normal, Invalid));
    Comp->Profile = nullptr;
    TestFalse(TEXT("空 profile 明确拒绝"), Comp->TryImpact(Contact, Normal, Fast));
    Comp->Profile = P;
    for (float Threshold : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()})
    {
        P->BreakSpeedCms = Threshold;
        TestFalse(TEXT("无效 profile 阈值拒绝"), Comp->TryImpact(Contact, Normal, Fast));
    }
    P->BreakSpeedCms = 260.f;
    TestFalse(TEXT("所有负向冲击均保留完整杯"), Cup->IsBroken());
    TestEqual(TEXT("负向冲击没有洒出"), T.Transfers()->GetBalance().SpilledML, 0.0);
    TArray<FString> Rows;
    CaptureBalance(Rows, TEXT("allRejected"), T.Transfers(), Cup, nullptr);
    TestTrue(TEXT("保护消退后恰好阈值的闭合冲击触发"),
        Comp->TryImpact(Contact, Normal, FVector(0, 0, -260)));
    TestTrue(TEXT("杯子破碎标记"), Cup->IsBroken());
    TestFalse(TEXT("杯身隐藏"), Cup->ContainerMesh->IsVisible());
    TestFalse(TEXT("抓取停用"), Cup->GrabComp->IsActive());
    TestFalse(TEXT("原 Actor 停止 Tick"), Cup->IsActorTickEnabled());
    TestFalse(TEXT("原 Actor 关闭碰撞"), Cup->GetActorEnableCollision());
    TestFalse(TEXT("采样组件停止 Tick"), Comp->IsComponentTickEnabled());
    TestFalse(TEXT("液体 FX 隐藏"), Cup->LiquidFX->IsVisible());
    TestFalse(TEXT("倒液 FX 隐藏"), Cup->PourFX->IsVisible());
    const int32 Revision = Cup->GetLiquidRevision();
    TestFalse(TEXT("破碎后再高速冲击拒绝"), Comp->TryImpact(Contact, Normal, Fast));
    TestFalse(TEXT("业务破碎重入拒绝"), Cup->BreakContainerForImpact());
    TestEqual(TEXT("一次破碎只洒出 100 mL"), T.Transfers()->GetBalance().SpilledML, 100.0);
    TestEqual(TEXT("重复冲击不推进 revision"), Cup->GetLiquidRevision(), Revision);
    int32 DebrisBatches = 0;
    for (TActorIterator<AGlassDebrisActor> It(T.World); It; ++It) ++DebrisBatches;
    TestEqual(TEXT("无碎片资产也只生成一个业务 debris 批次"), DebrisBatches, 1);
    Rows.Add(FString::Printf(TEXT("{\"debrisBatches\":%d,\"thresholdCms\":%.17g}"),
        DebrisBatches, static_cast<double>(P->BreakSpeedCms)));
    CaptureBalance(Rows, TEXT("brokenOnce"), T.Transfers(), Cup, nullptr);
    CheckBalanced(*this, TEXT("一次触发账本闭合"), T.Transfers());
    return SaveGroup(*this, TEXT("TriggerOnceGentleTangentialAndGrace"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassRecoveryLedgerTest,
    "CyberCafe.Glass.RecoverySingleAndLedgerInjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassRecoveryLedgerTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FGlassTestWorld T;
    // 经理先生成，确保 BeginPlay 快照取得原始 Fill，而不是原型启动刷新后的表现值。
    AGlassRecoveryActor* Manager = T.World->SpawnActor<AGlassRecoveryActor>();
    const FTransform CupTransform(FRotator(0, 35, 0), FVector(150, 20, 60));
    const FTransform BottleTransform(FRotator(0, -25, 0), FVector(300, -40, 70));
    ACupActor* Cup = T.Vessel<ACupActor>(150.f, 0.f, CoffeeId, CupTransform);
    ABottleActor* Bottle = T.Vessel<ABottleActor>(200.f, .5f, MilkId, BottleTransform);
    ACupActor* Target = T.Vessel<ACupActor>(100.f, 0.f, CoffeeId);
    UGlassBreakProfile* CupProfile = T.Profile(150.f);
    UGlassBreakProfile* BottleProfile = T.Profile();
    Manager->BindPrototype(Cup, Bottle, CupProfile, BottleProfile);
    TestTrue(TEXT("公开原型杯绑定"), Manager->PrototypeCup.Get() == Cup);
    TestTrue(TEXT("公开原型瓶绑定"), Manager->PrototypeBottle.Get() == Bottle);
    T.BeginPlay();
    TestTrue(TEXT("经理通过 NotifyBeginPlay 正常启动"), Manager->HasActorBegunPlay());
    TestNotNull(TEXT("fixture 真实 GI 总线有效"), T.GI->GetSubsystem<UCyberCafeEventSubsystem>());
    TestTrue(TEXT("槽位零是原型杯"), Manager->GetCurrent(0) == Cup);
    TestTrue(TEXT("槽位一是原型瓶"), Manager->GetCurrent(1) == Bottle);
    UGlassBreakableComponent* CupComp = Cup->FindComponentByClass<UGlassBreakableComponent>();
    UGlassBreakableComponent* BottleComp = Bottle->FindComponentByClass<UGlassBreakableComponent>();
    if (!TestNotNull(TEXT("经理自动安装杯破碎组件"), CupComp) ||
        !TestNotNull(TEXT("经理自动安装瓶破碎组件"), BottleComp)) return false;
    TestTrue(TEXT("安装杯 profile"), CupComp->Profile.Get() == CupProfile);
    TestTrue(TEXT("安装瓶 profile"), BottleComp->Profile.Get() == BottleProfile);
    TestTrue(TEXT("安装组件都已正常 BeginPlay"), CupComp->HasBegunPlay() && BottleComp->HasBegunPlay());
    ULiquidTransferSubsystem* S = T.Transfers();
    TArray<FString> Rows;
    const auto CaptureSlots = [&](const TCHAR* Stage, const ALiquidContainerActor* PreviousCup,
        const ALiquidContainerActor* PreviousBottle)
    {
        Rows.Add(FString::Printf(
            TEXT("{\"stage\":\"%s\",\"canRecoverCup\":%s,\"canRecoverBottle\":%s,\"cupReferenceChanged\":%s,\"bottleReferenceChanged\":%s}"),
            Stage, Manager->CanRecover(0) ? TEXT("true") : TEXT("false"),
            Manager->CanRecover(1) ? TEXT("true") : TEXT("false"),
            Manager->GetCurrent(0) != PreviousCup ? TEXT("true") : TEXT("false"),
            Manager->GetCurrent(1) != PreviousBottle ? TEXT("true") : TEXT("false")));
    };
    TestEqual(TEXT("原型初始奶为 100 mL"), S->GetBalance().InitialML, 100.0);
    TestEqual(TEXT("原型不误记为替换注入"), S->GetBalance().InjectedML, 0.0);
    CaptureBalance(Rows, TEXT("prototypes"), S, Bottle, Cup);
    for (int32 Index : {-1, 0, 1, 2})
    {
        TestFalse(TEXT("完整容器或非法索引不能恢复"), Manager->CanRecover(Index));
        TestFalse(TEXT("完整容器或非法索引不能重复生成"), Manager->Recover(Index));
    }
    TestNull(TEXT("负索引无当前容器"), Manager->GetCurrent(-1));
    TestNull(TEXT("越界索引无当前容器"), Manager->GetCurrent(2));
    TestTrue(TEXT("完整拒绝不替换杯"), Manager->GetCurrent(0) == Cup);
    TestTrue(TEXT("完整拒绝不替换瓶"), Manager->GetCurrent(1) == Bottle);

    Bottle->SetActorLocation(FVector(700, 80, 10));
    Bottle->SetCapacityML(120.f);
    Bottle->InitialIngredientId = TEXT("Water");
    Bottle->LiquidColor = FLinearColor::Green;
    TestEqual(TEXT("原型运行时消耗 10 mL"), Bottle->ConsumeLiquid(10.f), 10.f);
    TestEqual(TEXT("原型运行时发射 20 mL"), S->EmitVolume(Bottle, 20.0,
        FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 20.0);
    AdvanceSamples(BottleComp);
    TestTrue(TEXT("原型瓶通过真实冲击 API 破碎"),
        BottleComp->TryImpact(Bottle->GetActorLocation(), FVector::UpVector, FVector(0, 0, -400)));
    TestTrue(TEXT("只有破损瓶可恢复"), Manager->CanRecover(1));
    TestFalse(TEXT("完整杯仍不能恢复"), Manager->CanRecover(0));
    TestEqual(TEXT("原型只洒权威余量 70 mL"), S->GetBalance().SpilledML, 70.0);
    CaptureBalance(Rows, TEXT("oldBottleBroken"), S, Bottle, Cup);
    CaptureSlots(TEXT("brokenBottleEligibility"), Cup, Bottle);
    ALiquidContainerActor* const OldBottle = Manager->GetCurrent(1);
    const FLiquidBalance BeforeBottleRecovery = S->GetBalance();
    if (!TestTrue(TEXT("破损瓶单次恢复成功"), Manager->Recover(1))) return false;
    ALiquidContainerActor* NewBottle = Manager->GetCurrent(1);
    if (!TestNotNull(TEXT("恢复后当前瓶有效"), NewBottle)) return false;
    // Destroy 后旧地址可能暂时有效，只比较身份，不要求立即 GC，也不再解引用旧地址。
    TestTrue(TEXT("恢复明确替换旧引用"), NewBottle != OldBottle);
    TestTrue(TEXT("恢复瓶保持同 class"), NewBottle->GetClass() == ABottleActor::StaticClass());
    TestTrue(TEXT("恢复捕获的 spawn transform，而非破碎地点"), NewBottle->GetActorTransform().Equals(BottleTransform));
    TestEqual(TEXT("恢复捕获的容量而非运行时容量"), NewBottle->MaxVolumeML, 200.f);
    TestEqual(TEXT("恢复捕获的 .5 Fill"), NewBottle->FillAmount, .5f);
    TestEqual(TEXT("恢复捕获的 Milk ID 而非运行时 Water"), NewBottle->InitialIngredientId, MilkId);
    TestTrue(TEXT("恢复捕获的颜色"), NewBottle->LiquidColor.Equals(FLinearColor::White));
    TestEqual(TEXT("恢复权威奶量 100 mL"), NewBottle->GetIngredientVolumeML(MilkId), 100.0);
    TestFalse(TEXT("新瓶不继承 broken"), NewBottle->IsBroken());
    TestFalse(TEXT("新瓶不继承 frozen"), NewBottle->IsLiquidFrozen());
    TestTrue(TEXT("新瓶有 replacement 标记"), NewBottle->bInitialVolumeIsReplacement);
    TestTrue(TEXT("新瓶恢复可见抓取组件"), NewBottle->ContainerMesh->IsVisible() && NewBottle->GrabComp->IsActive());
    TestEqual(TEXT("恢复不重置历史 Initial"), S->GetBalance().InitialML, BeforeBottleRecovery.InitialML);
    TestEqual(TEXT("恢复奶只计一次注入 100 mL"), S->GetBalance().InjectedML, 100.0);
    TestEqual(TEXT("旧瓶 Destroy 不重复记消耗"), S->GetBalance().RemovedML, BeforeBottleRecovery.RemovedML);
    TestEqual(TEXT("恢复不清理旧瓶的在途 20 mL"), S->GetBalance().InFlightML, 20.0);
    TestTrue(TEXT("恢复瓶不替换另一槽的杯"), Manager->GetCurrent(0) == Cup);
    UGlassBreakableComponent* NewBottleComp = NewBottle->FindComponentByClass<UGlassBreakableComponent>();
    if (!TestNotNull(TEXT("新瓶自动安装破碎组件"), NewBottleComp)) return false;
    TestTrue(TEXT("新瓶沿用绑定 profile 且正常 BeginPlay"),
        NewBottleComp->Profile.Get() == BottleProfile && NewBottleComp->HasBegunPlay());
    FGlassTestWorld::SuppressSimulation(NewBottle);
    CaptureBalance(Rows, TEXT("newBottleInjected"), S, NewBottle, Cup);
    CaptureSlots(TEXT("newBottleIdentity"), Cup, OldBottle);
    CheckBalanced(*this, TEXT("瓶恢复瞬间账本闭合"), S);
    TestFalse(TEXT("新瓶完整，不可再次恢复"), Manager->CanRecover(1));
    TestFalse(TEXT("重复按钮不克隆新瓶"), Manager->Recover(1));
    TestTrue(TEXT("重复恢复保留当前新瓶身份"), Manager->GetCurrent(1) == NewBottle);
    S->Simulate(.3);
    TestEqual(TEXT("恢复和旧瓶 Destroy 后原包仍到达"), Target->GetIngredientVolumeML(MilkId), 20.0);
    TestEqual(TEXT("原包只结算一次"), S->GetBalance().InFlightML, 0.0);
    CaptureBalance(Rows, TEXT("oldTailReceived"), S, NewBottle, Target);

    Cup->SetActorLocation(FVector(900, 50, 10));
    Cup->SetCapacityML(60.f);
    AdvanceSamples(CupComp);
    TestTrue(TEXT("空杯也可通过冲击破碎"),
        CupComp->TryImpact(Cup->GetActorLocation(), FVector::UpVector, FVector(0, 0, -150)));
    TestTrue(TEXT("破损空杯可恢复"), Manager->CanRecover(0));
    CaptureBalance(Rows, TEXT("oldEmptyCupBroken"), S, NewBottle, Cup);
    CaptureSlots(TEXT("brokenCupEligibility"), Cup, NewBottle);
    ALiquidContainerActor* const OldCup = Manager->GetCurrent(0);
    const FLiquidBalance BeforeCupRecovery = S->GetBalance();
    if (!TestTrue(TEXT("空杯恢复成功"), Manager->Recover(0))) return false;
    ALiquidContainerActor* NewCup = Manager->GetCurrent(0);
    if (!TestNotNull(TEXT("恢复后当前杯有效"), NewCup)) return false;
    TestTrue(TEXT("新杯身份不同且同 class"), NewCup != OldCup && NewCup->GetClass() == ACupActor::StaticClass());
    TestTrue(TEXT("新杯恢复原 spawn transform"), NewCup->GetActorTransform().Equals(CupTransform));
    TestEqual(TEXT("新杯恢复原容量 150"), NewCup->MaxVolumeML, 150.f);
    TestEqual(TEXT("新杯恢复原初始空 Fill"), NewCup->FillAmount, 0.f);
    TestEqual(TEXT("新杯权威量为零"), NewCup->GetPreciseVolumeML(), 0.0);
    TestEqual(TEXT("新杯恢复初始 Coffee ID"), NewCup->InitialIngredientId, CoffeeId);
    TestTrue(TEXT("新杯设置 replacement 标记"), NewCup->bInitialVolumeIsReplacement);
    TestFalse(TEXT("新杯不继承破碎或冻结"), NewCup->IsBroken() || NewCup->IsLiquidFrozen());
    TestTrue(TEXT("恢复杯不替换已恢复瓶"), Manager->GetCurrent(1) == NewBottle);
    TestEqual(TEXT("空杯恢复不虚构注入"), S->GetBalance().InjectedML, BeforeCupRecovery.InjectedML);
    TestEqual(TEXT("空杯恢复不重记 Initial"), S->GetBalance().InitialML, BeforeCupRecovery.InitialML);
    UGlassBreakableComponent* NewCupComp = NewCup->FindComponentByClass<UGlassBreakableComponent>();
    if (!TestNotNull(TEXT("新杯自动安装破碎组件"), NewCupComp)) return false;
    TestTrue(TEXT("新杯沿用杯 profile"), NewCupComp->Profile.Get() == CupProfile);
    FGlassTestWorld::SuppressSimulation(NewCup);
    TestFalse(TEXT("新空杯完整，不可重复恢复"), Manager->CanRecover(0));
    TestFalse(TEXT("重复空杯按钮拒绝"), Manager->Recover(0));
    TestTrue(TEXT("重复恢复保留新杯身份"), Manager->GetCurrent(0) == NewCup);
    S->Simulate(3.5);
    TestEqual(TEXT("恢复后的在途不重复抵达"), Target->GetPreciseVolumeML(), 20.0);
    TestEqual(TEXT("全过程只有 100 mL 替换注入"), S->GetBalance().InjectedML, 100.0);
    CaptureBalance(Rows, TEXT("newEmptyCup"), S, NewBottle, NewCup);
    CaptureSlots(TEXT("newCupIdentity"), OldCup, NewBottle);
    CheckBalanced(*this, TEXT("杯瓶恢复与旧尾流账本闭合"), S);
    return SaveGroup(*this, TEXT("RecoverySingleAndLedgerInjection"), Rows);
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
