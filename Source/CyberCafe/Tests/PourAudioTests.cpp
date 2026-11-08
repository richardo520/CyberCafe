// 仅验证保存配置、真实液体账本和非实时设备上的组件播放请求；不代表渲染音频或头显可听。
// 音频用例运行环境须启用 -AudioMixer 且不带 -nosound；设备创建失败明确失败，不跳过。
#include "Tests/PourAudioTestProbe.h"
#include "Coffee/Brew/HandPourKettleActor.h"
void UPourAudioTestProbe::CloseDuringEmission(float Fill,FLinearColor Color)
{
    ++Notifications;
    if (auto* Source=Kettle.Get()) Source->OnCapAttached();
}
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ActorEditorUtils.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "AudioDeviceManager.h"
#include "CoreGlobals.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Liquid/BottleActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Coffee/Bean/DirectBeanJarActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GrabComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace CyberCafePourAudioTests
{
constexpr auto Options = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double Tolerance = 1.e-5;
constexpr float Dt = .02f;
const TCHAR* SoundPath = TEXT("/Game/VRTemplate/Audio/Pouring/S_PourWaterLoop.S_PourWaterLoop");
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
const TCHAR* KettlePath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandPourKettle.BP_HandPourKettle_C");
const TCHAR* BottlePath = TEXT("/Game/VRTemplate/MenuIngredients/Bottles/BP_Ingredient_Water.BP_Ingredient_Water_C");
const TCHAR* CupPath = TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C");

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}

USoundWave* LoadSoundWave(FAutomationTestBase& Test)
{
    auto* Sound = LoadObject<USoundWave>(nullptr, SoundPath);
    if (!Test.TestNotNull(TEXT("精确路径的已保存 SoundWave 可加载"), Sound)) return nullptr;
    Test.TestEqual(TEXT("共享声音资产路径完全一致"), Sound->GetPathName(), FString(SoundPath));
    Test.TestTrue(TEXT("已保存波形启用循环，不由测试修改"), Sound->bLooping != 0);
    // UE 5.6 的 USoundWave 没有 GetNumChannels()，该方法属于代理；这里读取公开资产信息。
    Test.TestEqual(TEXT("已保存波形为单声道"), Sound->NumChannels, 1);
    Test.TestEqual(TEXT("导入采样率为 44100 Hz，使用真实 getter"), Sound->GetImportedSampleRate(), uint32(44100));
    Test.TestTrue(TEXT("源波形时长超过一秒，不能只凭循环的无限时长通过"),
        FMath::IsFinite(Sound->Duration) && Sound->Duration > 1.f);
    Test.TestTrue(TEXT("真实 GetDuration 返回有效长循环时长"), Sound->GetDuration() > 1.f);
    return Test.HasAnyErrors() ? nullptr : Sound;
}

void CheckDefaults(FAutomationTestBase& Test, const ALiquidContainerActor* A, const USoundWave* Sound)
{
    const FString Label = A->GetPathName();
    if (const auto* Jar = Cast<ADirectBeanJarActor>(A))
    {
        Test.TestFalse(Label + TEXT(" 豆罐明确禁用液体循环水声"), Jar->bEnablePourAudio);
        Test.TestFalse(Label + TEXT(" 豆罐不允许倒液或接液"), Jar->bCanPour || Jar->bAcceptLiquidFromOthers);
        Near(Test, Label + TEXT(" 豆罐初始Fill恒零"), Jar->FillAmount, 0);
        Near(Test, Label + TEXT(" 豆罐权威液体恒零"), Jar->GetPreciseVolumeML(), 0);
        Test.TestNull(Label + TEXT(" 豆罐不分配倒水声音组件"), Jar->GetPourAudioComponent());
        Test.TestFalse(Label + TEXT(" 豆罐无倒水声音请求"), Jar->IsPourAudioRequested());
        Near(Test, Label + TEXT(" 豆罐液体音频流量恒零"), Jar->PourAudioActualFlow, 0);
        Test.TestEqual(Label + TEXT(" 豆罐水声启动次数恒零"), Jar->AudioStartCount, 0);
        return;
    }
    Test.TestTrue(Label + TEXT(" 继承启用音频"), A->bEnablePourAudio);
    Test.TestTrue(Label + TEXT(" 共用已保存声音"), A->PourLoopSound.Get() == Sound);
    Near(Test, Label + TEXT(" 默认音量"), A->PourAudioVolume, .45);
    Near(Test, Label + TEXT(" 默认参考流量"), A->PourAudioReferenceFlow, 40);
    Near(Test, Label + TEXT(" 默认淡入"), A->PourAudioFadeIn, .06);
    Near(Test, Label + TEXT(" 默认淡出"), A->PourAudioFadeOut, .12);
    Test.TestNull(Label + TEXT(" BeginPlay 前未创建 lazy 音频组件"), A->GetPourAudioComponent());
    Test.TestFalse(Label + TEXT(" 初始无播放请求"), A->IsPourAudioRequested());
    Near(Test, Label + TEXT(" 初始实际流量"), A->PourAudioActualFlow, 0);
    Test.TestEqual(Label + TEXT(" 初始启动计数"), A->AudioStartCount, 0);
}

// 每个独立场景持有自己的 World/GI/设备，不借用编辑器或头显的主设备。
struct FWorld
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    FAudioDeviceHandle Audio;
    bool bReady = false;

    explicit FWorld(FAutomationTestBase& InTest, bool bWithAudio) : Test(InTest)
    {
        if (!Test.TestNotNull(TEXT("引擎上下文存在"), GEngine)) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(bWithAudio).CreatePhysicsScene(true).CreateNavigation(false)
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
        if (bWithAudio)
        {
            auto* Manager = FAudioDeviceManager::Get();
            if (!Test.TestNotNull(TEXT("音频设备管理器存在；须无 -nosound 且启用 -AudioMixer"), Manager)) return;
            FAudioDeviceParams Params;
            Params.Scope = EAudioDeviceScope::Unique;
            Params.bIsNonRealtime = true;
            Params.AssociatedWorld = World;
            Audio = Manager->RequestAudioDevice(Params);
            if (!Test.TestNotNull(TEXT("必须成功创建独立非实时音频设备，失败不能 skip"), Audio.GetAudioDevice())) return;
            if (!Test.TestTrue(TEXT("设备确实为非实时，不能向头显硬件输出"), Audio->IsNonRealtime())) return;
            World->SetAudioDevice(Audio);
            if (!Test.TestTrue(TEXT("World 绑定本夹具设备"), World->GetAudioDeviceRaw() == Audio.GetAudioDevice())) return;
            if (!Test.TestTrue(TEXT("音频夹具 World 允许音频"), World->AllowAudioPlayback())) return;
        }
        else
        {
            Test.TestFalse(TEXT("无音频夹具由 InitializationValues 禁止音频"), World->AllowAudioPlayback());
            Test.TestFalse(TEXT("无音频夹具没有请求设备"), Audio.IsValid());
        }
        bReady = Test.TestNotNull(TEXT("真实液体转移 subsystem 已创建"), Transfer());
    }

    ~FWorld()
    {
        if (!World) return;
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        World->SetAudioDevice(FAudioDeviceHandle());
        Audio.Reset();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    ULiquidTransferSubsystem* Transfer() const
    {
        return World ? World->GetSubsystem<ULiquidTransferSubsystem>() : nullptr;
    }

    static void DisablePhysicsAndGraphics(AActor* A)
    {
        TArray<UStaticMeshComponent*> Meshes;
        A->GetComponents(Meshes);
        for (auto* Mesh : Meshes)
        {
            Mesh->SetSimulatePhysics(false);
            Mesh->SetEnableGravity(false);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetGenerateOverlapEvents(false);
        }
        TArray<UNiagaraComponent*> FXs;
        A->GetComponents(FXs);
        for (auto* FX : FXs)
        {
            FX->SetAutoActivate(false);
            FX->SetRenderingEnabled(false);
            FX->SetVisibility(false);
            FX->SetHiddenInGame(true);
            FX->PrimaryComponentTick.bCanEverTick = false;
            FX->SetComponentTickEnabled(false);
        }
        if (auto* Grab = A->FindComponentByClass<UGrabComponent>())
        {
            Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
            Grab->bSimulateOnDrop = false;
            Grab->OnGrabHapticEffect = nullptr;
        }
    }

    template <typename T>
    T* Spawn(UClass* Class, bool bOpenBottle = true, float InitialFill = 1.f)
    {
        if (!bReady || !Test.TestNotNull(TEXT("真实保存的待生成类存在"), Class)) return nullptr;
        // 空世界无落点/接液方；靠近默认 listener，仅避免空间衰减范围外的立即拒播。
        const FTransform Pose(FQuat::Identity, FVector(0, 0, 80), FVector::OneVector);
        auto* A = World->SpawnActorDeferred<T>(Class, Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!Test.TestNotNull(TEXT("真实 deferred 容器生成成功"), A)) return nullptr;
        DisablePhysicsAndGraphics(A);
        A->FinishSpawning(Pose);
        DisablePhysicsAndGraphics(A);
        // 只清液体渲染模板；绝不清 PourFX Asset，generic Ribbon 判定依赖其元数据。
        A->LiquidFXTemplate = nullptr;
        if (A->LiquidFX) A->LiquidFX->SetAsset(nullptr);
        A->SplashEffectTemplate = nullptr;
        A->bEnableSplash = false;
        A->GlassBreakProfile = nullptr;
        A->FillAmount = InitialFill;
        if (auto* Bottle = Cast<ABottleActor>(A))
        {
            Bottle->bStartCapped = !bOpenBottle;
            Bottle->PourHaptic = nullptr;
        }
        if (auto* Kettle = Cast<AHandPourKettleActor>(A))
        {
            Kettle->MaxVolumeML = 500.f;
            Kettle->MaxFlowMLPerSecond = 28.f;
            Kettle->PourTiltDegrees = 40.f;
            // 故意允许一次基类路径：壶 Tick 必须自行屏蔽 generic 发量，而不是夹具代为屏蔽。
            Kettle->bCanPour = true;
        }
        if (!Test.TestNotNull(TEXT("保留 PourFX 组件"), A->PourFX.Get())
            || !Test.TestNotNull(TEXT("保留真实 PourFX 资产与 Ribbon 元数据"), A->PourFX->GetAsset())) return nullptr;
        Test.TestTrue(TEXT("实例类精确匹配保存 BP"), A->GetClass() == Class);
        Test.TestFalse(TEXT("设置初始库存时尚未 BeginPlay"), A->HasActorBegunPlay());
        Test.TestNull(TEXT("Spawn/Construction 不提前创建音频组件"), A->GetPourAudioComponent());
        return A;
    }

    bool Begin(ALiquidContainerActor* A)
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        DisablePhysicsAndGraphics(A);
        if (auto* Bottle = Cast<ABottleActor>(A))
            if (Bottle->CapActor) DisablePhysicsAndGraphics(Bottle->CapActor.Get());
        return Test.TestTrue(TEXT("WorldSettings.NotifyBeginPlay 确实驱动容器 BeginPlay"), A->HasActorBegunPlay());
    }

    void TickN(int32 N = 1)
    {
        for (int32 I = 0; I < N; ++I)
        {
            ++GFrameCounter;
            World->Tick(LEVELTICK_All, Dt);
        }
    }
};

void CheckBalance(FAutomationTestBase& Test, const FWorld& F)
{
    const auto B = F.Transfer()->GetBalance();
    Test.TestTrue(TEXT("账本各体积有限且非负"),
        FMath::IsFinite(B.InitialML) && B.InitialML >= 0 && FMath::IsFinite(B.InjectedML) && B.InjectedML >= 0
        && FMath::IsFinite(B.RemovedML) && B.RemovedML >= 0 && FMath::IsFinite(B.ContainerML) && B.ContainerML >= 0
        && FMath::IsFinite(B.InFlightML) && B.InFlightML >= 0 && FMath::IsFinite(B.SpilledML) && B.SpilledML >= 0);
    Near(Test, TEXT("真实账本 ErrorML 为零"), B.ErrorML, 0);
    Near(Test, TEXT("初始+注入=库存+在途+撒漏+移除"), B.InitialML + B.InjectedML,
        B.ContainerML + B.InFlightML + B.SpilledML + B.RemovedML);
}

bool CheckPlaying(FAutomationTestBase& Test, ALiquidContainerActor* A, USoundWave* Sound)
{
    auto* Audio = A->GetPourAudioComponent();
    if (!Test.TestNotNull(TEXT("真实正出流 lazy 创建音频组件"), Audio)) return false;
    Test.TestTrue(TEXT("真实组件 IsPlaying，不等同于头显可听"), Audio->IsPlaying());
    Test.TestTrue(TEXT("组件处于 active 播放请求"), Audio->IsActive());
    Test.TestTrue(TEXT("公开音频请求为 true"), A->IsPourAudioRequested());
    Test.TestTrue(TEXT("实际音频流量严格为正"), A->PourAudioActualFlow > 0);
    Test.TestTrue(TEXT("播放声音为精确共享 Loop"), Audio->Sound.Get() == Sound);
    Test.TestFalse(TEXT("同一组件默认禁止多实例重叠播放"), Audio->bCanPlayMultipleInstances != 0);
    Test.TestTrue(TEXT("音频世界缩放恒为单位，不继承容器或 PourFX 缩放"),
        Audio->GetComponentScale().Equals(FVector::OneVector, Tolerance));
    Test.TestTrue(TEXT("音频组件归属实际出液 Actor"), Audio->GetOwner() == A);
    Test.TestTrue(TEXT("实际音频组件启用空间化和衰减覆盖"), Audio->bAllowSpatialization && Audio->bOverrideAttenuation && Audio->AttenuationOverrides.bSpatialize && Audio->AttenuationOverrides.bAttenuate);
    Test.TestTrue(TEXT("实际音源球形近距35cm、衰减180cm"), Audio->AttenuationOverrides.AttenuationShape == EAttenuationShape::Sphere && FMath::IsNearlyEqual(Audio->AttenuationOverrides.AttenuationShapeExtents.X,35.f) && FMath::IsNearlyEqual(Audio->AttenuationOverrides.FalloffDistance,180.f));
    Test.TestTrue(TEXT("循环源允许衰减到静音后持续"), Sound->IsPlayWhenSilent());
    return !Test.HasAnyErrors();
}

void CheckImmediateStop(FAutomationTestBase& Test, const ALiquidContainerActor* A)
{
    Test.TestFalse(TEXT("立即停止后无音频请求"), A->IsPourAudioRequested());
    Near(Test, TEXT("立即停止后实际音频流量归零"), A->PourAudioActualFlow, 0);
    if (const auto* Audio = A->GetPourAudioComponent())
    {
        Test.TestFalse(TEXT("立即停止令组件 IsPlaying 为 false"), Audio->IsPlaying());
        Test.TestFalse(TEXT("立即停止令组件 active 为 false"), Audio->IsActive());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioSavedLoopAndSceneDefaults,
    "CyberCafe.PourAudio.SavedLoopAndSceneDefaults", CyberCafePourAudioTests::Options)
bool FPourAudioSavedLoopAndSceneDefaults::RunTest(const FString&)
{
    auto* Map = LoadObject<UWorld>(nullptr, MapPath);
    if (!TestNotNull(TEXT("读取保存 L_Cafe，不启动或另存地图"), Map)
        || !TestNotNull(TEXT("保存地图 PersistentLevel 存在"), Map->PersistentLevel.Get())) return false;
    if (!TestFalse(TEXT("保存世界未 BeginPlay"), Map->HasBegunPlay())) return false;
    struct FSnapshot
    {
        AActor* Actor;
        FTransform Pose;
        float Fill;
        float Capacity;
        double ML;
    };
    TArray<FSnapshot> Before;
    int32 ActorCount = 0;
    for (AActor* Actor : Map->PersistentLevel->Actors)
    {
        if (!IsValid(Actor)) continue;
        // Match the EditorActorSubsystem count; PersistentLevel also owns
        // world-settings/builder/transient actors not listed as saved scene props.
        if (Actor->IsEditable() && Actor->IsListedInSceneOutliner() && !Actor->IsTemplate() &&
            !Actor->HasAnyFlags(RF_Transient) && !FActorEditorUtils::IsABuilderBrush(Actor) && !Actor->IsA(AWorldSettings::StaticClass())) ++ActorCount;
        auto* Liquid = Cast<ALiquidContainerActor>(Actor);
        Before.Add({Actor, Actor->GetActorTransform(), Liquid ? Liquid->FillAmount : 0.f,
            Liquid ? Liquid->MaxVolumeML : 0.f, Liquid ? Liquid->GetPreciseVolumeML() : 0.0});
    }
    TestEqual(TEXT("仅新增一个豆罐后的保存场景有313个Actor"), ActorCount, 313);
    const bool bMapDirty = Map->GetOutermost()->IsDirty();
    auto* Sound = LoadSoundWave(*this);
    if (!Sound) return false;
    CheckDefaults(*this, GetDefault<ALiquidContainerActor>(), Sound);
    CheckDefaults(*this, GetDefault<ABottleActor>(), Sound);
    CheckDefaults(*this, GetDefault<ACupActor>(), Sound);
    const auto* NativeKettle = GetDefault<AHandPourKettleActor>();
    CheckDefaults(*this, NativeKettle, Sound);
    Near(*this, TEXT("native 壶容量仍为 500mL"), NativeKettle->MaxVolumeML, 500);
    Near(*this, TEXT("native 壶阈值仍为 40 度"), NativeKettle->PourTiltDegrees, 40);
    Near(*this, TEXT("native 壶最大流量仍为 28mL/s"), NativeKettle->MaxFlowMLPerSecond, 28);
    int32 Kettles = 0, Bottles = 0, Cups = 0, Liquids = 0, PourEnabled = 0, BeanJars = 0;
    for (const auto& Snapshot : Before)
    {
        auto* A = Cast<ALiquidContainerActor>(Snapshot.Actor);
        if (!A) continue;
        ++Liquids;
        PourEnabled += A->bEnablePourAudio;
        if (A->IsA<ADirectBeanJarActor>()) ++BeanJars;
        else if (A->IsA<AHandPourKettleActor>()) ++Kettles;
        else if (A->IsA<ABottleActor>()) ++Bottles;
        else if (A->IsA<ACupActor>()) ++Cups;
        CheckDefaults(*this, A, Sound);
        CheckDefaults(*this, A->GetClass()->GetDefaultObject<ALiquidContainerActor>(), Sound);
        Near(*this, TEXT("保存 Fill 不改变"), A->FillAmount, Snapshot.Fill);
        Near(*this, TEXT("保存容量不改变"), A->MaxVolumeML, Snapshot.Capacity);
        Near(*this, TEXT("保存权威 mL 不改变"), A->GetPreciseVolumeML(), Snapshot.ML);
    }
    TestTrue(TEXT("遍历到壶、瓶、杯，未只检查一个基类"), Kettles > 0 && Bottles > 0 && Cups > 0 && Liquids > 3);
    TestEqual(TEXT("新版布局18个液体适配器，含零液体豆罐"), Liquids, 18);
    TestEqual(TEXT("原17个实际液体容器保留出流水声"), PourEnabled, 17);
    TestEqual(TEXT("额外唯一豆罐不计入实际液体或水声源"), BeanJars, 1);
    TestEqual(TEXT("液体适配器恰好等于17水声容器加1豆罐"), Liquids, PourEnabled + BeanJars);
    for (const auto& Snapshot : Before)
        TestTrue(TEXT("全部已保存 Actor 布局和缩放均不改变"), Snapshot.Actor->GetActorTransform().Equals(Snapshot.Pose, Tolerance));
    TestEqual(TEXT("只读断言不使地图变脏"), Map->GetOutermost()->IsDirty(), bMapDirty);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioNoAudioWorldDoesNotAllocateAndLedgerUnchanged,
    "CyberCafe.PourAudio.NoAudioWorldDoesNotAllocateAndLedgerUnchanged", CyberCafePourAudioTests::Options)
bool FPourAudioNoAudioWorldDoesNotAllocateAndLedgerUnchanged::RunTest(const FString&)
{
    auto* Sound = LoadSoundWave(*this);
    auto* Class = LoadClass<ABottleActor>(nullptr, BottlePath);
    FWorld F(*this, false);
    if (!Sound || !F.bReady) return false;
    auto* Bottle = F.Spawn<ABottleActor>(Class);
    if (!Bottle || !F.Begin(Bottle)) return false;
    TestFalse(TEXT("纯 open fixture 不由保存默认瓶盖阻止出流"), Bottle->bStartCapped);
    TestFalse(TEXT("open fixture 未封盖"), Bottle->bIsCapped);
    TestNull(TEXT("open fixture 没生成盖子"), Bottle->CapActor.Get());
    Bottle->PourRatePerSecond = 40.f;
    Bottle->SetActorRotation(FRotator(120, 0, 0));
    const double Initial = Bottle->GetPreciseVolumeML();
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        Bottle->bEnablePourAudio = Mode != 1;
        Bottle->PourLoopSound = Mode == 2 ? nullptr : Sound;
        const double Before = Bottle->GetPreciseVolumeML();
        Bottle->Tick(Dt);
        Near(*this, TEXT("有声配置/禁用/空声音均真实扣 0.8mL"), Before - Bottle->GetPreciseVolumeML(), 40.0 * Dt);
        TestTrue(TEXT("无设备仍有真实出液状态"), Bottle->bIsPouring);
        TestNull(TEXT("无音频 World 从不分配 AudioComponent"), Bottle->GetPourAudioComponent());
        TestEqual(TEXT("无音频 World 从不请求启动"), Bottle->AudioStartCount, 0);
        CheckImmediateStop(*this, Bottle);
        CheckBalance(*this, F);
    }
    Near(*this, TEXT("正倾三帧的账本是真实 ActualDebit"), Initial - Bottle->GetPreciseVolumeML(), 3.0 * 40.0 * Dt);
    Near(*this, TEXT("实际发量完整进入在途而非音频改变流量"), F.Transfer()->GetBalance().InFlightML, Initial - Bottle->GetPreciseVolumeML());
    Bottle->OnCapAttached();
    const double CappedML = Bottle->GetPreciseVolumeML();
    F.TickN(3);
    TestTrue(TEXT("封盖状态真实上锁"), Bottle->bIsCapped && !Bottle->bCanPour);
    Near(*this, TEXT("封盖后库存不变"), Bottle->GetPreciseVolumeML(), CappedML);
    TestNull(TEXT("封盖后仍无音频组件"), Bottle->GetPourAudioComponent());
    CheckImmediateStop(*this, Bottle);
    CheckBalance(*this, F);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioWithAudioDeviceKettleLifecycle,
    "CyberCafe.PourAudio.WithAudioDeviceKettleLifecycle", CyberCafePourAudioTests::Options)
bool FPourAudioWithAudioDeviceKettleLifecycle::RunTest(const FString&)
{
    auto* Sound = LoadSoundWave(*this);
    auto* Class = LoadClass<AHandPourKettleActor>(nullptr, KettlePath);
    FWorld F(*this, true);
    if (!Sound || !F.bReady) return false;
    auto* K = F.Spawn<AHandPourKettleActor>(Class);
    if (!K || !F.Begin(K)) return false;
    F.TickN();
    Near(*this, TEXT("正立原生库存为 500mL"), K->GetPreciseVolumeML(), 500);
    TestNull(TEXT("正立首次 Tick 不分配音频"), K->GetPourAudioComponent());
    TestEqual(TEXT("正立启动次数为零"), K->AudioStartCount, 0);
    TestFalse(TEXT("正立不请求音频"), K->IsPourAudioRequested());
    // 当前壶嘴局部 X/Z 均为正；UE 正 Pitch 抬起 +X，朝下倾65度须用负 Pitch。
    // 不改壶嘴法线来迁就夹具，仍由真实 SpoutTM 验证有效方向。
    K->SetActorRotation(FRotator(-65, 0, 0));
    if (!TestTrue(TEXT("负 Pitch65 度的真实壶嘴法线朝下超过 .3"),
        FVector::DotProduct(K->GetKettleSpoutWorldTransform().GetUnitAxis(EAxis::X), FVector::DownVector) > .3)) return false;
    const double Before = K->GetPreciseVolumeML();
    F.TickN();
    if (!CheckPlaying(*this, K, Sound)) return false;
    auto* Audio = K->GetPourAudioComponent();
    Near(*this, TEXT("65 度专用流量为 17.5，未被基类重复发量"), Before - K->GetPreciseVolumeML(), 17.5 * Dt);
    Near(*this, TEXT("音频速率来自当帧实际 debit"), K->PourAudioActualFlow, (Before - K->GetPreciseVolumeML()) / Dt);
    TestTrue(TEXT("声音位于真实 GetKettleSpoutWorldTransform 壶嘴"),
        Audio->GetComponentLocation().Equals(K->GetKettleSpoutWorldTransform().GetLocation(), Tolerance));
    const float SoftVolume = Audio->VolumeMultiplier;
    const float SoftPitch = Audio->PitchMultiplier;
    const double ContinuousBefore = K->GetPreciseVolumeML();
    for (int32 I = 0; I < 20; ++I)
    {
        F.TickN();
        TestTrue(TEXT("连续 Tick 复用同一组件"), K->GetPourAudioComponent() == Audio);
        TestEqual(TEXT("Super Tick 临时禁倒不令声音每帧重启"), K->AudioStartCount, 1);
        TestTrue(TEXT("连续有效出流组件保持 IsPlaying"), Audio->IsPlaying());
        TestTrue(TEXT("连续有效出流保持请求"), K->IsPourAudioRequested());
    }
    Near(*this, TEXT("连续二十帧只由专用壶路径扣量"), ContinuousBefore - K->GetPreciseVolumeML(), 20.0 * 17.5 * Dt);
    K->SetActorLocation(FVector(20, 10, 90));
    K->SetActorScale3D(FVector(1.3));
    F.TickN();
    TestTrue(TEXT("移动缩放后的声音仍跟随固定壶嘴"),
        Audio->GetComponentLocation().Equals(K->GetKettleSpoutWorldTransform().GetLocation(), Tolerance));
    TestTrue(TEXT("壶身缩放不放大 AudioComponent"), Audio->GetComponentScale().Equals(FVector::OneVector, Tolerance));
    K->SetActorRotation(FRotator(-85, 0, 0));
    const double SteepBefore = K->GetPreciseVolumeML();
    F.TickN();
    Near(*this, TEXT("85 度最大流量仍为 28mL/s"), SteepBefore - K->GetPreciseVolumeML(), 28.0 * Dt);
    Near(*this, TEXT("85 度音频按实际 28mL/s"), K->PourAudioActualFlow, 28);
    TestTrue(TEXT("俯倒增大实际音量"), Audio->VolumeMultiplier > SoftVolume);
    TestTrue(TEXT("俯倒增大实际音高"), Audio->PitchMultiplier > SoftPitch);
    Near(*this, TEXT("满速音量只有一份流量增益"), Audio->VolumeMultiplier, K->PourAudioVolume * FMath::Sqrt(28.f / 40.f));
    CheckBalance(*this, F);
    K->SetActorRotation(FRotator::ZeroRotator);
    const double UprightML = K->GetPreciseVolumeML();
    F.TickN();
    TestFalse(TEXT("正立立即撤销播放请求"), K->IsPourAudioRequested());
    Near(*this, TEXT("正立音频逻辑速率归零"), K->PourAudioActualFlow, 0);
    Near(*this, TEXT("正立停止不扣量"), K->GetPreciseVolumeML(), UprightML);
    Near(*this, TEXT("正常停止配置为 .12 秒淡出尾部"), K->PourAudioFadeOut, .12);
    // 非实时设备没有推进渲染音频时钟；不能用 World 假时间断言 .12 秒后 IsPlaying=false。
    TestTrue(TEXT("正常停止真实进入 FadeOut 而不是立即 Stop"), Audio->GetPlayState() == EAudioComponentPlayState::FadingOut);
    K->SetActorRotation(FRotator(-65, 0, 0));
    F.TickN();
    if (!CheckPlaying(*this, K, Sound)) return false;
    TestTrue(TEXT("淡出期间快速恢复仍复用组件"), K->GetPourAudioComponent() == Audio);
    TestEqual(TEXT("停止后快速恢复只启动第二次"), K->AudioStartCount, 2);
    TestFalse(TEXT("快速恢复仍禁止多实例 overlap"), Audio->bCanPlayMultipleInstances != 0);
    K->FreezeLiquidForServing();
    CheckImmediateStop(*this, K);
    const double FrozenML = K->GetPreciseVolumeML();
    F.TickN(2);
    CheckImmediateStop(*this, K);
    Near(*this, TEXT("冻结后不再出流"), K->GetPreciseVolumeML(), FrozenML);
    CheckBalance(*this, F);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioBottleRealEmit,
    "CyberCafe.PourAudio.AudioBottleRealEmit", CyberCafePourAudioTests::Options)
bool FPourAudioBottleRealEmit::RunTest(const FString&)
{
    auto* Sound = LoadSoundWave(*this);
    auto* Class = LoadClass<ABottleActor>(nullptr, BottlePath);
    if (!Sound || !TestNotNull(TEXT("已保存水瓶类可加载"), Class)) return false;
    const auto* CDO = Class->GetDefaultObject<ABottleActor>();
    if (!TestNotNull(TEXT("已保存水瓶含真实默认 CapClass"), CDO->CapClass.Get())) return false;
    TestTrue(TEXT("已保存水瓶默认初始封盖"), CDO->bStartCapped);
    FWorld F(*this, true);
    if (!F.bReady) return false;
    auto* Bottle = F.Spawn<ABottleActor>(Class, false);
    if (!Bottle || !F.Begin(Bottle)) return false;
    if (!TestNotNull(TEXT("真实 BeginPlay 生成了保存瓶盖"), Bottle->CapActor.Get())) return false;
    Bottle->PourRatePerSecond = 40.f;
    Bottle->SetActorRotation(FRotator(110, 0, 0));
    const double CappedML = Bottle->GetPreciseVolumeML();
    F.TickN();
    TestTrue(TEXT("110 度仍真实封盖且禁止倒液"), Bottle->bIsCapped && !Bottle->bCanPour);
    Near(*this, TEXT("真实封盖不产生 debit"), Bottle->GetPreciseVolumeML(), CappedML);
    TestNull(TEXT("封盖倾倒不创建音频组件"), Bottle->GetPourAudioComponent());
    TestEqual(TEXT("封盖倾倒启动次数仍零"), Bottle->AudioStartCount, 0);
    // 验证瓶身的真实开盖通知，不伪造 VR 抓取/扭转手势；盖子物理链接不属于本音频用例。
    Bottle->OnCapDetached();
    TestFalse(TEXT("开盖通知解除真实封盖状态"), Bottle->bIsCapped);
    TestTrue(TEXT("开盖通知允许真实 generic 倒液"), Bottle->bCanPour);
    Bottle->PourRatePerSecond = 0.f;
    F.TickN();
    TestTrue(TEXT("零 rate 时可有 bIsPouring，不能据此伪造声音"), Bottle->bIsPouring);
    Near(*this, TEXT("零 rate 没有实际 debit"), Bottle->GetPreciseVolumeML(), CappedML);
    TestNull(TEXT("零 rate 不分配声音组件"), Bottle->GetPourAudioComponent());
    TestFalse(TEXT("零 rate 不请求声音"), Bottle->IsPourAudioRequested());
    Bottle->PourRatePerSecond = 40.f;
    F.TickN();
    if (!CheckPlaying(*this, Bottle, Sound)) return false;
    auto* Audio = Bottle->GetPourAudioComponent();
    Near(*this, TEXT("真实水瓶当帧出流 0.8mL"), CappedML - Bottle->GetPreciseVolumeML(), 40.0 * Dt);
    Near(*this, TEXT("generic 音频实际速率为 40"), Bottle->PourAudioActualFlow, 40);
    TestTrue(TEXT("声音与实际 generic PourFX 出液口重合，不调用 protected helper"),
        Audio->GetComponentLocation().Equals(Bottle->PourFX->GetComponentLocation(), Tolerance));
    Bottle->SetActorLocation(FVector(10, 15, 100));
    F.TickN();
    TestTrue(TEXT("移动水瓶后声音跟随实际 PourFX 口沿"),
        Audio->GetComponentLocation().Equals(Bottle->PourFX->GetComponentLocation(), Tolerance));
    Bottle->OnCapAttached();
    TestFalse(TEXT("正流中封盖立即 StopPouring 撤销请求"), Bottle->IsPourAudioRequested());
    TestFalse(TEXT("正流中封盖立即停止液流状态"), Bottle->bIsPouring);
    // 封盖是正常 FadeOut，不要求此刻 IsPlaying=false。
    const double RecappedML = Bottle->GetPreciseVolumeML();
    F.TickN();
    Near(*this, TEXT("重新封盖不继续扣量"), Bottle->GetPreciseVolumeML(), RecappedML);
    Bottle->OnCapDetached();
    F.TickN();
    if (!CheckPlaying(*this, Bottle, Sound)) return false;
    TestTrue(TEXT("再开盖不创建第二个组件"), Bottle->GetPourAudioComponent() == Audio);
    TestEqual(TEXT("真实二次开流启动次数为2"), Bottle->AudioStartCount, 2);
    Bottle->FreezeLiquidForServing();
    CheckImmediateStop(*this, Bottle);
    CheckBalance(*this, F);
    TestTrue(TEXT("保存 CDO 默认盖子仍存在且未被夹具改变"), CDO->CapClass.Get() != nullptr && CDO->bStartCapped);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioCupRealEmit,
    "CyberCafe.PourAudio.AudioCupRealEmit", CyberCafePourAudioTests::Options)
bool FPourAudioCupRealEmit::RunTest(const FString&)
{
    auto* Sound = LoadSoundWave(*this);
    auto* Class = LoadClass<ACupActor>(nullptr, CupPath);
    FWorld F(*this, true);
    if (!Sound || !F.bReady) return false;
    auto* Cup = F.Spawn<ACupActor>(Class, true, 0.f);
    if (!Cup || !F.Begin(Cup)) return false;
    TestFalse(TEXT("真实 Highball 未冻结"), Cup->IsLiquidFrozen());
    Cup->SetActorRotation(FRotator(110, 0, 0));
    F.TickN();
    Near(*this, TEXT("空 Highball 库存为零"), Cup->GetPreciseVolumeML(), 0);
    TestNull(TEXT("空杯倾倒不创建声音"), Cup->GetPourAudioComponent());
    TestFalse(TEXT("空杯不请求声音"), Cup->IsPourAudioRequested());
    Near(*this, TEXT("公开 API 加入真实 100mL Coffee"), Cup->AddIngredientLiquid(100, TEXT("Coffee"), FLinearColor::White), 100);
    Near(*this, TEXT("公开 API 加入真实 100mL Milk"), Cup->AddIngredientLiquid(100, TEXT("Milk"), FLinearColor::White), 100);
    Near(*this, TEXT("杯中配比总量 200mL"), Cup->GetPreciseVolumeML(), 200);
    const double Before = Cup->GetPreciseVolumeML();
    F.TickN();
    if (!CheckPlaying(*this, Cup, Sound)) return false;
    const double Actual = Before - Cup->GetPreciseVolumeML();
    TestTrue(TEXT("真实杯基类路径产生正实际出流"), Actual > 0);
    Near(*this, TEXT("杯音频速率使用真实 Actual/Dt"), Cup->PourAudioActualFlow, Actual / Dt);
    TestTrue(TEXT("杯与壶瓶共用原 Loop，未按原液制造新声音"), Cup->GetPourAudioComponent()->Sound.Get() == Sound);
    TestTrue(TEXT("杯声音在实际动态口沿 PourFX"),
        Cup->GetPourAudioComponent()->GetComponentLocation().Equals(Cup->PourFX->GetComponentLocation(), Tolerance));
    Near(*this, TEXT("Coffee 跟随真实发量按比例减少"), Cup->GetIngredientVolumeML(TEXT("Coffee")), 100 - Actual / 2);
    Near(*this, TEXT("Milk 跟随真实发量按比例减少"), Cup->GetIngredientVolumeML(TEXT("Milk")), 100 - Actual / 2);
    Near(*this, TEXT("杯配方加入量完整记录到 Injected"), F.Transfer()->GetBalance().InjectedML, 200);
    CheckBalance(*this, F);
    Cup->FreezeLiquidForServing();
    CheckImmediateStop(*this, Cup);
    const double FrozenML = Cup->GetPreciseVolumeML();
    F.TickN();
    Near(*this, TEXT("冻结杯不继续发量"), Cup->GetPreciseVolumeML(), FrozenML);
    CheckBalance(*this, F);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioStreamDisableNoSoundZeroRateAndDestroyedClean,
    "CyberCafe.PourAudio.StreamDisableNoSoundZeroRateAndDestroyedClean", CyberCafePourAudioTests::Options)
bool FPourAudioStreamDisableNoSoundZeroRateAndDestroyedClean::RunTest(const FString&)
{
    auto* Sound = LoadSoundWave(*this);
    auto* Class = LoadClass<ABottleActor>(nullptr, BottlePath);
    if (!Sound || !TestNotNull(TEXT("已保存水瓶类存在"), Class)) return false;
    for (int32 Case = 0; Case < 2; ++Case)
    {
        // 禁用/销毁与破碎分别创建 World，避免重复来源的体积账互相抵消。
        FWorld F(*this, true);
        if (!F.bReady) return false;
        auto* Bottle = F.Spawn<ABottleActor>(Class);
        if (!Bottle || !F.Begin(Bottle)) return false;
        Bottle->PourRatePerSecond = 40.f;
        Bottle->SetActorRotation(FRotator(110, 0, 0));
        Bottle->Tick(Dt);
        if (!CheckPlaying(*this, Bottle, Sound)) return false;
        auto* Audio = Bottle->GetPourAudioComponent();
        if (Case == 1)
        {
            TestTrue(TEXT("在真实正流中 BreakContainerForImpact 成功"), Bottle->BreakContainerForImpact());
            TestTrue(TEXT("破碎标志真实为 true"), Bottle->IsBroken());
            CheckImmediateStop(*this, Bottle);
            Near(*this, TEXT("破碎容器剩余液体真实结算为零"), Bottle->GetPreciseVolumeML(), 0);
            Bottle->OnCapDetached();
            Bottle->OnCapAttached();
            Bottle->Tick(Dt);
            TestFalse(TEXT("破碎后瓶盖回调不能重新允许倒液"), Bottle->bCanPour);
            CheckImmediateStop(*this, Bottle);
            CheckBalance(*this, F);
            continue;
        }
        const double EnabledDebit = 40.0 * Dt;
        Bottle->bEnablePourAudio = false;
        double Before = Bottle->GetPreciseVolumeML();
        Bottle->Tick(Dt);
        Near(*this, TEXT("禁用声音仍真实发同量"), Before - Bottle->GetPreciseVolumeML(), EnabledDebit);
        CheckImmediateStop(*this, Bottle);
        TestEqual(TEXT("禁用不会额外启动"), Bottle->AudioStartCount, 1);
        Bottle->bEnablePourAudio = true;
        Bottle->Tick(Dt);
        if (!CheckPlaying(*this, Bottle, Sound)) return false;
        TestEqual(TEXT("重新启用仅启动第2次"), Bottle->AudioStartCount, 2);
        TestTrue(TEXT("启用复用原组件"), Bottle->GetPourAudioComponent() == Audio);
        USoundBase* SavedSound = Bottle->PourLoopSound.Get();
        Bottle->PourLoopSound = nullptr;
        Before = Bottle->GetPreciseVolumeML();
        Bottle->Tick(Dt);
        Near(*this, TEXT("声音未配置仍真实发同量"), Before - Bottle->GetPreciseVolumeML(), EnabledDebit);
        CheckImmediateStop(*this, Bottle);
        Bottle->PourLoopSound = SavedSound;
        Bottle->Tick(Dt);
        if (!CheckPlaying(*this, Bottle, Sound)) return false;
        TestEqual(TEXT("恢复声音启动第3次"), Bottle->AudioStartCount, 3);
        Bottle->PourRatePerSecond = 0.f;
        Before = Bottle->GetPreciseVolumeML();
        Bottle->Tick(Dt);
        TestTrue(TEXT("零 rate 允许液流视觉状态仍 true"), Bottle->bIsPouring);
        TestFalse(TEXT("零 Actual 立即撤销音频请求"), Bottle->IsPourAudioRequested());
        Near(*this, TEXT("零 rate 不扣量"), Bottle->GetPreciseVolumeML(), Before);
        Near(*this, TEXT("零 rate 音频实际速率归零"), Bottle->PourAudioActualFlow, 0);
        Bottle->PourRatePerSecond = 40.f;
        Bottle->Tick(Dt);
        if (!CheckPlaying(*this, Bottle, Sound)) return false;
        TestEqual(TEXT("零 rate 后恢复启动第4次"), Bottle->AudioStartCount, 4);
        TWeakObjectPtr<UAudioComponent> WeakAudio(Audio);
        TestTrue(TEXT("真实正流 Actor Destroy 成功"), Bottle->Destroy());
        TestTrue(TEXT("Destroyed/EndPlay 后音频失效或立即停止"), !WeakAudio.IsValid() || !WeakAudio->IsPlaying());
        CheckBalance(*this, F);
        F.Transfer()->Simulate(F.Transfer()->GetUnsettledFlightHorizon() + .05);
        Near(*this, TEXT("销毁后的尾包仍真实结清，声音不参与结算"), F.Transfer()->GetBalance().InFlightML, 0);
        CheckBalance(*this, F);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioTinyRemainderActualAudioRateNotNominal,
    "CyberCafe.PourAudio.TinyRemainderActualAudioRateNotNominal", CyberCafePourAudioTests::Options)
bool FPourAudioTinyRemainderActualAudioRateNotNominal::RunTest(const FString&)
{
    auto* Sound = LoadSoundWave(*this);
    auto* BottleClass = LoadClass<ABottleActor>(nullptr, BottlePath);
    auto* KettleClass = LoadClass<AHandPourKettleActor>(nullptr, KettlePath);
    if (!Sound) return false;
    {
        FWorld F(*this, false);
        if (!F.bReady) return false;
        auto* Bottle = F.Spawn<ABottleActor>(BottleClass, true, 0.f);
        if (!Bottle) return false;
        Bottle->MaxVolumeML = 750.f;
        if (!F.Begin(Bottle)) return false;
        Bottle->PourRatePerSecond = 40.f;
        Near(*this, TEXT("公开 API 注入水瓶 .1mL Water"), Bottle->AddIngredientLiquid(.1f, TEXT("Water"), FLinearColor::White), .1);
        const double Tail = Bottle->GetPreciseVolumeML();
        // 这是公开 EmitPour 交易 API 的返回值测试，不把它说成 Actor Tick 会发声音。
        const double Actual = F.Transfer()->EmitPour(Bottle, Dt, Bottle->PourFX->GetComponentTransform());
        Near(*this, TEXT("generic EmitPour double 返回全部实际 .1，而不是名义 .8"), Actual, Tail);
        TestTrue(TEXT("实际返回量小于名义需求"), Actual > 0 && Actual < 40.0 * Dt);
        Near(*this, TEXT("水瓶余量完整实际扣清"), Bottle->GetPreciseVolumeML(), 0);
        Near(*this, TEXT(".1mL 实际进入在途"), F.Transfer()->GetBalance().InFlightML, Tail);
        TestNull(TEXT("EmitPour API 本身不分配音频"), Bottle->GetPourAudioComponent());
        TestEqual(TEXT("EmitPour API 本身不启动音频"), Bottle->AudioStartCount, 0);
        CheckBalance(*this, F);
        Bottle->SetActorRotation(FRotator(110, 0, 0));
        Bottle->Tick(Dt);
        TestFalse(TEXT("下一空瓶 Actor 帧不请求声音"), Bottle->IsPourAudioRequested());
    }
    {
        FWorld F(*this, true);
        if (!F.bReady) return false;
        auto* B = F.Spawn<ABottleActor>(BottleClass, true, 0.f);
        if (!B || !F.Begin(B)) return false;
        B->PourRatePerSecond=40.f;
        B->AddIngredientLiquid(.1f,TEXT("Water"),FLinearColor::White);
        B->SetActorRotation(FRotator(110,0,0)); B->Tick(Dt);
        if (!CheckPlaying(*this,B,Sound))return false;
        Near(*this,TEXT("generic Actor尾量全部实际发出"),B->GetPreciseVolumeML(),0);
        Near(*this,TEXT("generic Actor尾量声音用5mL/s"),B->PourAudioActualFlow,.1/Dt);
        TestFalse(TEXT("generic最后帧VFX状态仍按原规则停止"),B->bIsPouring);
        TestEqual(TEXT("generic尾量首次只发起一次声音"),B->AudioStartCount,1);
        B->bCanPour=false; // Disabling logic after the final packet must not strand a looping voice.
        B->Tick(Dt);
        TestFalse(TEXT("generic下一空帧即使禁倒仍撤销音频"),B->IsPourAudioRequested());
        TestEqual(TEXT("空帧不会重新起声"),B->AudioStartCount,1);
        CheckBalance(*this,F);
    }
    {
        FWorld F(*this, true);
        if (!F.bReady) return false;
        auto* K = F.Spawn<AHandPourKettleActor>(KettleClass, true, 0.f);
        if (!K || !F.Begin(K)) return false;
        Near(*this, TEXT("公开 API 注入壶 .1mL Water"), K->AddIngredientLiquid(.1f, TEXT("Water"), FLinearColor::White), .1);
        K->SetActorRotation(FRotator(-85, 0, 0));
        const double Tail = K->GetPreciseVolumeML();
        const float Actual = K->PourWater(Dt);
        Near(*this, TEXT("专用 PourWater 实际返回 .1，不是名义 .56"), Actual, Tail);
        if (!CheckPlaying(*this, K, Sound)) return false;
        Near(*this, TEXT("空壶前最后一帧库存扣清"), K->GetPreciseVolumeML(), 0);
        TestTrue(TEXT("实际最后一帧仍保留 bIsPouring 和音频请求"), K->bIsPouring && K->IsPourAudioRequested());
        const double ActualRate = Tail / Dt;
        Near(*this, TEXT("专用壶当前实际流量为约5而非28"), K->CurrentFlowMLPerSecond, ActualRate);
        Near(*this, TEXT("音频诊断实际流量为约5而非28"), K->PourAudioActualFlow, ActualRate);
        auto* Audio = K->GetPourAudioComponent();
        Near(*this, TEXT("余量音量按 sqrt(.1/.02/40)，非名义最大流量"), Audio->VolumeMultiplier,
            K->PourAudioVolume * FMath::Sqrt(static_cast<float>(ActualRate / 40.0)));
        Near(*this, TEXT("余量音高也使用实际5mL/s"), Audio->PitchMultiplier, .94 + .1 * ActualRate / 40.0);
        TestTrue(TEXT("余量声音仍在真实壶嘴"), Audio->GetComponentLocation().Equals(K->GetKettleSpoutWorldTransform().GetLocation(), Tolerance));
        Near(*this, TEXT("余量账本在途量完整"), F.Transfer()->GetBalance().InFlightML, Tail);
        CheckBalance(*this, F);
        K->Tick(Dt);
        TestFalse(TEXT("下一空壶帧停止音频请求"), K->IsPourAudioRequested());
        TestFalse(TEXT("下一空壶帧停止出液状态"), K->bIsPouring);
        Near(*this, TEXT("下一空壶帧实际流量为零"), K->PourAudioActualFlow, 0);
        TestEqual(TEXT("最后一帧只启动一次，没有空帧重启"), K->AudioStartCount, 1);
        CheckBalance(*this, F);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioKettleBrokenAndEmptySafety,
    "CyberCafe.PourAudio.KettleBrokenAndEmptySafety", CyberCafePourAudioTests::Options)
bool FPourAudioKettleBrokenAndEmptySafety::RunTest(const FString&)
{
    auto* Sound = LoadSoundWave(*this);
    auto* Class = LoadClass<AHandPourKettleActor>(nullptr, KettlePath);
    if (!Sound || !TestNotNull(TEXT("已保存壶类存在"), Class)) return false;
    for (int32 Case = 0; Case < 2; ++Case)
    {
        FWorld F(*this, true);
        if (!F.bReady) return false;
        auto* K = F.Spawn<AHandPourKettleActor>(Class, true, Case == 0 ? 1.f : 0.f);
        if (!K || !F.Begin(K)) return false;
        TestNull(TEXT("壶初始无盖子实例"), K->CapActor.Get());
        TestFalse(TEXT("壶 open fixture 未封盖"), K->bIsCapped);
        K->SetActorRotation(FRotator(-85, 0, 0));
        if (Case == 1)
        {
            F.TickN();
            Near(*this, TEXT("空壶正倾不发生真实流量"), K->CurrentFlowMLPerSecond, 0);
            TestNull(TEXT("空壶正倾不分配音频组件"), K->GetPourAudioComponent());
            TestEqual(TEXT("空壶正倾启动计数为零"), K->AudioStartCount, 0);
            TestFalse(TEXT("空壶正倾不请求声音"), K->IsPourAudioRequested());
            Near(*this, TEXT("公开注入100mL后才允许出流"), K->AddIngredientLiquid(100, TEXT("Water"), FLinearColor::White), 100);
        }
        F.TickN();
        if (!CheckPlaying(*this, K, Sound)) return false;
        const double Remaining = K->GetPreciseVolumeML();
        if (Case == 0)
        {
            TestTrue(TEXT("专用壶正流中真实破碎成功"), K->BreakContainerForImpact());
            TestTrue(TEXT("专用壶真实标记破碎"), K->IsBroken());
            CheckImmediateStop(*this, K);
            Near(*this, TEXT("破碎只结算真实剩余体积"), F.Transfer()->GetBalance().SpilledML, Remaining);
            K->OnCapAttached();
            K->OnCapDetached();
            F.TickN();
            CheckImmediateStop(*this, K);
            TestFalse(TEXT("破碎壶与盖回调不能重新出流"), K->bIsPouring);
            TestEqual(TEXT("破碎后的壶不重启循环"), K->AudioStartCount, 1);
        }
        else
        {
            Near(*this, TEXT("公开 ConsumeLiquid 移除真实剩余水"), K->ConsumeLiquid(500.f), Remaining);
            F.TickN();
            TestFalse(TEXT("库存耗尽的下一帧撤销声音请求"), K->IsPourAudioRequested());
            TestFalse(TEXT("库存耗尽的下一帧停止液流状态"), K->bIsPouring);
            Near(*this, TEXT("库存耗尽的下一帧音频实际速率归零"), K->PourAudioActualFlow, 0);
            TestEqual(TEXT("空壶不因尾音或 IsPlaying 状态再次启动"), K->AudioStartCount, 1);
            // 耗尽按正常 FadeOut 收尾，不用未推进的设备时钟声称声音尾部已渲染结束。
        }
        CheckBalance(*this, F);
    }
    return !HasAnyErrors();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioKettleReentrantStop,
    "CyberCafe.PourAudio.KettleCallbackClosureCannotRestartStreamAudio", CyberCafePourAudioTests::Options)
bool FPourAudioKettleReentrantStop::RunTest(const FString&)
{
    using namespace CyberCafePourAudioTests;
    auto* Sound=LoadSoundWave(*this);auto* Class=LoadClass<AHandPourKettleActor>(nullptr,KettlePath);
    if(!Sound)return false;
    FWorld F(*this,true);if(!F.bReady)return false;
    auto* K=F.Spawn<AHandPourKettleActor>(Class);if(!K||!F.Begin(K))return false;
    K->SetActorRotation(FRotator(-85,0,0));F.TickN();if(!CheckPlaying(*this,K,Sound))return false;
    auto* Probe=NewObject<UPourAudioTestProbe>(K);Probe->Kettle=K;
    K->OnLiquidChanged.AddDynamic(Probe,&UPourAudioTestProbe::CloseDuringEmission);
    const double Before=K->GetPreciseVolumeML();const float Emitted=K->PourWater(.08f);
    TestEqual(TEXT("回调只发生在首个已提交片"),Probe->Notifications,1);
    TestTrue(TEXT("实际首片仍记账但后续片停止"),Emitted>0&&Emitted<28*.08);
    Near(*this,TEXT("取消不撤销已提交实际液量"),Before-K->GetPreciseVolumeML(),Emitted);
    TestTrue(TEXT("关闭回调保持封口状态"),K->bIsCapped);
    TestFalse(TEXT("同一PourWater不能恢复水柱"),K->bIsPouring);
    TestFalse(TEXT("同一PourWater不能恢复声音"),K->IsPourAudioRequested());
    TestEqual(TEXT("取消后没有多一次启动"),K->AudioStartCount,1);
    const double ClosedVolume=K->GetPreciseVolumeML();F.TickN(10);
    Near(*this,TEXT("关闭后帧不再扣量"),K->GetPreciseVolumeML(),ClosedVolume);
    K->OnLiquidChanged.RemoveDynamic(Probe,&UPourAudioTestProbe::CloseDuringEmission);
    K->OnCapDetached();F.TickN();if(!CheckPlaying(*this,K,Sound))return false;
    TestEqual(TEXT("真实重开后允许再次起声"),K->AudioStartCount,2);
    CheckBalance(*this,F);return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPourAudioRestore,
    "CyberCafe.PourAudio.RestoreStopsOldVoiceAndReplacementRemainsSilent", CyberCafePourAudioTests::Options)
bool FPourAudioRestore::RunTest(const FString&)
{
    using namespace CyberCafePourAudioTests;
    auto* Sound=LoadSoundWave(*this);auto* Class=LoadClass<ACupActor>(nullptr,CupPath);if(!Sound)return false;
    FWorld F(*this,true);if(!F.bReady)return false;
    auto* Cup=F.Spawn<ACupActor>(Class,true,0.f);if(!Cup)return false;
    auto* Restore=F.World->SpawnActor<AGlassRecoveryActor>();Restore->bUnifiedRestore=true;Restore->RestoreTargets={Cup};Restore->PrototypeCup=Cup;
    if(!F.Begin(Cup))return false;
    Cup->AddIngredientLiquid(100,TEXT("Water"),FLinearColor::White);Cup->SetActorRotation(FRotator(110,0,0));F.TickN();if(!CheckPlaying(*this,Cup,Sound))return false;
    TWeakObjectPtr<UAudioComponent> OldAudio= Cup->GetPourAudioComponent();
    if(!TestTrue(TEXT("显式单杯恢复事务成功"),Restore->RestoreWorkbench()))return false;
    TestTrue(TEXT("旧杯声音在恢复销毁时立即结束"),!OldAudio.IsValid()||!OldAudio->IsPlaying());
    auto* NewCup=Cast<ACupActor>(Restore->PrototypeCup.Get());if(!TestNotNull(TEXT("恢复发布新杯"),NewCup))return false;
    TestTrue(TEXT("新杯仍原类但不是旧身份"),NewCup!=Cup&&NewCup->GetClass()==Class);
    TestTrue(TEXT("新杯继承同循环且可继续使用"),NewCup->PourLoopSound.Get()==Sound&&NewCup->bEnablePourAudio);
    TestNull(TEXT("空替换杯没有残留旧声组件"),NewCup->GetPourAudioComponent());
    TestFalse(TEXT("恢复不自动起声"),NewCup->IsPourAudioRequested());
    Near(*this,TEXT("恢复新杯仍空"),NewCup->GetPreciseVolumeML(),0);CheckBalance(*this,F);
    return !HasAnyErrors();
}
#endif
