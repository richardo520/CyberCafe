// 本组只读保存资产；运行夹具只生成独立 Game World，不对原 L_Cafe 执行 BeginPlay 或保存。
// 主代理编译后运行 CyberCafe.MediaAudio；音频须 -AudioMixer、无 -nosound，媒体须原生 WMF 与正常媒体 Tick。
// 媒体没有真实帧就失败；-NullRHI 不保证 WMF 解码，必要时由主代理选择 -RenderOffscreen -nohmd。
// 外部证据由主代理生成并传入：-CafeMediaBaseline=<current_before.json>
// -CafeMediaSourceBaseline=<before_sha256.json> -CafeMediaVideoEvidence=<视频证据.json>。
// 视频证据字段：source_sha256、copied_sha256、file_size_bytes、ffmpeg_decode_exit_code、
// video_codec、width、height、frame_rate、duration_seconds、audio_codec、audio_sample_rate、audio_channels。
// FFmpeg 证据只证明外部完整解码；本组另行验证 UE 原生打开、播放时钟和实际解码样本，不调用 FFmpeg。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "CafeSceneMusicComponent.h"
#include "Media/CafeTVPlaybackComponent.h"
#include "Tutorial/CustomerDrinkComponent.h"
#include "Tutorial/CustomerDrinkMotion.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialNPC.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "ActiveSound.h"
#include "ActorEditorUtils.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "AudioDeviceManager.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include <atomic>
#include "IMediaTextureSample.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInterface.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "MediaSampleQueue.h"
#include "MediaSoundComponent.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "bcrypt.lib")
#endif

namespace CafeMediaAudioTests
{
constexpr auto Options = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
const TCHAR* TVMeshPath = TEXT("/Game/Cafe/Meshes/B05/SM_B05_CRT_TV.SM_B05_CRT_TV");
const TCHAR* ScreenMaterialPath = TEXT("/Game/Cafe/TV/M_CafeTVVideo.M_CafeTVVideo");
const TCHAR* MasterPath = TEXT("/Engine/EngineSounds/Master.Master");
const TCHAR* MusicPath = TEXT("/Game/VRTemplate/Audio/Music/S_WarmColdContrast_Remastered.S_WarmColdContrast_Remastered");
const TCHAR* MotionPath = TEXT("/Game/Characters/BarGirl/FirstCustomer/Highball/DA_BarGirlHighballDrinkMotion.DA_BarGirlHighballDrinkMotion");
const TCHAR* CalibratedAnimationPath = TEXT("/Game/Characters/BarGirl/FirstCustomer/Highball/A_BarGirl_HighballDrink_Calibrated.A_BarGirl_HighballDrink_Calibrated");

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected, double Tolerance = 1.e-5)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}

// 保存世界可能尚未注册组件；沿序列化父链计算，不把未注册的 ComponentToWorld 当作布局基线。
FTransform SavedTransform(const USceneComponent* Component)
{
    FTransform Result = FTransform::Identity;
    for (const USceneComponent* Current = Component; Current; Current = Current->GetAttachParent())
        Result = Result * Current->GetRelativeTransform();
    return Result;
}

struct FSavedScene
{
    FAutomationTestBase& Test;
    TStrongObjectPtr<UWorld> World;
    ATutorialDirector* Director = nullptr;
    AStaticMeshActor* TV = nullptr;
    UCafeTVPlaybackComponent* Playback = nullptr;
    UCafeSceneMusicComponent* Music = nullptr;
    bool bDirty = false;
    bool bReady = false;

    explicit FSavedScene(FAutomationTestBase& InTest) : Test(InTest), World(LoadObject<UWorld>(nullptr, MapPath))
    {
        if (!Test.TestNotNull(TEXT("只读加载精确保存 L_Cafe"), World.Get())) return;
        bDirty = World->GetOutermost()->IsDirty();
        if (!Test.TestFalse(TEXT("禁止对原保存地图执行 BeginPlay"), World->HasBegunPlay())
            || !Test.TestNotNull(TEXT("保存地图 PersistentLevel 存在"), World->PersistentLevel.Get())) return;
        int32 Directors = 0;
        int32 TVComponents = 0;
        for (const auto& ActorPtr : World->PersistentLevel->Actors)
        {
            AActor* Actor = ActorPtr.Get();
            if (!IsValid(Actor)) continue;
            if (ATutorialDirector* Candidate = Cast<ATutorialDirector>(Actor))
            {
                Director = Candidate;
                ++Directors;
            }
            if (Actor->GetFName() == FName(TEXT("StaticMeshActor_9"))) TV = Cast<AStaticMeshActor>(Actor);
            TArray<UCafeTVPlaybackComponent*> Components;
            Actor->GetComponents(Components);
            TVComponents += Components.Num();
            for (UCafeTVPlaybackComponent* Component : Components)
                Test.TestEqual(TEXT("播放组件只属于原电视，不另建 Actor"), Component->GetOwner()->GetFName(), FName(TEXT("StaticMeshActor_9")));
        }
        if (!Test.TestEqual(TEXT("保存地图只有一个 Director"), Directors, 1)
            || !Test.TestNotNull(TEXT("原电视 StaticMeshActor_9 存在"), TV)
            || !Test.TestEqual(TEXT("整张保存地图只有一个 TV 播放组件"), TVComponents, 1)) return;
        Playback = TV->FindComponentByClass<UCafeTVPlaybackComponent>();
        Music = Director->FindComponentByClass<UCafeSceneMusicComponent>();
        if (!Test.TestNotNull(TEXT("原 TV 已保存原生播放组件"), Playback)
            || !Test.TestNotNull(TEXT("原 Director 已保存 SceneMusic"), Music)) return;
        bReady = true;
    }

    ~FSavedScene()
    {
        if (World.IsValid())
        {
            Test.TestFalse(TEXT("只读检查后原地图仍未 BeginPlay"), World->HasBegunPlay());
            Test.TestEqual(TEXT("测试不改变原地图脏标记"), World->GetOutermost()->IsDirty(), bDirty);
        }
    }
};

TSharedPtr<FJsonObject> ReadEvidence(FAutomationTestBase& Test, const TCHAR* Argument)
{
    FString Path;
    if (!Test.TestTrue(FString::Printf(TEXT("主代理须提供外部证据参数 -%s"), Argument),
        FParse::Value(FCommandLine::Get(), Argument, Path) && !Path.IsEmpty())) return nullptr;
    FString Text;
    if (!Test.TestTrue(TEXT("外部证据文件只读加载成功"), FFileHelper::LoadFileToString(Text, *Path))) return nullptr;
    TSharedPtr<FJsonObject> Result;
    if (!Test.TestTrue(TEXT("外部证据是有效 JSON 对象"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result)
        && Result.IsValid())) return nullptr;
    return Result;
}

bool ReadNumber(FAutomationTestBase& Test, const FJsonObject& Object, const TCHAR* Field, double& Value)
{
    return Test.TestTrue(FString::Printf(TEXT("证据含有限数值字段 %s"), Field),
        Object.TryGetNumberField(Field, Value) && FMath::IsFinite(Value));
}

FString HashBytesNative(FAutomationTestBase& Test, uint8* Bytes, uint64 NumBytes)
{
#if PLATFORM_WINDOWS
    if (!Test.TestTrue(TEXT("SHA256 输入字节数不超过 Windows ULONG 上限，禁止截断"), NumBytes <= uint64(MAX_uint32))) return FString();
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    const NTSTATUS OpenStatus = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!Test.TestTrue(FString::Printf(TEXT("BCryptOpenAlgorithmProvider SHA256 成功（NTSTATUS=0x%08x）"), uint32(OpenStatus)), OpenStatus >= 0)) return FString();
    uint8 Digest[32] = {};
    ULONG ObjectLength = 0, BytesWritten = 0;
    NTSTATUS HashStatus = BCryptGetProperty(Algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&ObjectLength), sizeof(ObjectLength), &BytesWritten, 0);
    TArray<uint8> HashObject;
    if (HashStatus >= 0) HashObject.SetNumUninitialized(ObjectLength);
    BCRYPT_HASH_HANDLE Hash = nullptr;
    if (HashStatus >= 0) HashStatus = BCryptCreateHash(Algorithm, &Hash, HashObject.GetData(), ObjectLength, nullptr, 0, 0);
    if (HashStatus >= 0) HashStatus = BCryptHashData(Hash, Bytes, static_cast<ULONG>(NumBytes), 0);
    if (HashStatus >= 0) HashStatus = BCryptFinishHash(Hash, Digest, sizeof(Digest), 0);
    if (Hash) BCryptDestroyHash(Hash);
    // Use the SDK's baseline API rather than Windows10-only BCryptHash declarations.
    const NTSTATUS CloseStatus = BCryptCloseAlgorithmProvider(Algorithm, 0);
    const bool bHashed = Test.TestTrue(FString::Printf(TEXT("BCrypt SHA256 成功（NTSTATUS=0x%08x）"), uint32(HashStatus)), HashStatus >= 0);
    const bool bClosed = Test.TestTrue(FString::Printf(TEXT("BCryptCloseAlgorithmProvider 成功（NTSTATUS=0x%08x）"), uint32(CloseStatus)), CloseStatus >= 0);
    if (!bHashed || !bClosed) return FString();
    const TCHAR* Hex = TEXT("0123456789abcdef");
    FString Result;
    Result.Reserve(64);
    for (uint8 Byte : Digest)
    {
        Result.AppendChar(Hex[Byte >> 4]);
        Result.AppendChar(Hex[Byte & 0x0f]);
    }
    return Result;
#else
    Test.AddError(TEXT("测试专用原生 SHA256 unsupported：当前非 Windows 平台，不能核验真实文件 SHA256。"));
    return FString();
#endif
}

FString FileSHA256(FAutomationTestBase& Test, const FString& Path, int64* OutSize = nullptr)
{
    TArray<uint8> Bytes;
    if (!Test.TestTrue(TEXT("从运行时当前 ProjectDir 读取实际文件，而非作者工作区副本"), FFileHelper::LoadFileToArray(Bytes, *Path))) return FString();
    if (OutSize) *OutSize = Bytes.Num();
    return HashBytesNative(Test, Bytes.GetData(), uint64(Bytes.Num()));
}

USoundClass* LoadMaster(FAutomationTestBase& Test)
{
    USoundClass* Master = LoadObject<USoundClass>(nullptr, MasterPath);
    Test.TestNotNull(TEXT("加载 Engine 原 Master SoundClass，不另建类"), Master);
    return Master;
}

bool CheckMixAsset(FAutomationTestBase& Test, USoundMix* Mix, USoundClass* Master)
{
    if (!Test.TestNotNull(TEXT("保存的 GlobalGainMix 资产存在"), Mix) || !Master) return false;
    Test.TestFalse(TEXT("Mix 是保存资产而不是临时测试替身"), Mix->HasAnyFlags(RF_Transient));
    Test.TestTrue(TEXT("Mix 是项目资产"), Mix->GetPathName().StartsWith(TEXT("/Game/")));
    Test.TestFalse(TEXT("全局音量调整不附加 EQ"), Mix->bApplyEQ != 0);
    if (!Test.TestEqual(TEXT("唯一 Master adjuster；禁止对每个子类再叠一份"), Mix->SoundClassEffects.Num(), 1)) return false;
    const FSoundClassAdjuster& Adjuster = Mix->SoundClassEffects[0];
    Test.TestTrue(TEXT("adjuster 精确指向原 Engine Master"), Adjuster.SoundClassObject == Master);
    Near(Test, TEXT("Master VolumeAdjuster 为 2"), Adjuster.VolumeAdjuster, 2);
    Near(Test, TEXT("PitchAdjuster 保持 1"), Adjuster.PitchAdjuster, 1);
    Near(Test, TEXT("VoiceCenter 不额外放大"), Adjuster.VoiceCenterChannelVolumeAdjuster, 1);
    Test.TestTrue(TEXT("一次 Master 调整覆盖所有子类"), Adjuster.bApplyToChildren != 0);
    Test.TestTrue(TEXT("Mix 负 Duration 维持整个场景生命周期"), Mix->Duration < 0);
    return !Test.HasAnyErrors();
}

// 沿用 SceneMusicTests 的真实 GI/World/设备夹具；不写私有状态，不修改全局 FX cvar。
struct FGameWorld
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    FAudioDeviceHandle Device;
    bool bReady = false;
    bool bEnded = false;

    FGameWorld(FAutomationTestBase& InTest, bool bForNativeMedia, bool bNonRealtime = true) : Test(InTest)
    {
        if (!Test.TestNotNull(TEXT("引擎存在"), GEngine)) return;
        UWorld::InitializationValues Values;
        Values.AllowAudioPlayback(true).CreatePhysicsScene(false).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false).RequiresHitProxies(false).CreateFXSystem(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
        if (!Test.TestNotNull(TEXT("创建独立无物理无 AI 的真实 Game World"), World)) return;
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
        if (bForNativeMedia)
        {
            // 原生 WMF 要真实媒体/音频时钟；借用主设备但不改 ActiveDevice 或原用户 World。
            Device = GEngine->GetMainAudioDevice();
        }
        else
        {
            FAudioDeviceManager* Manager = FAudioDeviceManager::Get();
            if (!Test.TestNotNull(TEXT("设备管理器存在；启用 -AudioMixer 且无 -nosound"), Manager)) return;
            FAudioDeviceParams Params;
            Params.Scope = EAudioDeviceScope::Unique;
            Params.bIsNonRealtime = bNonRealtime;
            Params.AssociatedWorld = World;
            Device = Manager->RequestAudioDevice(Params);
        }
        if (!Test.TestNotNull(TEXT("真实音频设备必须可用，不能 skip 或模拟"), Device.GetAudioDevice())) return;
        Context.AudioDeviceID = Device.GetDeviceID();
        if (bForNativeMedia)
        {
            FAudioDeviceManager* Manager = FAudioDeviceManager::Get();
            if (!Test.TestNotNull(TEXT("主设备对应的管理器存在"), Manager)) return;
            // 主句柄没有关联此 World；由管理器生成绑定句柄，避免 UWorld::SetAudioDevice 的关联世界断言。
            Manager->SetAudioDevice(*World, Device.GetDeviceID());
            Device = World->GetAudioDevice();
        }
        else World->SetAudioDevice(Device);
        Test.TestTrue(TEXT("Game Context 拥有本 GI"), Context.OwningGameInstance == GI);
        Test.TestTrue(TEXT("World 绑定正确真实音频设备"), World->GetAudioDeviceRaw() == Device.GetAudioDevice());
        Test.TestTrue(TEXT("World 允许音频播放"), World->AllowAudioPlayback());
        bReady = !Test.HasAnyErrors();
    }

    void Begin()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->MinUndilatedFrameTime = 0.f;
        World->GetWorldSettings()->NotifyBeginPlay();
        Test.TestTrue(TEXT("只对独立 Game World 执行真实 BeginPlay"), World->HasBegunPlay());
    }

    void End()
    {
        if (!bEnded && World && World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        bEnded = true;
    }

    ~FGameWorld()
    {
        if (!World) return;
        End();
        if (GI) GI->Shutdown();
        World->SetAudioDevice(FAudioDeviceHandle());
        Device.Reset();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    UCafeSceneMusicComponent* SpawnMusic(const UCafeSceneMusicComponent* Saved)
    {
        if (!bReady) return nullptr;
        ATutorialDirector* Director = World->SpawnActorDeferred<ATutorialDirector>(ATutorialDirector::StaticClass(),
            FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Test.TestNotNull(TEXT("独立原生 Director 生成成功"), Director)) return nullptr;
        Director->bAutoStart = false;
        UCafeSceneMusicComponent* Music = Director->FindComponentByClass<UCafeSceneMusicComponent>();
        if (!Test.TestNotNull(TEXT("原生 SceneMusic 默认子对象存在"), Music)) return nullptr;
        Music->Music = Saved->Music;
        Music->GlobalGainMix = Saved->GlobalGainMix;
        Music->Volume = Saved->Volume;
        Music->FadeInSeconds = Saved->FadeInSeconds;
        Music->bAutoPlay = Saved->bAutoPlay;
        Director->FinishSpawning(FTransform::Identity);
        Test.TestFalse(TEXT("复制保存属性后尚未 BeginPlay"), Music->HasBegunPlay());
        return Music;
    }
};

struct FMixSnapshot
{
    bool bFound = false;
    uint32 Active = 0;
    uint32 Passive = 0;
    TMap<USoundClass*, FSoundClassProperties> Classes;
};

FMixSnapshot ReadMix(FAudioDevice* Device, USoundMix* Mix, const TArray<USoundClass*>& Classes)
{
    FMixSnapshot Result;
    FAudioThread::RunCommandOnAudioThread([Device, Mix, Classes, &Result]()
    {
        if (const FSoundMixState* State = Device->GetSoundMixModifiers().Find(Mix))
        {
            Result.bFound = true;
            Result.Active = State->ActiveRefCount;
            Result.Passive = State->PassiveRefCount;
        }
        for (USoundClass* Class : Classes)
            if (const FSoundClassProperties* Properties = Device->GetSoundClassCurrentProperties(Class))
                Result.Classes.Add(Class, *Properties);
    });
    FAudioCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait();
    return Result;
}

void CheckRefs(FAutomationTestBase& Test, FAudioDevice* Device, USoundMix* Mix, uint32 Expected, const FString& Label)
{
    const FMixSnapshot Snapshot = ReadMix(Device, Mix, {});
    if (Expected != 0) Test.TestTrue(Label + TEXT("：真实设备含 Mix"), Snapshot.bFound);
    Test.TestEqual(Label + TEXT("：真实 ActiveRefCount"), Snapshot.Active, Expected);
    Test.TestEqual(Label + TEXT("：没有被动重复 Push"), Snapshot.Passive, uint32(0));
}

void CheckBGM(FAutomationTestBase& Test, UCafeSceneMusicComponent* Music)
{
    UAudioComponent* Audio = Music->GetMusicAudioComponent();
    if (!Test.TestNotNull(TEXT("BGM 真实音源存在"), Audio)) return;
    Test.TestTrue(TEXT("BGM 真实音源正在播放，不等同于头显可听"), Audio->IsPlaying());
    Test.TestTrue(TEXT("播放的是原保存 BGM"), Audio->Sound == Music->Music);
    Near(Test, TEXT("BGM 源只用原 .22，不局部加倍"), Audio->VolumeMultiplier, .22);
    Near(Test, TEXT("BGM 音高不变"), Audio->PitchMultiplier, 1);
    Test.TestFalse(TEXT("BGM 不空间化"), Audio->bAllowSpatialization != 0);
    Test.TestFalse(TEXT("BGM 禁止同组件叠播"), Audio->bCanPlayMultipleInstances != 0);
    TArray<UAudioComponent*> Components;
    Audio->GetOwner()->GetComponents(Components);
    Test.TestEqual(TEXT("BGM Owner 恰有一个实际音源"), Components.Num(), 1);
    struct FGain { int32 Count = 0; float Volume = 0; float Pitch = 0; float Fade = 0; } Gain;
    Music->GetWorld()->GetAudioDeviceRaw()->SendCommandToActiveSounds(Audio->GetAudioComponentID(), [&Gain](FActiveSound& Sound)
    {
        ++Gain.Count;
        Gain.Volume = Sound.VolumeMultiplier;
        Gain.Pitch = Sound.PitchMultiplier;
        Gain.Fade = Sound.ComponentVolumeFader.GetTargetVolume();
    });
    FAudioCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait();
    Test.TestEqual(TEXT("设备上 BGM 只有一个真实 ActiveSound"), Gain.Count, 1);
    Near(Test, TEXT("真实 BGM ActiveSound 单份 .22"), Gain.Volume, .22);
    Near(Test, TEXT("真实 BGM ActiveSound Pitch 为 1"), Gain.Pitch, 1);
    Near(Test, TEXT("淡入目标为 unity，不再乘 .22 或 2"), Gain.Fade, 1);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaSavedTVAttachments,
    "CyberCafe.MediaAudio.SourceSavedMapTVComponentAttachments", Options)
bool FCafeMediaSavedTVAttachments::RunTest(const FString&)
{
    FSavedScene Scene(*this);
    if (!Scene.bReady) return false;
    TestEqual(TEXT("原 TV label 保留"), Scene.TV->GetActorLabel(), FString(TEXT("B05_CRT_TV")));
    TestTrue(TEXT("TV Actor 根变换不因烘焙顶点坐标而挪动"), SavedTransform(Scene.TV->GetRootComponent()).Equals(FTransform::Identity, 1.e-5));
    UStaticMeshComponent* Mesh = Scene.TV->GetStaticMeshComponent();
    TestTrue(TEXT("保留原电视 StaticMesh Root"), Scene.TV->GetRootComponent() == Mesh);
    TestEqual(TEXT("沿用原 TV 网格"), GetPathNameSafe(Mesh->GetStaticMesh()), FString(TVMeshPath));
    TestEqual(TEXT("原五个材质槽全部保留"), Mesh->GetNumMaterials(), 5);
    const TCHAR* Materials[] = { TEXT("MI_TVShell"), TEXT("M_CafeTVVideo"), TEXT("MI_BlackPaint"), TEXT("MI_Chrome"), TEXT("MI_SteelBrushed") };
    for (int32 Slot = 0; Slot < 5 && Slot < Mesh->GetNumMaterials(); ++Slot)
    {
        TestEqual(FString::Printf(TEXT("保存槽位 %d：仅槽 1 是授权视频材质，其余保留原名"), Slot), GetNameSafe(Mesh->GetMaterial(Slot)), FString(Materials[Slot]));
        if (Slot != 1)
            TestEqual(FString::Printf(TEXT("保存非屏幕槽位 %d 精确保留 baseline 原资产"), Slot), GetPathNameSafe(Mesh->GetMaterial(Slot)),
                FString::Printf(TEXT("/Game/Cafe/Materials/%s.%s"), Materials[Slot], Materials[Slot]));
    }
    TestNotNull(TEXT("保存屏幕材质硬引用已存在"), Scene.Playback->ScreenMaterial.Get());
    TestTrue(TEXT("保存槽 1 精确等于播放组件 ScreenMaterial"), Mesh->GetMaterial(1) == Scene.Playback->ScreenMaterial);
    TestEqual(TEXT("保存槽 1 精确指向授权 M_CafeTVVideo 资产"), GetPathNameSafe(Mesh->GetMaterial(1)), FString(ScreenMaterialPath));
    TestTrue(TEXT("保存实例确为原生类，不是替代蓝图组件"), Scene.Playback->GetClass() == UCafeTVPlaybackComponent::StaticClass());
    TestFalse(TEXT("播放组件不是临时组件"), Scene.Playback->HasAnyFlags(RF_Transient));
    TestTrue(TEXT("组件外层仍为原 TV"), Scene.Playback->GetOuter() == Scene.TV);
    TestTrue(TEXT("原 TV 实例组件数组保存该组件"), Scene.TV->GetInstanceComponents().Contains(Scene.Playback));
    TestTrue(TEXT("组件直接挂到原 TV Root"), Scene.Playback->GetAttachParent() == Mesh);
    TestTrue(TEXT("保存本地偏移等于作者实测屏幕中心"), Scene.Playback->GetRelativeLocation().Equals(Scene.Playback->SoundOffset, 1.e-5));
    TestTrue(TEXT("烘焙屏幕中心偏移未误当 Actor 世界原点"), Scene.Playback->SoundOffset.Equals(FVector(318.1, 45.77, 204.0), .1));
    TestEqual(TEXT("保存和运行时都仅使用屏幕槽位 1"), Scene.Playback->ScreenMaterialIndex, 1);
    Near(*this, TEXT("TV 源音量保留 1"), Scene.Playback->Volume, 1);
    TestTrue(TEXT("TV 保存配置启用"), Scene.Playback->bEnabled);
    TestTrue(TEXT("保存配置启用真实回绕诊断"), Scene.Playback->bEnableLoopDiagnostics);
    TestFalse(TEXT("原保存组件没有 BeginPlay"), Scene.Playback->HasBegunPlay());
    TestTrue(TEXT("保存组件生命周期初始关闭"), Scene.Playback->bLifecycleClosed);
    TestNull(TEXT("原保存组件没有动态音源"), Scene.Playback->GetTVAudioComponent());
    TestEqual(TEXT("原保存组件未伪造打开计数"), Scene.Playback->MediaOpenedCount, 0);
    TestEqual(TEXT("原保存组件未伪造回绕计数"), Scene.Playback->LoopRewindCount, 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaSavedAssetReferences,
    "CyberCafe.MediaAudio.ExactSavedMediaAssetUPROPERTYReferences", Options)
bool FCafeMediaSavedAssetReferences::RunTest(const FString&)
{
    FSavedScene Scene(*this);
    if (!Scene.bReady) return false;
    UCafeTVPlaybackComponent* TV = Scene.Playback;
    const TCHAR* Fields[] = { TEXT("MediaSource"), TEXT("MediaPlayer"), TEXT("MediaTexture"), TEXT("ScreenMaterial") };
    UObject* Values[] = { TV->MediaSource.Get(), TV->MediaPlayer.Get(), TV->MediaTexture.Get(), TV->ScreenMaterial.Get() };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Fields); ++Index)
    {
        FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(TV->GetClass(), Fields[Index]);
        if (!TestNotNull(TEXT("媒体引用是可序列化硬引用 UPROPERTY"), Property)
            || !TestNotNull(TEXT("媒体引用已由主代理保存，不由夹具补齐"), Values[Index])) return false;
        TestFalse(TEXT("媒体硬引用不标记 Transient"), Property->HasAnyPropertyFlags(CPF_Transient));
        TestFalse(TEXT("媒体硬引用不被 cook 的 EditorOnly 过滤"), Property->HasAnyPropertyFlags(CPF_EditorOnly));
        TestTrue(TEXT("反射读取的是实际保存引用"), Property->GetObjectPropertyValue_InContainer(TV) == Values[Index]);
        TestFalse(TEXT("引用目标不是临时资产"), Values[Index]->HasAnyFlags(RF_Transient));
        TestTrue(TEXT("精确路径 LoadObject 返回同一保存资产"), LoadObject<UObject>(nullptr, *Values[Index]->GetPathName()) == Values[Index]);
    }
    TestNotNull(TEXT("Source 确为原生 FileMediaSource"), Cast<UFileMediaSource>(TV->MediaSource));
    TestEqual(TEXT("使用主代理配置的 MP_TV_Loop"), TV->MediaPlayer->GetFName(), FName(TEXT("MP_TV_Loop")));
    TestTrue(TEXT("保存 MediaTexture 确实引用同一播放器"), TV->MediaTexture->GetMediaPlayer() == TV->MediaPlayer);
    TestFalse(TEXT("禁止原生旁路音频与 MediaSound 双音源"), TV->MediaPlayer->NativeAudioOut);
    UMaterial* Material = Cast<UMaterial>(TV->ScreenMaterial);
    if (!TestNotNull(TEXT("保存 ScreenMaterial 是实际作者 UMaterial"), Material)) return false;
    UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
    if (!TestNotNull(TEXT("实际材质含保存的 EditorOnly 图数据"), EditorOnlyData)
        || !TestNotNull(TEXT("实际 EmissiveColor 根已连接表达式"), EditorOnlyData->EmissiveColor.Expression)) return false;
    // 只沿 Emissive 根的真实输入链检查，脱离输出的媒体节点不能作证；Visited 防止环路重复遍历。
    TSet<const UMaterialExpression*> Visited;
    TFunction<bool(UMaterialExpression*)> ReachesMediaTexture = [&](UMaterialExpression* Expression)
    {
        if (!Expression || Visited.Contains(Expression)) return false;
        Visited.Add(Expression);
        if (const UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Expression))
            if (Sample->Texture == TV->MediaTexture && Sample->SamplerType == SAMPLERTYPE_LinearColor) return true;
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        const TArrayView<FExpressionInput*> Inputs = Expression->GetInputsView();
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
        for (const FExpressionInput* Input : Inputs)
            if (Input && ReachesMediaTexture(Input->Expression)) return true;
        return false;
    };
    TestTrue(TEXT("Emissive 实际可达 TextureSample，硬引用同一新式 MediaTexture 且使用匹配的 LinearColor sampler"),
        ReachesMediaTexture(EditorOnlyData->EmissiveColor.Expression));
    // UE5.6 的 GetReferencedTextures 返回表达式引用缓存，补验硬引用；它不能替代上面的输出可达性。
    bool bReferencedMediaTexture = false;
    for (const TObjectPtr<UObject>& Texture : Material->GetReferencedTextures())
        bReferencedMediaTexture |= Texture.Get() == TV->MediaTexture.Get();
    TestTrue(TEXT("保存材质表达式引用缓存仍含同一 MediaTexture 硬引用"), bReferencedMediaTexture);
    // GetUsedTextures 依赖已编译 shader 资源；NullRHI 下可缺 External Texture，只记录诊断，不作保存图断言。
    TArray<UTexture*> Textures;
    Material->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
    AddInfo(FString::Printf(TEXT("运行时 shader GetUsedTextures：数量=%d，包含保存 MediaTexture=%d；保存引用由 Emissive 图验证。"),
        Textures.Num(), Textures.Contains(TV->MediaTexture)));
    const UCafeTVPlaybackComponent* Defaults = GetDefault<UCafeTVPlaybackComponent>();
    TestNull(TEXT("默认组件不污染其他 TV 的 Source"), Defaults->MediaSource.Get());
    TestNull(TEXT("默认组件不污染其他 TV 的 Player"), Defaults->MediaPlayer.Get());
    TestNull(TEXT("默认组件不污染其他 TV 的 Texture"), Defaults->MediaTexture.Get());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaMoviePackaging,
    "CyberCafe.MediaAudio.ContentMovieRelativePathPackagingAndSHAEvidence", Options)
bool FCafeMediaMoviePackaging::RunTest(const FString&)
{
    uint8 KnownBytes[] = { 0x61, 0x62, 0x63 };
    if (!TestEqual(TEXT("测试专用原生 SHA256 自检：UTF-8 abc，无 BOM 或终止符"),
        HashBytesNative(*this, KnownBytes, UE_ARRAY_COUNT(KnownBytes)),
        FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")))) return false;
    FSavedScene Scene(*this);
    if (!Scene.bReady) return false;
    UFileMediaSource* Source = Cast<UFileMediaSource>(Scene.Playback->MediaSource);
    if (!TestNotNull(TEXT("真实保存 FileMediaSource"), Source)) return false;
    FString Relative = Source->GetFilePath();
    FPaths::NormalizeFilename(Relative);
    TestTrue(TEXT("保存路径是相对路径，无作者盘符或个人目录"), FPaths::IsRelative(Relative));
    TestTrue(TEXT("支持 Content 相对或 Project 相对 Movies 路径"),
        Relative == TEXT("./Movies/TV_Loop.mp4") || Relative == TEXT("./Content/Movies/TV_Loop.mp4"));
    TestFalse(TEXT("不保存个人源视频路径"), Relative.Contains(TEXT(":")) || Relative.Contains(TEXT(".."))
        || Relative.Contains(TEXT("Users/"), ESearchCase::IgnoreCase) || Relative.Contains(TEXT("WorkBuddy"), ESearchCase::IgnoreCase));
    FString ExpectedPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Movies/TV_Loop.mp4")));
    FString ActualPath = Source->GetFullPath();
    FPaths::NormalizeFilename(ExpectedPath);
    FPaths::NormalizeFilename(ActualPath);
    TestEqual(TEXT("GetFullPath 解析到当前运行项目 Content/Movies，不硬编码 J 或作者工作区"), ActualPath, ExpectedPath);
    TestTrue(TEXT("FileMediaSource.Validate 检查实际运行文件存在"), Source->Validate());
    FStrProperty* FileProperty = FindFProperty<FStrProperty>(UFileMediaSource::StaticClass(), TEXT("FilePath"));
    if (TestNotNull(TEXT("FilePath 是实际保存的 FString UPROPERTY"), FileProperty))
    {
        TestFalse(TEXT("FilePath 不被 Transient 或 EditorOnly cook 标记过滤"), FileProperty->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly));
        TestEqual(TEXT("公开 getter 读取的就是原保存 UPROPERTY"), FileProperty->GetPropertyValue_InContainer(Source), Source->GetFilePath());
    }
    TestFalse(TEXT("没有强制整片预缓存"), Source->PrecacheFile);
    const TCHAR* PackagingSection = TEXT("/Script/UnrealEd.ProjectPackagingSettings");
    bool bSkipMovies = false;
    // UE5.6 的排除电影开关实际为 bSkipMovies；缺省 false，不读取不存在的包装属性。
    GConfig->GetBool(PackagingSection, TEXT("bSkipMovies"), bSkipMovies, GGameIni);
    TestFalse(TEXT("bSkipMovies 缺省或显式为 false，默认 Movies 将以 NonUFS 复制"), bSkipMovies);
    TArray<FString> UFSMovies;
    GConfig->GetArray(PackagingSection, TEXT("UFSMovies"), UFSMovies, GGameIni);
    TestFalse(TEXT("TV_Loop 不强制收入原生文件播放器无法直接打开的 pak/UFS 列表"),
        UFSMovies.Contains(TEXT("TV_Loop")) || UFSMovies.Contains(TEXT("TV_Loop.mp4")));
    TSharedPtr<FJsonObject> Evidence = ReadEvidence(*this, TEXT("CafeMediaVideoEvidence="));
    if (!Evidence) return false;
    int64 Size = 0;
    const FString Hash = FileSHA256(*this, ActualPath, &Size);
    TestTrue(TEXT("实际 MP4 非空而非占位文件"), Size > 1024);
    double Value = 0;
    if (ReadNumber(*this, *Evidence, TEXT("file_size_bytes"), Value)) Near(*this, TEXT("当前运行 MP4 大小与原源复制证据一致"), double(Size), Value, 0);
    FString SourceHash, CopiedHash;
    TestTrue(TEXT("证据有原源 SHA256"), Evidence->TryGetStringField(TEXT("source_sha256"), SourceHash) && SourceHash.Len() == 64);
    TestTrue(TEXT("证据有复制后 SHA256"), Evidence->TryGetStringField(TEXT("copied_sha256"), CopiedHash) && CopiedHash.Len() == 64);
    TestEqual(TEXT("运行项目实际文件 SHA 等于原源"), Hash, SourceHash.ToLower());
    TestEqual(TEXT("原源与复制文件 SHA 一致，禁止转码冒充原源"), SourceHash.ToLower(), CopiedHash.ToLower());
    if (ReadNumber(*this, *Evidence, TEXT("ffmpeg_decode_exit_code"), Value)) Near(*this, TEXT("主代理外部 FFmpeg 完整解码退出码"), Value, 0, 0);
    FString Codec;
    TestTrue(TEXT("原源证据 video_codec=h264"), Evidence->TryGetStringField(TEXT("video_codec"), Codec) && Codec == TEXT("h264"));
    TestTrue(TEXT("原源证据 audio_codec=aac"), Evidence->TryGetStringField(TEXT("audio_codec"), Codec) && Codec == TEXT("aac"));
    const TCHAR* NumberFields[] = { TEXT("width"), TEXT("height"), TEXT("frame_rate"), TEXT("duration_seconds"), TEXT("audio_sample_rate"), TEXT("audio_channels") };
    const double Expected[] = { 1470, 630, 24, 15.07, 32000, 2 };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(NumberFields); ++Index)
        if (ReadNumber(*this, *Evidence, NumberFields[Index], Value)) Near(*this, NumberFields[Index], Value, Expected[Index], Index == 3 ? .05 : 1.e-5);
    AddInfo(TEXT("本例证明相对解析、文件 SHA 和 staging 配置；不声称已执行 packaged cook，真实解码由原生媒体用例另验。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaSavedCalibratedMotion,
    "CyberCafe.MediaAudio.SavedNPCCalibratedAnimationAndStartPose", Options)
bool FCafeMediaSavedCalibratedMotion::RunTest(const FString&)
{
    FSavedScene Scene(*this);
    if (!Scene.bReady) return false;
    ATutorialNPC* NPC = Scene.Director->NPCRef;
    if (!TestNotNull(TEXT("原 Director 仍指向保存 NPC"), NPC)
        || !TestNotNull(TEXT("原 NPC 喝饮组件"), NPC->CustomerDrink.Get())) return false;
    UCustomerDrinkMotion* Motion = NPC->CustomerDrink->Motion;
    UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, CalibratedAnimationPath);
    if (!TestNotNull(TEXT("保存 Motion 引用"), Motion) || !TestNotNull(TEXT("主代理须先导入并保存新作者校准动画"), Animation)) return false;
    TestEqual(TEXT("只更新原 Highball Motion，不切回旧杯动作"), Motion->GetPathName(), FString(MotionPath));
    TestTrue(TEXT("Motion 已真正 Assign 新动画，不仅在作者目录生成 FBX"), Motion->Animation == Animation);
    TestTrue(TEXT("保存校准 Motion 有效"), Motion->IsValidMotion());
    TestEqual(TEXT("保留耦合样本 211 帧"), Motion->CupSamples.Num(), 211);
    Near(*this, TEXT("耦合采样仍为 30Hz"), Motion->SampleRate, 30);
    Near(*this, TEXT("完整动作仍为 7 秒"), Motion->Duration(), 7);
    Near(*this, TEXT("新动画时长与杯轨道一致"), Animation->GetPlayLength(), Motion->Duration(), .001);
    Near(*this, TEXT("不以扩大拒绝阈值掩盖起点偏差"), Motion->MaxStartOffsetCM, 5);
    TestTrue(TEXT("Highball 支持缩放仍为 1.1"), Motion->SupportedCupScale.Equals(FVector(1.1), 1.e-5));
    TestEqual(TEXT("支持网格仍为原 Highball"), GetPathNameSafe(Motion->SupportedCupMesh),
        FString(TEXT("/Game/VRTemplate/MenuIngredients/Meshes/SM_Highball_Cup.SM_Highball_Cup")));
    TestTrue(TEXT("新动画与原 NPC 网格 Skeleton 相同"), NPC->GetMesh()->GetSkeletalMeshAsset()
        && NPC->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton() == Animation->GetSkeleton());
    Near(*this, TEXT("喝掉体积仍为 20mL"), Motion->SipVolumeML, 20);
    Near(*this, TEXT("SipStart 不变"), Motion->SipStart, 3.2);
    Near(*this, TEXT("SipEnd 不变"), Motion->SipEnd, 3.7);
    Near(*this, TEXT("GraspTime 不变"), Motion->GraspTime, 1.55);
    Near(*this, TEXT("ReleaseTime 不变"), Motion->ReleaseTime, 5.35);
    AActor* Spot = nullptr;
    for (const auto& Actor : Scene.World->PersistentLevel->Actors)
        if (IsValid(Actor.Get()) && Actor->GetFName() == FName(TEXT("BP_ServingSpot_C_1"))) Spot = Actor.Get();
    if (!TestNotNull(TEXT("原保存服务点存在，不由夹具移动"), Spot)) return false;
    const FVector Start = (Motion->Sample(0) * SavedTransform(NPC->GetMesh())).GetLocation();
    Near(*this, TEXT("保存时零杯轨道与真实原 Spot 对齐"), FVector::Distance(Start, SavedTransform(Spot->GetRootComponent()).GetLocation()), 0, .01);
    TestTrue(TEXT("新样本实际校准到 (-2,41,114.681...) cm"), Motion->Sample(0).GetLocation().Equals(FVector(-2, 41, 114.68106781), .01));
    if (Animation->GetSkeleton())
    {
        const FReferenceSkeleton& Skeleton = Animation->GetSkeleton()->GetReferenceSkeleton();
        const int32 HandIndex = Skeleton.FindBoneIndex(TEXT("hand_r"));
        if (!TestTrue(TEXT("新动画保留原 hand_r 骨骼"), HandIndex != INDEX_NONE)) return false;
        FTransform Poses[2] = { FTransform::Identity, FTransform::Identity };
        const double Times[] = { 0, 3.2 };
        for (int32 PoseIndex = 0; PoseIndex < 2; ++PoseIndex)
            for (int32 Bone = HandIndex; Bone != INDEX_NONE; Bone = Skeleton.GetParentIndex(Bone))
            {
                FTransform Local;
                Animation->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(Times[PoseIndex]), true);
                Poses[PoseIndex] = Poses[PoseIndex] * Local;
            }
        TestTrue(TEXT("已保存新动画有实际手部位移，不是仅改 Motion 样本"), FVector::Distance(Poses[0].GetLocation(), Poses[1].GetLocation()) > 5);
    }
    AddInfo(TEXT("本例只检查新动画绑定及保存时零对齐；原两项 MainMap 真实喝饮时钟/反馈回归仍由主代理原样运行。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaPreservedLayoutAndInput,
    "CyberCafe.MediaAudio.PreservedSavedLayoutGripAndMenuPointerInput", Options)
bool FCafeMediaPreservedLayoutAndInput::RunTest(const FString&)
{
    FSavedScene Scene(*this);
    if (!Scene.bReady) return false;
    TSharedPtr<FJsonObject> Baseline = ReadEvidence(*this, TEXT("CafeMediaBaseline="));
    TSharedPtr<FJsonObject> SourceBaseline = ReadEvidence(*this, TEXT("CafeMediaSourceBaseline="));
    if (!Baseline || !SourceBaseline) return false;
    const TSharedPtr<FJsonObject>* Actors = nullptr;
    if (!TestTrue(TEXT("原场景证据含 actors 对象"), Baseline->TryGetObjectField(TEXT("actors"), Actors) && Actors && Actors->IsValid())) return false;
    TestEqual(TEXT("使用本轮改前 313 个原 Actor 的布局证据"), (*Actors)->Values.Num(), 313);
    TMap<FString, AActor*> SavedActors;
    // 与 baseline 的 GetAllLevelActors 及现有保存地图测试使用相同编辑器筛选，不按内部 Actor 名放行。
    for (const auto& ActorPtr : Scene.World->PersistentLevel->Actors)
    {
        AActor* Actor = ActorPtr.Get();
        if (!IsValid(Actor) || !Actor->IsEditable() || !Actor->IsListedInSceneOutliner() || Actor->IsTemplate()
            || Actor->HasAnyFlags(RF_Transient) || Actor->IsA<AWorldSettings>() || FActorEditorUtils::IsABuilderBrush(Actor)) continue;
        SavedActors.Add(Actor->GetName(), Actor);
    }
    TestEqual(TEXT("保存地图编辑器 Actor 精确 313 个，不能放行任何额外布局 Actor"), SavedActors.Num(), 313);
    for (const auto& Pair : (*Actors)->Values)
    {
        AActor** Found = SavedActors.Find(Pair.Key);
        if (!TestTrue(Pair.Key + TEXT(" 原 Actor 仍存在，不能替代或删除"), Found != nullptr)) continue;
        const TSharedPtr<FJsonObject> Record = Pair.Value->AsObject();
        if (!TestTrue(TEXT("布局记录有效"), Record.IsValid())) continue;
        FString ClassPath, Label;
        TestTrue(TEXT("布局记录有原 class"), Record->TryGetStringField(TEXT("class"), ClassPath));
        TestTrue(TEXT("布局记录有原 label"), Record->TryGetStringField(TEXT("label"), Label));
        TestEqual(Pair.Key + TEXT(" 类不变"), (*Found)->GetClass()->GetPathName(), ClassPath);
        TestEqual(Pair.Key + TEXT(" label 不变"), (*Found)->GetActorLabel(), Label);
        const TArray<TSharedPtr<FJsonValue>>* Pose = nullptr;
        if (!TestTrue(TEXT("原 pose 有九个数值"), Record->TryGetArrayField(TEXT("pose"), Pose) && Pose && Pose->Num() == 9)) continue;
        double V[9] = {};
        bool bValid = true;
        for (int32 Index = 0; Index < 9; ++Index) bValid &= (*Pose)[Index]->TryGetNumber(V[Index]) && FMath::IsFinite(V[Index]);
        if (!TestTrue(TEXT("原 pose 数值有效"), bValid)) continue;
        const FTransform Expected(FRotator(V[3], V[4], V[5]), FVector(V[0], V[1], V[2]), FVector(V[6], V[7], V[8]));
        TestTrue(Pair.Key + TEXT(" 原 Root 序列化位置、旋转、缩放不变（容差 1e-5）"),
            SavedTransform((*Found)->GetRootComponent()).Equals(Expected, 1.e-5));
    }
    for (const auto& Pair : SavedActors)
        TestTrue(Pair.Key + TEXT(" 不新增电视代理/布局 Actor，精确匹配原名单"), (*Actors)->Values.Contains(Pair.Key));
    const TCHAR* ProtectedFiles[] = {
        TEXT("Source/CyberCafe/GrabComponent.cpp"), TEXT("Source/CyberCafe/GrabComponent.h"),
        TEXT("Source/CyberCafe/Character/VRPawn.cpp"), TEXT("Source/CyberCafe/Character/VRPawn.h"),
        TEXT("Source/CyberCafe/Character/MenuPointerComponent.cpp"), TEXT("Source/CyberCafe/Character/MenuPointerComponent.h")
    };
    for (const TCHAR* RelativeFile : ProtectedFiles)
    {
        FString ExpectedHash;
        if (!TestTrue(TEXT("改前证据含原 Grip/MenuPointer 输入文件 SHA"), SourceBaseline->TryGetStringField(RelativeFile, ExpectedHash))) continue;
        TestEqual(FString(RelativeFile) + TEXT(" 原输入源码字节不变"), FileSHA256(*this, FPaths::Combine(FPaths::ProjectDir(), RelativeFile)), ExpectedHash.ToLower());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaSavedGlobalMix,
    "CyberCafe.MediaAudio.SavedDirectorSingleMasterGlobalGainMix", Options)
bool FCafeMediaSavedGlobalMix::RunTest(const FString&)
{
    FSavedScene Scene(*this);
    if (!Scene.bReady) return false;
    if (!CheckMixAsset(*this, Scene.Music->GlobalGainMix, LoadMaster(*this))) return false;
    Near(*this, TEXT("保存 BGM Volume 未从 .22 改成 .44"), Scene.Music->Volume, .22);
    TestEqual(TEXT("原 BGM 资产不替换"), GetPathNameSafe(Scene.Music->Music), FString(MusicPath));
    USoundWave* Wave = Cast<USoundWave>(Scene.Music->Music);
    if (TestNotNull(TEXT("原 BGM SoundWave"), Wave)) TestTrue(TEXT("原 BGM 循环配置不变"), Wave->bLooping != 0);
    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UCafeSceneMusicComponent::StaticClass(), TEXT("GlobalGainMix"));
    if (TestNotNull(TEXT("Director SceneMusic 的 Mix 是硬引用 UPROPERTY"), Property))
    {
        TestFalse(TEXT("Mix 引用可保存和 cook，非 Transient"), Property->HasAnyPropertyFlags(CPF_Transient));
        TestTrue(TEXT("反射读取实际保存 Mix"), Property->GetObjectPropertyValue_InContainer(Scene.Music) == Scene.Music->GlobalGainMix);
    }
    TestNull(TEXT("原生 SceneMusic CDO 不全局启用 Mix"), GetDefault<UCafeSceneMusicComponent>()->GlobalGainMix.Get());
    const ATutorialDirector* Defaults = GetDefault<ATutorialDirector>();
    const UCafeSceneMusicComponent* DefaultMusic = Defaults->FindComponentByClass<UCafeSceneMusicComponent>();
    if (TestNotNull(TEXT("原生 Director CDO 的 SceneMusic"), DefaultMusic)) TestNull(TEXT("原生 Director 默认不污染其他场景"), DefaultMusic->GlobalGainMix.Get());
    TestFalse(TEXT("原保存实例尚未 Push"), Scene.Music->IsGlobalGainMixRequested());
    TestEqual(TEXT("原保存实例 Mix 启动次数为零"), Scene.Music->GetGlobalGainMixStartCount(), 0);
    return !HasAnyErrors();
}

// 通过真实设备公开 getter 观察所有原 Master 子类；不新造子类、不直接调用私有 ParseSoundClasses。
class FActualMixCommand : public IAutomationLatentCommand
{
    FAutomationTestBase& Test;
    TUniquePtr<FSavedScene> Scene;
    TUniquePtr<FGameWorld> Fixture;
    UCafeSceneMusicComponent* Music = nullptr;
    TArray<USoundClass*> Classes;
    FMixSnapshot Before;
    double Started = 0;
public:
    explicit FActualMixCommand(FAutomationTestBase& InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        if (!Fixture)
        {
            Scene = MakeUnique<FSavedScene>(Test);
            if (!Scene->bReady) return true;
            USoundClass* Master = LoadMaster(Test);
            if (!CheckMixAsset(Test, Scene->Music->GlobalGainMix, Master)) return true;
            Classes.Add(Master);
            for (int32 Index = 0; Index < Classes.Num(); ++Index)
                for (USoundClass* Child : Classes[Index]->ChildClasses) if (Child) Classes.AddUnique(Child);
            // 类增益插值依赖真实设备时钟；此例使用独立实时设备，避免非实时时钟停在零导致假失败。
            Fixture = MakeUnique<FGameWorld>(Test, false, false);
            if (!Fixture->bReady) return true;
            Fixture->Device->Update(true);
            Before = ReadMix(Fixture->Device.GetAudioDevice(), Scene->Music->GlobalGainMix, Classes);
            if (!Test.TestEqual(TEXT("独立设备开始时本 Mix 无活动引用"), Before.Active, uint32(0))) return true;
            Music = Fixture->SpawnMusic(Scene->Music);
            if (!Music) return true;
            Fixture->Begin();
            Test.TestEqual(TEXT("BeginPlay 恰好 Push 一次"), Music->GetGlobalGainMixStartCount(), 1);
            Test.TestTrue(TEXT("getter 返回实际已应用保存 Mix"), Music->GetGlobalAudioMixForTest() == Scene->Music->GlobalGainMix);
            UAudioComponent* First = Music->GetMusicAudioComponent();
            for (int32 Index = 0; Index < 10; ++Index) Music->StartMusic();
            Test.TestTrue(TEXT("重复 StartMusic 复用同一个源"), Music->GetMusicAudioComponent() == First);
            Test.TestEqual(TEXT("十次重复启动 BGM 请求计数仍为 1"), Music->GetMusicStartCount(), 1);
            Test.TestEqual(TEXT("十次重复启动 Mix 请求计数仍为 1"), Music->GetGlobalGainMixStartCount(), 1);
            CheckBGM(Test, Music);
            CheckRefs(Test, Fixture->Device.GetAudioDevice(), Scene->Music->GlobalGainMix, 1, TEXT("重复启动后"));
            Started = FPlatformTime::Seconds();
            return Test.HasAnyErrors();
        }
        Fixture->Device->Update(true);
        const FMixSnapshot After = ReadMix(Fixture->Device.GetAudioDevice(), Scene->Music->GlobalGainMix, Classes);
        bool bSettled = true;
        for (USoundClass* Class : Classes)
        {
            const FSoundClassProperties* A = Before.Classes.Find(Class);
            const FSoundClassProperties* B = After.Classes.Find(Class);
            bSettled &= A && B && FMath::Abs(B->Volume - A->Volume * 2) < 1.e-4;
        }
        if (!bSettled && FPlatformTime::Seconds() - Started < 8.0) return false;
        for (USoundClass* Class : Classes)
        {
            const FSoundClassProperties* A = Before.Classes.Find(Class);
            const FSoundClassProperties* B = After.Classes.Find(Class);
            if (!Test.TestTrue(Class->GetName() + TEXT(" 真实设备前后都有 SoundClassProperties"), A && B)) continue;
            Near(Test, Class->GetName() + TEXT(" 实际类音量仅变为两倍，子类不能四倍"), B->Volume, A->Volume * 2, 1.e-4);
            Near(Test, Class->GetName() + TEXT(" 实际类 Pitch 不变"), B->Pitch, A->Pitch, 1.e-4);
        }
        Test.AddInfo(FString::Printf(TEXT("实测原 Master 树的 %d 个真实类；BGM 源 .22，类增益 2。不是耳机可听或 PCM 渲染幅度证明。"), Classes.Num()));
        CheckRefs(Test, Fixture->Device.GetAudioDevice(), Scene->Music->GlobalGainMix, 1, TEXT("类增益稳定后"));
        Fixture->End();
        CheckRefs(Test, Fixture->Device.GetAudioDevice(), Scene->Music->GlobalGainMix, 0, TEXT("独立 World EndPlay 后"));
        return true;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaActualMasterAndBGM,
    "CyberCafe.MediaAudio.ActualDeviceMasterChildrenTimesTwoAndSingleBGM", Options)
bool FCafeMediaActualMasterAndBGM::RunTest(const FString&)
{
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FActualMixCommand>(*this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaMixStopEndPop,
    "CyberCafe.MediaAudio.StopMusicKeepsMixEndPlayPopsExactlyOnce", Options)
bool FCafeMediaMixStopEndPop::RunTest(const FString&)
{
    FSavedScene Scene(*this);
    if (!Scene.bReady) return false;
    FGameWorld Fixture(*this, false);
    if (!Fixture.bReady) return false;
    USoundMix* Mix = Scene.Music->GlobalGainMix;
    if (!CheckMixAsset(*this, Mix, LoadMaster(*this))) return false;
    UCafeSceneMusicComponent* Music = Fixture.SpawnMusic(Scene.Music);
    if (!Music) return false;
    // 独立设备上的第二个真实所有者，用引用计数证明结束只 Pop 自己的一次，而不是清空所有 Mix。
    UGameplayStatics::PushSoundMixModifier(Fixture.World, Mix);
    CheckRefs(*this, Fixture.Device.GetAudioDevice(), Mix, 1, TEXT("独立观察所有者 Push 后"));
    Fixture.Begin();
    CheckRefs(*this, Fixture.Device.GetAudioDevice(), Mix, 2, TEXT("SceneMusic BeginPlay 后"));
    Music->StopMusic();
    Music->StopMusic();
    TestFalse(TEXT("手停 BGM 撤销音乐请求"), Music->IsMusicRequested());
    TestTrue(TEXT("手停 BGM 不撤销全场景音效 Mix"), Music->IsGlobalGainMixRequested());
    CheckRefs(*this, Fixture.Device.GetAudioDevice(), Mix, 2, TEXT("重复 StopMusic 不 Pop"));
    Music->StartMusic();
    TestEqual(TEXT("恢复 BGM 不重复 Push"), Music->GetGlobalGainMixStartCount(), 1);
    CheckRefs(*this, Fixture.Device.GetAudioDevice(), Mix, 2, TEXT("恢复 BGM 后"));
    Fixture.End();
    TestFalse(TEXT("真实 World EndPlay 撤销 Mix 请求"), Music->IsGlobalGainMixRequested());
    TestNull(TEXT("真实 World EndPlay 清空实际应用指针"), Music->GetGlobalAudioMixForTest());
    CheckRefs(*this, Fixture.Device.GetAudioDevice(), Mix, 1, TEXT("EndPlay 只 Pop SceneMusic 的一次"));
    Music->DestroyComponent();
    CheckRefs(*this, Fixture.Device.GetAudioDevice(), Mix, 1, TEXT("随后 DestroyComponent 不重复 Pop"));
    UGameplayStatics::PopSoundMixModifier(Fixture.World, Mix);
    CheckRefs(*this, Fixture.Device.GetAudioDevice(), Mix, 0, TEXT("观察所有者独立 Pop 收尾"));
    return !HasAnyErrors();
}

void CheckTVSound(FAutomationTestBase& Test, UCafeTVPlaybackComponent* TV)
{
    UMediaSoundComponent* Sound = TV->GetTVAudioComponent();
    if (!Test.TestNotNull(TEXT("原生打开后创建实际 MediaSound"), Sound)) return;
    Test.TestTrue(TEXT("TV MediaSound 使用 mono，不以 stereo 伪造空间中心"), Sound->Channels == EMediaSoundChannels::Mono);
    Test.TestTrue(TEXT("MediaSound 直接挂在 TV 播放组件上"), Sound->GetAttachParent() == TV);
    Test.TestTrue(TEXT("MediaSound 本地变换为零，不重复加屏幕偏移"), Sound->GetRelativeTransform().Equals(FTransform::Identity, 1.e-5));
    Test.TestTrue(TEXT("实际音源世界位置等于原电视烘焙屏幕中心"),
        Sound->GetComponentLocation().Equals(TV->GetAttachParent()->GetComponentTransform().TransformPosition(TV->SoundOffset), .001));
    Test.TestTrue(TEXT("TV 启用空间化和衰减"), Sound->bAllowSpatialization && Sound->bOverrideAttenuation
        && Sound->AttenuationOverrides.bSpatialize && Sound->AttenuationOverrides.bAttenuate);
    Test.TestTrue(TEXT("TV 使用球形衰减"), Sound->AttenuationOverrides.AttenuationShape == EAttenuationShape::Sphere);
    Near(Test, TEXT("中心半径 60cm"), Sound->AttenuationOverrides.AttenuationShapeExtents.X, 60);
    Near(Test, TEXT("falloff 500cm"), Sound->AttenuationOverrides.FalloffDistance, 500);
    Test.TestTrue(TEXT("MediaSound 用真实保存播放器"), Sound->GetMediaPlayer() == TV->MediaPlayer);
    Test.TestTrue(TEXT("MediaSound 使用引擎默认类，纳入 Master"), Sound->SoundClass == GetDefault<UAudioSettings>()->GetDefaultSoundClass());
    UAudioComponent* Audio = Sound->GetAudioComponent();
    if (Test.TestNotNull(TEXT("Synth 内部实际音频组件"), Audio))
    {
        Near(Test, TEXT("TV 实际源音量 1，不能再局部乘 2"), Audio->VolumeMultiplier, 1);
        Near(Test, TEXT("TV 实际源 Pitch 保持 1"), Audio->PitchMultiplier, 1);
        Test.TestFalse(TEXT("Synth 音频源不叠播多实例"), Audio->bCanPlayMultipleInstances != 0);
    }
    TArray<UMediaSoundComponent*> Sources;
    TV->GetOwner()->GetComponents(Sources);
    Test.TestEqual(TEXT("原 TV 副本只有一个 MediaSound 源"), Sources.Num(), 1);
    Test.TestFalse(TEXT("播放器原生音频旁路关闭，没有双声源"), TV->MediaPlayer->NativeAudioOut);
}

bool CheckMediaPlayerClosedOrNoBackend(FAutomationTestBase& Test, UMediaPlayer* Player, const FString& Label)
{
    if (!Test.TestNotNull(Label + TEXT("：实际播放器存在"), Player)) return false;
    // UE5.6 facade 在没有 native Player 时 IsClosed 返回 false；只读观察，不 Close 用户预览来满足前置条件。
    if (Player->GetPlayerFacade()->GetPlayer().IsValid())
        return Test.TestTrue(Label + TEXT("：已有 native backend 必须 IsClosed"), Player->IsClosed());
    // fresh 状态或 Close 后 backend 被释放：必须确无 backend，且所有可观察的活动/错误状态为空。
    return Test.TestTrue(Label + TEXT("：无 native backend、PlayerName/Url 为空且无播放/准备/就绪/错误状态"),
        Player->GetPlayerName().IsNone() && Player->GetUrl().IsEmpty()
        && !Player->IsPlaying() && !Player->IsPreparing() && !Player->IsReady() && !Player->HasError());
}

// 只观察真正 BeginPlay/OpenSource 的原资产链；不广播假事件、不 Seek、不改速率、不推进解码时钟。
class FNativePlaybackCommand : public IAutomationLatentCommand
{
    FAutomationTestBase& Test;
    bool bCheckLoop;
    TUniquePtr<FSavedScene> Scene;
    TUniquePtr<FGameWorld> Fixture;
    UCafeTVPlaybackComponent* TV = nullptr;
    TSharedPtr<FMediaTextureSampleQueue, ESPMode::ThreadSafe> VideoQueue;
    struct FEnvelopeRecord
    {
        std::atomic<bool> bRecording{true};
        std::atomic<int32> EnvelopeEvents{0};
        std::atomic<float> NonzeroPeak{0.f};
    };
    TSharedRef<FEnvelopeRecord, ESPMode::ThreadSafe> EnvelopeRecord = MakeShared<FEnvelopeRecord, ESPMode::ThreadSafe>();
    TWeakObjectPtr<UAudioComponent> EnvelopeAudioComponent;
    FDelegateHandle EnvelopeHandle;
    TWeakObjectPtr<UMediaSoundComponent> InitialSound;
    bool bEnvelopeBoundBeforeStart = false;
    double Started = 0;
    double LastTick = 0;
    int32 VideoFrames = 0;
    int32 AudioRenderedTimes = 0;
    FTimespan LastAudioRenderedSampleTime = FTimespan::MinValue();
    FTimespan FirstFrameTime;
    FTimespan LastFrameTime;
    FTimespan PreviousTime;
    int32 ObservedRewinds = 0;
    int32 FramesAfterRewind = 0;
    bool bAdvanced = false;
    bool bCheckedOpened = false;
    bool bHasTime = false;
    bool bClosed = false;

    void TryBindAudioEnvelope()
    {
        if (EnvelopeHandle.IsValid() || bClosed || !TV) return;
        UMediaSoundComponent* Sound = TV->GetTVAudioComponent();
        UAudioComponent* Audio = IsValid(Sound) ? Sound->GetAudioComponent() : nullptr;
        // 只等待生产 Register/Initialize 创建的实际源；getter 不创建源，测试也不调用 Initialize/Start。
        if (!IsValid(Audio)) return;
        bEnvelopeBoundBeforeStart = TV->MediaOpenedCount == 0 && !Sound->IsActive() && !Audio->IsPlaying();
        InitialSound = Sound;
        EnvelopeAudioComponent = Audio;
        const uint64 AudioComponentID = Audio->GetAudioComponentID();
        const TSharedRef<FEnvelopeRecord, ESPMode::ThreadSafe> Record = EnvelopeRecord;
        // 引擎将渲染器包络派发到 GameThread；共享原子记录不捕获 this，解绑后的迟到任务不能访问夹具。
        EnvelopeHandle = Audio->OnAudioSingleEnvelopeValueNative.AddLambda(
            [Record, AudioComponentID](const UAudioComponent* SourceAudio, const USoundWave* PlayingSoundWave, float EnvelopeValue)
            {
                if (!Record->bRecording.load() || !SourceAudio || !PlayingSoundWave
                    || SourceAudio->GetAudioComponentID() != AudioComponentID
                    || SourceAudio->Sound.Get() != PlayingSoundWave || !FMath::IsFinite(EnvelopeValue)) return;
                float Peak = Record->NonzeroPeak.load();
                while (EnvelopeValue > Peak && !Record->NonzeroPeak.compare_exchange_weak(Peak, EnvelopeValue)) {}
                Record->EnvelopeEvents.fetch_add(1);
            });
    }

    FString Diagnostics() const
    {
        UMediaPlayer* Player = TV ? TV->MediaPlayer.Get() : nullptr;
        return FString::Printf(TEXT("原生媒体：Player=%s，Opened=%d，OpenFailed=%d，Frames=%d，EnvelopeEvents=%d，NonzeroPeak=%.9g，AudioRenderedTimes=%d，LastAudioRenderedSampleTime=%.6f，EnvelopeBoundBeforeStart=%d，Time=%.3f，Rewinds=%d，Info=%s，Stats=%s"),
            Player ? *Player->GetPlayerName().ToString() : TEXT("无"), TV ? TV->MediaOpenedCount : 0,
            TV ? TV->MediaOpenFailedCount : 0, VideoFrames, EnvelopeRecord->EnvelopeEvents.load(), EnvelopeRecord->NonzeroPeak.load(),
            AudioRenderedTimes, LastAudioRenderedSampleTime.GetTotalSeconds(), bEnvelopeBoundBeforeStart,
            Player ? Player->GetTime().GetTotalSeconds() : 0, ObservedRewinds,
            Player ? *Player->GetPlayerFacade()->GetInfo() : TEXT("无"),
            Player ? *Player->GetPlayerFacade()->GetStats() : TEXT("无"));
    }

    void Close()
    {
        // 先撤销监听，再触发生产 EndPlay/Stop；既不留下 this 引用，也不误删 Synth 自己的原生监听。
        EnvelopeRecord->bRecording.store(false);
        if (EnvelopeHandle.IsValid())
        {
            if (UAudioComponent* Audio = EnvelopeAudioComponent.Get()) Audio->OnAudioSingleEnvelopeValueNative.Remove(EnvelopeHandle);
            EnvelopeHandle.Reset();
        }
        if (bClosed || !TV) return;
        bClosed = true;
        const int32 Opens = TV->MediaOpenedCount;
        const int32 Rewinds = TV->LoopRewindCount;
        TWeakObjectPtr<UMediaSoundComponent> WeakSound(TV->GetTVAudioComponent());
        if (bCheckLoop)
        {
            Fixture->End();
            Test.TestTrue(TEXT("真实 World EndPlay 自己关闭 TV，不由手停代替"), TV->bLifecycleClosed);
            Test.TestNull(TEXT("真实 World EndPlay 自己销毁 TV 动态音源"), TV->GetTVAudioComponent());
        }
        TV->StopPlayback();
        TV->StopPlayback();
        Test.TestTrue(TEXT("重复 StopPlayback 收束生命周期"), TV->bLifecycleClosed);
        Test.TestNull(TEXT("停止后不残留动态 MediaSound"), TV->GetTVAudioComponent());
        Test.TestTrue(TEXT("停止后旧动态音源失效或未激活"), !WeakSound.IsValid() || !WeakSound->IsActive());
        Test.TestFalse(TEXT("停止后真实播放器不再播放"), TV->MediaPlayer->IsPlaying());
        CheckMediaPlayerClosedOrNoBackend(Test, TV->MediaPlayer.Get(), TEXT("停止后真实播放器关闭或精确无 backend"));
        Test.TestFalse(TEXT("停止后诊断 Tick 关闭"), TV->IsComponentTickEnabled());
        Fixture->End();
        Test.TestEqual(TEXT("EndPlay 不重新打开或制造打开通知"), TV->MediaOpenedCount, Opens);
        Test.TestEqual(TEXT("EndPlay 不制造回绕诊断"), TV->LoopRewindCount, Rewinds);
    }

public:
    FNativePlaybackCommand(FAutomationTestBase& InTest, bool bInCheckLoop) : Test(InTest), bCheckLoop(bInCheckLoop) {}
    virtual ~FNativePlaybackCommand() { Close(); }
    virtual bool Update() override
    {
        if (!Fixture)
        {
            Scene = MakeUnique<FSavedScene>(Test);
            if (!Scene->bReady) return true;
            UCafeTVPlaybackComponent* SavedTV = Scene->Playback;
            if (!Test.TestNotNull(TEXT("主代理保存播放器存在"), SavedTV->MediaPlayer.Get())
                || !Test.TestNotNull(TEXT("主代理保存纹理存在"), SavedTV->MediaTexture.Get())) return true;
            // 不打断用户当前资产预览，也不改用合成播放器绕过冲突。
            if (!CheckMediaPlayerClosedOrNoBackend(Test, SavedTV->MediaPlayer.Get(),
                TEXT("原播放器须已关闭或 fresh 无 backend；用户预览须由主代理隔离"))) return true;
            UFileMediaSource* Source = Cast<UFileMediaSource>(SavedTV->MediaSource);
            if (!Test.TestTrue(TEXT("原生打开前实际运行 FileSource.Validate 成功"), Source && Source->Validate())) return true;
            Fixture = MakeUnique<FGameWorld>(Test, true);
            if (!Fixture->bReady) return true;
            FActorSpawnParameters Params;
            Params.Template = Scene->TV;
            Params.ObjectFlags = RF_Transient;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FTransform Pose = SavedTransform(Scene->TV->GetRootComponent());
            AStaticMeshActor* Copy = Fixture->World->SpawnActor<AStaticMeshActor>(Scene->TV->GetClass(), Pose.GetLocation(), Pose.Rotator(), Params);
            if (!Test.TestNotNull(TEXT("从真实保存原 TV Template 生成独立副本"), Copy)) return true;
            TV = Copy->FindComponentByClass<UCafeTVPlaybackComponent>();
            if (!Test.TestNotNull(TEXT("Template 必须带保存播放组件，不现场补造替身"), TV)) return true;
            Test.TestTrue(TEXT("TV 组件归属独立副本，不操作原地图组件"), TV != SavedTV && TV->GetOwner() == Copy);
            Test.TestTrue(TEXT("TV 副本沿用原 Root Mesh"), Copy->GetStaticMeshComponent()->GetStaticMesh() == Scene->TV->GetStaticMeshComponent()->GetStaticMesh());
            Test.TestEqual(TEXT("副本仍保留五个材质槽"), Copy->GetStaticMeshComponent()->GetNumMaterials(), 5);
            Test.TestTrue(TEXT("副本 MediaPlayer 仍为实际保存 MP_TV_Loop"), TV->MediaPlayer == SavedTV->MediaPlayer);
            Test.TestTrue(TEXT("副本 Texture 仍为实际保存媒体纹理"), TV->MediaTexture == SavedTV->MediaTexture);
            Test.TestTrue(TEXT("副本组件仍直接挂原类型 Root"), TV->GetAttachParent() == Copy->GetRootComponent());
            if (Test.HasAnyErrors()) { TV = nullptr; return true; }
            VideoQueue = MakeShared<FMediaTextureSampleQueue, ESPMode::ThreadSafe>(16);
            TV->MediaPlayer->GetPlayerFacade()->AddVideoSampleSink(VideoQueue.ToSharedRef());
            Started = LastTick = FPlatformTime::Seconds();
            Fixture->Begin();
            // 异步 OnMediaOpened 尚未到达时，立即监听 Register 已创建的实际 AudioComponent。
            // 绝不注册 observer 音频 sink，避免占据只向 Sinks[0] 喂样本的 PrimaryAudioSink。
            TryBindAudioEnvelope();
            Test.TestTrue(TEXT("真实 BeginPlay 自动启动 TV 生命周期，无手动 Start 代替"), TV->HasBegunPlay() && !TV->bLifecycleClosed);
            if (Test.HasAnyErrors()) { Close(); return true; }
            return false;
        }
        if (!TV) return true;
        TryBindAudioEnvelope();
        const double Now = FPlatformTime::Seconds();
        const float Delta = float(FMath::Clamp(Now - LastTick, .001, .1));
        LastTick = Now;
        // 媒体模块由真正引擎帧 Tick；只 Tick 本独立 World 的组件，不重复 Tick facade 或改 GFrameCounter。
        Fixture->World->Tick(LEVELTICK_All, Delta);
        if (TV->MediaOpenFailedCount != 0 || TV->MediaPlayer->HasError() || TV->bLifecycleClosed)
        {
            Test.AddError(TEXT("WMF/真实播放器打开或播放失败，不 fake pass、不 skip。") + Diagnostics());
            Close();
            return true;
        }
        TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Frame;
        while (VideoQueue->Dequeue(Frame))
        {
            if (!Frame.IsValid()) continue;
            const FIntPoint Dimensions = Frame->GetOutputDim();
            const bool bPayload = Frame->GetBuffer() != nullptr || Frame->GetTexture() != nullptr;
            if (!Test.TestTrue(TEXT("真实视频样本含 CPU 像素或 GPU 纹理，不只轨道元数据"), bPayload)
                || !Test.TestTrue(TEXT("真实解码样本输出尺寸 1470x630"), Dimensions == FIntPoint(1470, 630)))
            {
                Close();
                return true;
            }
            if (VideoFrames == 0) FirstFrameTime = Frame->GetTime().Time;
            LastFrameTime = Frame->GetTime().Time;
            ++VideoFrames;
            if (ObservedRewinds > 0) ++FramesAfterRewind;
            Frame.Reset();
        }
        UMediaPlayer* Player = TV->MediaPlayer;
        // 只读由真实 MediaSound Generator 消费后写回的时间，不给 observer 或 facade 手填音频时钟。
        const FTimespan RenderedTime = Player->GetPlayerFacade()->GetLastAudioRenderedSampleTime();
        if (RenderedTime > FTimespan::Zero() && RenderedTime != LastAudioRenderedSampleTime) ++AudioRenderedTimes;
        LastAudioRenderedSampleTime = RenderedTime;
        if (TV->MediaOpenedCount > 0 && Player->IsPlaying())
        {
            const FTimespan Time = Player->GetTime();
            if (bHasTime)
            {
                if (Time > PreviousTime + FTimespan::FromMilliseconds(10)) bAdvanced = true;
                if (PreviousTime > Time + FTimespan::FromMilliseconds(100)) ++ObservedRewinds;
            }
            PreviousTime = Time;
            bHasTime = true;
            if (!bCheckedOpened)
            {
                bCheckedOpened = true;
                Test.TestEqual(TEXT("实际 OnMediaOpened 只触发一次"), TV->MediaOpenedCount, 1);
                Test.TestTrue(TEXT("实际 native factory 是 WmfMedia"), Player->GetPlayerName() == FName(TEXT("WmfMedia")));
                Test.TestTrue(TEXT("OnMediaOpened 后原生循环已启用"), Player->IsLooping());
                Test.TestEqual(TEXT("原源包含一个视频轨道"), Player->GetNumTracks(EMediaPlayerTrack::Video), 1);
                Test.TestEqual(TEXT("原源包含一个音频轨道"), Player->GetNumTracks(EMediaPlayerTrack::Audio), 1);
                const int32 VideoTrack = Player->GetSelectedTrack(EMediaPlayerTrack::Video);
                const int32 AudioTrack = Player->GetSelectedTrack(EMediaPlayerTrack::Audio);
                Test.TestTrue(TEXT("真实音视频轨道均已选择"), VideoTrack != INDEX_NONE && AudioTrack != INDEX_NONE);
                Test.TestTrue(TEXT("轨道元数据尺寸也一致，但不是解码证据"), Player->GetVideoTrackDimensions(VideoTrack, INDEX_NONE) == FIntPoint(1470, 630));
                Near(Test, TEXT("真实轨道帧率 24fps"), Player->GetVideoTrackFrameRate(VideoTrack, INDEX_NONE), 24, .01);
                Near(Test, TEXT("真实播放器时长 15.07秒"), Player->GetDuration().GetTotalSeconds(), 15.07, .05);
                Test.TestEqual(TEXT("源音频轨道声明 stereo；这是元数据而不是 PCM 样本检验"), Player->GetAudioTrackChannels(AudioTrack, INDEX_NONE), 2);
                Test.TestEqual(TEXT("源音频轨道声明 32000Hz；不代表 Mixer 输出 PCM 采样率"), Player->GetAudioTrackSampleRate(AudioTrack, INDEX_NONE), 32000);
                Test.TestTrue(TEXT("实际音源须在原生 Start 前已创建并绑定包络；不能创建替身或重启来补监听"),
                    EnvelopeHandle.IsValid() && bEnvelopeBoundBeforeStart);
                CheckTVSound(Test, TV);
                UMediaSoundComponent* Sound = TV->GetTVAudioComponent();
                UAudioComponent* Audio = IsValid(Sound) ? Sound->GetAudioComponent() : nullptr;
                Test.TestTrue(TEXT("包络监听属于同一个正在播放的生产 MediaSound 实际音源"),
                    Sound == InitialSound.Get() && Sound && Sound->IsActive() && Audio && Audio == EnvelopeAudioComponent.Get() && Audio->IsPlaying());
                for (int32 Index = 0; Index < 10; ++Index) Test.TestTrue(TEXT("重复 StartPlayback 接受但不能重开"), TV->StartPlayback());
                Test.TestTrue(TEXT("十次 StartPlayback 保留同一 MediaSound"), InitialSound.Get() == TV->GetTVAudioComponent());
                Test.TestEqual(TEXT("重复启动后 OnMediaOpened 仍为一次"), TV->MediaOpenedCount, 1);
                Test.TestTrue(TEXT("运行时只替换实际 TV 副本屏幕槽"),
                    TV->GetOwner()->FindComponentByClass<UStaticMeshComponent>()->GetMaterial(1) == TV->ScreenMaterial);
                Test.TestTrue(TEXT("原保存 TV 槽 1 仍为授权保存 ScreenMaterial，不被副本播放改写"),
                    Scene->TV->GetStaticMeshComponent()->GetMaterial(1) == Scene->Playback->ScreenMaterial);
                Test.TestEqual(TEXT("原保存 TV 槽 1 仍精确引用授权 M_CafeTVVideo 资产"),
                    GetPathNameSafe(Scene->TV->GetStaticMeshComponent()->GetMaterial(1)), FString(ScreenMaterialPath));
            }
        }
        if (Test.HasAnyErrors()) { Close(); return true; }
        const int32 EnvelopeEvents = EnvelopeRecord->EnvelopeEvents.load();
        const float NonzeroPeak = EnvelopeRecord->NonzeroPeak.load();
        const bool bAudioRendered = EnvelopeEvents > 3 && NonzeroPeak > 1.e-6f
            && AudioRenderedTimes > 0 && LastAudioRenderedSampleTime > FTimespan::Zero();
        const bool bDecoded = bCheckedOpened && bAdvanced && VideoFrames > 2 && bAudioRendered && LastFrameTime != FirstFrameTime;
        const bool bLooped = ObservedRewinds > 0 && TV->LoopRewindCount > 0 && FramesAfterRewind >= 2;
        if (bDecoded && (!bCheckLoop || bLooped))
        {
            Test.AddInfo(Diagnostics());
            Test.TestTrue(TEXT("同一实际 AudioComponent 持续收到超过三次原生渲染包络事件"), EnvelopeEvents > 3);
            Test.TestTrue(TEXT("真实音源包络峰值大于 1e-6，原 AAC 音轨不是静音"), NonzeroPeak > 1.e-6f);
            Test.TestTrue(TEXT("真实 MediaSound Generator 已消费音频并写回正的已渲染样本时间"),
                AudioRenderedTimes > 0 && LastAudioRenderedSampleTime > FTimespan::Zero());
            Test.AddInfo(TEXT("音频证据来自实际 Synth 内部 AudioComponent 的 Mixer 包络及 Generator 消费时钟；32000Hz/stereo 只检验源轨道声明，不冒充逐 PCM 样本或头显可听证明。"));
            Test.AddInfo(FString::Printf(TEXT("真实纹理当前尺寸 %dx%d；通过条件使用 native sample payload，不把纹理默认宽高冒充帧。"),
                TV->MediaTexture->GetWidth(), TV->MediaTexture->GetHeight()));
            Test.TestEqual(TEXT("自然循环不能重开文件或重新广播 Opened"), TV->MediaOpenedCount, 1);
            Test.TestTrue(TEXT("循环/播放一直复用同一空间 MediaSound"), TV->GetTVAudioComponent() == InitialSound.Get());
            if (bCheckLoop)
            {
                Test.TestTrue(TEXT("真实播放时间出现自然回绕，没有测试 Seek"), ObservedRewinds >= 1);
                Test.TestTrue(TEXT("组件自己的回绕诊断确实增加，不拿 EndReached 冒充"), TV->LoopRewindCount >= 1);
                Test.TestTrue(TEXT("回绕后继续输出真实新帧"), FramesAfterRewind >= 2);
            }
            Close();
            return true;
        }
        if (Now - Started > (bCheckLoop ? 45.0 : 20.0))
        {
            Test.AddError(TEXT("原生媒体等待超时：须有自然播放时钟、真实视频 payload、超过三次原生包络及非零峰值、正的 Generator 已渲染音频时间；检查 WMF、真实 Mixer 音源与媒体 Tick，不 fake pass、不 skip。") + Diagnostics());
            Close();
            return true;
        }
        return false;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaNativeDecodedPlayback,
    "CyberCafe.MediaAudio.NativeBeginPlayOpenedPlaybackTimeAndDecodedFrames", Options)
bool FCafeMediaNativeDecodedPlayback::RunTest(const FString&)
{
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FNativePlaybackCommand>(*this, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCafeMediaNaturalLoopLifecycle,
    "CyberCafe.MediaAudio.NativeNaturalLoopRewindDecodedFramesAndStopEndPlay", Options)
bool FCafeMediaNaturalLoopLifecycle::RunTest(const FString&)
{
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FNativePlaybackCommand>(*this, true));
    return true;
}
}
#endif
