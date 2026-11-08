// @trace CF-01 CF-02 CF-03 CF-04
// 保存地图只读；闭环使用真实 BP、查询碰撞和正常 World Tick，不代表头显或像素验收。
// 本文件不启动构建、不改生产/旧测试/配置；JSON 仅在测试实际执行时写出。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/IceInteraction.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialStep.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/ServingSpotActor.h"
#include "GrabComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

namespace CoffeeAssetRuntimeTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr float StepSeconds = .01f;
constexpr double LiquidTolerance = 1.e-5;
// float余额可有有界欠产；总毫升账本和双精度整批上限不放宽。
constexpr double BudgetRoundingTolerance = 2.e-4;
const FName Water(TEXT("Water")), Coffee(TEXT("Coffee")), Milk(TEXT("Milk"));
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
const TCHAR* GrinderPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Coffee/Grinder/BP_CoffeeGrinder.BP_CoffeeGrinder_C");
const TCHAR* DripperPath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandDripper.BP_HandDripper_C");
const TCHAR* KettlePath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandPourKettle.BP_HandPourKettle_C");
const TCHAR* CupPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup03.BP_Cup03_C");
const TCHAR* PrimaryCupClassPath = TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C");
const TCHAR* PrimaryCupMeshPath = TEXT("/Game/VRTemplate/MenuIngredients/Meshes/SM_Highball_Cup.SM_Highball_Cup");
const TCHAR* PrimaryCupProfilePath = TEXT("/Game/VRTemplate/MenuIngredients/Breakage/cup_Highball/DA_cup_Highball_Break.DA_cup_Highball_Break");

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected,
    double Tolerance = LiquidTolerance)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.17g，预期 %.17g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}

FString Quote(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\")); Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r")); Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t")); Value.ReplaceInline(TEXT("\b"), TEXT("\\b"));
    Value.ReplaceInline(TEXT("\f"), TEXT("\\f"));
    return TEXT("\"") + Value + TEXT("\"");
}

// 不增加 Json 模块依赖；失败的非有限测量写 null，缺失测量不填零。
struct FRow
{
    TArray<FString> Fields;
    void Text(const TCHAR* Key, const FString& Value) { Fields.Add(Quote(Key) + TEXT(":") + Quote(Value)); }
    void Number(const TCHAR* Key, double Value)
    {
        Fields.Add(Quote(Key) + TEXT(":") + (FMath::IsFinite(Value)
            ? FString::Printf(TEXT("%.17g"), Value) : FString(TEXT("null"))));
    }
    void Bool(const TCHAR* Key, bool Value) { Fields.Add(Quote(Key) + (Value ? TEXT(":true") : TEXT(":false"))); }
    void Vector(const TCHAR* Key, const FVector& Value)
    {
        FRow V; V.Number(TEXT("x"), Value.X); V.Number(TEXT("y"), Value.Y); V.Number(TEXT("z"), Value.Z);
        Fields.Add(Quote(Key) + TEXT(":") + V.Json());
    }
    FString Json() const { return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}"); }
};

bool SaveSamples(FAutomationTestBase& Test, const TCHAR* Group, const TCHAR* Scope, const TArray<FString>& Rows)
{
    // 只汇总本进程确实执行过的组，不读取历史 JSON 补齐未运行项。
    static TMap<FString, FString> Executed;
    FRow Header; Header.Text(TEXT("group"), Group); Header.Text(TEXT("scope"), Scope);
    Header.Text(TEXT("status"), Test.HasAnyErrors() ? TEXT("Failed") : TEXT("Passed"));
    Header.Number(TEXT("sampleCount"), Rows.Num());
    Executed.Add(Group, Header.Json().LeftChop(1) + TEXT(",\"rows\":[") + FString::Join(Rows, TEXT(",")) + TEXT("]}"));
    TArray<FString> Keys; Executed.GetKeys(Keys); Keys.Sort();
    TArray<FString> Groups; for (const FString& Key : Keys) Groups.Add(Executed.FindChecked(Key));
    const FString Json = TEXT("{\"schemaVersion\":1,\"recordedAtUTC\":") + Quote(FDateTime::UtcNow().ToIso8601())
        + TEXT(",\"actualSavedMapRun\":false,\"headsetVerified\":false,\"screenshotsCaptured\":false,\"groups\":[")
        + FString::Join(Groups, TEXT(",")) + TEXT("]}\n");
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("CoffeeRuntime");
    return Test.TestTrue(TEXT("保存本次实际状态与账本采样"), IFileManager::Get().MakeDirectory(*Directory, true)
        && FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("saved_chain.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

struct FEvidence
{
    FAutomationTestBase& Test;
    const TCHAR* Group;
    const TCHAR* Scope;
    TArray<FString> Rows;
    FEvidence(FAutomationTestBase& InTest, const TCHAR* InGroup, const TCHAR* InScope)
        : Test(InTest), Group(InGroup), Scope(InScope) {}
    ~FEvidence() { SaveSamples(Test, Group, Scope, Rows); }
    void Add(const FRow& Row) { Rows.Add(Row.Json()); }
};

void FinishCompilation()
{
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
}

struct FAssets
{
    UClass* Grinder = LoadClass<ACoffeeGrinderActor>(nullptr, GrinderPath);
    UClass* Dripper = LoadClass<AHandDripperActor>(nullptr, DripperPath);
    UClass* Kettle = LoadClass<AHandPourKettleActor>(nullptr, KettlePath);
    UClass* Cup = LoadClass<ACupActor>(nullptr, CupPath);
    bool Check(FAutomationTestBase& Test) const
    {
        bool Result = Test.TestNotNull(TEXT("原保存研磨器 BP"), Grinder);
        Result &= Test.TestNotNull(TEXT("新保存滤杯 BP，未保存时明确失败"), Dripper);
        Result &= Test.TestNotNull(TEXT("新保存手冲壶 BP，未保存时明确失败"), Kettle);
        Result &= Test.TestNotNull(TEXT("原保存 Cup03 BP"), Cup);
        return Result;
    }
};

// 仅静音、移除测试 HUD/粒子和禁刚体模拟；不移动口点、不替换实际容器网格或关闭查询碰撞。
void QuietActor(AActor* Actor)
{
    TArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes);
    for (auto* Mesh : Meshes) { Mesh->SetSimulatePhysics(false); Mesh->SetEnableGravity(false); }
    TArray<UNiagaraComponent*> FXs; Actor->GetComponents(FXs);
    for (auto* FX : FXs) { FX->SetAutoActivate(false); FX->SetAsset(nullptr); FX->DeactivateImmediate(); }
    TArray<UAudioComponent*> Sounds; Actor->GetComponents(Sounds);
    for (auto* Sound : Sounds) { Sound->Stop(); Sound->SetAutoActivate(false); Sound->SetSound(nullptr); }
    TArray<UGrabComponent*> Grabs; Actor->GetComponents(Grabs);
    for (auto* Grab : Grabs)
    {
        Grab->PhysicsMode = EGrabPhysicsMode::Kinematic; Grab->bSimulateOnDrop = false;
        Grab->bCaptureHand = false; Grab->bHideHandWhileHeld = false; Grab->OnGrabHapticEffect = nullptr;
    }
    if (auto* G = Cast<ACoffeeGrinderActor>(Actor)) { G->LoadedGrindSound = nullptr; G->EmptyGrindSound = nullptr; }
    if (auto* H = Cast<AGrinderHandleActor>(Actor)) H->TurnHaptic = nullptr;
    if (auto* D = Cast<AGrinderDrawerActor>(Actor))
    {
        D->GroundPourSound = nullptr; D->DetachSound = nullptr; D->ReattachSound = nullptr; D->DetachHaptic = nullptr;
    }
    if (auto* A = Cast<ALiquidContainerActor>(Actor))
    {
        A->bCanPour = false; A->bEnableSplash = false; A->bEnableDecal = false;
        A->bDebugDrawTrace = false; A->LiquidFXTemplate = nullptr; A->SplashEffectTemplate = nullptr;
    }
    if (auto* K = Cast<AHandPourKettleActor>(Actor)) K->PourHaptic = nullptr;
    if (auto* D = Cast<AHandDripperActor>(Actor)) D->DripSound = nullptr;
    if (auto* Cup = Cast<ACupActor>(Actor))
    {
        Cup->VolumeWidgetClass = nullptr; Cup->bEditorPreviewWidget = false; Cup->bFaceCamera = false;
        Cup->VolumeWidgetComp->SetWidgetClass(nullptr); Cup->VolumeWidgetComp->SetHiddenInGame(true);
        Cup->VolumeWidgetComp->SetComponentTickEnabled(false);
    }
}

struct FRuntimeWorld
{
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    int32 Frames = 0;
    TArray<AActor*> Actors;
    explicit FRuntimeWorld(FAutomationTestBase& Test)
    {
        if (!Test.TestNotNull(TEXT("独立游戏世界需要 Engine"), GEngine)) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("独立 Game World"), World)) return;
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine); Context.OwningGameInstance = GI;
        World->SetGameInstance(GI); GI->Init();
    }
    ~FRuntimeWorld()
    {
        if (!World) return;
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    template <typename T>
    T* Spawn(UClass* Class, const FVector& Location, float InitialFill = -1.f)
    {
        if (!World || !Class) return nullptr;
        const FTransform Pose(FQuat::Identity, Location, Class->GetDefaultObject<AActor>()->GetActorScale3D());
        T* Actor = World->SpawnActorDeferred<T>(Class, Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!Actor) return nullptr;
        QuietActor(Actor);
        if (auto* Liquid = Cast<ALiquidContainerActor>(Actor))
            if (InitialFill >= 0.f) Liquid->FillAmount = InitialFill;
        Actor->FinishSpawning(Pose); QuietActor(Actor); Actors.Add(Actor);
        return Actor;
    }
    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
        for (AActor* A : Actors) QuietActor(A);
        // 子件必须由原 BP 的 SpawnChildParts 产生，不能手工替换 HandleClass/DrawerClass。
        for (TActorIterator<AGrinderHandleActor> It(World); It; ++It) QuietActor(*It);
        for (TActorIterator<AGrinderDrawerActor> It(World); It; ++It) QuietActor(*It);
    }
    void Tick(float Dt = StepSeconds)
    {
        ++GFrameCounter; World->Tick(LEVELTICK_All, Dt); ++Frames;
    }
    ULiquidTransferSubsystem* Transfers() const { return World ? World->GetSubsystem<ULiquidTransferSubsystem>() : nullptr; }
};

bool PrepareBody(FAutomationTestBase& Test, UStaticMeshComponent* Mesh)
{
    if (!Test.TestNotNull(TEXT("实际保存网格组件"), Mesh)
        || !Test.TestNotNull(TEXT("实际保存静态网格"), Mesh->GetStaticMesh().Get())) return false;
    UBodySetup* Body = Mesh->GetStaticMesh()->GetBodySetup();
    if (!Test.TestNotNull(TEXT("实际查询碰撞 BodySetup"), Body)) return false;
    Body->CreatePhysicsMeshes(); Mesh->SetSimulatePhysics(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Mesh->RecreatePhysicsState();
    return Test.TestTrue(TEXT("保留真实有效查询碰撞，不开启刚体模拟"), Mesh->IsRegistered()
        && Mesh->GetBodyInstance() && Mesh->GetBodyInstance()->IsValidBodyInstance() && !Mesh->IsSimulatingPhysics());
}

void CheckBalance(FAutomationTestBase& Test, ULiquidTransferSubsystem* Transfer)
{
    const FLiquidBalance B = Transfer->GetBalance();
    Test.TestTrue(TEXT("全账本有限非负，含撒漏而非忽略项"), FMath::IsFinite(B.InitialML) && B.InitialML >= 0
        && FMath::IsFinite(B.InjectedML) && B.InjectedML >= 0 && FMath::IsFinite(B.RemovedML) && B.RemovedML >= 0
        && FMath::IsFinite(B.ContainerML) && B.ContainerML >= 0 && FMath::IsFinite(B.InFlightML) && B.InFlightML >= 0
        && FMath::IsFinite(B.SpilledML) && B.SpilledML >= 0 && B.PacketCount >= 0);
    Test.TestTrue(TEXT("真实源库存、在途包、接液及撒漏闭合，小于 1e-5mL"),
        FMath::IsFinite(B.ErrorML) && FMath::Abs(B.ErrorML) < LiquidTolerance);
}

void Capture(FEvidence& Evidence, const TCHAR* Stage, const FRuntimeWorld& T, const AHandDripperActor* D,
    const ACupActor* Cup, const AHandPourKettleActor* K = nullptr, const ACoffeeGrinderActor* G = nullptr,
    const AGrinderDrawerActor* Drawer = nullptr)
{
    FRow Row; Row.Text(TEXT("stage"), Stage); Row.Number(TEXT("worldFrame"), T.Frames);
    Row.Number(TEXT("worldTimeSeconds"), T.World->GetTimeSeconds()); Row.Number(TEXT("worldDeltaSeconds"), T.World->GetDeltaSeconds());
    const FLiquidBalance B = T.Transfers()->GetBalance();
    Row.Number(TEXT("initialML"), B.InitialML); Row.Number(TEXT("injectedML"), B.InjectedML);
    Row.Number(TEXT("removedML"), B.RemovedML); Row.Number(TEXT("containerML"), B.ContainerML);
    Row.Number(TEXT("inFlightML"), B.InFlightML); Row.Number(TEXT("spilledML"), B.SpilledML);
    Row.Number(TEXT("balanceErrorML"), B.ErrorML); Row.Number(TEXT("packetCount"), B.PacketCount);
    Row.Number(TEXT("unsettledSeconds"), T.Transfers()->GetUnsettledFlightHorizon());
    Row.Text(TEXT("dripperClass"), D->GetClass()->GetPathName()); Row.Vector(TEXT("entryWorldCM"), D->PourEntryPoint->GetComponentLocation());
    Row.Vector(TEXT("outletWorldCM"), D->GetActorTransform().TransformPosition(D->OutletLocal));
    Row.Number(TEXT("storageML"), D->GetPreciseVolumeML()); Row.Number(TEXT("storageRawWaterML"), D->GetIngredientVolumeML(Water));
    Row.Number(TEXT("storageRawCoffeeML"), D->GetIngredientVolumeML(Coffee)); Row.Number(TEXT("groundsGrams"), D->GetGroundsGrams());
    Row.Number(TEXT("coffeeBudgetML"), D->GetCoffeeBudgetML()); Row.Number(TEXT("producedCoffeeML"), D->CoffeeProducedML);
    Row.Number(TEXT("drippedML"), D->DrippedML); Row.Number(TEXT("discardedGroundsGrams"), D->DiscardedGroundsGrams);
    Row.Number(TEXT("brewGeneration"), D->BrewGeneration); Row.Number(TEXT("brewState"), static_cast<uint8>(D->BrewState));
    Row.Text(TEXT("statusText"), D->StatusText->Text.ToString());
    Row.Text(TEXT("cupClass"), Cup->GetClass()->GetPathName()); Row.Vector(TEXT("cupEntryWorldCM"), Cup->PourEntryPoint->GetComponentLocation());
    Row.Number(TEXT("receiverML"), Cup->GetPreciseVolumeML()); Row.Number(TEXT("receiverCoffeeML"), Cup->GetIngredientVolumeML(Coffee));
    Row.Number(TEXT("receiverWaterML"), Cup->GetIngredientVolumeML(Water)); Row.Number(TEXT("receiverMilkML"), Cup->GetIngredientVolumeML(Milk));
    Row.Text(TEXT("receiverMaterial"), GetPathNameSafe(Cup->LiquidMaterialAsset.Get()));
    if (K)
    {
        Row.Text(TEXT("kettleClass"), K->GetClass()->GetPathName()); Row.Number(TEXT("sourceWaterML"), K->GetIngredientVolumeML(Water));
        Row.Number(TEXT("sourceCurrentML"), K->GetPreciseVolumeML()); Row.Number(TEXT("flowMLPerSecond"), K->CurrentFlowMLPerSecond);
        Row.Vector(TEXT("spoutWorldCM"), K->PourFX->GetComponentLocation()); Row.Vector(TEXT("kettleForward"), K->GetActorForwardVector());
        Row.Number(TEXT("waterJetInstances"), K->WaterJet->GetInstanceCount());
    }
    if (G)
    {
        Row.Number(TEXT("beanGrams"), G->GetCurrentBeanGrams()); Row.Number(TEXT("grinderGroundsGrams"), G->GetCurrentGroundGrams());
        Row.Number(TEXT("consumedBeanGrams"), G->GetTotalBeanConsumedGrams()); Row.Number(TEXT("groundOutputGrams"), G->GetTotalGroundOutputGrams());
        Row.Number(TEXT("grindLossGrams"), G->GetGrindLossGrams());
    }
    if (Drawer)
    {
        Row.Number(TEXT("drawerGrams"), Drawer->GetGroundsGrams()); Row.Number(TEXT("spilledGroundsGrams"), Drawer->SpilledGroundGrams);
        Row.Number(TEXT("lastPouredGrams"), Drawer->LastPouredGroundGrams); Row.Number(TEXT("lastAcceptedGrams"), Drawer->LastAcceptedGroundGrams);
        Row.Vector(TEXT("powderLipWorldCM"), Drawer->GroundPourPoint->GetComponentLocation());
        Row.Vector(TEXT("powderPathEndCM"), Drawer->LastGroundPourEnd);
    }
    Evidence.Add(Row);
}

bool CheckSavedMesh(FAutomationTestBase& Test, UStaticMeshComponent* Component, const TCHAR* Path, FEvidence& Evidence)
{
    UStaticMesh* Expected = LoadObject<UStaticMesh>(nullptr, Path);
    if (!Test.TestNotNull(TEXT("独立保存的手冲模型"), Expected) || !Test.TestNotNull(TEXT("保存 BP 网格组件"), Component)) return false;
    Test.TestTrue(TEXT("BP 实际引用独立副本而非空网格或源模型"), Component->GetStaticMesh() == Expected);
    FinishCompilation();
    const FBox Box = Expected->GetBoundingBox();
    Test.TestTrue(TEXT("独立副本有实际三维几何"), Box.IsValid && !Box.Min.ContainsNaN() && !Box.Max.ContainsNaN()
        && Box.GetSize().GetMin() > UE_SMALL_NUMBER && Expected->GetNumLODs() > 0 && Expected->GetNumTriangles(0) > 0);
    Test.TestNotNull(TEXT("保存独立模型的碰撞"), Expected->GetBodySetup());
    FRow Row; Row.Text(TEXT("stage"), TEXT("savedIndependentMesh")); Row.Text(TEXT("mesh"), Expected->GetPathName());
    Row.Vector(TEXT("boundsMinCM"), Box.Min); Row.Vector(TEXT("boundsMaxCM"), Box.Max);
    Row.Vector(TEXT("rootRelativeScale"), Component->GetRelativeScale3D()); Evidence.Add(Row);
    return !Test.HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeSavedAssetsAndSystemsTest,
    "CyberCafe.CoffeeRuntime.SavedCoffeeAssetsAndExistingSystems", Flags)
bool FCoffeeSavedAssetsAndSystemsTest::RunTest(const FString&)
{
    FEvidence Evidence(*this, TEXT("SavedCoffeeAssetsAndExistingSystems"), TEXT("readOnlySavedAssets"));
    FAssets Assets; if (!Assets.Check(*this)) return false;
    FinishCompilation();
    const auto* G = Assets.Grinder->GetDefaultObject<ACoffeeGrinderActor>();
    const auto* D = Assets.Dripper->GetDefaultObject<AHandDripperActor>();
    const auto* K = Assets.Kettle->GetDefaultObject<AHandPourKettleActor>();
    TestTrue(TEXT("两个新 BP 直接继承批准的 native 类"), Assets.Dripper->GetSuperClass() == AHandDripperActor::StaticClass()
        && Assets.Kettle->GetSuperClass() == AHandPourKettleActor::StaticClass());
    for (UClass* Class : {Assets.Grinder, Assets.Dripper, Assets.Kettle, Assets.Cup})
        TestTrue(TEXT("全部为保存 BP 生成类"), Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
    Near(*this, TEXT("原研磨器容量保留15g"), G->BeanCapacityGrams, 15);
    Near(*this, TEXT("原六圈转换率"), G->GramsPerDegree, 15.0 / 2160.0);
    Near(*this, TEXT("原研磨效率为一"), G->GrindEfficiency, 1);
    TestTrue(TEXT("原主体仍只计正向"), G->bOneWayEffective && G->EffectiveDirectionSign == 1);
    if (!TestNotNull(TEXT("原保存 HandleClass"), G->HandleClass.Get()) || !TestNotNull(TEXT("原保存 DrawerClass"), G->DrawerClass.Get())) return false;
    const auto* Handle = G->HandleClass->GetDefaultObject<AGrinderHandleActor>();
    const auto* Drawer = G->DrawerClass->GetDefaultObject<AGrinderDrawerActor>();
    TestTrue(TEXT("原柄只计正向且方向符号为一"), Handle->bOneWayEffective && Handle->EffectiveDirectionSign == 1);
    Near(*this, TEXT("保存柄采样半径匹配原网格34cm局部握柄端"), Handle->HandleArmRadius, 34);
    TestNotNull(TEXT("原研磨器保存有豆声音反馈"), G->LoadedGrindSound ? G->LoadedGrindSound.Get() : G->GrindSFX->Sound.Get());
    TestNotNull(TEXT("保存柄触觉反馈非空"), Handle->TurnHaptic.Get());
    TestNotNull(TEXT("保存抽屉出粉声音非空"), Drawer->GroundPourSound.Get());
    TestNotNull(TEXT("保存抽屉原网格保留"), Drawer->DrawerMesh->GetStaticMesh().Get());
    TestTrue(TEXT("豆堆及粉堆采用平移而非压扁"), G->bTranslateBeanPile && Drawer->bTranslateGroundPile);
    Near(*this, TEXT("保存滤杯容量120mL"), D->MaxVolumeML, 120);
    Near(*this, TEXT("保存闷蒸8秒"), D->BloomSeconds, 8); Near(*this, TEXT("保存滴流12mL/s"), D->DrainRateMLPerSecond, 12);
    Near(*this, TEXT("保存有限产率16mL/g"), D->CoffeeYieldMLPerGram, 16);
    Near(*this, TEXT("保存壶容量500mL"), K->MaxVolumeML, 500); Near(*this, TEXT("壶倾倒阈值40度"), K->PourTiltDegrees, 40);
    Near(*this, TEXT("壶最大流量28mL/s"), K->MaxFlowMLPerSecond, 28);
    TestTrue(TEXT("壶实际原料是Water且禁用基类重复出液"), K->InitialIngredientId == Water && !K->bCanPour);
    TestTrue(TEXT("保存壶口是实际 PourFX 组件坐标"), K->PourFX->GetRelativeLocation().Equals(FVector(16.8, 0, 13), .001));
    TestNotNull(TEXT("保存滴流声音反馈非空"), D->DripSound.Get());
    TestNotNull(TEXT("保存咖啡输出材质钩子配置非空"), D->CoffeeLiquidMaterial.Get());
    TestNotNull(TEXT("保存清水输出材质非空"), D->WaterLiquidMaterial.Get());
    if (!CheckSavedMesh(*this, D->ContainerMesh.Get(), TEXT("/Game/VRTemplate/CoffeeBrew/Meshes/SM_HandDripper.SM_HandDripper"), Evidence)
        || !CheckSavedMesh(*this, K->ContainerMesh.Get(), TEXT("/Game/VRTemplate/CoffeeBrew/Meshes/SM_HandKettle.SM_HandKettle"), Evidence)) return false;
    TestTrue(TEXT("滤杯和壶为两个独立模型"), D->ContainerMesh->GetStaticMesh() != K->ContainerMesh->GetStaticMesh());
    TestEqual(TEXT("滤纸复用独立已有源 SM，不替代滤杯外壳"), GetPathNameSafe(D->PaperMesh->GetStaticMesh().Get()),
        FString(TEXT("/Game/coffee_grinder_scene/CoffeeBean/FilterSet/SM_ConeFilterPaper.SM_ConeFilterPaper")));
    TestTrue(TEXT("滤纸与滤杯外壳保持分离"), D->PaperMesh->GetStaticMesh() != D->ContainerMesh->GetStaticMesh()
        && D->PaperMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestNotNull(TEXT("粉床复用实际粉网格"), D->GroundBed->GetStaticMesh().Get());
    TestTrue(TEXT("粉床仅为展示，不用碰撞或粒子结算"), D->GroundBed->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

    UWorld* Map = LoadObject<UWorld>(nullptr, MapPath);
    if (!TestNotNull(TEXT("只读加载原保存地图"), Map) || !TestNotNull(TEXT("原地图 PersistentLevel"), Map->PersistentLevel.Get())) return false;
    const bool BegunBefore = Map->HasBegunPlay(); const float TimeBefore = Map->GetTimeSeconds();
    AHandDripperActor* MapD = nullptr; AHandPourKettleActor* MapK = nullptr; ACupActor* MapCup = nullptr;
    ACoffeeGrinderActor* OldG = nullptr; AGlassRecoveryActor* Reset = nullptr;
    ATutorialDirector* Director = nullptr; AOrderHologramActor* Holo = nullptr; AIceStationActor* Ice = nullptr;
    int32 Grinders = 0, Directors = 0, Holograms = 0, IceStations = 0, Resets = 0;
    for (AActor* A : Map->PersistentLevel->Actors)
    {
        if (!A) continue;
        if (A->GetFName() == TEXT("HandDripper_Practice")) MapD = Cast<AHandDripperActor>(A);
        if (A->GetFName() == TEXT("HandKettle_Practice")) MapK = Cast<AHandPourKettleActor>(A);
        if (A->GetFName() == TEXT("HandCoffeeCup_Practice")) MapCup = Cast<ACupActor>(A);
        if (auto* Value = Cast<ACoffeeGrinderActor>(A)) { ++Grinders; if (Value->GetClass() == Assets.Grinder) OldG = Value; }
        if (auto* Value = Cast<AGlassRecoveryActor>(A))
        {
            const bool bTutorialManager = A->GetFName() == TEXT("GlassRecovery_Tutorial");
            if (bTutorialManager) { ++Resets; Reset = Value; }
            FRow Row; Row.Text(TEXT("stage"), TEXT("savedRecoveryManager"));
            Row.Text(TEXT("manager"), Value->GetPathName()); Row.Text(TEXT("managerClass"), Value->GetClass()->GetPathName());
            Row.Bool(TEXT("countedTutorialManager"), bTutorialManager);
            Row.Text(TEXT("prototypeCup"), GetPathNameSafe(Value->PrototypeCup.Get()));
            Row.Text(TEXT("cupProfile"), GetPathNameSafe(Value->CupProfile.Get()));
            Row.Number(TEXT("restoreTargetCount"), Value->RestoreTargets.Num()); Evidence.Add(Row);
        }
        TestFalse(TEXT("退场的原第一杯实例不再留在地图"), A->GetFName() == TEXT("BP_Cup01_C_1"));
        if (auto* Value = Cast<ATutorialDirector>(A)) { ++Directors; if (A->GetFName() == TEXT("BP_TutorialDirecter_C_0")) Director = Value; }
        if (auto* Value = Cast<AOrderHologramActor>(A)) { ++Holograms; if (A->GetFName() == TEXT("BP_OrderHologram_Tutorial")) Holo = Value; }
        if (auto* Value = Cast<AIceStationActor>(A)) { ++IceStations; if (A->GetFName() == TEXT("IceStation_Practice")) Ice = Value; }
    }
    if (!TestNotNull(TEXT("保存新滤杯实例"), MapD) || !TestNotNull(TEXT("保存新壶实例"), MapK)
        || !TestNotNull(TEXT("保存新咖啡接收杯"), MapCup) || !TestNotNull(TEXT("原研磨器实例"), OldG)
        || !TestNotNull(TEXT("原RESET实例"), Reset) || !TestNotNull(TEXT("原六步导演"), Director)
        || !TestNotNull(TEXT("原全息订单"), Holo) || !TestNotNull(TEXT("原冰站"), Ice)) return false;
    TestEqual(TEXT("原研磨器唯一"), Grinders, 1); TestEqual(TEXT("原导演唯一"), Directors, 1);
    TestEqual(TEXT("原全息唯一"), Holograms, 1); TestEqual(TEXT("原冰站唯一"), IceStations, 1); TestEqual(TEXT("命名 GlassRecovery_Tutorial 唯一，独立菜单经理不计入"), Resets, 1);
    TestTrue(TEXT("新实例使用真实保存BP及原Cup03"), MapD->GetClass() == Assets.Dripper && MapK->GetClass() == Assets.Kettle && MapCup->GetClass() == Assets.Cup);
    TestTrue(TEXT("新实例保持批准练习位置"), MapD->GetActorLocation().Equals(FVector(-151, 94, 107), .02)
        && MapK->GetActorLocation().Equals(FVector(-180, 112, 92), .02) && MapCup->GetActorLocation().Equals(FVector(-151, 94, 97.12), .02));
    TestTrue(TEXT("场景实际引用同一独立网格"), MapD->ContainerMesh->GetStaticMesh() == D->ContainerMesh->GetStaticMesh()
        && MapK->ContainerMesh->GetStaticMesh() == K->ContainerMesh->GetStaticMesh());
    TestTrue(TEXT("实际滤杯出口高于原Cup03杯口"), MapD->GetActorTransform().TransformPosition(MapD->OutletLocal).Z
        > MapCup->PourEntryPoint->GetComponentLocation().Z);
    TestEqual(TEXT("旧RESET目标仍严格六项"), Reset->RestoreTargets.Num(), 6);
    TSet<AActor*> Targets;
    for (const auto& Target : Reset->RestoreTargets)
    {
        if (!TestNotNull(TEXT("旧六目标引用有效"), Target.Get())) continue;
        Targets.Add(Target.Get()); TestTrue(TEXT("旧目标属于原地图"), Target->GetLevel() == Map->PersistentLevel.Get());
    }
    TestEqual(TEXT("旧六目标互不重复"), Targets.Num(), 6);
    TestFalse(TEXT("旧RESET不包含新壶或研磨器"), Targets.Contains(MapK) || Targets.Contains(OldG));
    TestFalse(TEXT("旧RESET未扩入新滤杯或新接收杯"), Targets.Contains(MapD) || Targets.Contains(MapCup));
    ACupActor* PrimaryCup = Cast<ACupActor>(Reset->PrototypeCup.Get());
    if (!TestNotNull(TEXT("从指定经理 PrototypeCup 读取第一杯"), PrimaryCup)) return false;
    TestTrue(TEXT("新首杯及原奶瓶身份属于原六目标"), Reset->PrototypeBottle
        && Reset->PrototypeBottle->GetFName() == TEXT("BP_Bottle04_Milk_Tutorial")
        && Targets.Contains(PrimaryCup) && Targets.Contains(Reset->PrototypeBottle.Get()));
    TestTrue(TEXT("第一杯属于原保存地图"), PrimaryCup->GetLevel() == Map->PersistentLevel.Get());
    TestEqual(TEXT("第一杯实例为批准的新 Highball"), PrimaryCup->GetName(), FString(TEXT("BP_MenuCup_Highball_C_1")));
    TestEqual(TEXT("第一杯 class 精确匹配 Highball"), PrimaryCup->GetClass()->GetPathName(), FString(PrimaryCupClassPath));
    TestEqual(TEXT("第一杯网格精确匹配 Highball"), GetPathNameSafe(PrimaryCup->ContainerMesh->GetStaticMesh().Get()), FString(PrimaryCupMeshPath));
    TestTrue(TEXT("第一杯根网格及缩放保持批准的 1.1"), PrimaryCup->GetRootComponent() == PrimaryCup->ContainerMesh.Get()
        && PrimaryCup->ContainerMesh->GetRelativeScale3D().Equals(FVector(1.1), .0001));
    TestEqual(TEXT("第一杯容量为 300mL"), PrimaryCup->MaxVolumeML, 300.f);
    TestEqual(TEXT("经理首杯 profile 精确匹配 Highball"), GetPathNameSafe(Reset->CupProfile.Get()), FString(PrimaryCupProfilePath));
    TestEqual(TEXT("第一杯自身 profile 精确匹配 Highball"), GetPathNameSafe(PrimaryCup->GlassBreakProfile.Get()), FString(PrimaryCupProfilePath));
    TestTrue(TEXT("原冰站与RESET仍共享原冰铲"), Ice->Scoop && Ice->Scoop.Get() == Reset->RestoreScoop.Get());
    TestTrue(TEXT("原冰桶及冰杯引用保留"), Ice->Bucket && Ice->Cup && Ice->Cup.Get() != MapCup);
    if (Ice->Cup)
    {
        TestEqual(TEXT("原冰杯仍为原Cup03"), Ice->Cup->GetClass()->GetPathName(), FString(CupPath));
        TestEqual(TEXT("冰站仍引用 IceCup_Practice"), Ice->Cup->GetName(), FString(TEXT("IceCup_Practice")));
        TestTrue(TEXT("冰站练习杯独立于新恢复首杯"), Ice->Cup.Get() != PrimaryCup);
    }
    TestEqual(TEXT("菜单插入后教学恰好八步"), Director->Steps.Num(), 8);
    const TCHAR* Names[] = {TEXT("TutorialStep_ShowMessage_0"), TEXT("TutorialStep_ShowMessage_1"), TEXT("TutorialStep_ShowMessage_2"),
        TEXT("TutorialStep_ShowMessage_3"), TEXT("TutorialStep_WaitForServe_0"), TEXT("TutorialStep_ConsumeServedDrink_0")};
    // 变更文案精确采用 menu_ui_implementation/applied.json.step_texts，未变更三步保留原字。
    const TCHAR* Hints[] = {TEXT("你醒了？……刚刚说到我教你做一杯拿铁，你就睡着了。快，先看看你的台子。"),
        TEXT("台子上有咖啡液和牛奶，把瓶盖拿掉，往杯子里倒。"),
        TEXT("空杯子在你左手边。"),
        TEXT("够不着就别探身子了。左手按 X，或者右手按 A，隔空就能拿过来。"),
        TEXT("给我做一杯拿铁。咖啡和牛奶差不多一半一半，加起来一百八到两百毫升。做好了放到吧台前面，不对的话拿回去改。"),
        TEXT("时零准备尝一口，稍等一下。")};
    const int32 OriginalStepIndices[] = {0, 1, 2, 3, 6, 7};
    for (int32 I = 0; I < 6 && Director->Steps.IsValidIndex(OriginalStepIndices[I]); ++I)
    {
        UTutorialStep* Step = Director->Steps[OriginalStepIndices[I]].Get(); if (!TestNotNull(TEXT("原教学步骤对象"), Step)) continue;
        TestEqual(TEXT("六步原顺序及名称"), Step->GetName(), FString(Names[I]));
        TestTrue(TEXT("原步骤仍归属旧导演"), Step->GetOuter() == Director);
        const FString Type = I < 4 ? TEXT("TutorialStep_ShowMessage") : (I == 4 ? TEXT("TutorialStep_WaitForServe") : TEXT("TutorialStep_ConsumeServedDrink"));
        TestEqual(TEXT("原步骤类型保留"), Step->GetClass()->GetPathName(), TEXT("/Script/CyberCafe.") + Type);
        TestEqual(TEXT("步骤提示精确匹配批准的冰拿铁基线"), Step->HintText.ToString(), FString(Hints[I]));
        const FString Completed = I < 4 ? TEXT("") : (I == 4
            ? TEXT("分量对，比例也对。这杯我收下了。") : TEXT("……嗯，奶和咖啡融得挺好。这杯是给人喝的了。"));
        TestEqual(TEXT("步骤完成文本精确匹配批准基线"), Step->CompletedText.ToString(), Completed);
        FRow Row; Row.Text(TEXT("stage"), TEXT("savedOldStep")); Row.Number(TEXT("index"), OriginalStepIndices[I]);
        Row.Text(TEXT("path"), Step->GetPathName()); Row.Text(TEXT("hint"), Step->HintText.ToString());
        Row.Text(TEXT("completed"), Step->CompletedText.ToString()); Evidence.Add(Row);
    }
    for (int32 MenuIndex = 4; MenuIndex <= 5 && Director->Steps.IsValidIndex(MenuIndex); ++MenuIndex)
    {
        UTutorialStep* Step = Director->Steps[MenuIndex].Get();
        if (!TestNotNull(TEXT("新增菜单步骤实例有效"), Step)) return false;
        TestEqual(TEXT("新增菜单步骤名称与索引一致"), Step->GetName(), FString(MenuIndex == 4
            ? TEXT("TutorialStep_ShowMessage_Menu_0") : TEXT("TutorialStep_WaitForRecipeMenu_0")));
        TestEqual(TEXT("新增菜单步骤采用指定原生类型"), Step->GetClass()->GetPathName(), FString(MenuIndex == 4
            ? TEXT("/Script/CyberCafe.TutorialStep_ShowMessage") : TEXT("/Script/CyberCafe.TutorialStep_WaitForRecipeMenu")));
        TestTrue(TEXT("新增菜单步骤内联归属原导演"), Step->GetOuter() == Director);
        TestFalse(TEXT("新增菜单步骤提示文本非空"), Step->HintText.IsEmpty());
        TestEqual(TEXT("菜单步骤提示精确匹配 applied.json"), Step->HintText.ToString(), FString(MenuIndex == 4
            ? TEXT("菜单上点一下冰拿铁，就能看怎么做。") : TEXT("点一下屏幕里的冰拿铁。")));
        TestEqual(TEXT("菜单步骤完成文本精确匹配批准基线"), Step->CompletedText.ToString(), FString(MenuIndex == 4
            ? TEXT("") : TEXT("对，就照这份来。先放一铲冰。")));
    }
    auto* Wait = Director->Steps.IsValidIndex(6) ? Cast<UTutorialStep_WaitForServe>(Director->Steps[6].Get()) : nullptr;
    if (!TestNotNull(TEXT("旧拿铁 Step[6] 上菜步骤"), Wait) || !TestNotNull(TEXT("原拿铁配方"), Wait->RequiredRecipe.Get())) return false;
    TestTrue(TEXT("原上菜全息引用不变"), Wait->OrderHologram.Get() == Holo);
    TestEqual(TEXT("原配方路径不变"), Wait->RequiredRecipe->GetPathName(), FString(TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
    TestEqual(TEXT("原上菜点不变"), GetPathNameSafe(Wait->RequiredSpot.Get()), FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_ServingSpot_C_1")));
    TestEqual(TEXT("原 RecipeId Latte 保留"), Wait->RequiredRecipe->RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("拿铁杯 class 精确匹配 Highball"), GetPathNameSafe(Wait->RequiredRecipe->AcceptedCupClass.Get()), FString(PrimaryCupClassPath));
    TestEqual(TEXT("旧加冰建议值保留"), Wait->RequiredRecipe->ExpectedIceCount, 3);
    if (!TestNotNull(TEXT("原服务位存在"), Wait->RequiredSpot.Get()) || !TestNotNull(TEXT("原 NPC 引用存在"), Director->NPCRef.Get())) return false;
    TestTrue(TEXT("原服务位仅 Z 更新为批准高度"), Wait->RequiredSpot->GetActorLocation().Equals(FVector(-21, 71, 114.68106781), .0001));
    TestEqual(TEXT("原 NPC 实例引用不变"), Director->NPCRef->GetPathName(), FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_TutorialNPC_Manny_C_1")));
    TestTrue(TEXT("原 NPC 位置不变"), Director->NPCRef->GetActorLocation().Equals(FVector(-9, 48, 82), .001));
    Near(*this, TEXT("原拿铁目标200mL"), Wait->RequiredRecipe->TargetVolumeML, 200);
    Near(*this, TEXT("原拿铁下界180mL"), Wait->RequiredRecipe->MinVolumeML, 180);
    Near(*this, TEXT("原拿铁上界220mL"), Wait->RequiredRecipe->MaxVolumeML, 220);
    TestEqual(TEXT("原拿铁仍恰好两种成分"), Wait->RequiredRecipe->Rules.Num(), 2);
    TSet<FName> Ingredients;
    for (const auto& Rule : Wait->RequiredRecipe->Rules)
    {
        Ingredients.Add(Rule.IngredientId); Near(*this, TEXT("旧拿铁比例下界45%"), Rule.MinFraction, .45);
        Near(*this, TEXT("旧拿铁比例上界55%"), Rule.MaxFraction, .55);
    }
    TestTrue(TEXT("原拿铁仍为Coffee与Milk"), Ingredients.Contains(Coffee) && Ingredients.Contains(Milk));
    TestEqual(TEXT("读取期间未BeginPlay原地图"), Map->HasBegunPlay(), BegunBefore);
    TestEqual(TEXT("读取期间未推进原地图时钟"), Map->GetTimeSeconds(), static_cast<double>(TimeBefore));
    FRow Row; Row.Text(TEXT("stage"), TEXT("savedMapReferences")); Row.Number(TEXT("restoreTargetCount"), Targets.Num());
    Row.Text(TEXT("dripper"), MapD->GetPathName()); Row.Text(TEXT("kettle"), MapK->GetPathName()); Row.Text(TEXT("receiver"), MapCup->GetPathName());
    Row.Text(TEXT("primaryCup"), PrimaryCup->GetPathName()); Row.Text(TEXT("primaryCupClass"), PrimaryCup->GetClass()->GetPathName());
    Row.Text(TEXT("primaryCupMesh"), GetPathNameSafe(PrimaryCup->ContainerMesh->GetStaticMesh().Get()));
    Row.Text(TEXT("primaryCupProfile"), GetPathNameSafe(Reset->CupProfile.Get()));
    Row.Number(TEXT("primaryCupCapacityML"), PrimaryCup->MaxVolumeML);
    Row.Vector(TEXT("primaryCupRootScale"), PrimaryCup->ContainerMesh->GetRelativeScale3D());
    Row.Vector(TEXT("dripperPositionCM"), MapD->GetActorLocation()); Row.Vector(TEXT("kettlePositionCM"), MapK->GetActorLocation());
    Row.Vector(TEXT("receiverPositionCM"), MapCup->GetActorLocation()); Evidence.Add(Row);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeRealSavedGroundsWaterReceiverTest,
    "CyberCafe.CoffeeRuntime.RealSavedGroundsAndWaterToCoffeeReceiver", Flags)
bool FCoffeeRealSavedGroundsWaterReceiverTest::RunTest(const FString&)
{
    FEvidence Evidence(*this, TEXT("RealSavedGroundsAndWaterToCoffeeReceiver"), TEXT("actualSavedBlueprintsStaticCollisionWorldTickPublicGrindAPI"));
    FAssets Assets; if (!Assets.Check(*this)) return false;
    FinishCompilation(); FRuntimeWorld T(*this); if (!T.World) return false;
    auto* G = T.Spawn<ACoffeeGrinderActor>(Assets.Grinder, FVector(300, 0, 0));
    auto* D = T.Spawn<AHandDripperActor>(Assets.Dripper, FVector::ZeroVector);
    // Fill 在 BeginPlay 前设置；100mL来自壶的实际初始库存，不调用 AddLiquid 或手工记账。
    auto* K = T.Spawn<AHandPourKettleActor>(Assets.Kettle, FVector(-100, 0, 0), .2f);
    auto* Cup = T.Spawn<ACupActor>(Assets.Cup, FVector(0, 0, -9.88), 0.f);
    if (!TestNotNull(TEXT("实际原研磨BP实例"), G) || !TestNotNull(TEXT("实际保存滤杯"), D)
        || !TestNotNull(TEXT("实际保存壶"), K) || !TestNotNull(TEXT("实际原Cup03"), Cup)) return false;
    T.BeginPlay();
    auto* S = T.Transfers(); if (!TestNotNull(TEXT("真实World液体转移系统"), S)) return false;
    TestTrue(TEXT("真实GI与事件总线"), T.World->GetGameInstance() == T.GI && T.GI->GetSubsystem<UCyberCafeEventSubsystem>() != nullptr);
    TestTrue(TEXT("世界正常BeginPlay，静态碰撞存在但不模拟刚体"), T.World->HasBegunPlay() && T.World->GetPhysicsScene()
        && !T.World->bShouldSimulatePhysics && G->HasActorBegunPlay() && D->HasActorBegunPlay() && K->HasActorBegunPlay() && Cup->HasActorBegunPlay());
    auto* Handle = G->HandleRef.Get(); auto* Drawer = G->DrawerRef.Get();
    if (!TestNotNull(TEXT("原BP生产路径生成真实Handle"), Handle) || !TestNotNull(TEXT("原BP生产路径生成真实Drawer"), Drawer)) return false;
    TestTrue(TEXT("子件来自原BP保存子类且正常BeginPlay"), Handle->GetClass() == G->HandleClass.Get()
        && Drawer->GetClass() == G->DrawerClass.Get() && Handle->HasActorBegunPlay() && Drawer->HasActorBegunPlay());
    TestTrue(TEXT("原柄与屉绑定真实主体"), Handle->OwnerGrinder.Get() == G && Drawer->OwnerGrinder.Get() == G);
    TestTrue(TEXT("抽屉起始Attached且生产搬粉回调已绑定"), Drawer->State == AGrinderDrawerActor::EDrawerState::Attached
        && G->OnGrindProgress.Contains(Drawer, FName(TEXT("OnGrinderGrindProgress"))));
    if (!PrepareBody(*this, G->BodyMesh.Get()) || !PrepareBody(*this, Drawer->DrawerMesh.Get())
        || !PrepareBody(*this, D->ContainerMesh.Get()) || !PrepareBody(*this, K->ContainerMesh.Get()) || !PrepareBody(*this, Cup->ContainerMesh.Get())) return false;
    TestTrue(TEXT("展示子网格不挡实际入粉/入水"), D->PaperMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision
        && D->GroundBed->GetCollisionEnabled() == ECollisionEnabled::NoCollision && D->WaterSurface->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    Near(*this, TEXT("壶初始真实Water100"), K->GetIngredientVolumeML(Water), 100);
    Near(*this, TEXT("初始账本只有壶100"), S->GetBalance().InitialML, 100);
    Near(*this, TEXT("没有额外注入"), S->GetBalance().InjectedML, 0);
    Near(*this, TEXT("直立壶实际公开API不出水"), K->PourWater(StepSeconds), 0);
    Near(*this, TEXT("投豆公共API接纳15g"), G->TryAddBeans(15), 15);
    Near(*this, TEXT("保存主体反向不消耗"), G->AdvanceGrinding(-180, .5f), 0);
    // 已批准的业务净角样本，不声称经过头显跟踪；实际搬粉仍由生产 delegate 完成。
    for (int32 I = 0; I < 12; ++I) G->AdvanceGrinding(180, .5f);
    Near(*this, TEXT("真实六圈消耗15g"), G->GetTotalBeanConsumedGrams(), 15);
    Near(*this, TEXT("主体真实豆清空"), G->GetCurrentBeanGrams(), 0);
    Near(*this, TEXT("主体产粉已搬走，不重复保留"), G->GetCurrentGroundGrams(), 0);
    Near(*this, TEXT("真实Attached抽屉收到15g"), Drawer->GetGroundsGrams(), 15);
    Near(*this, TEXT("累计真实产粉15g"), G->GetTotalGroundOutputGrams(), 15);
    Near(*this, TEXT("豆粉损耗账闭合"), G->GetTotalBeanConsumedGrams(), G->GetTotalGroundOutputGrams() + G->GetGrindLossGrams());
    G->AdvanceGrinding(180, .5f); Drawer->OnGrinderGrindProgress(G->GetCurrentBeanGrams(), G->GetCurrentGroundGrams());
    Near(*this, TEXT("磨空及重复同步不双计"), Drawer->GetGroundsGrams(), 15);
    Capture(Evidence, TEXT("actualAttachedDrawerAfterSixTurns"), T, D, Cup, K, G, Drawer);

    // 夹具仅控制抽屉脱离和位姿；口点保持BP保存值与实际继承缩放，不改粉量或目标入口。
    Drawer->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    Drawer->State = AGrinderDrawerActor::EDrawerState::Detached;
    const FVector LipWorld = D->PourEntryPoint->GetComponentLocation() + FVector(0, 0, 13.5);
    const FVector LipLocal = Drawer->GetActorTransform().InverseTransformPosition(Drawer->GroundPourPoint->GetComponentLocation());
    const FQuat Tilt(FVector::ForwardVector, -PI / 3);
    Drawer->SetActorLocationAndRotation(LipWorld - Tilt.RotateVector(LipLocal * Drawer->GetActorScale3D()), Tilt,
        false, nullptr, ETeleportType::TeleportPhysics);
    TestTrue(TEXT("实际保存抽屉口点逆变换在目标正上方"), Drawer->GroundPourPoint->GetComponentLocation().Equals(LipWorld, .001));
    TestTrue(TEXT("出粉使用局部+Y朝下，不误用+X"), FVector::DotProduct(Drawer->DrawerMesh->GetComponentQuat().RotateVector(FVector::YAxisVector), FVector::DownVector) > .3);
    for (int32 I = 0; I < 4 && Drawer->GetGroundsGrams() > 0; ++I)
    {
        const double Before = Drawer->GetGroundsGrams(), AcceptedBefore = D->GetGroundsGrams(), SpillBefore = Drawer->SpilledGroundGrams;
        const double Poured = Drawer->PourGrounds(1.f);
        Near(*this, TEXT("每次扣粉等于公开真实出粉"), Before - Drawer->GetGroundsGrams(), Poured);
        Near(*this, TEXT("每次出粉等于实际接纳加撒粉"), Poured, D->GetGroundsGrams() - AcceptedBefore + Drawer->SpilledGroundGrams - SpillBefore);
        TestTrue(TEXT("第一真实圆盘命中，而非绕过物理障碍"), Drawer->LastGroundPourEnd.Equals(D->PourEntryPoint->GetComponentLocation(), .001));
        Capture(Evidence, TEXT("actualSavedDrawerPublicPour"), T, D, Cup, K, G, Drawer);
    }
    Near(*this, TEXT("抽屉真实归零"), Drawer->GetGroundsGrams(), 0);
    Near(*this, TEXT("滤杯实际收到15g，不由粉粒子结算"), D->GetGroundsGrams(), 15);
    Near(*this, TEXT("正确入粉无撒漏"), Drawer->SpilledGroundGrams, 0);
    Near(*this, TEXT("真实粉总量闭合"), G->GetTotalGroundOutputGrams(), G->GetCurrentGroundGrams() + Drawer->GetGroundsGrams() + D->GetGroundsGrams() + Drawer->SpilledGroundGrams);
    TestTrue(TEXT("真实粉床随实际克数显示"), D->GroundBed->IsVisible());
    if (!TestNotNull(TEXT("实际保存粉床网格，缺失时失败而不崩溃"), D->GroundBed->GetStaticMesh().Get())) return false;
    const FBox GroundBox = D->GroundBed->GetStaticMesh()->GetBoundingBox().TransformBy(D->GroundBed->GetComponentTransform());
    const FVector GroundCenter = D->GetActorTransform().InverseTransformPosition(GroundBox.GetCenter());
    const double GroundRadius = FMath::Max(GroundBox.GetExtent().X, GroundBox.GetExtent().Y);
    const FVector EntryLocal = D->GetActorTransform().InverseTransformPosition(D->PourEntryPoint->GetComponentLocation());
    const double ConeRadius = D->PourEntryRadius * FMath::Max(0.0, GroundBox.Min.Z - D->GetActorLocation().Z) / EntryLocal.Z;
    TestTrue(TEXT("保存粉床实际直径约3cm且低于口沿，不作像素断言"), GroundRadius * 2 >= 2.0 && GroundRadius * 2 <= 3.6
        && GroundBox.Max.Z < D->PourEntryPoint->GetComponentLocation().Z && GroundCenter.Z > 0);
    TestTrue(TEXT("粉床实际边界在锥内且小于外壳物理半径"), GroundRadius + GroundCenter.Size2D() <= ConeRadius + .2
        && GroundRadius < D->ContainerMesh->Bounds.BoxExtent.X);

    // 抽屉离开水流路径；测试操作者移动壶，不改壶口或颗粒出生位置。
    Drawer->SetActorLocation(FVector(300, 100, 0), false, nullptr, ETeleportType::TeleportPhysics);
    const FVector SpoutLocal = K->GetActorTransform().InverseTransformPosition(K->PourFX->GetComponentLocation());
    const FQuat KettleTilt = FRotator(-60, 0, 0).Quaternion();
    const FVector SpoutWorld = D->GetActorLocation() + FVector(-2, 0, 15);
    K->SetActorLocationAndRotation(SpoutWorld - KettleTilt.RotateVector(SpoutLocal * K->GetActorScale3D()), KettleTilt,
        false, nullptr, ETeleportType::TeleportPhysics);
    TestTrue(TEXT("真实壶嘴组件在滤杯上方"), K->PourFX->GetComponentLocation().Equals(SpoutWorld, .001));
    TestTrue(TEXT("真实Pitch-60使壶尖朝下"), FVector::DotProduct(K->GetActorForwardVector(), FVector::DownVector) > .3);
    Capture(Evidence, TEXT("beforeActualKettleWorldTick"), T, D, Cup, K, G, Drawer);
    bool SawWaterPackets = false, SawBloom = false, SawCoffeePackets = false, SawJet = false;
    double PeakStorage = 0, FirstWetTime = -1, FirstDripTime = -1;
    int32 PeakPackets = 0;
    for (int32 Frame = 0; Frame < 2500; ++Frame)
    {
        T.Tick(); // 唯一液体调度；不调用 Simulate、AdvanceBrew、EmitVolume 或目标接液API。
        const FLiquidBalance B = S->GetBalance();
        SawWaterPackets |= B.PacketCount > 0 && D->DrippedML == 0;
        SawCoffeePackets |= B.PacketCount > 0 && D->DrippedML > 0;
        // The kettle now drives the shared thin Ribbon rather than 20 sphere drops.
        // FX assets are disabled in this logical world, so observe its continuous
        // driver state; dedicated KettleRibbon tests verify the saved asset/params.
        SawJet |= K->bIsPouring && K->CurrentFlowMLPerSecond > 0 && K->WaterJet->GetInstanceCount() == 0;
        PeakPackets = FMath::Max(PeakPackets, B.PacketCount); PeakStorage = FMath::Max(PeakStorage, D->GetPreciseVolumeML());
        if (D->GetIngredientVolumeML(Water) > 0 && FirstWetTime < 0) FirstWetTime = T.World->GetTimeSeconds();
        if (D->DrippedML > 0 && FirstDripTime < 0) FirstDripTime = T.World->GetTimeSeconds();
        if (D->BrewState == ECoffeeBrewState::Blooming)
        {
            SawBloom = true; Near(*this, TEXT("实际闷蒸不提前滴流"), D->DrippedML, 0);
            Near(*this, TEXT("实际闷蒸源库存仍为Water"), D->GetIngredientVolumeML(Coffee), 0);
        }
        if (Frame == 0) Near(*this, TEXT("正常Tick只出一次14mL/s，不走基类双出液"), K->CurrentFlowMLPerSecond, 14, .001);
        CheckBalance(*this, S);
        if (Frame % 25 == 0) Capture(Evidence, TEXT("actualLiquidWorldTick"), T, D, Cup, K, G, Drawer);
    }
    TestTrue(TEXT("真实观察到清水包、闷蒸、滴流包与连续Ribbon驱动状态"), SawWaterPackets && SawBloom && SawCoffeePackets && SawJet);
    TestTrue(TEXT("真实世界时钟至少闷蒸8秒才滴流"), FirstWetTime >= 0 && FirstDripTime - FirstWetTime >= 7.98);
    TestTrue(TEXT("实际储水没有越过120mL"), PeakStorage > 90 && PeakStorage <= D->MaxVolumeML + LiquidTolerance);
    Near(*this, TEXT("真实水壶最终零库存"), K->GetPreciseVolumeML(), 0);
    Near(*this, TEXT("滤杯最终零库存"), D->GetPreciseVolumeML(), 0);
    Near(*this, TEXT("实际总滴量100mL"), D->DrippedML, 100);
    Near(*this, TEXT("接收原Cup03真实Coffee100"), Cup->GetIngredientVolumeML(Coffee), 100);
    Near(*this, TEXT("原Cup03真实总量100"), Cup->GetPreciseVolumeML(), 100);
    Near(*this, TEXT("输出标签不是Water"), Cup->GetIngredientVolumeML(Water), 0);
    Near(*this, TEXT("没有额外Milk"), Cup->GetIngredientVolumeML(Milk), 0);
    Near(*this, TEXT("没有额外Unknown"), Cup->GetIngredientVolumeML(TEXT("Unknown")), 0);
    Near(*this, TEXT("源Water只改出液包，不原地回注Coffee"), D->GetIngredientVolumeML(Coffee), 0);
    Near(*this, TEXT("粉仍保留已用15g"), D->GetGroundsGrams(), 15);
    TestTrue(TEXT("同批Spent不恢复成干粉"), D->BrewState == ECoffeeBrewState::Spent && !D->CanReceiveGrounds());
    Near(*this, TEXT("同批有限咖啡预算扣到140"), D->GetCoffeeBudgetML(), 140, .05);
    const FLiquidBalance Final = S->GetBalance();
    Near(*this, TEXT("全过程没有第二次注入"), Final.InjectedML, 0); Near(*this, TEXT("全过程没有手工移除"), Final.RemovedML, 0);
    Near(*this, TEXT("真实静态几何闭环无撒漏"), Final.SpilledML, 0); Near(*this, TEXT("全部实际在途包结算"), Final.InFlightML, 0);
    TestEqual(TEXT("真实液包计数归零"), Final.PacketCount, 0);
    TestTrue(TEXT("输出材质由实际子类钩子传至接收杯"), D->CoffeeLiquidMaterial && Cup->LiquidMaterialAsset.Get() == D->CoffeeLiquidMaterial.Get());
    TestFalse(TEXT("无需HUD或假持有条件"), K->IsHeld() || D->IsHeld() || Cup->IsHeld());
    TestFalse(TEXT("真实容器未破损或冻结"), K->IsBroken() || D->IsBroken() || Cup->IsBroken() || Cup->IsLiquidFrozen());
    FRow Timing; Timing.Text(TEXT("stage"), TEXT("measuredWorldTiming")); Timing.Number(TEXT("firstWetTimeSeconds"), FirstWetTime);
    Timing.Number(TEXT("firstDripTimeSeconds"), FirstDripTime); Timing.Number(TEXT("peakStorageML"), PeakStorage);
    Timing.Number(TEXT("peakPacketCount"), PeakPackets); Evidence.Add(Timing);
    Capture(Evidence, TEXT("actualSavedChainFinal"), T, D, Cup, K, G, Drawer);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeReplacePaperFineFlowTest,
    "CyberCafe.CoffeeRuntime.ReplacePaperAndFineFlow", Flags)
bool FCoffeeReplacePaperFineFlowTest::RunTest(const FString&)
{
    FEvidence Evidence(*this, TEXT("ReplacePaperAndFineFlow"), TEXT("nativeFixtureWorldTickFiniteBudgetAndPublicDelayedPackets"));
    for (bool CatchOldPacket : {false, true})
    {
        FRuntimeWorld T(*this); if (!T.World) return false;
        auto* D = T.Spawn<AHandDripperActor>(AHandDripperActor::StaticClass(), FVector(300, 0, 20), 0.f);
        auto* Cup = T.Spawn<ACupActor>(ACupActor::StaticClass(), FVector::ZeroVector, 0.f);
        if (!TestNotNull(TEXT("旧批次延迟包的原生滤杯"), D) || !TestNotNull(TEXT("旧包近远命中原生接收杯"), Cup)) return false;
        Cup->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector); Cup->PourEntryRadius = 4;
        T.BeginPlay(); auto* S = T.Transfers(); if (!TestNotNull(TEXT("旧包真实世界账本"), S)) return false;
        Near(*this, TEXT("旧纸真实接纳2g"), D->AddGrounds(2, D->PourEntryPoint->GetComponentLocation()), 2);
        Near(*this, TEXT("旧纸初始预算32"), D->GetCoffeeBudgetML(), 32);
        Near(*this, TEXT("旧纸明确注入真实10mL"), D->AddIngredientLiquid(10, Water, FLinearColor::White), 10);
        const FVector PacketOrigin = Cup->PourEntryPoint->GetComponentLocation()
            + FVector(CatchOldPacket ? 0 : 100, 0, 10);
        // 本组故意通过公开Emit构造旧来源延迟包，不将合成起点冒称保存壶嘴几何。
        Near(*this, TEXT("旧滤杯真实扣10并创建延迟Coffee包"), S->EmitVolume(D, 10,
            PacketOrigin, FVector(0, 0, -50), FVector::ZeroVector, .5), 10);
        Near(*this, TEXT("旧包发射后源库存实际零"), D->GetPreciseVolumeML(), 0);
        Near(*this, TEXT("实际旧包已扣咖啡预算到22"), D->GetCoffeeBudgetML(), 22);
        Near(*this, TEXT("旧包仍在途10"), S->GetBalance().InFlightML, 10);
        TestFalse(TEXT("源空但旧包未结算时拒绝换纸"), D->ReplacePaper());
        for (int32 I = 0; I < 40; ++I) T.Tick();
        Near(*this, TEXT("延迟出生前不能凭空信用目标杯"), Cup->GetPreciseVolumeML(), 0);
        Capture(Evidence, TEXT("nativeOldPacketBeforeBirth"), T, D, Cup);
        for (int32 I = 0; I < 360; ++I) { T.Tick(); CheckBalance(*this, S); }
        Near(*this, TEXT("旧包结算后源没有重新出液"), D->DrippedML, 10);
        Near(*this, TEXT("旧包结算后源仍实际空"), D->GetPreciseVolumeML(), 0);
        Near(*this, TEXT("旧包物理结算后在途零"), S->GetBalance().InFlightML, 0);
        TestEqual(TEXT("旧包物理结算后包计数零"), S->GetBalance().PacketCount, 0);
        Near(*this, TEXT("近口旧Coffee包接纳，远口不隔空信用"), Cup->GetIngredientVolumeML(Coffee), CatchOldPacket ? 10 : 0);
        Near(*this, TEXT("未命中旧包10必须记入撒漏"), S->GetBalance().SpilledML, CatchOldPacket ? 0 : 10);
        const int32 Generation = D->BrewGeneration;
        if (!TestTrue(TEXT("真实空源且旧包全结算后换纸"), D->ReplacePaper())) return false;
        Near(*this, TEXT("换纸丢弃旧2g"), D->DiscardedGroundsGrams, 2);
        Near(*this, TEXT("换纸丢弃未用22预算，而非恢复旧32"), D->GetCoffeeBudgetML(), 0);
        Near(*this, TEXT("新纸实际粉量零"), D->GetGroundsGrams(), 0);
        TestEqual(TEXT("旧包测试换纸只推进一代"), D->BrewGeneration, Generation + 1);
        D->SetActorLocation(FVector(0, 0, 20), false, nullptr, ETeleportType::TeleportPhysics);
        Near(*this, TEXT("新空纸明确注入10mLWater"), D->AddIngredientLiquid(10, Water, FLinearColor::White), 10);
        for (int32 I = 0; I < 500; ++I) { T.Tick(); CheckBalance(*this, S); }
        Near(*this, TEXT("新纸无粉只透Water10"), Cup->GetIngredientVolumeML(Water), 10);
        Near(*this, TEXT("新纸不再复用旧粉生产Coffee"), D->CoffeeProducedML, 0);
        Near(*this, TEXT("旧接收Coffee记录不被换纸或补水改写"), Cup->GetIngredientVolumeML(Coffee), CatchOldPacket ? 10 : 0);
        Near(*this, TEXT("两次实际注水20守恒，明确包含旧包撒漏"),
            Cup->GetPreciseVolumeML() + S->GetBalance().SpilledML, 20);
        Near(*this, TEXT("旧包案例所有尾包结清"), S->GetBalance().InFlightML, 0);
        FRow Row; Row.Text(TEXT("stage"), TEXT("nativeSettledOldPacketAndDiscardedRemainingBudget"));
        Row.Bool(TEXT("oldPacketCaught"), CatchOldPacket); Row.Vector(TEXT("syntheticOldPacketOriginCM"), PacketOrigin); Evidence.Add(Row);
        Capture(Evidence, TEXT("nativeOldPacketNewEmptyPaperFinal"), T, D, Cup);
    }
    for (int32 Run = 0; Run < 3; ++Run)
    {
        FRuntimeWorld T(*this); if (!T.World) return false;
        auto* D = T.Spawn<AHandDripperActor>(AHandDripperActor::StaticClass(), FVector(0, 0, 20), 0.f);
        auto* Cup = T.Spawn<ACupActor>(ACupActor::StaticClass(), FVector::ZeroVector, 0.f);
        auto* Donor = T.Spawn<ACupActor>(ACupActor::StaticClass(), FVector(1000, 0, 20), .1f);
        if (!TestNotNull(TEXT("有限预算原生滤杯夹具"), D) || !TestNotNull(TEXT("原生接液夹具"), Cup)
            || !TestNotNull(TEXT("旧批补水的真实20mL来源"), Donor)) return false;
        // 本组明确为原生口点夹具，不将其混称保存BP几何验收。
        Cup->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector); Cup->PourEntryRadius = 4;
        Donor->InitialIngredientId = Water;
        T.BeginPlay(); auto* S = T.Transfers(); if (!TestNotNull(TEXT("原生有限批次真实账本"), S)) return false;
        Near(*this, TEXT("实际入口接纳2g"), D->AddGrounds(2, D->PourEntryPoint->GetComponentLocation()), 2);
        Near(*this, TEXT("2g有限咖啡预算32mL"), D->GetCoffeeBudgetML(), 32);
        Near(*this, TEXT("原生夹具明确注入40mLWater"), D->AddIngredientLiquid(40, Water, FLinearColor::White), 40);
        TestFalse(TEXT("存水时禁止换纸"), D->ReplacePaper());
        const float Dt = Run == 1 ? .05f : StepSeconds;
        if (Run == 2)
        {
            // Explicit test-world override: normal UE clamps a 10s input to a much shorter frame.
            T.World->GetWorldSettings()->MaxUndilatedFrameTime=10.f;
            T.Tick(10.f); // 仅测试World放开上限，先8秒闷蒸，再2秒积分，工程配置不改。
            Near(*this, TEXT("真实10秒长帧滴量24而非120"), D->DrippedML, 24, .001);
            Capture(Evidence, TEXT("nativeActualTenSecondWorldTick"), T, D, Cup);
        }
        for (int32 I = 0; I < FMath::CeilToInt(25.f / Dt); ++I)
        {
            T.Tick(Dt); CheckBalance(*this, S);
            if (I % 50 == 0) Capture(Evidence, TEXT("nativeFiniteBudgetWorldTick"), T, D, Cup);
        }
        Near(*this, TEXT("2g细流产出接近32，允许有界float欠产"), Cup->GetIngredientVolumeML(Coffee), 32, BudgetRoundingTolerance);
        TestTrue(TEXT("双精度整批咖啡上限仍严格不超过32"), D->CoffeeProducedML <= 32.0 + 1.e-9
            && Cup->GetIngredientVolumeML(Coffee) <= 32.0 + 1.e-9);
        Near(*this, TEXT("超出预算8mL及欠产尾差保留Water"), Cup->GetIngredientVolumeML(Water), 8, BudgetRoundingTolerance);
        Near(*this, TEXT("真实接收总量40"), Cup->GetPreciseVolumeML(), 40);
        Near(*this, TEXT("细流不会生成无限float尾量"), D->CoffeeProducedML, 32, BudgetRoundingTolerance);
        Near(*this, TEXT("有限预算耗尽"), D->GetCoffeeBudgetML(), 0);
        Near(*this, TEXT("滴空保留原2g"), D->GetGroundsGrams(), 2);
        Near(*this, TEXT("已用批次不允许追加粉"), D->AddGrounds(1, D->PourEntryPoint->GetComponentLocation()), 0);
        const int32 OldGeneration = D->BrewGeneration;
        // 旧批次延迟入水包来自真实来源库存；在途时即使滤杯空也不能换纸。
        Near(*this, TEXT("旧批真实来源发出延迟10mL"), S->EmitVolume(Donor, 10,
            D->PourEntryPoint->GetComponentLocation() + FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector, .5), 10);
        TestFalse(TEXT("零存水但有在途包时拒绝换纸"), D->ReplacePaper());
        TestEqual(TEXT("拒绝换纸不改批次"), D->BrewGeneration, OldGeneration);
        for (int32 I = 0; I < 500; ++I) { T.Tick(); CheckBalance(*this, S); }
        Near(*this, TEXT("同批补水不恢复Coffee预算"), D->CoffeeProducedML, 32, BudgetRoundingTolerance);
        Near(*this, TEXT("同批补水仅多10mLWater"), Cup->GetIngredientVolumeML(Water), 18, BudgetRoundingTolerance);
        TestEqual(TEXT("同批补水不新增generation"), D->BrewGeneration, OldGeneration);
        Near(*this, TEXT("换纸前存水确实为零"), D->GetPreciseVolumeML(), 0);
        Near(*this, TEXT("换纸前所有在途真实结清"), S->GetBalance().InFlightML, 0);
        const FLiquidBalance BeforeReplace = S->GetBalance();
        if (!TestTrue(TEXT("仅零存水且零在途时公开换纸成功"), D->ReplacePaper())) return false;
        Near(*this, TEXT("换纸实际丢弃2g，不回加研磨器或源粉"), D->DiscardedGroundsGrams, 2);
        Near(*this, TEXT("换纸后粉量零"), D->GetGroundsGrams(), 0); Near(*this, TEXT("换纸不恢复旧32预算"), D->GetCoffeeBudgetML(), 0);
        TestEqual(TEXT("换纸仅推进一代"), D->BrewGeneration, OldGeneration + 1);
        Near(*this, TEXT("换纸不注入液体"), S->GetBalance().InjectedML, BeforeReplace.InjectedML);
        Near(*this, TEXT("换纸不移除液体"), S->GetBalance().RemovedML, BeforeReplace.RemovedML);
        Near(*this, TEXT("换纸不吞掉或伪造撒漏"), S->GetBalance().SpilledML, BeforeReplace.SpilledML);
        Capture(Evidence, TEXT("nativePaperReplacedAfterSettledFlight"), T, D, Cup);
        Near(*this, TEXT("新空滤纸接收真实第二个延迟10mL来源包"), S->EmitVolume(Donor, 10,
            D->PourEntryPoint->GetComponentLocation() + FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector, .5), 10);
        TestFalse(TEXT("新纸也不能绕过在途保护"), D->ReplacePaper());
        for (int32 I = 0; I < 500; ++I) { T.Tick(); CheckBalance(*this, S); }
        Near(*this, TEXT("新空滤纸不生产咖啡"), D->CoffeeProducedML, 0);
        Near(*this, TEXT("旧杯Coffee保持原有界产出，不无限重用旧粉"), Cup->GetIngredientVolumeML(Coffee), 32, BudgetRoundingTolerance);
        TestTrue(TEXT("补水及换纸后咖啡仍严格不超过原批次上限32"), Cup->GetIngredientVolumeML(Coffee) <= 32.0 + 1.e-9);
        Near(*this, TEXT("新空滤纸保持第二包Water10"), Cup->GetIngredientVolumeML(Water), 28, BudgetRoundingTolerance);
        Near(*this, TEXT("实际总供给60闭合到接收杯"), Cup->GetPreciseVolumeML(), 60);
        Near(*this, TEXT("真实来源20全部扣除"), Donor->GetPreciseVolumeML(), 0);
        Near(*this, TEXT("原生组仅初始化20及明确注入40"), S->GetBalance().InitialML + S->GetBalance().InjectedML, 60);
        Near(*this, TEXT("全部真实液包已结算"), S->GetBalance().InFlightML, 0); TestEqual(TEXT("尾包计数归零"), S->GetBalance().PacketCount, 0);
        Near(*this, TEXT("原生几何路径无漏量，不能只测总误差"), S->GetBalance().SpilledML, 0);
        FRow Variant; Variant.Text(TEXT("stage"), TEXT("measuredNativeFlowVariant")); Variant.Number(TEXT("run"), Run);
        Variant.Number(TEXT("dtSeconds"), Dt); Variant.Bool(TEXT("tenSecondWorldHitch"), Run == 2); Evidence.Add(Variant);
        Capture(Evidence, TEXT("nativeNewEmptyPaperFinal"), T, D, Cup);
    }
    return !HasAnyErrors();
}
}
#endif // WITH_DEV_AUTOMATION_TESTS
