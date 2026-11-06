// @trace CF-03
// 仅验证保存绑定、公共出流状态及真实液体账本；清空测试实例 Niagara 后不作像素/头显验收声明。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GrabComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <limits>

namespace CyberCafeKettleRibbonTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double Tolerance = 1.e-4;
constexpr float StepSeconds = .02f;
constexpr int32 SourceFrames = 10;
const TCHAR* KettlePath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandPourKettle.BP_HandPourKettle_C");
const TCHAR* RibbonPath = TEXT("/Game/LiquidMaterials_VFXPack/Effects/P_Ribbon.P_Ribbon");
const FVector SpoutLocal(16.8f, 0.f, 13.f);

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}

FString Quote(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    Value.ReplaceInline(TEXT("\b"), TEXT("\\b"));
    Value.ReplaceInline(TEXT("\f"), TEXT("\\f"));
    return TEXT("\"") + Value + TEXT("\"");
}

struct FRow
{
    TArray<FString> Fields;
    void Text(const TCHAR* Key, const FString& Value) { Fields.Add(Quote(Key) + TEXT(":") + Quote(Value)); }
    void Number(const TCHAR* Key, double Value)
    {
        Fields.Add(Quote(Key) + TEXT(":") + (FMath::IsFinite(Value)
            ? FString::Printf(TEXT("%.12g"), Value) : FString(TEXT("null"))));
    }
    void Bool(const TCHAR* Key, bool Value) { Fields.Add(Quote(Key) + (Value ? TEXT(":true") : TEXT(":false"))); }
    FString Json() const { return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}"); }
};

struct FEvidence
{
    FAutomationTestBase& Test;
    const TCHAR* Group;
    TArray<FString> Rows;
    FEvidence(FAutomationTestBase& InTest, const TCHAR* InGroup) : Test(InTest), Group(InGroup) {}
    void Add(const FRow& Row) { Rows.Add(Row.Json()); }
    ~FEvidence()
    {
        // 未提供运行目录时不产生文件；不读取历史证据、不写构建或任务文件。
        const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("VISUAL_FIX_RUN_DIR"));
        if (Root.IsEmpty()) return;
        static TMap<FString, FString> ExecutedGroups;
        FRow Header;
        Header.Text(TEXT("group"), Group);
        Header.Text(TEXT("status"), Test.HasAnyErrors() ? TEXT("Failed") : TEXT("Passed"));
        Header.Number(TEXT("sampleCount"), Rows.Num());
        ExecutedGroups.Add(Group, Header.Json().LeftChop(1) + TEXT(",\"samples\":[")
            + FString::Join(Rows, TEXT(",")) + TEXT("]}"));
        TArray<FString> Keys;
        ExecutedGroups.GetKeys(Keys);
        Keys.Sort();
        TArray<FString> Groups;
        for (const FString& Key : Keys) Groups.Add(ExecutedGroups.FindChecked(Key));
        const FString Json = TEXT("{\"schemaVersion\":1,\"recordedAtUTC\":")
            + Quote(FDateTime::UtcNow().ToIso8601())
            + TEXT(",\"scope\":\"saved_blueprint_binding_and_headless_game_world_logic\",\"visualVerified\":false,\"headsetVerified\":false,\"activationResetCountVerified\":false,\"savedMapVerified\":false,\"groups\":[")
            + FString::Join(Groups, TEXT(",")) + TEXT("]}\n");
        const FString Directory = Root / TEXT("Native");
        Test.TestTrue(TEXT("写出本进程实际执行的热水壶逻辑证据"),
            IFileManager::Get().MakeDirectory(*Directory, true)
            && FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("KettleRibbonTests.json")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
};

void CheckWaterJet(FAutomationTestBase& Test, const AHandPourKettleActor* K)
{
    if (!Test.TestNotNull(TEXT("兼容 WaterJet 子对象存在"), K->WaterJet.Get())) return;
    Test.TestEqual(TEXT("WaterJet 不含旧实例球"), K->WaterJet->GetInstanceCount(), 0);
    Test.TestTrue(TEXT("WaterJet 始终隐藏"), K->WaterJet->bHiddenInGame);
    Test.TestFalse(TEXT("WaterJet 不可见"), K->WaterJet->IsVisible());
    Test.TestFalse(TEXT("WaterJet 不允许组件 Tick"), K->WaterJet->PrimaryComponentTick.bCanEverTick);
    Test.TestFalse(TEXT("WaterJet 未启用组件 Tick"), K->WaterJet->IsComponentTickEnabled());
}

bool CheckConfiguration(FAutomationTestBase& Test, FEvidence& Evidence,
    const AHandPourKettleActor* K, UNiagaraSystem* OriginalRibbon, const TCHAR* Stage)
{
    if (!Test.TestNotNull(TEXT("热水壶配置对象存在"), K)
        || !Test.TestNotNull(TEXT("原共享 P_Ribbon 是真实 Niagara System"), OriginalRibbon)
        || !Test.TestNotNull(TEXT("配置中的 PourFX 组件存在"), K->PourFX.Get())) return false;
    const bool bOriginal = Test.TestTrue(TEXT("PourFX 直接绑定原 P_Ribbon，非副本或替代粒子"),
        K->PourFX->GetAsset() == OriginalRibbon);
    Near(Test, TEXT("细水柱宽度为 0.25cm"), K->KettleRibbonThicknessCM, .25);
    Near(Test, TEXT("Ribbon 强度为 0.35"), K->FlowStrength, .35);
    Near(Test, TEXT("最大业务流速保持 28mL/s"), K->MaxFlowMLPerSecond, 28);
    Near(Test, TEXT("倾倒阈值保持 40 度"), K->PourTiltDegrees, 40);
    Test.TestFalse(TEXT("壶不启用瓶杯基类倒液"), K->bCanPour);
    Test.TestFalse(TEXT("壶不接收其他容器入液"), K->bAcceptLiquidFromOthers);
    Test.TestFalse(TEXT("壶业务包不使用 Ribbon 年龄曲线"), K->bUseRibbonTrajectory);
    Test.TestTrue(TEXT("禁用 Ribbon 小水花"), K->bNoSplashes);
    Test.TestTrue(TEXT("禁用 Ribbon Bottle List"), K->bNoList);
    Test.TestFalse(TEXT("禁用 Ribbon 地面湿迹"), K->bEnableDecal);
    CheckWaterJet(Test, K);
    FRow Row;
    Row.Text(TEXT("stage"), Stage);
    Row.Text(TEXT("classPath"), K->GetClass()->GetPathName());
    Row.Text(TEXT("pourFXAssetPathBeforeNull"), GetPathNameSafe(K->PourFX->GetAsset()));
    Row.Bool(TEXT("originalRibbonBinding"), bOriginal);
    Row.Number(TEXT("thicknessCM"), K->KettleRibbonThicknessCM);
    Row.Number(TEXT("flowStrength"), K->FlowStrength);
    Row.Bool(TEXT("noSplashes"), K->bNoSplashes);
    Row.Bool(TEXT("noList"), K->bNoList);
    Row.Bool(TEXT("decalEnabled"), K->bEnableDecal);
    Evidence.Add(Row);
    return !Test.HasAnyErrors();
}

struct FSavedAssets
{
    // 先加载实际保存 BP，再取 CDO；禁止用 native 默认值填补缺失保存资产。
    UClass* Class = LoadClass<AHandPourKettleActor>(nullptr, KettlePath);
    UNiagaraSystem* Ribbon = LoadObject<UNiagaraSystem>(nullptr, RibbonPath);
    bool Check(FAutomationTestBase& Test, FEvidence& Evidence) const
    {
        if (!Test.TestNotNull(TEXT("实际保存热水壶 BP 可加载，缺失明确失败"), Class)) return false;
        Test.TestEqual(TEXT("实际保存 BP 类路径"), Class->GetPathName(), FString(KettlePath));
        return CheckConfiguration(Test, Evidence, Class->GetDefaultObject<AHandPourKettleActor>(),
            Ribbon, TEXT("savedBlueprintCDOBeforeSpawnAndNull"));
    }
};

struct FNativeWorld
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    int32 Frames = 0;
    explicit FNativeWorld(FAutomationTestBase& InTest) : Test(InTest)
    {
        if (!Test.TestNotNull(TEXT("引擎上下文可用"), GEngine)) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
            true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("创建真实 Game World"), World)) return;
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
    }
    ~FNativeWorld()
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
    bool Ready() { return Test.TestNotNull(TEXT("真实液体转移账本可用"), Transfer()); }
    static void DisablePhysicsAndAutoFX(AHandPourKettleActor* K)
    {
        TArray<UStaticMeshComponent*> Meshes;
        K->GetComponents(Meshes);
        for (UStaticMeshComponent* Mesh : Meshes)
        {
            Mesh->SetSimulatePhysics(false);
            Mesh->SetEnableGravity(false);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetGenerateOverlapEvents(false);
        }
        TArray<UNiagaraComponent*> FXs;
        K->GetComponents(FXs);
        for (UNiagaraComponent* FX : FXs) FX->SetAutoActivate(false);
        K->GrabComp->PhysicsMode = EGrabPhysicsMode::Kinematic;
        K->GrabComp->bSimulateOnDrop = false;
        K->GrabComp->bCaptureHand = false;
        K->GrabComp->bHideHandWhileHeld = false;
        K->GrabComp->OnGrabHapticEffect = nullptr;
        K->PourHaptic = nullptr;
    }
    AHandPourKettleActor* Spawn(UClass* Class, UNiagaraSystem* Ribbon, FEvidence& Evidence)
    {
        if (!World || !Class) return nullptr;
        // 远离地面与接液方，且关闭物理；Fill 在 BeginPlay 注册前确定，初始账不是 Injected。
        const FTransform Pose(FQuat::Identity, FVector(0, 0, 10000), FVector::OneVector);
        auto* K = World->SpawnActorDeferred<AHandPourKettleActor>(Class, Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!Test.TestNotNull(TEXT("Deferred 创建热水壶"), K)) return nullptr;
        K->MaxVolumeML = 500.f;
        K->FillAmount = .2f;
        DisablePhysicsAndAutoFX(K);
        K->FinishSpawning(Pose);
        // Construction Script 已执行；先核对实际 BP 实例资产，之后仅在测试实例中清空。
        if (!CheckConfiguration(Test, Evidence, K, Ribbon, TEXT("spawnedInstanceBeforeBeginPlayAndNull"))) return nullptr;
        DisablePhysicsAndAutoFX(K);
        TArray<UNiagaraComponent*> FXs;
        K->GetComponents(FXs);
        for (UNiagaraComponent* FX : FXs)
        {
            FX->DeactivateImmediate();
            FX->SetAsset(nullptr);
        }
        K->LiquidFXTemplate = nullptr;
        K->SplashEffectTemplate = nullptr;
        K->GlassBreakProfile = nullptr;
        K->WaterLiquidMaterial = nullptr;
        K->LiquidMaterialAsset = nullptr;
        K->bEnableSplash = false;
        K->CapClass = nullptr;
        K->bStartCapped = false;
        K->MaxVolumeML = 500.f;
        K->FillAmount = .2f;
        K->InitialIngredientId = TEXT("Water");
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        DisablePhysicsAndAutoFX(K);
        Test.TestTrue(TEXT("热水壶确实执行 BeginPlay"), K->HasActorBegunPlay());
        Test.TestTrue(TEXT("保留真实 Actor Tick"), K->IsActorTickEnabled());
        Test.TestNull(TEXT("仅逻辑实例的 PourFX 资产已清空"), K->PourFX->GetAsset());
        Test.TestFalse(TEXT("BeginPlay 后尚未出流"), K->bIsPouring);
        Near(Test, TEXT("BeginPlay 后当前流量为零"), K->CurrentFlowMLPerSecond, 0);
        CheckWaterJet(Test, K);
        Near(Test, TEXT("BeginPlay 初始化真实库存 100mL"), K->GetPreciseVolumeML(), 100);
        Near(Test, TEXT("初始 Water 身份为 100mL"), K->GetIngredientVolumeML(TEXT("Water")), 100);
        Near(Test, TEXT("初始登记不走二次注入"), Transfer()->GetBalance().InitialML, 100);
        Near(Test, TEXT("未注入额外水量"), Transfer()->GetBalance().InjectedML, 0);
        return K;
    }
    void Tick(float Dt = StepSeconds)
    {
        ++GFrameCounter;
        World->Tick(LEVELTICK_All, Dt);
        ++Frames;
    }
};

void CheckBalance(FAutomationTestBase& Test, ULiquidTransferSubsystem* Transfer)
{
    const FLiquidBalance B = Transfer->GetBalance();
    Test.TestTrue(TEXT("全账本有限且非负"), FMath::IsFinite(B.InitialML) && B.InitialML >= 0
        && FMath::IsFinite(B.InjectedML) && B.InjectedML >= 0
        && FMath::IsFinite(B.RemovedML) && B.RemovedML >= 0
        && FMath::IsFinite(B.ContainerML) && B.ContainerML >= 0
        && FMath::IsFinite(B.InFlightML) && B.InFlightML >= 0
        && FMath::IsFinite(B.SpilledML) && B.SpilledML >= 0 && B.PacketCount >= 0);
    Near(Test, TEXT("库存+在途+撒漏+移除等于初始+注入"),
        B.ContainerML + B.InFlightML + B.SpilledML + B.RemovedML, B.InitialML + B.InjectedML);
    Near(Test, TEXT("真实 GetBalance.ErrorML 为零"), B.ErrorML, 0);
}

void CheckFloatOverride(FAutomationTestBase& Test, const UNiagaraComponent* FX,
    const TCHAR* Name, float Expected)
{
    const FNiagaraVariableBase Variable(FNiagaraTypeDefinition::GetFloatDef(), FName(Name));
    const uint8* Data = FX->GetOverrideParameters().GetParameterData(Variable);
    if (!Test.TestNotNull(FString::Printf(TEXT("实际 Niagara Override 存在：%s"), Name), Data)) return;
    float Actual = 0.f;
    FMemory::Memcpy(&Actual, Data, sizeof(Actual));
    Near(Test, FString::Printf(TEXT("实际 Niagara Override：%s"), Name), Actual, Expected);
}

void Capture(FEvidence& Evidence, const TCHAR* Stage, const FNativeWorld& T, const AHandPourKettleActor* K)
{
    const FLiquidBalance B = T.Transfer()->GetBalance();
    FRow Row;
    Row.Text(TEXT("stage"), Stage);
    Row.Text(TEXT("runtimeClassPath"), K->GetClass()->GetPathName());
    Row.Number(TEXT("worldFrame"), T.Frames);
    Row.Number(TEXT("worldTimeSeconds"), T.World->GetTimeSeconds());
    Row.Number(TEXT("sourceML"), K->GetPreciseVolumeML());
    Row.Number(TEXT("flowMLPerSecond"), K->CurrentFlowMLPerSecond);
    Row.Bool(TEXT("bIsPouring"), K->bIsPouring);
    Row.Bool(TEXT("frozen"), K->IsLiquidFrozen());
    Row.Bool(TEXT("logicInstanceFXAssetNull"), K->PourFX->GetAsset() == nullptr);
    Row.Number(TEXT("waterJetInstances"), K->WaterJet->GetInstanceCount());
    Row.Bool(TEXT("waterJetHidden"), K->WaterJet->bHiddenInGame);
    Row.Number(TEXT("initialML"), B.InitialML);
    Row.Number(TEXT("injectedML"), B.InjectedML);
    Row.Number(TEXT("removedML"), B.RemovedML);
    Row.Number(TEXT("containerML"), B.ContainerML);
    Row.Number(TEXT("inFlightML"), B.InFlightML);
    Row.Number(TEXT("spilledML"), B.SpilledML);
    Row.Number(TEXT("errorML"), B.ErrorML);
    Row.Number(TEXT("packetCount"), B.PacketCount);
    Evidence.Add(Row);
}

void CheckStopped(FAutomationTestBase& Test, const AHandPourKettleActor* K)
{
    Test.TestFalse(TEXT("停止后 bIsPouring 为 false"), K->bIsPouring);
    Near(Test, TEXT("停止后当前流量归零"), K->CurrentFlowMLPerSecond, 0);
    CheckWaterJet(Test, K);
}

void ContinuousFrames(FAutomationTestBase& Test, FEvidence& Evidence, FNativeWorld& T, AHandPourKettleActor* K)
{
    K->SetActorRotation(FRotator(-60, 0, 0));
    Test.TestTrue(TEXT("负 60 度 ActorForward 确实朝下"),
        FVector::DotProduct(K->GetActorForwardVector(), FVector::DownVector) > .3);
    const double Before = K->GetPreciseVolumeML();
    Capture(Evidence, TEXT("beforeContinuousWorldTick"), T, K);
    for (int32 Frame = 1; Frame <= SourceFrames; ++Frame)
    {
        // 连续移动壶身，验证起点不会从上一帧已移动的 PourFX 反推而漂移。
        K->SetActorLocation(FVector(Frame, Frame * 2, 10000));
        const double FrameBefore = K->GetPreciseVolumeML();
        T.Tick();
        Test.TestTrue(TEXT("每个自然 World Tick 后连续出流状态保持 true"), K->bIsPouring);
        Near(Test, TEXT("60 度当前业务流量始终 14mL/s"), K->CurrentFlowMLPerSecond, 14);
        Near(Test, TEXT("每帧只扣一次 0.28mL，未被基类二次发量"), FrameBefore - K->GetPreciseVolumeML(), .28);
        Near(Test, TEXT("逐帧累计扣量正确"), K->GetPreciseVolumeML(), Before - Frame * .28);
        Test.TestTrue(TEXT("Ribbon 起点始终来自固定 local 壶嘴"),
            K->PourFX->GetComponentLocation().Equals(K->GetActorTransform().TransformPosition(SpoutLocal), Tolerance));
        Test.TestTrue(TEXT("Ribbon 方向跟随 Actor，保持单位缩放"),
            K->PourFX->GetComponentQuat().Equals(K->GetActorQuat(), Tolerance)
            && K->PourFX->GetRelativeScale3D().Equals(FVector::OneVector, Tolerance));
        CheckFloatOverride(Test, K->PourFX.Get(), TEXT("User.Thickness"), .25f);
        CheckFloatOverride(Test, K->PourFX.Get(), TEXT("User.FlowStrength"), .35f);
        CheckWaterJet(Test, K);
        CheckBalance(Test, T.Transfer());
        Capture(Evidence, TEXT("continuousWorldTick"), T, K);
    }
    Near(Test, TEXT("十帧 0.2 秒后由 100 降为 97.2mL"), K->GetPreciseVolumeML(), 97.2);
    Near(Test, TEXT("在途与撒漏合计只有 2.8mL"),
        T.Transfer()->GetBalance().InFlightML + T.Transfer()->GetBalance().SpilledML, 2.8);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKettleRibbonContinuousLifecycleTest,
    "CyberCafe.CoffeeVisual.KettleRibbonContinuousLifecycle", CyberCafeKettleRibbonTests::Flags)
bool FKettleRibbonContinuousLifecycleTest::RunTest(const FString&)
{
    using namespace CyberCafeKettleRibbonTests;
    FEvidence Evidence(*this, TEXT("KettleRibbonContinuousLifecycle"));
    FSavedAssets Assets;
    if (!Assets.Check(*this, Evidence)) return false;
    FNativeWorld T(*this);
    if (!T.Ready()) return false;
    auto* K = T.Spawn(AHandPourKettleActor::StaticClass(), Assets.Ribbon, Evidence);
    if (!K) return false;
    TestTrue(TEXT("逻辑回归确实使用 native kettle"), K->GetClass() == AHandPourKettleActor::StaticClass());
    ContinuousFrames(*this, Evidence, T, K);
    K->SetActorRotation(FRotator::ZeroRotator);
    T.Tick();
    CheckStopped(*this, K);
    Near(*this, TEXT("直立不追加发量"), K->GetPreciseVolumeML(), 97.2);
    // 仅停止源，尾包由真实 subsystem 自然结算；不能直接重写账本。
    T.Transfer()->Simulate(T.Transfer()->GetUnsettledFlightHorizon() + .05);
    Near(*this, TEXT("未命中容器的真实尾包全部结算为撒漏"), T.Transfer()->GetBalance().SpilledML, 2.8);
    Near(*this, TEXT("尾包结算后在途归零"), T.Transfer()->GetBalance().InFlightML, 0);
    TestEqual(TEXT("尾包结算后包计数归零"), T.Transfer()->GetBalance().PacketCount, 0);
    CheckBalance(*this, T.Transfer());
    Capture(Evidence, TEXT("uprightStoppedAndTailSettled"), T, K);
    Near(*this, TEXT("公开消费真实移除剩余 97.2mL"), K->ConsumeLiquid(200.f), 97.2);
    K->SetActorRotation(FRotator(-60, 0, 0));
    T.Tick();
    CheckStopped(*this, K);
    Near(*this, TEXT("空壶公开 PourWater 返回零"), K->PourWater(StepSeconds), 0);
    Near(*this, TEXT("空壶不产生新包"), T.Transfer()->GetBalance().InFlightML, 0);
    Near(*this, TEXT("移除账与已结算撒漏仍闭合"), T.Transfer()->GetBalance().RemovedML, 97.2);
    CheckBalance(*this, T.Transfer());
    Capture(Evidence, TEXT("emptyTiltedNoEmissionWithRemovedLedger"), T, K);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKettleRibbonSavedBlueprintBindingTest,
    "CyberCafe.CoffeeVisual.KettleRibbonSavedBlueprintBinding", CyberCafeKettleRibbonTests::Flags)
bool FKettleRibbonSavedBlueprintBindingTest::RunTest(const FString&)
{
    using namespace CyberCafeKettleRibbonTests;
    FEvidence Evidence(*this, TEXT("KettleRibbonSavedBlueprintBinding"));
    FSavedAssets Assets;
    if (!Assets.Check(*this, Evidence)) return false;
    FNativeWorld T(*this);
    if (!T.Ready()) return false;
    auto* K = T.Spawn(Assets.Class, Assets.Ribbon, Evidence);
    if (!K) return false;
    TestTrue(TEXT("逻辑实例确实来自实际保存 BP"), K->GetClass() == Assets.Class);
    ContinuousFrames(*this, Evidence, T, K);
    K->FreezeLiquidForServing();
    CheckStopped(*this, K);
    T.Tick();
    Near(*this, TEXT("保存 BP 冻结后不再出水"), K->GetPreciseVolumeML(), 97.2);
    CheckStopped(*this, K);
    CheckBalance(*this, T.Transfer());
    Capture(Evidence, TEXT("actualSavedBlueprintFrozen"), T, K);
    // 清空实例 FX 后 CDO 仍必须保持原共享资产与保存参数。
    CheckConfiguration(*this, Evidence, Assets.Class->GetDefaultObject<AHandPourKettleActor>(),
        Assets.Ribbon, TEXT("savedBlueprintCDOUntouchedAfterLogic"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKettleRibbonStopFreezeNoDoubleLedgerTest,
    "CyberCafe.CoffeeVisual.KettleRibbonStopFreezeNoDoubleLedger", CyberCafeKettleRibbonTests::Flags)
bool FKettleRibbonStopFreezeNoDoubleLedgerTest::RunTest(const FString&)
{
    using namespace CyberCafeKettleRibbonTests;
    FEvidence Evidence(*this, TEXT("KettleRibbonStopFreezeNoDoubleLedger"));
    FSavedAssets Assets;
    if (!Assets.Check(*this, Evidence)) return false;
    FNativeWorld T(*this);
    if (!T.Ready()) return false;
    auto* K = T.Spawn(AHandPourKettleActor::StaticClass(), Assets.Ribbon, Evidence);
    if (!K) return false;
    K->SetActorRotation(FRotator(-60, 0, 0));
    T.Tick();
    TestTrue(TEXT("首次自然 Tick 开始出流"), K->bIsPouring);
    Near(*this, TEXT("首次只发量一次"), K->GetPreciseVolumeML(), 99.72);
    K->SetActorRotation(FRotator::ZeroRotator);
    T.Tick();
    CheckStopped(*this, K);
    Near(*this, TEXT("直立停止不扣库存"), K->GetPreciseVolumeML(), 99.72);
    K->PourFX->DeactivateImmediate();
    K->SetActorRotation(FRotator(-60, 0, 0));
    T.Tick();
    TestTrue(TEXT("外部停用 FX 后重新倾倒可恢复公共出流状态"), K->bIsPouring);
    Near(*this, TEXT("恢复帧没有双倍发量"), K->GetPreciseVolumeML(), 99.44);
    Capture(Evidence, TEXT("retiltedAfterExplicitFXDeactivation"), T, K);
    const double BeforeInvalid = K->GetPreciseVolumeML();
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    for (float InvalidDt : {NaN, 0.f, -StepSeconds, std::numeric_limits<float>::infinity()})
    {
        Near(*this, TEXT("非法 PourWater 时间返回零"), K->PourWater(InvalidDt), 0);
        CheckStopped(*this, K);
        Near(*this, TEXT("非法时间不发量"), K->GetPreciseVolumeML(), BeforeInvalid);
        CheckBalance(*this, T.Transfer());
        Capture(Evidence, TEXT("invalidPourWaterTimeStopped"), T, K);
    }
    T.Tick();
    TestTrue(TEXT("非法时间后正常帧可重新开始"), K->bIsPouring);
    Near(*this, TEXT("再次恢复仍只发量一次"), K->GetPreciseVolumeML(), 99.16);
    // 在出流中制造一个旧 WaterJet 实例，再通过基类公共冻结 API 验证 virtual StopPouring 收尾。
    K->WaterJet->AddInstance(FTransform::Identity);
    K->WaterJet->SetHiddenInGame(false);
    K->WaterJet->SetVisibility(true);
    ALiquidContainerActor* Base = K;
    Base->FreezeLiquidForServing();
    TestTrue(TEXT("基类冻结 API 设置真实冻结状态"), K->IsLiquidFrozen());
    CheckStopped(*this, K);
    const double FrozenVolume = K->GetPreciseVolumeML();
    for (int32 Frame = 0; Frame < SourceFrames; ++Frame)
    {
        T.Tick();
        CheckStopped(*this, K);
        Near(*this, TEXT("冻结后自然 World Tick 保持库存"), K->GetPreciseVolumeML(), FrozenVolume);
        CheckBalance(*this, T.Transfer());
    }
    Near(*this, TEXT("冻结后公开 PourWater 返回零"), K->PourWater(StepSeconds), 0);
    Near(*this, TEXT("冻结后 subsystem 也拒绝发量"), T.Transfer()->EmitVolume(K, 1,
        K->GetActorTransform().TransformPosition(SpoutLocal), K->GetActorForwardVector() * 35,
        FVector(0, 0, -980)), 0);
    T.Transfer()->Simulate(T.Transfer()->GetUnsettledFlightHorizon() + .05);
    Near(*this, TEXT("仅三帧真实发量 0.84mL，冻结不重复结算"), T.Transfer()->GetBalance().SpilledML, .84);
    Near(*this, TEXT("冻结未把源余液记作移除"), T.Transfer()->GetBalance().RemovedML, 0);
    Near(*this, TEXT("冻结未二次注入"), T.Transfer()->GetBalance().InjectedML, 0);
    Near(*this, TEXT("冻结后尾包全部结清"), T.Transfer()->GetBalance().InFlightML, 0);
    CheckBalance(*this, T.Transfer());
    Capture(Evidence, TEXT("frozenNoEmissionAndTailSettled"), T, K);
    return !HasAnyErrors();
}
#endif
