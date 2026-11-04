// @trace IC-01 IC-02 IC-03 IC-04
// 合成 World 契约测试，不代表硬件、网格或音效验收；仅运行时导出真实 getter/账本采样。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Liquid/IceInteraction.h"
#include "Liquid/CupActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "GrabComponent.h"
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

namespace IceInteractionTests
{
const FName CoffeeId(TEXT("Coffee"));
const FName MilkId(TEXT("Milk"));
const FName WaterId(TEXT("Water"));

struct FIceTestWorld
{
    UWorld* World;
    UGameInstance* GI;
    TArray<ACupActor*> Cups;

    FIceTestWorld()
    {
        // 与 GlassBreakageTests 一致：真实 GI/WorldContext，允许物理场景但不推进物理。
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

    ~FIceTestWorld()
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    static UStaticMesh* BasicMesh()
    {
        // 只复用原生冰类已有的标准 Engine Cube fallback，不加载项目 BP 或新增资产。
        return GetDefault<AIceCubeActor>()->Mesh->GetStaticMesh().Get();
    }

    static void SuppressMesh(UStaticMeshComponent* Mesh)
    {
        Mesh->SetSimulatePhysics(false);
        Mesh->SetEnableGravity(false);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
    }

    static void SuppressGrab(UGrabComponent* Grab)
    {
        Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
        Grab->bSimulateOnDrop = false;
        Grab->bCaptureHand = false;
        Grab->OnGrabHapticEffect = nullptr;
    }

    static void SuppressCup(ACupActor* Cup)
    {
        SuppressMesh(Cup->ContainerMesh.Get());
        Cup->bCanPour = false;
        Cup->bEnableSplash = false;
        Cup->LiquidFXTemplate = nullptr;
        Cup->SplashEffectTemplate = nullptr;
        Cup->LiquidFX->SetAutoActivate(false);
        Cup->LiquidFX->SetAsset(nullptr);
        Cup->LiquidFX->DeactivateImmediate();
        Cup->PourFX->SetAutoActivate(false);
        Cup->PourFX->SetAsset(nullptr);
        Cup->PourFX->DeactivateImmediate();
        SuppressGrab(Cup->GrabComp.Get());
    }

    ACupActor* Cup(const FTransform& Transform = FTransform::Identity,
        float CapacityML = 300.f, float InitialFill = 0.f)
    {
        ACupActor* A = World->SpawnActorDeferred<ACupActor>(ACupActor::StaticClass(),
            Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        SuppressCup(A);
        A->ContainerMesh->SetStaticMesh(BasicMesh());
        A->MaxVolumeML = CapacityML;
        A->FillAmount = InitialFill;
        A->InitialIngredientId = CoffeeId;
        A->LiquidColor = FLinearColor::White;
        A->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector);
        A->PourEntryPoint->SetRelativeRotation(FRotator::ZeroRotator);
        A->PourEntryPoint->SetRelativeScale3D(FVector::OneVector);
        A->PourEntryRadius = 3.3f;
        A->VolumeWidgetClass = nullptr;
        A->bFaceCamera = false;
        A->GrabComp->GrabType = EGrabType::Free;
        A->FinishSpawning(Transform);
        SuppressCup(A);
        Cups.Add(A);
        return A;
    }

    UCupIceComponent* AttachIce(ACupActor* A)
    {
        UCupIceComponent* Component = NewObject<UCupIceComponent>(A);
        Component->InteriorRadiusCM = 3.3f;
        Component->InteriorDepthCM = 8.f;
        Component->MaxCupIce = 6;
        Component->ClinkSound = nullptr;
        A->AddInstanceComponent(Component);
        Component->RegisterComponent();
        return Component;
    }

    AIceBucketActor* Bucket(const FTransform& Transform = FTransform::Identity)
    {
        AIceBucketActor* A = World->SpawnActorDeferred<AIceBucketActor>(
            AIceBucketActor::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        A->LoadRadiusCM = 9.f;
        A->LoadMinZCM = 8.f;
        A->LoadMaxZCM = 20.f;
        SuppressMesh(A->Mesh.Get());
        A->FinishSpawning(Transform);
        return A;
    }

    AIceScoopActor* Scoop(AIceBucketActor* BucketActor)
    {
        const FTransform BucketTransform = BucketActor->Mesh->GetComponentTransform();
        const FTransform Transform(BucketTransform.GetRotation(),
            BucketTransform.TransformPosition(FVector(-10, 0, 12)));
        AIceScoopActor* A = World->SpawnActorDeferred<AIceScoopActor>(
            AIceScoopActor::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        A->Mesh->SetStaticMesh(BasicMesh());
        A->Bucket = BucketActor;
        A->IceClass = AIceCubeActor::StaticClass();
        A->IcePerScoop = 3;
        A->BowlCenter = FVector(10, 0, 0);
        A->PourTiltDegrees = 55.f;
        A->ReleaseIntervalSeconds = .16f;
        A->ScoopSound = nullptr;
        SuppressMesh(A->Mesh.Get());
        SuppressGrab(A->Grab.Get());
        A->FinishSpawning(Transform);
        SuppressMesh(A->Mesh.Get());
        SuppressGrab(A->Grab.Get());
        return A;
    }

    AIceCubeActor* Cube(const FVector& Position, float RadiusCM = 1.25f)
    {
        const FTransform Transform(Position);
        AIceCubeActor* A = World->SpawnActorDeferred<AIceCubeActor>(
            AIceCubeActor::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        A->RadiusCM = RadiusCM;
        A->FreeLifetimeSeconds = 12.f;
        A->FinishSpawning(Transform);
        SuppressMesh(A->Mesh.Get());
        return A;
    }

    void SuppressIceSimulation()
    {
        for (TActorIterator<AIceCubeActor> It(World); It; ++It) SuppressMesh(It->Mesh.Get());
        for (TActorIterator<AIceScoopActor> It(World); It; ++It)
        {
            SuppressMesh(It->Mesh.Get());
            SuppressGrab(It->Grab.Get());
        }
        for (ACupActor* A : Cups) SuppressCup(A);
    }

    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        // 原生 BeginPlay/ReleaseToWorld 会打开物理；只关模拟，不伪造冰业务状态。
        SuppressIceSimulation();
    }

    UIceWorldSubsystem* IceSystem() const { return World->GetSubsystem<UIceWorldSubsystem>(); }
    ULiquidTransferSubsystem* Transfers() const { return World->GetSubsystem<ULiquidTransferSubsystem>(); }
};

void TickCup(UCupIceComponent* Component, int32 Frames = 100)
{
    for (int32 I = 0; I < Frames; ++I) Component->TickComponent(.02f, LEVELTICK_All, nullptr);
}

struct FLiquidSnapshot
{
    double Volume;
    FLiquidComposition Composition;
    float Capacity;
    float Fill;
    int32 Revision;
    FLiquidBalance Balance;

    FLiquidSnapshot(const ACupActor* Cup, ULiquidTransferSubsystem* Transfers)
        : Volume(Cup->GetPreciseVolumeML()), Composition(Cup->GetComposition()),
          Capacity(Cup->MaxVolumeML), Fill(Cup->FillAmount), Revision(Cup->GetLiquidRevision()),
          Balance(Transfers->GetBalance()) {}
};

void CheckLiquidUnchanged(FAutomationTestBase& Test, const TCHAR* Stage,
    const FLiquidSnapshot& Before, ACupActor* Cup, ULiquidTransferSubsystem* Transfers)
{
    const FString Prefix = FString(Stage) + TEXT(": ");
    Test.TestEqual(Prefix + TEXT("权威 mL 不变"), Cup->GetPreciseVolumeML(), Before.Volume);
    Test.TestEqual(Prefix + TEXT("容量不变"), Cup->MaxVolumeML, Before.Capacity);
    Test.TestEqual(Prefix + TEXT("Fill 不变"), Cup->FillAmount, Before.Fill);
    Test.TestEqual(Prefix + TEXT("revision 不变"), Cup->GetLiquidRevision(), Before.Revision);
    const FLiquidComposition After = Cup->GetComposition();
    Test.TestEqual(Prefix + TEXT("成分项数不变"), After.Ingredients.Num(), Before.Composition.Ingredients.Num());
    Test.TestEqual(Prefix + TEXT("成分总量不变"), After.Total(), Before.Composition.Total());
    for (const auto& Pair : Before.Composition.Ingredients)
        Test.TestEqual(Prefix + Pair.Key.ToString(), After.Get(Pair.Key), Pair.Value);
    const FLiquidBalance B = Transfers->GetBalance();
    Test.TestEqual(Prefix + TEXT("InitialML 不变"), B.InitialML, Before.Balance.InitialML);
    Test.TestEqual(Prefix + TEXT("InjectedML 不变"), B.InjectedML, Before.Balance.InjectedML);
    Test.TestEqual(Prefix + TEXT("RemovedML 不变"), B.RemovedML, Before.Balance.RemovedML);
    Test.TestEqual(Prefix + TEXT("ContainerML 不变"), B.ContainerML, Before.Balance.ContainerML);
    Test.TestEqual(Prefix + TEXT("InFlightML 不变"), B.InFlightML, Before.Balance.InFlightML);
    Test.TestEqual(Prefix + TEXT("SpilledML 不变"), B.SpilledML, Before.Balance.SpilledML);
    Test.TestEqual(Prefix + TEXT("PacketCount 不变"), B.PacketCount, Before.Balance.PacketCount);
    Test.TestTrue(Prefix + TEXT("账本有限且闭合"), FMath::IsFinite(B.ErrorML) && FMath::Abs(B.ErrorML) < 1.e-6);
}

void Capture(TArray<FString>& Rows, const TCHAR* Stage, const FIceTestWorld& T,
    const AIceScoopActor* Scoop = nullptr, const UCupIceComponent* CupIce = nullptr,
    const ACupActor* Cup = nullptr)
{
    const FLiquidBalance B = T.Transfers()->GetBalance();
    TArray<FString> Cubes;
    int32 FreeCount = 0, InScoopCount = 0, InCupCount = 0;
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
    {
        const AIceCubeActor* Ice = *It;
        if (!IsValid(Ice) || Ice->IsActorBeingDestroyed()) continue;
        const TCHAR* State = TEXT("Free");
        if (Ice->State == EIceState::Free) ++FreeCount;
        else if (Ice->State == EIceState::InScoop) { State = TEXT("InScoop"); ++InScoopCount; }
        else if (Ice->State == EIceState::InCup) { State = TEXT("InCup"); ++InCupCount; }
        Cubes.Add(FString::Printf(
            TEXT("{\"id\":%u,\"state\":\"%s\",\"hasCupIce\":%s,\"hasScoop\":%s,\"radiusCM\":%.17g,\"simulating\":%s,\"xCM\":%.17g,\"yCM\":%.17g,\"zCM\":%.17g}"),
            Ice->GetUniqueID(), State, Ice->CupIce ? TEXT("true") : TEXT("false"),
            Ice->Scoop ? TEXT("true") : TEXT("false"), static_cast<double>(Ice->RadiusCM),
            Ice->Mesh->IsSimulatingPhysics() ? TEXT("true") : TEXT("false"),
            Ice->GetActorLocation().X, Ice->GetActorLocation().Y, Ice->GetActorLocation().Z));
    }
    FString CupJson(TEXT("null"));
    if (Cup)
        CupJson = FString::Printf(
            TEXT("{\"volumeML\":%.17g,\"coffeeML\":%.17g,\"milkML\":%.17g,\"waterML\":%.17g,\"compositionML\":%.17g,\"ingredientCount\":%d,\"capacityML\":%.17g,\"fill\":%.17g,\"revision\":%d,\"frozen\":%s,\"broken\":%s}"),
            Cup->GetPreciseVolumeML(), Cup->GetIngredientVolumeML(CoffeeId),
            Cup->GetIngredientVolumeML(MilkId), Cup->GetIngredientVolumeML(WaterId),
            Cup->GetComposition().Total(), Cup->GetComposition().Ingredients.Num(),
            static_cast<double>(Cup->MaxVolumeML), static_cast<double>(Cup->FillAmount),
            Cup->GetLiquidRevision(), Cup->IsLiquidFrozen() ? TEXT("true") : TEXT("false"),
            Cup->IsBroken() ? TEXT("true") : TEXT("false"));
    Rows.Add(FString::Printf(
        TEXT("{\"stage\":\"%s\",\"liveIce\":%d,\"maxLiveIce\":%d,\"loadedCount\":%d,\"cupIceCount\":%d,\"freeCount\":%d,\"inScoopCount\":%d,\"inCupCount\":%d,\"initialML\":%.17g,\"injectedML\":%.17g,\"removedML\":%.17g,\"containerML\":%.17g,\"inFlightML\":%.17g,\"spilledML\":%.17g,\"errorML\":%.17g,\"packetCount\":%d,\"cup\":%s,\"cubes\":[%s]}"),
        Stage, T.IceSystem()->GetLiveCount(), T.IceSystem()->MaxLiveIce,
        Scoop ? Scoop->GetLoadedCount() : 0, CupIce ? CupIce->GetIceCount() : 0,
        FreeCount, InScoopCount, InCupCount, B.InitialML, B.InjectedML, B.RemovedML,
        B.ContainerML, B.InFlightML, B.SpilledML, B.ErrorML, B.PacketCount,
        *CupJson, *FString::Join(Cubes, TEXT(","))));
}

bool SaveGroup(FAutomationTestBase& Test, const TCHAR* Group, const TArray<FString>& Rows)
{
    // 只汇总本进程真正执行的组，不读取旧 JSON 补造结果，也不在创建源码时输出采样。
    static TMap<FString, FString> CapturedGroups;
    CapturedGroups.Add(Group, TEXT("[") + FString::Join(Rows, TEXT(",\n")) + TEXT("]"));
    TArray<FString> Names;
    CapturedGroups.GetKeys(Names);
    Names.Sort();
    TArray<FString> Groups;
    for (const FString& Name : Names)
        Groups.Add(TEXT("\"") + Name + TEXT("\":") + CapturedGroups[Name]);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Ice");
    if (!Test.TestTrue(TEXT("创建冰运行时采样目录"),
        IFileManager::Get().MakeDirectory(*Directory, true))) return false;
    const FString Json = TEXT("{\"sampleKind\":\"syntheticWorldPublicAPI\",\"groups\":{\n")
        + FString::Join(Groups, TEXT(",\n")) + TEXT("\n}}\n");
    return Test.TestTrue(TEXT("保存真实冰状态、计数和液体账本"),
        FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("behaviour.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceBucketBoundaryTest,
    "CyberCafe.Ice.BucketBoundaryNoRepeatedLoadInfiniteSupply",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceBucketBoundaryTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FIceTestWorld T;
    AIceBucketActor* Bucket = T.Bucket(FTransform(FVector(200, -100, 30)));
    AIceScoopActor* Scoop = T.Scoop(Bucket);
    const FTransform BucketTM = Bucket->Mesh->GetComponentTransform();
    const FTransform ScoopTM = Scoop->GetActorTransform();
    TestTrue(TEXT("圆半径与最低 Z 等值边界接纳"), Bucket->ContainsBowl(BucketTM.TransformPosition(FVector(9, 0, 8))));
    TestTrue(TEXT("最高 Z 等值边界接纳"), Bucket->ContainsBowl(BucketTM.TransformPosition(FVector(0, 9, 20))));
    TestFalse(TEXT("圆外拒绝"), Bucket->ContainsBowl(BucketTM.TransformPosition(FVector(9.1, 0, 12))));
    TestFalse(TEXT("方框角落不误当圆内"), Bucket->ContainsBowl(BucketTM.TransformPosition(FVector(8, 8, 12))));
    TestFalse(TEXT("低于 8 拒绝"), Bucket->ContainsBowl(BucketTM.TransformPosition(FVector(0, 0, 7.9))));
    TestFalse(TEXT("高于 20 拒绝"), Bucket->ContainsBowl(BucketTM.TransformPosition(FVector(0, 0, 20.1))));
    T.BeginPlay();
    if (!TestNotNull(TEXT("冰 World subsystem 存在"), T.IceSystem())) return false;
    TestTrue(TEXT("铲随真实世界 BeginPlay"), Scoop->HasActorBegunPlay());
    if (!TestNotNull(TEXT("原生冰标准 Engine Cube fallback 存在"), FIceTestWorld::BasicMesh())) return false;
    TestTrue(TEXT("装冰类显式使用原生 AIceCubeActor"), Scoop->IceClass.Get() == AIceCubeActor::StaticClass());
    TestTrue(TEXT("BowlCenter X10 抵消铲 Actor X-10，桶中心 Z12 在界内"),
        Bucket->ContainsBowl(Scoop->Mesh->GetComponentTransform().TransformPosition(Scoop->BowlCenter)));
    TArray<FString> Rows;
    Capture(Rows, TEXT("initial"), T, Scoop);
    if (!TestTrue(TEXT("初次武装进入装入原生冰"), Scoop->TryLoadFromBucket())) return false;
    TestEqual(TEXT("首批三块"), Scoop->GetLoadedCount(), 3);
    TestEqual(TEXT("首批三块注册为活冰"), T.IceSystem()->GetLiveCount(), 3);
    TestFalse(TEXT("桶内重复加载拒绝"), Scoop->TryLoadFromBucket());
    Capture(Rows, TEXT("firstBatch"), T, Scoop);
    for (int32 I = 0; I < 3; ++I) TestTrue(TEXT("逐块释放首批"), Scoop->ReleaseOne());
    T.SuppressIceSimulation();
    TestEqual(TEXT("释放后铲为空"), Scoop->GetLoadedCount(), 0);
    TestFalse(TEXT("空铲第四次释放拒绝"), Scoop->ReleaseOne());
    TestFalse(TEXT("仍在桶内不能重装"), Scoop->TryLoadFromBucket());
    for (int32 I = 0; I < 10; ++I) Scoop->Tick(.02f);
    TestEqual(TEXT("桶内连续 Tick 不重复生成"), Scoop->GetLoadedCount(), 0);
    TestEqual(TEXT("自由首批仍计活冰而非扣桶库存"), T.IceSystem()->GetLiveCount(), 3);
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
    {
        TestTrue(TEXT("首批真实释放为 Free"), It->State == EIceState::Free);
        TestNull(TEXT("首批释放清空铲指针"), It->Scoop.Get());
        TestTrue(TEXT("独立冰沿用标准 Cube fallback"), It->Mesh->GetStaticMesh().Get() == FIceTestWorld::BasicMesh());
    }
    Capture(Rows, TEXT("releasedStillInside"), T, Scoop);
    Scoop->SetActorLocation(BucketTM.TransformPosition(FVector(50, 0, 12)));
    Scoop->Tick(.02f);
    TestEqual(TEXT("出桶 Tick 只重武装，不装冰"), Scoop->GetLoadedCount(), 0);
    Scoop->SetActorTransform(ScoopTM);
    if (!TestTrue(TEXT("回桶能装第二批，无限供应不耗桶库存"), Scoop->TryLoadFromBucket())) return false;
    TestEqual(TEXT("第二批仍为三块"), Scoop->GetLoadedCount(), 3);
    TestEqual(TEXT("两批独立活冰共六块"), T.IceSystem()->GetLiveCount(), 6);
    T.SuppressIceSimulation();
    Capture(Rows, TEXT("secondBatch"), T, Scoop);
    return SaveGroup(*this, TEXT("BucketBoundaryNoRepeatedLoadInfiniteSupply"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceCupEntryTest,
    "CyberCafe.Ice.CupEntryFromTopRejectSideBelowAndSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceCupEntryTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FIceTestWorld T;
    ACupActor* Cup = T.Cup(FTransform(FRotator(0, 37, 0), FVector(100, 20, 60)));
    UCupIceComponent* CupIce = T.AttachIce(Cup);
    T.BeginPlay();
    TestTrue(TEXT("杯冰组件真实 BeginPlay"), CupIce->HasBegunPlay());
    TestTrue(TEXT("组件绑定原生杯"), CupIce->GetCup() == Cup);
    const FTransform Entry = Cup->PourEntryPoint->GetComponentTransform();
    TestTrue(TEXT("入口显式零偏移，使用杯 transform"), Entry.Equals(Cup->GetActorTransform()));
    const FVector Old = Entry.TransformPosition(FVector(0, 0, 5));
    const FVector Current = Entry.TransformPosition(FVector(0, 0, -.5));
    AIceCubeActor* Accepted = T.Cube(Current);
    TArray<FString> Rows;
    TestTrue(TEXT("半径 1.25 自由冰从上方穿口接纳"), CupIce->TryAccept(Accepted, Old, Current));
    TestTrue(TEXT("接纳后状态 InCup"), Accepted->State == EIceState::InCup);
    TestTrue(TEXT("接纳后杯组件指针正确"), Accepted->CupIce.Get() == CupIce);
    TestNull(TEXT("接纳后没有铲所有权"), Accepted->Scoop.Get());
    TestEqual(TEXT("杯计数一块"), CupIce->GetIceCount(), 1);
    TestFalse(TEXT("同冰第二次接纳拒绝"), CupIce->TryAccept(Accepted, Old, Current));
    Capture(Rows, TEXT("acceptedOnce"), T, nullptr, CupIce, Cup);
    const FVector SideOld = Entry.TransformPosition(FVector(10, 0, 5));
    const FVector SideNew = Entry.TransformPosition(FVector(10, 0, -1));
    AIceCubeActor* Side = T.Cube(SideNew);
    TestFalse(TEXT("侧面越过 rim 高度仍在壁外，拒绝"), CupIce->TryAccept(Side, SideOld, SideNew));
    AIceCubeActor* Below = T.Cube(Entry.TransformPosition(FVector(0, 0, -1)));
    TestFalse(TEXT("从杯底下方上移不算从上穿口"), CupIce->TryAccept(Below,
        Entry.TransformPosition(FVector(0, 0, -5)), Entry.TransformPosition(FVector(0, 0, -1))));
    AIceCubeActor* Large = T.Cube(Current, 5.f);
    TestFalse(TEXT("半径五厘米超过杯内半径，拒绝"), CupIce->TryAccept(Large, Old, Current));
    for (AIceCubeActor* Rejected : {Side, Below, Large})
    {
        TestTrue(TEXT("拒收冰保持 Free"), Rejected->State == EIceState::Free);
        TestNull(TEXT("拒收冰没有杯组件"), Rejected->CupIce.Get());
    }
    TestEqual(TEXT("所有拒绝不增加计数"), CupIce->GetIceCount(), 1);
    Capture(Rows, TEXT("sideBelowAndSizeRejected"), T, nullptr, CupIce, Cup);
    return SaveGroup(*this, TEXT("CupEntryFromTopRejectSideBelowAndSize"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceLiquidUnchangedTest,
    "CyberCafe.Ice.IceDoesNotChangeVolumeCapacityCompositionLiquidRevision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceLiquidUnchangedTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FIceTestWorld T;
    ACupActor* Cup = T.Cup();
    UCupIceComponent* CupIce = T.AttachIce(Cup);
    T.BeginPlay();
    TestEqual(TEXT("真实成分 API 注入 Coffee 100 mL"), Cup->AddIngredientLiquid(100.f, CoffeeId, FLinearColor::Black), 100.f);
    TestEqual(TEXT("真实成分 API 注入 Milk 100 mL"), Cup->AddIngredientLiquid(100.f, MilkId, FLinearColor::White), 100.f);
    TestEqual(TEXT("容量 300 mL"), Cup->MaxVolumeML, 300.f);
    TestTrue(TEXT("真实 200/300 Fill 接近 .66667"), FMath::IsNearlyEqual(Cup->FillAmount, 2.f / 3.f, 1.e-6f));
    TestEqual(TEXT("冰前权威液量 200 mL"), Cup->GetPreciseVolumeML(), 200.0);
    TestEqual(TEXT("冰前 Coffee 成分 100 mL"), Cup->GetIngredientVolumeML(CoffeeId), 100.0);
    TestEqual(TEXT("冰前 Milk 成分 100 mL"), Cup->GetIngredientVolumeML(MilkId), 100.0);
    TestEqual(TEXT("两次真实成分注液只计 200 mL"), T.Transfers()->GetBalance().InjectedML, 200.0);
    const FLiquidSnapshot Before(Cup, T.Transfers());
    TArray<FString> Rows;
    Capture(Rows, TEXT("mixtureBeforeIce"), T, nullptr, CupIce, Cup);
    const FTransform Entry = Cup->PourEntryPoint->GetComponentTransform();
    AIceCubeActor* Ice = T.Cube(Entry.TransformPosition(FVector(0, 0, -.5)));
    if (!TestTrue(TEXT("真实接纳冰"), CupIce->TryAccept(Ice,
        Entry.TransformPosition(FVector(0, 0, 5)), Entry.TransformPosition(FVector(0, 0, -.5))))) return false;
    CheckLiquidUnchanged(*this, TEXT("接纳立即"), Before, Cup, T.Transfers());
    TickCup(CupIce, 200);
    TestEqual(TEXT("四秒小步模拟后杯内仍有一块冰"), CupIce->GetIceCount(), 1);
    CheckLiquidUnchanged(*this, TEXT("200 帧冰表现"), Before, Cup, T.Transfers());
    Capture(Rows, TEXT("iceMotionLiquidUnchanged"), T, nullptr, CupIce, Cup);
    Cup->FreezeLiquidForServing();
    const FTransform FrozenIce = Ice->GetActorTransform();
    TickCup(CupIce, 100);
    TestTrue(TEXT("冻结标记已置位"), Cup->IsLiquidFrozen());
    TestEqual(TEXT("冻结后原有冰保留"), CupIce->GetIceCount(), 1);
    TestTrue(TEXT("冻结后冰摆放不被 Tick 改写"), Ice->GetActorTransform().Equals(FrozenIce));
    CheckLiquidUnchanged(*this, TEXT("冻结后"), Before, Cup, T.Transfers());
    TestEqual(TEXT("冰不会新增 Water"), Cup->GetIngredientVolumeML(WaterId), 0.0);
    Capture(Rows, TEXT("frozenLiquidUnchanged"), T, nullptr, CupIce, Cup);
    return SaveGroup(*this, TEXT("IceDoesNotChangeVolumeCapacityCompositionLiquidRevision"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceBrokenReleaseTest,
    "CyberCafe.Ice.FrozenBrokenReleaseAndFreshCupNoCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceBrokenReleaseTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FIceTestWorld T;
    ACupActor* FrozenCup = T.Cup(FTransform(FVector(100, 0, 0)));
    UCupIceComponent* FrozenIce = T.AttachIce(FrozenCup);
    ACupActor* Cup = T.Cup();
    UCupIceComponent* CupIce = T.AttachIce(Cup);
    T.BeginPlay();
    TArray<FString> Rows;
    FrozenCup->FreezeLiquidForServing();
    const FTransform FrozenEntry = FrozenCup->PourEntryPoint->GetComponentTransform();
    AIceCubeActor* Rejected = T.Cube(FrozenEntry.TransformPosition(FVector(0, 0, -.5)));
    TestFalse(TEXT("已冻结杯拒绝自由冰"), FrozenIce->TryAccept(Rejected,
        FrozenEntry.TransformPosition(FVector(0, 0, 5)), FrozenEntry.TransformPosition(FVector(0, 0, -.5))));
    TestEqual(TEXT("冻结杯冰数为零"), FrozenIce->GetIceCount(), 0);
    TestTrue(TEXT("冻结拒收保留自由态"), Rejected->State == EIceState::Free);
    TestNull(TEXT("冻结拒收不写杯指针"), Rejected->CupIce.Get());
    Capture(Rows, TEXT("frozenRejected"), T, nullptr, FrozenIce, FrozenCup);
    TestEqual(TEXT("非冻结杯真实注入 Coffee 100"), Cup->AddIngredientLiquid(100.f, CoffeeId, FLinearColor::Black), 100.f);
    TestEqual(TEXT("非冻结杯真实注入 Milk 100"), Cup->AddIngredientLiquid(100.f, MilkId, FLinearColor::White), 100.f);
    const FTransform Entry = Cup->PourEntryPoint->GetComponentTransform();
    TArray<AIceCubeActor*> Accepted;
    for (int32 I = 0; I < 3; ++I)
    {
        AIceCubeActor* Ice = T.Cube(Entry.TransformPosition(FVector(0, 0, -.5)));
        if (!TestTrue(TEXT("非冻结杯接纳真实冰"), CupIce->TryAccept(Ice,
            Entry.TransformPosition(FVector(0, 0, 5)), Entry.TransformPosition(FVector(0, 0, -.5))))) return false;
        Accepted.Add(Ice);
    }
    TestEqual(TEXT("破碎前杯内三块冰"), CupIce->GetIceCount(), 3);
    const FLiquidBalance Before = T.Transfers()->GetBalance();
    Capture(Rows, TEXT("beforeBreak"), T, nullptr, CupIce, Cup);
    if (!TestTrue(TEXT("非冻结杯业务破碎成功"), Cup->BreakContainerForImpact())) return false;
    CupIce->TickComponent(.02f, LEVELTICK_All, nullptr);
    TestEqual(TEXT("破碎 Tick 释放全部冰"), CupIce->GetIceCount(), 0);
    for (AIceCubeActor* Ice : Accepted)
    {
        TestTrue(TEXT("破碎释放后 Free"), Ice->State == EIceState::Free);
        TestNull(TEXT("破碎释放后 CupIce 清空"), Ice->CupIce.Get());
        TestNull(TEXT("破碎释放后 Scoop 清空"), Ice->Scoop.Get());
        TestNull(TEXT("破碎释放后没有附着父 Actor"), Ice->GetAttachParentActor());
    }
    T.SuppressIceSimulation();
    TestEqual(TEXT("原杯余液只洒出一次 200 mL"), T.Transfers()->GetBalance().SpilledML, Before.SpilledML + 200.0);
    TestEqual(TEXT("原杯权威液量归零"), Cup->GetPreciseVolumeML(), 0.0);
    TestEqual(TEXT("原杯成分归零"), Cup->GetComposition().Total(), 0.0);
    TestFalse(TEXT("重复破碎拒绝"), Cup->BreakContainerForImpact());
    TickCup(CupIce, 20);
    TestEqual(TEXT("重复 Tick 不重复洒液"), T.Transfers()->GetBalance().SpilledML, Before.SpilledML + 200.0);
    TestEqual(TEXT("冰释放不记成额外注液"), T.Transfers()->GetBalance().InjectedML, Before.InjectedML);
    TestEqual(TEXT("冰释放不记成消耗"), T.Transfers()->GetBalance().RemovedML, Before.RemovedML);
    Capture(Rows, TEXT("brokenReleasedOnce"), T, nullptr, CupIce, Cup);
    // 同 class/transform/容量的新杯独立创建，不复制旧实例的表现数组或液体快照。
    ACupActor* FreshCup = T.Cup(Cup->GetActorTransform(), Cup->MaxVolumeML);
    UCupIceComponent* FreshIce = T.AttachIce(FreshCup);
    TestTrue(TEXT("新杯同类但身份不同"), FreshCup != Cup && FreshCup->GetClass() == Cup->GetClass());
    TestTrue(TEXT("新杯同 transform"), FreshCup->GetActorTransform().Equals(Cup->GetActorTransform()));
    TestTrue(TEXT("新杯及新增组件已 BeginPlay"), FreshCup->HasActorBegunPlay() && FreshIce->HasBegunPlay());
    TestEqual(TEXT("新杯不继承旧冰"), FreshIce->GetIceCount(), 0);
    TestEqual(TEXT("新杯不继承旧液量"), FreshCup->GetPreciseVolumeML(), 0.0);
    TestEqual(TEXT("新杯无成分"), FreshCup->GetComposition().Ingredients.Num(), 0);
    TestEqual(TEXT("新杯不会出现冰水"), FreshCup->GetIngredientVolumeML(WaterId), 0.0);
    TestFalse(TEXT("新杯不继承破碎或冻结"), FreshCup->IsBroken() || FreshCup->IsLiquidFrozen());
    TestEqual(TEXT("新空杯不虚构额外注液"), T.Transfers()->GetBalance().InjectedML, Before.InjectedML);
    TestEqual(TEXT("新杯不重复结算原杯洒液"), T.Transfers()->GetBalance().SpilledML, Before.SpilledML + 200.0);
    const FLiquidBalance After = T.Transfers()->GetBalance();
    TestTrue(TEXT("破碎与新空杯账本闭合"), FMath::IsFinite(After.ErrorML) && FMath::Abs(After.ErrorML) < 1.e-6);
    Capture(Rows, TEXT("freshCupNoCopy"), T, nullptr, FreshIce, FreshCup);
    return SaveGroup(*this, TEXT("FrozenBrokenReleaseAndFreshCupNoCopy"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceActivityCapTest,
    "CyberCafe.Ice.ActivityCapAndExpiredFreeCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceActivityCapTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FIceTestWorld T;
    AIceBucketActor* Bucket = T.Bucket();
    AIceScoopActor* Scoop = T.Scoop(Bucket);
    T.BeginPlay();
    UIceWorldSubsystem* System = T.IceSystem();
    if (!TestNotNull(TEXT("世界冰活动上限 subsystem"), System)) return false;
    System->MaxLiveIce = 6;
    const FTransform Inside = Scoop->GetActorTransform();
    TArray<FString> Rows;
    for (int32 Batch = 0; Batch < 2; ++Batch)
    {
        TestTrue(TEXT("上限六可分配三块批次"), System->CanAllocate(3));
        if (!TestTrue(TEXT("实际加载批次"), Scoop->TryLoadFromBucket())) return false;
        TestEqual(TEXT("每批三块"), Scoop->GetLoadedCount(), 3);
        for (int32 I = 0; I < 3; ++I) TestTrue(TEXT("实际释放批次"), Scoop->ReleaseOne());
        T.SuppressIceSimulation();
        Scoop->SetActorLocation(FVector(50, 0, 12));
        Scoop->Tick(.02f);
        Scoop->SetActorTransform(Inside);
        Capture(Rows, Batch == 0 ? TEXT("threeFree") : TEXT("sixFree"), T, Scoop);
    }
    TestEqual(TEXT("六块自由冰仍全部计入上限"), System->GetLiveCount(), 6);
    TestFalse(TEXT("已满不能分配第三批三块"), System->CanAllocate(3));
    TestFalse(TEXT("已满也不能分配一块"), System->CanAllocate(1));
    TestFalse(TEXT("重武装后第三批实际加载仍被上限拒绝"), Scoop->TryLoadFromBucket());
    TestEqual(TEXT("拒绝第三批保持空铲"), Scoop->GetLoadedCount(), 0);
    TArray<TWeakObjectPtr<AIceCubeActor>> FreeCubes;
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
    {
        TestTrue(TEXT("上限测试对象是真自由冰"), It->State == EIceState::Free);
        FreeCubes.Add(TWeakObjectPtr<AIceCubeActor>(*It));
    }
    if (!TestEqual(TEXT("真实六块独立 Actor"), FreeCubes.Num(), 6)) return false;
    Capture(Rows, TEXT("thirdBatchBlocked"), T, Scoop);
    AIceCubeActor* First = FreeCubes[0].Get();
    if (!TestNotNull(TEXT("待销毁首块存在"), First)) return false;
    TestTrue(TEXT("真实 Actor Destroy 调用"), First->Destroy());
    // 不手工 ForgetIce；由实际 EndPlay 注销，且不要求即时 GC。
    First = nullptr;
    TestEqual(TEXT("Destroy 后活冰减至五"), System->GetLiveCount(), 5);
    TestTrue(TEXT("Destroy 后恢复一块额度"), System->CanAllocate(1));
    TestFalse(TEXT("只销毁一块不足以装三块"), System->CanAllocate(3));
    Capture(Rows, TEXT("destroyRestoresOneSlot"), T, Scoop);
    AIceCubeActor* Expiring = FreeCubes[1].Get();
    if (!TestNotNull(TEXT("待过期自由冰存在"), Expiring)) return false;
    TestFalse(TEXT("无物理模拟仍进入自由寿命测试"), Expiring->Mesh->IsSimulatingPhysics());
    TestEqual(TEXT("原生自由寿命十二秒"), Expiring->FreeLifetimeSeconds, 12.f);
    Expiring->Tick(12.f);
    Expiring = nullptr;
    TestEqual(TEXT("无模拟的自由冰十二秒 Tick 仍自动注销"), System->GetLiveCount(), 4);
    TestTrue(TEXT("过期再恢复第二个额度"), System->CanAllocate(2));
    Capture(Rows, TEXT("firstFreeExpired"), T, Scoop);
    for (int32 I = 2; I < FreeCubes.Num(); ++I)
        if (AIceCubeActor* Ice = FreeCubes[I].Get()) Ice->Tick(12.f);
    TestEqual(TEXT("其余自由冰过期后活冰归零"), System->GetLiveCount(), 0);
    TestTrue(TEXT("过期恢复完整三块批次额度"), System->CanAllocate(3));
    if (!TestTrue(TEXT("清理后原铲真实重新加载三块"), Scoop->TryLoadFromBucket())) return false;
    TestEqual(TEXT("清理后装载三块"), Scoop->GetLoadedCount(), 3);
    TestEqual(TEXT("清理后只有三块新活冰"), System->GetLiveCount(), 3);
    T.SuppressIceSimulation();
    Capture(Rows, TEXT("cleanupThenReload"), T, Scoop);
    return SaveGroup(*this, TEXT("ActivityCapAndExpiredFreeCleanup"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceTiltDumpTest,
    "CyberCafe.Ice.TiltDumpSpacingAndNoHiddenButton",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceTiltDumpTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实 WorldContext 需要 Engine"), GEngine)) return false;
    FIceTestWorld T;
    AIceBucketActor* Bucket = T.Bucket();
    AIceScoopActor* Scoop = T.Scoop(Bucket);
    T.BeginPlay();
    if (!TestTrue(TEXT("倾倒前真实装载三块"), Scoop->TryLoadFromBucket())) return false;
    TArray<FString> Rows;
    TestFalse(TEXT("未调用抓取 API，铲当前不被持有"), Scoop->Grab->IsHeld());
    Scoop->SetActorLocation(FVector(50, 0, 12));
    Scoop->SetActorRotation(FRotator(0, 0, 90));
    for (int32 I = 0; I < 20; ++I) Scoop->Tick(.02f);
    TestEqual(TEXT("仅 Roll90 侧翻而铲尖不向下，不出冰"), Scoop->GetLoadedCount(), 3);
    Capture(Rows, TEXT("sideRollDoesNotPour"), T, Scoop);
    // UE 负 Pitch 使前向 X 轴朝下；不能用侧翻或按钮模拟真实铲尖向下条件。
    Scoop->SetActorRotation(FRotator(-70, 0, 0));
    TestTrue(TEXT("倾斜超过公开 55 度阈值"), FVector::DotProduct(Scoop->GetActorUpVector(), FVector::UpVector)
        < FMath::Cos(FMath::DegreesToRadians(Scoop->PourTiltDegrees)));
    TestTrue(TEXT("铲尖确实向下"), FVector::DotProduct(Scoop->GetActorForwardVector(), FVector::DownVector) > .45);
    Scoop->Tick(.02f);
    TestEqual(TEXT("只靠倾角 Tick，未持有也自然释放首块"), Scoop->GetLoadedCount(), 2);
    T.SuppressIceSimulation();
    Capture(Rows, TEXT("firstTiltRelease"), T, Scoop);
    for (int32 I = 0; I < 7; ++I) Scoop->Tick(.02f);
    TestEqual(TEXT("首块后 .14 秒尚未到 .16 间隔"), Scoop->GetLoadedCount(), 2);
    // .02 步长容许一个浮点边界帧，禁止同帧批量倾倒。
    for (int32 I = 0; I < 2; ++I) Scoop->Tick(.02f);
    TestEqual(TEXT("跨过首个间隔后恰好再释放一块"), Scoop->GetLoadedCount(), 1);
    T.SuppressIceSimulation();
    Capture(Rows, TEXT("secondSpacedRelease"), T, Scoop);
    for (int32 I = 0; I < 6; ++I) Scoop->Tick(.02f);
    TestEqual(TEXT("第二块后短于间隔，仍保留末块"), Scoop->GetLoadedCount(), 1);
    for (int32 I = 0; I < 3; ++I) Scoop->Tick(.02f);
    TestEqual(TEXT("再次跨过间隔后末块释放"), Scoop->GetLoadedCount(), 0);
    TestFalse(TEXT("全过程没有隐藏抓取或倒冰按钮条件"), Scoop->Grab->IsHeld());
    TestEqual(TEXT("三块释放后仍是三块活冰"), T.IceSystem()->GetLiveCount(), 3);
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
    {
        TestTrue(TEXT("倾倒后每块均 Free"), It->State == EIceState::Free);
        TestNull(TEXT("倾倒后铲指针清空"), It->Scoop.Get());
        TestNull(TEXT("倾倒不伪造入杯"), It->CupIce.Get());
    }
    T.SuppressIceSimulation();
    Capture(Rows, TEXT("allSpacedReleases"), T, Scoop);
    return SaveGroup(*this, TEXT("TiltDumpSpacingAndNoHiddenButton"), Rows);
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
