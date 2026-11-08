// 验证保存配置、真实 BeginPlay 与非实时设备上的播放请求，不代表已渲染音频或头显可听。
// 音频用例须启用 -AudioMixer 且不带 -nosound；设备创建失败明确失败，不跳过。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "CafeSceneMusicComponent.h"
#include "Tutorial/TutorialDirector.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "AudioDeviceManager.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include <limits>

namespace CyberCafeSceneMusicTests
{
constexpr auto Options = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double ValueTolerance = 1.e-5;
const TCHAR* MusicAssetPath = TEXT("/Game/VRTemplate/Audio/Music/S_WarmColdContrast_Remastered.S_WarmColdContrast_Remastered");
const TCHAR* CafeMapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");

bool CheckNear(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected,
    double Tolerance = ValueTolerance)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}

USoundWave* LoadMusicAsset(FAutomationTestBase& Test)
{
    USoundWave* Wave = LoadObject<USoundWave>(nullptr, MusicAssetPath);
    if (!Test.TestNotNull(TEXT("精确路径的已保存音乐 SoundWave 可加载"), Wave)) return nullptr;
    if (!Test.TestEqual(TEXT("音乐资产路径完全一致"), Wave->GetPathName(), FString(MusicAssetPath))) return nullptr;
    return Wave;
}

void CheckAudioComponentCount(FAutomationTestBase& Test, const ATutorialDirector* Director,
    int32 Expected, const FString& Label)
{
    TArray<UAudioComponent*> AudioComponents;
    Director->GetComponents(AudioComponents);
    Test.TestEqual(Label + TEXT("：Owner 的真实音频组件数量"), AudioComponents.Num(), Expected);
}

UCafeSceneMusicComponent* FindSceneMusic(FAutomationTestBase& Test, const ATutorialDirector* Director)
{
    TArray<UCafeSceneMusicComponent*> Components;
    Director->GetComponents(Components);
    if (!Test.TestEqual(TEXT("Director 恰有一个原生 SceneMusic 组件"), Components.Num(), 1)) return nullptr;
    UCafeSceneMusicComponent* SceneMusic = Components[0];
    if (!Test.TestNotNull(TEXT("SceneMusic 组件存在"), SceneMusic)) return nullptr;
    Test.TestEqual(TEXT("沿用 constructor 的 SceneMusic 子对象名称"), SceneMusic->GetFName(), FName(TEXT("SceneMusic")));
    Test.TestTrue(TEXT("SceneMusic 是 Director 的默认子对象"), SceneMusic->HasAnyFlags(RF_DefaultSubObject));
    Test.TestTrue(TEXT("SceneMusic 归属原 Director，没有另建音乐 Actor"), SceneMusic->GetOwner() == Director);
    Test.TestFalse(TEXT("SceneMusic 不支持 Tick"), SceneMusic->PrimaryComponentTick.bCanEverTick);
    Test.TestFalse(TEXT("SceneMusic 未启用组件 Tick"), SceneMusic->IsComponentTickEnabled());
    return SceneMusic;
}

void CheckSilent(FAutomationTestBase& Test, const ATutorialDirector* Director,
    const UCafeSceneMusicComponent* SceneMusic, const FString& Label)
{
    Test.TestNull(Label + TEXT("：未分配音乐音频组件"), SceneMusic->GetMusicAudioComponent());
    Test.TestFalse(Label + TEXT("：没有播放请求"), SceneMusic->IsMusicRequested());
    Test.TestEqual(Label + TEXT("：启动次数为零"), SceneMusic->GetMusicStartCount(), 0);
    CheckAudioComponentCount(Test, Director, 0, Label);
}

// 每个夹具独立持有 Game World、GI 和 Unique 非实时设备，不借用主设备，不模拟音频接口。
struct FSceneMusicTestWorld
{
    FAutomationTestBase& Test;
    UWorld* TestWorld = nullptr;
    UGameInstance* GameInstance = nullptr;
    FAudioDeviceHandle DeviceHandle;
    bool bReady = false;

    explicit FSceneMusicTestWorld(FAutomationTestBase& InTest, bool bWithAudio) : Test(InTest)
    {
        if (!Test.TestNotNull(TEXT("引擎上下文存在"), GEngine)) return;
        UWorld::InitializationValues InitValues;
        InitValues.AllowAudioPlayback(bWithAudio).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        TestWorld = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
            true, ERHIFeatureLevel::SM5, &InitValues);
        if (!Test.TestNotNull(TEXT("创建独立真实 Game World"), TestWorld)) return;
        FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
        WorldContext.SetCurrentWorld(TestWorld);
        GameInstance = NewObject<UGameInstance>(GEngine);
        WorldContext.OwningGameInstance = GameInstance;
        TestWorld->SetGameInstance(GameInstance);
        GameInstance->Init();
        if (!Test.TestTrue(TEXT("夹具属于 Game World"), TestWorld->IsGameWorld())) return;
        if (bWithAudio)
        {
            FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get();
            if (!Test.TestNotNull(TEXT("设备管理器存在；须无 -nosound 且启用 -AudioMixer"), DeviceManager)) return;
            FAudioDeviceParams DeviceParams;
            DeviceParams.Scope = EAudioDeviceScope::Unique;
            DeviceParams.bIsNonRealtime = true;
            DeviceParams.AssociatedWorld = TestWorld;
            DeviceHandle = DeviceManager->RequestAudioDevice(DeviceParams);
            if (!Test.TestNotNull(TEXT("独立音频设备创建必须成功，失败不可 skip"), DeviceHandle.GetAudioDevice())) return;
            if (!Test.TestTrue(TEXT("设备确为非实时，不向头显输出"), DeviceHandle->IsNonRealtime())) return;
            TestWorld->SetAudioDevice(DeviceHandle);
            if (!Test.TestTrue(TEXT("World 绑定本夹具设备"), TestWorld->GetAudioDeviceRaw() == DeviceHandle.GetAudioDevice())) return;
            if (!Test.TestTrue(TEXT("真实音频 World 允许音频播放"), TestWorld->AllowAudioPlayback())) return;
        }
        else
        {
            if (!Test.TestFalse(TEXT("InitializationValues 禁止无音频 World 播放"), TestWorld->AllowAudioPlayback())) return;
            if (!Test.TestFalse(TEXT("无音频夹具未请求独立设备"), DeviceHandle.IsValid())) return;
        }
        bReady = true;
    }

    ~FSceneMusicTestWorld()
    {
        if (!TestWorld) return;
        if (TestWorld->HasBegunPlay()) TestWorld->EndPlay(EEndPlayReason::Quit);
        if (GameInstance) GameInstance->Shutdown();
        TestWorld->SetAudioDevice(FAudioDeviceHandle());
        DeviceHandle.Reset();
        GEngine->DestroyWorldContext(TestWorld);
        TestWorld->DestroyWorld(false);
    }

    FSceneMusicTestWorld(const FSceneMusicTestWorld&) = delete;
    FSceneMusicTestWorld& operator=(const FSceneMusicTestWorld&) = delete;

    ATutorialDirector* SpawnDirector(USoundWave* ConfiguredMusic)
    {
        if (!bReady) return nullptr;
        const FTransform SpawnTransform = FTransform::Identity;
        ATutorialDirector* Director = TestWorld->SpawnActorDeferred<ATutorialDirector>(
            ATutorialDirector::StaticClass(), SpawnTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!Test.TestNotNull(TEXT("真实 deferred Director 生成成功"), Director)) return nullptr;
        UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(Test, Director);
        if (!SceneMusic) return nullptr;
        Test.TestNull(TEXT("新 Director 未继承地图实例的 Music 配置"), SceneMusic->Music.Get());
        Director->bAutoStart = false;
        SceneMusic->Music = ConfiguredMusic;
        Director->FinishSpawning(SpawnTransform);
        if (!Test.TestFalse(TEXT("FinishSpawning 后尚未 Actor BeginPlay"), Director->HasActorBegunPlay())) return nullptr;
        if (!Test.TestFalse(TEXT("FinishSpawning 后尚未组件 BeginPlay"), SceneMusic->HasBegunPlay())) return nullptr;
        Test.TestFalse(TEXT("夹具确实关闭教学自动开始"), Director->bAutoStart);
        Test.TestTrue(TEXT("音乐保留独立 bAutoPlay=true"), SceneMusic->bAutoPlay);
        Test.TestTrue(TEXT("配置只写入夹具实例"), SceneMusic->Music.Get() == ConfiguredMusic);
        CheckSilent(Test, Director, SceneMusic, TEXT("真实 BeginPlay 前"));
        return Director;
    }

    bool Begin(ATutorialDirector* Director)
    {
        TestWorld->InitializeActorsForPlay(FURL());
        AWorldSettings* Settings = TestWorld->GetWorldSettings();
        if (!Test.TestNotNull(TEXT("真实 WorldSettings 存在"), Settings)) return false;
        Settings->NotifyBeginPlay();
        UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(Test, Director);
        return SceneMusic
            && Test.TestTrue(TEXT("NotifyBeginPlay 驱动真实 World BeginPlay"), TestWorld->HasBegunPlay())
            && Test.TestTrue(TEXT("NotifyBeginPlay 驱动真实 Director BeginPlay"), Director->HasActorBegunPlay())
            && Test.TestTrue(TEXT("NotifyBeginPlay 驱动真实 SceneMusic BeginPlay"), SceneMusic->HasBegunPlay());
    }
};

UAudioComponent* CheckPlaying(FAutomationTestBase& Test, const ATutorialDirector* Director,
    const UCafeSceneMusicComponent* SceneMusic, USoundWave* Wave, int32 ExpectedStartCount = 1)
{
    UAudioComponent* MusicAudio = SceneMusic->GetMusicAudioComponent();
    if (!Test.TestNotNull(TEXT("音乐真实分配了 AudioComponent"), MusicAudio)) return nullptr;
    Test.TestTrue(TEXT("真实音频组件 IsPlaying，不等同于可听"), MusicAudio->IsPlaying());
    Test.TestTrue(TEXT("真实音频组件处于 Active 状态"), MusicAudio->IsActive());
    Test.TestTrue(TEXT("SceneMusic 自有播放请求为 true"), SceneMusic->IsMusicRequested());
    Test.TestEqual(TEXT("SceneMusic 自有启动计数"), SceneMusic->GetMusicStartCount(), ExpectedStartCount);
    Test.TestTrue(TEXT("实际播放声音是保存的 BGM"), MusicAudio->Sound.Get() == Wave);
    Test.TestTrue(TEXT("实际 AudioComponent 归属该 Director"), MusicAudio->GetOwner() == Director);
    Test.TestTrue(TEXT("实际 AudioComponent 已注册到该真实 World"),
        MusicAudio->IsRegistered() && MusicAudio->GetWorld() == Director->GetWorld());
    Test.TestFalse(TEXT("实际声音不允许空间化"), MusicAudio->bAllowSpatialization != 0);
    Test.TestTrue(TEXT("实际声音覆盖衰减配置"), MusicAudio->bOverrideAttenuation != 0);
    Test.TestFalse(TEXT("覆盖配置不衰减"), MusicAudio->AttenuationOverrides.bAttenuate != 0);
    Test.TestFalse(TEXT("覆盖配置不空间化"), MusicAudio->AttenuationOverrides.bSpatialize != 0);
    Test.TestFalse(TEXT("不是 UI Sound"), MusicAudio->bIsUISound != 0);
    Test.TestFalse(TEXT("禁止同一组件多实例播放"), MusicAudio->bCanPlayMultipleInstances != 0);
    Test.TestFalse(TEXT("音频组件不会自动激活"), MusicAudio->bAutoActivate != 0);
    Test.TestFalse(TEXT("停止后不自动销毁，允许复用"), MusicAudio->bAutoDestroy != 0);
    CheckNear(Test, TEXT("实际 PitchMultiplier 恒为 1"), MusicAudio->PitchMultiplier, 1.0);
    CheckNear(Test, TEXT("实际 VolumeMultiplier 为单份 .22 增益"), MusicAudio->VolumeMultiplier, .22);
    CheckAudioComponentCount(Test, Director, 1, TEXT("播放中没有重复音频组件"));
    return Test.HasAnyErrors() ? nullptr : MusicAudio;
}

// 只在音频线程读真实 ActiveSound，Fence 完成后才在主线程断言，不伪造或推进设备时钟。
void CheckActiveGain(FAutomationTestBase& Test, FAudioDevice* Device,
    const UAudioComponent* MusicAudio, float ExpectedGain)
{
    struct FRequestSnapshot
    {
        int32 Count = 0;
        float Gain = 0.f;
        float Pitch = 0.f;
        float FadeTarget = 0.f;
        float FadeDuration = 0.f;
    } Snapshot;
    Device->SendCommandToActiveSounds(MusicAudio->GetAudioComponentID(), [&Snapshot](FActiveSound& ActiveSound)
    {
        ++Snapshot.Count;
        Snapshot.Gain = ActiveSound.VolumeMultiplier;
        Snapshot.Pitch = ActiveSound.PitchMultiplier;
        Snapshot.FadeTarget = ActiveSound.ComponentVolumeFader.GetTargetVolume();
        Snapshot.FadeDuration = ActiveSound.ComponentVolumeFader.GetFadeDuration();
    });
    FAudioCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait();
    Test.TestEqual(TEXT("设备上该组件恰有一个真实 ActiveSound 请求"), Snapshot.Count, 1);
    CheckNear(Test, TEXT("真实 ActiveSound 音高恒为 1"), Snapshot.Pitch, 1.0);
    CheckNear(Test, TEXT("真实 ActiveSound 仅使用单份增益"), Snapshot.Gain, ExpectedGain);
    CheckNear(Test, TEXT("真实 FadeIn 淡到 unity，不再乘一次 .22"), Snapshot.FadeTarget, 1.0);
    CheckNear(Test, TEXT("真实 FadeIn 请求时长为 1 秒"), Snapshot.FadeDuration, 1.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeSceneMusicSavedMusicSettings,
    "CyberCafe.SceneMusic.SavedMusicSettings", CyberCafeSceneMusicTests::Options)
bool FCafeSceneMusicSavedMusicSettings::RunTest(const FString&)
{
    USoundWave* Wave = LoadMusicAsset(*this);
    if (!Wave) return false;
    const bool bInitiallyDirty = Wave->GetOutermost()->IsDirty();
    TestTrue(TEXT("保存的自制音乐已启用循环"), Wave->bLooping != 0);
    TestEqual(TEXT("保存波形为 stereo 双声道"), Wave->NumChannels, 2);
    TestEqual(TEXT("真实导入采样率为 48000 Hz"), Wave->GetImportedSampleRate(), uint32(48000));
    // 循环 GetDuration() 可能返回无限时长；原始导入时长必须读取 Duration。
    CheckNear(*this, TEXT("原始波形时长为 144.92 秒"), Wave->Duration, 144.92, .01);
    TestTrue(TEXT("真实 getter 的加载行为为 PrimeOnLoad"),
        Wave->GetLoadingBehavior() == ESoundWaveLoadingBehavior::PrimeOnLoad);
    TestTrue(TEXT("保存波形使用 Music SoundGroup"), Wave->SoundGroup == SOUNDGROUP_Music);
    TestEqual(TEXT("只读断言不改变音乐资产脏标记"), Wave->GetOutermost()->IsDirty(), bInitiallyDirty);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeSceneMusicSavedMapSingleComponent,
    "CyberCafe.SceneMusic.SavedMapSingleComponent", CyberCafeSceneMusicTests::Options)
bool FCafeSceneMusicSavedMapSingleComponent::RunTest(const FString&)
{
    UWorld* SavedWorld = LoadObject<UWorld>(nullptr, CafeMapPath);
    if (!TestNotNull(TEXT("只读加载原保存 L_Cafe"), SavedWorld)
        || !TestNotNull(TEXT("原地图 PersistentLevel 存在"), SavedWorld->PersistentLevel.Get())) return false;
    if (!TestFalse(TEXT("保存地图未 BeginPlay"), SavedWorld->HasBegunPlay())) return false;
    const bool bInitiallyDirty = SavedWorld->GetOutermost()->IsDirty();
    USoundWave* Wave = LoadMusicAsset(*this);
    if (!Wave) return false;
    TArray<AActor*> Directors;
    // 保存世界尚未初始化，不能用默认 OnlyActiveLevels 过滤掉未置为可见的 PersistentLevel。
    for (TActorIterator<ATutorialDirector> DirectorIt(SavedWorld, ATutorialDirector::StaticClass(),
        EActorIteratorFlags::SkipPendingKill); DirectorIt; ++DirectorIt)
        Directors.Add(*DirectorIt);
    if (!TestEqual(TEXT("原保存地图恰有一个 TutorialDirector"), Directors.Num(), 1)) return false;
    ATutorialDirector* Director = Cast<ATutorialDirector>(Directors[0]);
    if (!TestNotNull(TEXT("原地图 Director 类型正确"), Director)) return false;
    UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(*this, Director);
    if (!SceneMusic) return false;
    TestFalse(TEXT("原地图 Director 未 BeginPlay"), Director->HasActorBegunPlay());
    TestFalse(TEXT("原地图 SceneMusic 未 BeginPlay"), SceneMusic->HasBegunPlay());
    TestTrue(TEXT("仅地图实例配置精确音乐资产"), SceneMusic->Music.Get() == Wave);
    CheckNear(*this, TEXT("地图实例音量 .22"), SceneMusic->Volume, .22);
    CheckNear(*this, TEXT("地图实例淡入 1 秒"), SceneMusic->FadeInSeconds, 1.0);
    TestTrue(TEXT("地图实例启用独立自动播放"), SceneMusic->bAutoPlay);
    CheckSilent(*this, Director, SceneMusic, TEXT("原保存地图 BeginPlay 前"));
    TestEqual(TEXT("原地图教学仍保留 8 个步骤，不改写原文"), Director->Steps.Num(), 8);
    for (int32 StepIndex = 0; StepIndex < Director->Steps.Num(); ++StepIndex)
        TestTrue(FString::Printf(TEXT("原保存教学步骤 %d 仍存在"), StepIndex), Director->Steps[StepIndex] != nullptr);

    int32 MapMusicComponentCount = 0;
    // 未初始化的保存世界 GetLevels() 可为空，必须显式包含 PersistentLevel；不初始化或修改原地图。
    TArray<ULevel*> SavedLevels = SavedWorld->GetLevels();
    SavedLevels.AddUnique(SavedWorld->PersistentLevel.Get());
    for (ULevel* SavedLevel : SavedLevels)
    {
        if (!SavedLevel) continue;
        for (AActor* SavedActor : SavedLevel->Actors)
        {
            if (!IsValid(SavedActor)) continue;
            TArray<UCafeSceneMusicComponent*> MusicComponents;
            SavedActor->GetComponents(MusicComponents);
            MapMusicComponentCount += MusicComponents.Num();
            for (UCafeSceneMusicComponent* MusicComponent : MusicComponents)
                TestTrue(TEXT("全图 SceneMusic 只属于原 Director，没有新增音乐 Actor"),
                    SavedActor == Director && MusicComponent == SceneMusic);
        }
    }
    TestEqual(TEXT("整张保存地图恰有一个 SceneMusic"), MapMusicComponentCount, 1);
    const ATutorialDirector* NativeDefaults = GetDefault<ATutorialDirector>();
    UCafeSceneMusicComponent* NativeMusicDefaults = FindSceneMusic(*this, NativeDefaults);
    const ATutorialDirector* MapClassDefaults = Director->GetClass()->GetDefaultObject<ATutorialDirector>();
    UCafeSceneMusicComponent* MapClassMusicDefaults = FindSceneMusic(*this, MapClassDefaults);
    if (!NativeMusicDefaults || !MapClassMusicDefaults) return false;
    TestNull(TEXT("原生 Director CDO 的 Music 为空"), NativeMusicDefaults->Music.Get());
    TestNull(TEXT("地图 Director 类 CDO 的 Music 为空"), MapClassMusicDefaults->Music.Get());
    TestNull(TEXT("独立 SceneMusic 组件 CDO 的 Music 为空"), GetDefault<UCafeSceneMusicComponent>()->Music.Get());
    CheckSilent(*this, NativeDefaults, NativeMusicDefaults, TEXT("原生 Director CDO"));
    CheckSilent(*this, MapClassDefaults, MapClassMusicDefaults, TEXT("地图类 Director CDO"));
    TestEqual(TEXT("只读检查不使原地图变脏"), SavedWorld->GetOutermost()->IsDirty(), bInitiallyDirty);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeSceneMusicNoAudioWorld,
    "CyberCafe.SceneMusic.NoAudioWorld", CyberCafeSceneMusicTests::Options)
bool FCafeSceneMusicNoAudioWorld::RunTest(const FString&)
{
    USoundWave* Wave = LoadMusicAsset(*this);
    if (!Wave) return false;
    for (int32 Scenario = 0; Scenario < 2; ++Scenario)
    {
        const bool bWithAudio = Scenario == 1;
        FSceneMusicTestWorld Fixture(*this, bWithAudio);
        if (!Fixture.bReady) return false;
        ATutorialDirector* Director = Fixture.SpawnDirector(bWithAudio ? nullptr : Wave);
        if (!Director || !Fixture.Begin(Director)) return false;
        UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(*this, Director);
        if (!SceneMusic) return false;
        const FString Label = bWithAudio ? TEXT("真实音频 World 的未配置默认 Director") : TEXT("无音频 World 的已配置音乐");
        TestEqual(Label + TEXT("：夹具没有教学步骤"), Director->Steps.Num(), 0);
        CheckSilent(*this, Director, SceneMusic, Label + TEXT(" BeginPlay 后"));
        for (int32 Attempt = 0; Attempt < 10; ++Attempt) SceneMusic->StartMusic();
        CheckSilent(*this, Director, SceneMusic, Label + TEXT(" 重复手动启动后"));
        Director->StartTutorial(); // 无步骤，真实路径立即 FinishTutorial。
        CheckSilent(*this, Director, SceneMusic, Label + TEXT(" StartTutorial/Finish 后"));
        Director->SkipTutorial();
        CheckSilent(*this, Director, SceneMusic, Label + TEXT(" Skip/Finish 后"));
        TestTrue(Label + TEXT("：Director 仍处于真实 BeginPlay 生命周期"), Director->HasActorBegunPlay());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeSceneMusicGameAudioLifecycle,
    "CyberCafe.SceneMusic.GameAudioLifecycle", CyberCafeSceneMusicTests::Options)
bool FCafeSceneMusicGameAudioLifecycle::RunTest(const FString&)
{
    USoundWave* Wave = LoadMusicAsset(*this);
    if (!Wave) return false;
    FSceneMusicTestWorld Fixture(*this, true);
    if (!Fixture.bReady) return false;
    ATutorialDirector* Director = Fixture.SpawnDirector(Wave);
    if (!Director || !Fixture.Begin(Director)) return false;
    UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(*this, Director);
    if (!SceneMusic) return false;
    TestFalse(TEXT("音乐自动启动不依赖 bAutoStart 教学"), Director->bAutoStart);
    CheckNear(*this, TEXT("独立音乐的默认淡入时长"), SceneMusic->FadeInSeconds, 1.0);
    UAudioComponent* MusicAudio = CheckPlaying(*this, Director, SceneMusic, Wave);
    if (!MusicAudio) return false;
    TestTrue(TEXT("真实自动播放请求处于 FadingIn"), MusicAudio->GetPlayState() == EAudioComponentPlayState::FadingIn);
    CheckActiveGain(*this, Fixture.DeviceHandle.GetAudioDevice(), MusicAudio, .22f);
    for (int32 Attempt = 0; Attempt < 10; ++Attempt)
    {
        SceneMusic->StartMusic();
        TestTrue(TEXT("重复 StartMusic 始终复用同一音频组件"), SceneMusic->GetMusicAudioComponent() == MusicAudio);
        TestEqual(TEXT("重复 StartMusic 被自有请求 gate 阻止，计数仍为 1"), SceneMusic->GetMusicStartCount(), 1);
        TestTrue(TEXT("重复 StartMusic 保持正在播放"), MusicAudio->IsPlaying());
    }
    CheckAudioComponentCount(*this, Director, 1, TEXT("十次重复启动后"));
    CheckActiveGain(*this, Fixture.DeviceHandle.GetAudioDevice(), MusicAudio, .22f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeSceneMusicTutorialRestartSkipKeepMusic,
    "CyberCafe.SceneMusic.TutorialRestartSkipKeepMusic", CyberCafeSceneMusicTests::Options)
bool FCafeSceneMusicTutorialRestartSkipKeepMusic::RunTest(const FString&)
{
    USoundWave* Wave = LoadMusicAsset(*this);
    if (!Wave) return false;
    FSceneMusicTestWorld Fixture(*this, true);
    if (!Fixture.bReady) return false;
    ATutorialDirector* Director = Fixture.SpawnDirector(Wave);
    if (!Director || !Fixture.Begin(Director)) return false;
    UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(*this, Director);
    if (!SceneMusic) return false;
    TestEqual(TEXT("使用真实无步骤 Director，开始和跳过均立即完成"), Director->Steps.Num(), 0);
    UAudioComponent* MusicAudio = CheckPlaying(*this, Director, SceneMusic, Wave);
    if (!MusicAudio) return false;
    for (int32 Restart = 0; Restart < 3; ++Restart)
    {
        Director->StartTutorial();
        TestTrue(TEXT("StartTutorial/立即 Finish 不替换音乐组件"), SceneMusic->GetMusicAudioComponent() == MusicAudio);
        if (!CheckPlaying(*this, Director, SceneMusic, Wave)) return false;
        Director->SkipTutorial();
        TestTrue(TEXT("Skip/Finish 通知不替换音乐组件"), SceneMusic->GetMusicAudioComponent() == MusicAudio);
        if (!CheckPlaying(*this, Director, SceneMusic, Wave)) return false;
    }
    CheckActiveGain(*this, Fixture.DeviceHandle.GetAudioDevice(), MusicAudio, .22f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeSceneMusicStopResumeAndOwnerDestruction,
    "CyberCafe.SceneMusic.StopResumeAndOwnerDestruction", CyberCafeSceneMusicTests::Options)
bool FCafeSceneMusicStopResumeAndOwnerDestruction::RunTest(const FString&)
{
    USoundWave* Wave = LoadMusicAsset(*this);
    if (!Wave) return false;
    for (int32 Termination = 0; Termination < 3; ++Termination)
    {
        FSceneMusicTestWorld Fixture(*this, true);
        if (!Fixture.bReady) return false;
        ATutorialDirector* Director = Fixture.SpawnDirector(Wave);
        if (!Director || !Fixture.Begin(Director)) return false;
        UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(*this, Director);
        if (!SceneMusic) return false;
        UAudioComponent* MusicAudio = CheckPlaying(*this, Director, SceneMusic, Wave);
        if (!MusicAudio) return false;
        SceneMusic->StopMusic();
        TestFalse(TEXT("手停立即撤销播放请求"), SceneMusic->IsMusicRequested());
        TestFalse(TEXT("手停立即 IsPlaying=false，不等待淡出或 Tick"), MusicAudio->IsPlaying());
        TestEqual(TEXT("手停不增加启动计数"), SceneMusic->GetMusicStartCount(), 1);
        TestTrue(TEXT("手停保留原 AudioComponent"), SceneMusic->GetMusicAudioComponent() == MusicAudio);
        SceneMusic->StopMusic();
        TestEqual(TEXT("重复手停仍不增加计数"), SceneMusic->GetMusicStartCount(), 1);
        Director->StartTutorial();
        Director->SkipTutorial();
        TestFalse(TEXT("教学 Start/Skip/Finish 不会恢复已手停音乐"), SceneMusic->IsMusicRequested());
        TestFalse(TEXT("教学变化后手停音乐仍不播放"), MusicAudio->IsPlaying());
        SceneMusic->StartMusic();
        TestTrue(TEXT("恢复启动复用原 AudioComponent"), SceneMusic->GetMusicAudioComponent() == MusicAudio);
        if (!CheckPlaying(*this, Director, SceneMusic, Wave, 2)) return false;
        CheckActiveGain(*this, Fixture.DeviceHandle.GetAudioDevice(), MusicAudio, .22f);
        TWeakObjectPtr<UAudioComponent> WeakMusicAudio(MusicAudio);
        if (Termination == 0)
        {
            if (!TestTrue(TEXT("真实播放中 Owner Destroy 成功"), Director->Destroy())) return false;
            // Destroy 返回后未进行 GC；立即检查原组件的 EndPlay/Destroyed 请求收尾。
            TestFalse(TEXT("Owner Destroy 立即撤销音乐请求"), SceneMusic->IsMusicRequested());
            SceneMusic->StartMusic();
            TestFalse(TEXT("Owner 正在销毁时不能重新请求音乐"), SceneMusic->IsMusicRequested());
            TestEqual(TEXT("Owner 销毁后 StartMusic 不增加计数"), SceneMusic->GetMusicStartCount(), 2);
        }
        else if (Termination == 1)
        {
            SceneMusic->DestroyComponent();
            TestFalse(TEXT("直接组件 Destroy 立即撤销音乐请求"), SceneMusic->IsMusicRequested());
        }
        else
        {
            Fixture.TestWorld->EndPlay(EEndPlayReason::Quit);
            TestFalse(TEXT("真实 World EndPlay 立即撤销音乐请求"), SceneMusic->IsMusicRequested());
        }
        TestTrue(TEXT("生命周期结束立即令弱音频组件失效或 IsPlaying=false，无需 Tick"),
            !WeakMusicAudio.IsValid() || !WeakMusicAudio->IsPlaying());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeSceneMusicInvalidVolume,
    "CyberCafe.SceneMusic.InvalidVolume", CyberCafeSceneMusicTests::Options)
bool FCafeSceneMusicInvalidVolume::RunTest(const FString&)
{
    USoundWave* Wave = LoadMusicAsset(*this);
    if (!Wave) return false;
    const float Inputs[] = {2.f, std::numeric_limits<float>::quiet_NaN(), -1.f};
    const float ExpectedGains[] = {1.f, .22f, 0.f};
    for (int32 VolumeCase = 0; VolumeCase < 3; ++VolumeCase)
    {
        FSceneMusicTestWorld Fixture(*this, true);
        if (!Fixture.bReady) return false;
        ATutorialDirector* Director = Fixture.SpawnDirector(Wave);
        if (!Director) return false;
        UCafeSceneMusicComponent* SceneMusic = FindSceneMusic(*this, Director);
        if (!SceneMusic) return false;
        // 直接配置原始坏值，必须由真实 BeginPlay/StartMusic 自己校正，不能夹具先行清洗。
        SceneMusic->Volume = Inputs[VolumeCase];
        if (!Fixture.Begin(Director)) return false;
        UAudioComponent* MusicAudio = SceneMusic->GetMusicAudioComponent();
        if (!TestNotNull(TEXT("坏音量仍使用真实音频组件请求"), MusicAudio)) return false;
        const float ExpectedGain = ExpectedGains[VolumeCase];
        CheckNear(*this, TEXT(">1 clamp 1 / NaN fallback .22 / <0 clamp 0：组件配置有限"), SceneMusic->Volume, ExpectedGain);
        CheckNear(*this, TEXT("实际 VolumeMultiplier 只有一份校正增益"), MusicAudio->VolumeMultiplier, ExpectedGain);
        TestTrue(TEXT("包括零增益也保留有效播放请求"), SceneMusic->IsMusicRequested());
        TestTrue(TEXT("包括零增益也已发起真实 IsPlaying 请求，不等同于可听"), MusicAudio->IsPlaying());
        TestEqual(TEXT("校正音量只启动一次"), SceneMusic->GetMusicStartCount(), 1);
        CheckActiveGain(*this, Fixture.DeviceHandle.GetAudioDevice(), MusicAudio, ExpectedGain);
        SceneMusic->SetMusicVolume(.5f);
        CheckNear(*this, TEXT("播放中公开 SetMusicVolume 直接更新单份增益"), MusicAudio->VolumeMultiplier, .5);
        SceneMusic->SetMusicVolume(Inputs[VolumeCase]);
        CheckNear(*this, TEXT("播放中公开 setter 同样校正坏配置"), SceneMusic->Volume, ExpectedGain);
        CheckNear(*this, TEXT("播放中校正仍不重复乘增益"), MusicAudio->VolumeMultiplier, ExpectedGain);
        SceneMusic->StartMusic();
        TestTrue(TEXT("音量 setter 和 Start gate 不替换原音频组件"), SceneMusic->GetMusicAudioComponent() == MusicAudio);
        TestEqual(TEXT("音量变化不触发额外播放请求"), SceneMusic->GetMusicStartCount(), 1);
        TestFalse(TEXT("坏音量不允许多实例重叠"), MusicAudio->bCanPlayMultipleInstances != 0);
        CheckAudioComponentCount(*this, Director, 1, TEXT("坏音量校正后"));
        CheckActiveGain(*this, Fixture.DeviceHandle.GetAudioDevice(), MusicAudio, ExpectedGain);
    }
    return !HasAnyErrors();
}
}
#endif
