// UE5.6 反馈回归测试：仅测试保存配置和独立 Game World，不代表原地图 PIE、HMD 或渲染音频验收。
// 本文件已编写但未编译、未运行；不创建、复制或保存任何素材，不修改其他测试或 Build.cs。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Coffee/Bean/DirectBeanJarActor.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "Coffee/Brew/HandDripperAssemblyActor.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Coffee/Brew/HotWaterSteamComponent.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "GrabComponent.h"
#include "ActorEditorUtils.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "AudioDeviceManager.h"
#include "CollisionQueryParams.h"
#include "CoreGlobals.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include <cmath>
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace CyberCafeCoffeeFeedbackTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double Tolerance = 1.e-4;
constexpr double BoundsTolerance = .01;
constexpr float FrameDt = .016f;
constexpr const TCHAR* JarPath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/Blueprints/BP_DirectBeanJar.BP_DirectBeanJar_C");
constexpr const TCHAR* LidPath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/Blueprints/BP_ScrewBeanJarLid.BP_ScrewBeanJarLid_C");
constexpr const TCHAR* GrinderPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Coffee/Grinder/BP_CoffeeGrinder.BP_CoffeeGrinder_C");
constexpr const TCHAR* DripperPath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandDripper.BP_HandDripper_C");
constexpr const TCHAR* AssemblyPath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandDripperWithStand.BP_HandDripperWithStand_C");
constexpr const TCHAR* KettlePath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandPourKettle.BP_HandPourKettle_C");
constexpr const TCHAR* CupPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup03.BP_Cup03_C");
constexpr const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
constexpr const TCHAR* SteamPath = TEXT("/Game/VRTemplate/CoffeeBrew/Particles/P_Steam_Pour_Feedback.P_Steam_Pour_Feedback");
constexpr const TCHAR* GroundsPath = TEXT("/Game/coffee_grinder_scene/CoffeeBean/GroundsV02/SM_Grounds_Filter_V02.SM_Grounds_Filter_V02");
constexpr const TCHAR* PaperPath = TEXT("/Game/coffee_grinder_scene/CoffeeBean/FilterSet/SM_ConeFilterPaper.SM_ConeFilterPaper");
constexpr const TCHAR* GrindWavePath = TEXT("/Game/coffee_grinder_scene/Sound/S_Grind.S_Grind");
constexpr const TCHAR* GrindCuePath = TEXT("/Game/VRTemplate/CoffeeBrew/Audio/SC_Grind_Feedback_Loop.SC_Grind_Feedback_Loop");

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected,
    double AllowedError = Tolerance)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::IsFinite(Expected) && FMath::Abs(Actual - Expected) <= AllowedError);
}

bool Alive(const AActor* Actor)
{
    return IsValid(Actor) && !Actor->IsActorBeingDestroyed();
}

struct FAssets
{
    UClass* Jar = LoadClass<ADirectBeanJarActor>(nullptr, JarPath);
    UClass* Lid = LoadClass<ABottleCapActor>(nullptr, LidPath);
    UClass* Grinder = LoadClass<ACoffeeGrinderActor>(nullptr, GrinderPath);
    UClass* Dripper = LoadClass<AHandDripperActor>(nullptr, DripperPath);
    UClass* Kettle = LoadClass<AHandPourKettleActor>(nullptr, KettlePath);
    UClass* Cup = LoadClass<ACupActor>(nullptr, CupPath);

    bool Check(FAutomationTestBase& Test) const
    {
        bool Good = Test.TestNotNull(TEXT("保存豆罐类；缺失不得使用原生兜底"), Jar);
        Good &= Test.TestNotNull(TEXT("保存拉拔盖类"), Lid);
        Good &= Test.TestNotNull(TEXT("保存磨豆机类"), Grinder);
        Good &= Test.TestNotNull(TEXT("保存滤杯类"), Dripper);
        Good &= Test.TestNotNull(TEXT("保存手冲壶类"), Kettle);
        Good &= Test.TestNotNull(TEXT("保存原 Cup03 类"), Cup);
        if (!Good) return false;
        Good &= Test.TestNotNull(TEXT("保存磨豆机 DrawerClass"), Grinder->GetDefaultObject<ACoffeeGrinderActor>()->DrawerClass.Get());
        Good &= Test.TestNotNull(TEXT("保存磨豆机 HandleClass"), Grinder->GetDefaultObject<ACoffeeGrinderActor>()->HandleClass.Get());
        return Good;
    }
};

// 不修改碰撞档案、业务库存、蒸汽模板或新功能开关。只隔离刚体模拟、非蒸汽 FX、HUD 和手部反馈。
void Quiet(AActor* Actor, bool bWithAudio = false)
{
    if (!Alive(Actor)) return;
    Actor->SetActorTickEnabled(false);
    TArray<UStaticMeshComponent*> Meshes;
    Actor->GetComponents(Meshes);
    for (auto* Mesh : Meshes) Mesh->SetSimulatePhysics(false);
    TArray<UNiagaraComponent*> FXs;
    Actor->GetComponents(FXs);
    for (auto* FX : FXs)
    {
        FX->SetAutoActivate(false);
        FX->DeactivateImmediate();
        FX->SetAsset(nullptr);
        FX->SetComponentTickEnabled(false);
    }
    if (!bWithAudio)
    {
        TArray<UAudioComponent*> Audio;
        Actor->GetComponents(Audio);
        for (auto* Component : Audio) Component->Stop();
    }
    TArray<UGrabComponent*> Grabs;
    Actor->GetComponents(Grabs);
    for (auto* Grab : Grabs)
    {
        Grab->bCaptureHand = false;
        Grab->bHideHandWhileHeld = false;
        Grab->OnGrabHapticEffect = nullptr;
        Grab->SetComponentTickEnabled(false);
    }
    if (auto* Liquid = Cast<ALiquidContainerActor>(Actor))
    {
        Liquid->LiquidFXTemplate = nullptr;
        Liquid->SplashEffectTemplate = nullptr;
        Liquid->bEnableSplash = false;
        Liquid->bEnableDecal = false;
    }
    if (auto* Cup = Cast<ACupActor>(Actor))
    {
        Cup->VolumeWidgetClass = nullptr;
        Cup->bFaceCamera = false;
        if (Cup->VolumeWidgetComp)
        {
            Cup->VolumeWidgetComp->SetWidgetClass(nullptr);
            Cup->VolumeWidgetComp->SetComponentTickEnabled(false);
        }
    }
}

// 音频夹具复用 PourAudioTests 的 UE5.6 原生设备流程，不借用主设备、不改全局声音开关。
struct FWorld
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    FAudioDeviceHandle Audio;
    bool bWithAudio = false;
    bool bReady = false;

    explicit FWorld(FAutomationTestBase& InTest, bool bAudio = false) : Test(InTest), bWithAudio(bAudio)
    {
        if (!Test.TestNotNull(TEXT("真实 Engine 上下文"), GEngine)) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(bAudio).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("独立 Game World"), World)) return;
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
        if (bAudio)
        {
            auto* Manager = FAudioDeviceManager::Get();
            if (!Test.TestNotNull(TEXT("音频设备管理器；运行须 -AudioMixer 且不带 -nosound"), Manager)) return;
            FAudioDeviceParams Params;
            Params.Scope = EAudioDeviceScope::Unique;
            Params.bIsNonRealtime = true;
            Params.AssociatedWorld = World;
            Audio = Manager->RequestAudioDevice(Params);
            if (!Test.TestNotNull(TEXT("独立非实时设备必须创建成功，失败不跳过"), Audio.GetAudioDevice())) return;
            if (!Test.TestTrue(TEXT("设备确为非实时，不向 HMD 硬件输出"), Audio->IsNonRealtime())) return;
            World->SetAudioDevice(Audio);
            if (!Test.TestTrue(TEXT("真实 World 绑定本夹具设备并允许播放"),
                World->AllowAudioPlayback() && World->GetAudioDeviceRaw() == Audio.GetAudioDevice())) return;
        }
        else if (!Test.TestFalse(TEXT("非音频夹具不启用音频"), World->AllowAudioPlayback())) return;
        bReady = Test.TestNotNull(TEXT("真实液体账本 subsystem"), Transfer());
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

    template <typename T>
    T* Spawn(UClass* Class, const FVector& Location = FVector::ZeroVector)
    {
        if (!bReady || !Test.TestNotNull(TEXT("真实待生成类"), Class)) return nullptr;
        const FTransform Pose(FQuat::Identity, Location, Class->GetDefaultObject<AActor>()->GetActorScale3D());
        auto* Actor = World->SpawnActorDeferred<T>(Class, Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!Test.TestNotNull(TEXT("真实 deferred 生成"), Actor)) return nullptr;
        Quiet(Actor, bWithAudio);
        Actor->FinishSpawning(Pose);
        Quiet(Actor, bWithAudio);
        Actor->SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
        Test.TestTrue(TEXT("实例精确使用所请求的保存类"), Actor->GetClass() == Class);
        return Actor;
    }

    bool Begin()
    {
        if (!bReady) return false;
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        for (TActorIterator<AActor> It(World); It; ++It) Quiet(*It, bWithAudio);
        return Test.TestTrue(TEXT("真实 World 已 BeginPlay"), World->HasBegunPlay());
    }

    ULiquidTransferSubsystem* Transfer() const
    {
        return World ? World->GetSubsystem<ULiquidTransferSubsystem>() : nullptr;
    }

    void Tick(float Dt)
    {
        ++GFrameCounter;
        World->Tick(LEVELTICK_All, Dt);
    }

    UMotionControllerComponent* Hand(const FTransform& Pose, EControllerHand Side = EControllerHand::Left)
    {
        auto* Pawn = World->SpawnActor<APawn>();
        if (!Test.TestNotNull(TEXT("真实手柄 Pawn"), Pawn)) return nullptr;
        auto* MC = NewObject<UMotionControllerComponent>(Pawn);
        Pawn->SetRootComponent(MC);
        Pawn->AddInstanceComponent(MC);
        MC->SetTrackingSource(Side);
        MC->RegisterComponent();
        MC->SetComponentTickEnabled(false);
        Pawn->SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
        return MC;
    }

    AActor* Blocker(const FVector& Center)
    {
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!Test.TestNotNull(TEXT("真实遮挡 Cube"), Mesh)) return nullptr;
        auto* Actor = World->SpawnActor<AActor>();
        if (!Test.TestNotNull(TEXT("独立遮挡 Actor"), Actor)) return nullptr;
        auto* Component = NewObject<UStaticMeshComponent>(Actor);
        Actor->SetRootComponent(Component);
        Actor->AddInstanceComponent(Component);
        Component->SetStaticMesh(Mesh);
        Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Component->SetCollisionObjectType(ECC_WorldStatic);
        Component->SetCollisionResponseToAllChannels(ECR_Block);
        Component->RegisterComponent();
        Component->SetWorldLocation(Center);
        Component->SetWorldScale3D(FVector(.1, .1, .02));
        return Actor;
    }
};

void Balance(FAutomationTestBase& Test, const FWorld& F)
{
    const auto B = F.Transfer()->GetBalance();
    Near(Test, TEXT("毫升账本 ErrorML 为零"), B.ErrorML, 0);
    Near(Test, TEXT("初始加注入等于库存、在途、洒漏和移除"), B.InitialML + B.InjectedML,
        B.ContainerML + B.InFlightML + B.SpilledML + B.RemovedML);
    Test.TestTrue(TEXT("账本各体积有限且非负"), FMath::IsFinite(B.InitialML) && B.InitialML >= 0
        && FMath::IsFinite(B.InjectedML) && B.InjectedML >= 0 && FMath::IsFinite(B.RemovedML) && B.RemovedML >= 0
        && FMath::IsFinite(B.ContainerML) && B.ContainerML >= 0 && FMath::IsFinite(B.InFlightML) && B.InFlightML >= 0
        && FMath::IsFinite(B.SpilledML) && B.SpilledML >= 0);
}

FTransform Socket(const ADirectBeanJarActor* Jar)
{
    return Jar->ContainerMesh->GetSocketTransform(Jar->CapSocketName, RTS_World);
}

bool PullOpen(FAutomationTestBase& Test, FWorld& F, ADirectBeanJarActor* Jar,
    UMotionControllerComponent*& Hand)
{
    auto* Cap = Jar->CapActor.Get();
    if (!Test.TestNotNull(TEXT("BeginPlay 真正生成保存盖"), Cap)
        || !Test.TestNotNull(TEXT("盖子的真实 GrabComp"), Cap->GrabComp.Get())) return false;
    if (!Test.TestFalse(TEXT("实例继承保存的 direct-pull 配置，测试不切开关"), Cap->bRequireScrewTwist)) return false;
    if (!Test.TestTrue(TEXT("真实Grab类型仍为Custom且不捕获手部网格"),
        Cap->GrabComp->GrabType == EGrabType::Custom && !Cap->GrabComp->bCaptureHand)) return false;
    Hand = F.Hand(Socket(Jar));
    if (!Hand || !Test.TestTrue(TEXT("真实 Custom TryGrab 持有盖"), Cap->GrabComp->TryGrab(Hand))) return false;
    Test.TestTrue(TEXT("盖仍在原口且持有真实 controller"), Cap->bIsAttached && Jar->bIsCapped
        && Cap->GrabComp->IsHeld() && Cap->GrabComp->GetHoldingController() == Hand);
    const FTransform Mouth = Socket(Jar);
    Test.TestTrue(TEXT("保存拉拔阈值允许4cm手部位移"), Cap->DetachPullDistance > 0 && Cap->DetachPullDistance <= 4);
    Hand->SetWorldTransform(FTransform(Mouth.GetRotation(), Mouth.GetLocation()
        + Mouth.TransformVectorNoScale(FVector(0, 0, 4))), false, nullptr, ETeleportType::TeleportPhysics);
    // 直接运行生产 Tick 的拉拔分支；不调用 DetachFromBottle、SampleScrewHandPose 或伪造 IsTracked。
    Cap->Tick(.01f);
    Near(Test, TEXT("拉拔路径没有累计旋角"), Cap->TwistProgress, 0);
    return Test.TestTrue(TEXT("实际开盖且罐体和盖归属均保留"), !Cap->bIsAttached && !Jar->bIsCapped
        && Alive(Jar) && Alive(Cap) && Cap->OwnerBottle.Get() == Jar && Cap->GrabComp->IsHeld());
}

bool ReleaseLidAway(FAutomationTestBase& Test, ADirectBeanJarActor* Jar)
{
    auto* Cap = Jar->CapActor.Get();
    Cap->SetActorLocation(Socket(Jar).GetLocation() + FVector(0, 100, 0), false, nullptr, ETeleportType::TeleportPhysics);
    const bool Released = Test.TestTrue(TEXT("真实 TryRelease 在远处松盖"), Cap->GrabComp->TryRelease());
    Quiet(Cap);
    return Released && Test.TestFalse(TEXT("远处释放不误回盖"), Cap->bIsAttached);
}

FVector BeanStart(const ADirectBeanJarActor* Jar)
{
    const FTransform Body = Jar->ContainerMesh->GetComponentTransform();
    const FVector Up = Body.GetUnitAxis(EAxis::Z);
    const FVector RimDown = (FVector::DownVector - Up * FVector::DotProduct(FVector::DownVector, Up)).GetSafeNormal();
    const FVector Scale = Body.GetScale3D().GetAbs();
    return Body.TransformPosition(Jar->MouthLocal) + RimDown * Jar->OpeningRadiusCM * FMath::Min(Scale.X, Scale.Y);
}

void AimBeans(ADirectBeanJarActor* Jar, const FVector& Entry, double Height)
{
    Jar->SetActorRotation(FRotator(120, 0, 0), ETeleportType::TeleportPhysics);
    const FVector Direction = (Jar->ContainerMesh->GetUpVector() * 8 + FVector::DownVector * 20).GetSafeNormal();
    const FVector WantedStart = Entry - Direction * (Height / -Direction.Z);
    Jar->SetActorLocation(Jar->GetActorLocation() + WantedStart - BeanStart(Jar), false, nullptr, ETeleportType::TeleportPhysics);
}

void AimGrounds(AGrinderDrawerActor* Drawer, const FVector& Entry, double Height = 20, double Side = 0,
    double Degrees = -65)
{
    Drawer->SetActorRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Degrees)), ETeleportType::TeleportPhysics);
    const FVector WantedLip = Entry + FVector(0, Side, Height);
    Drawer->SetActorLocation(Drawer->GetActorLocation() + WantedLip - Drawer->GroundPourPoint->GetComponentLocation(),
        false, nullptr, ETeleportType::TeleportPhysics);
}

void NoPowder(FAutomationTestBase& Test, AGrinderDrawerActor* Drawer)
{
    if (!Test.TestNotNull(TEXT("实际 PowderStream 组件"), Drawer->PowderStream.Get())) return;
    Drawer->UpdatePowderStream(FrameDt);
    Test.TestEqual(TEXT("直接调用表现更新仍不生成粉流实例"), Drawer->PowderStream->GetInstanceCount(), 0);
    Test.TestFalse(TEXT("粉流不可见"), Drawer->PowderStream->IsVisible());
    Test.TestTrue(TEXT("粉流 HiddenInGame"), Drawer->PowderStream->bHiddenInGame != 0);
}

bool CheckJarConfig(FAutomationTestBase& Test, const ADirectBeanJarActor* Jar, const UClass* LidClass)
{
    if (!Test.TestNotNull(TEXT("保存豆罐"), Jar) || !Test.TestNotNull(TEXT("保存 BeanStream"), Jar->BeanStream.Get())) return false;
    Near(Test, TEXT("初始豆150g"), Jar->InitialBeanGrams, 150);
    Near(Test, TEXT("容量仍150g"), Jar->BeanCapacityGrams, 150);
    Near(Test, TEXT("出豆率仍5g/s"), Jar->BeanPourRateGramsPerSecond, 5);
    Near(Test, TEXT("倾倒阈值仍65度"), Jar->PourBeanTiltDegrees, 65);
    Near(Test, TEXT("射程仍40cm"), Jar->MaxBeanTravelCM, 40);
    Near(Test, TEXT("库存不是液体"), Jar->GetPreciseVolumeML(), 0);
    Test.TestTrue(TEXT("零液体且保持对应盖类"), !Jar->SupportsLiquidInventory() && !Jar->bEnablePourAudio
        && !Jar->bCanPour && !Jar->bAcceptLiquidFromOthers && Jar->CapClass.Get() == LidClass);
    Test.TestTrue(TEXT("BeanStream 保存局部缩放精确 .4/.4/.4"), Jar->BeanStream->GetRelativeScale3D().Equals(FVector(.4), Tolerance));
    Test.TestTrue(TEXT("BeanStream 保存 NoCollision/无物理/无重叠"),
        Jar->BeanStream->GetCollisionEnabled() == ECollisionEnabled::NoCollision
        && !Jar->BeanStream->IsSimulatingPhysics() && !Jar->BeanStream->GetGenerateOverlapEvents());
    return !Test.HasAnyErrors();
}

bool CheckDripperConfig(FAutomationTestBase& Test, const AHandDripperActor* Dripper)
{
    if (!Test.TestNotNull(TEXT("保存滤杯"), Dripper)) return false;
    Test.TestTrue(TEXT("保存滤杯 opt-in 半高粉床，不由测试赋值"), Dripper->bUseHalfPaperGroundBed);
    Near(Test, TEXT("视觉满量15g，不改变真实20g容量"), Dripper->VisualFullGroundsGrams, 15);
    Near(Test, TEXT("真实滤杯容量仍20g"), Dripper->MaxGroundsGrams, 20);
    Near(Test, TEXT("真实产率仍16mL/g"), Dripper->CoffeeYieldMLPerGram, 16);
    Test.TestTrue(TEXT("保存校准位置(0,0,1.35)"), Dripper->CalibratedGroundBedLocation.Equals(FVector(0, 0, 1.35), Tolerance));
    Test.TestTrue(TEXT("保存校准缩放使用指定值"), Dripper->CalibratedGroundBedScale.Equals(FVector(.6935438, .6935438, .6693654), Tolerance));
    if (!Test.TestNotNull(TEXT("原 GroundBed 组件"), Dripper->GroundBed.Get())
        || !Test.TestNotNull(TEXT("原 PaperMesh 组件"), Dripper->PaperMesh.Get())) return false;
    Test.TestTrue(TEXT("粉床使用原 GroundV02，不复制素材"), Dripper->GroundBed->GetStaticMesh()
        && Dripper->GroundBed->GetStaticMesh()->GetPathName() == GroundsPath);
    Test.TestTrue(TEXT("滤纸使用原素材"), Dripper->PaperMesh->GetStaticMesh()
        && Dripper->PaperMesh->GetStaticMesh()->GetPathName() == PaperPath);
    Test.TestTrue(TEXT("原组件直接附属真实 ContainerMesh"), Dripper->GroundBed->GetAttachParent() == Dripper->ContainerMesh.Get());
    Test.TestTrue(TEXT("粉床和纸保持仅视觉 NoCollision"),
        Dripper->GroundBed->GetCollisionEnabled() == ECollisionEnabled::NoCollision
        && Dripper->PaperMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    return !Test.HasAnyErrors();
}

bool CheckSteamConfig(FAutomationTestBase& Test, const AHandPourKettleActor* Kettle)
{
    const auto* Steam = Kettle ? Kettle->GetHotWaterSteamComponent() : nullptr;
    if (!Test.TestNotNull(TEXT("保存壶自带蒸汽组件"), Steam)) return false;
    Test.TestTrue(TEXT("保存蒸汽引用原 Cascade 模板"), Steam->SteamSystem && Steam->SteamSystem->GetPathName() == SteamPath);
    Test.TestTrue(TEXT("保存蒸汽尺寸大于旧.12并至少.4；不猜最终.45或.6"),
        FMath::IsFinite(Steam->SteamScale) && Steam->SteamScale > .12f && Steam->SteamScale >= .4f);
    Test.TestTrue(TEXT("保存落点保留时间长于旧.18秒"), FMath::IsFinite(Steam->ImpactHoldSeconds) && Steam->ImpactHoldSeconds > .18f);
    Test.AddInfo(FString::Printf(TEXT("实读保存 SteamScale=%.9g，ImpactHoldSeconds=%.9g；不是像素或云尺寸实测"),
        Steam->SteamScale, Steam->ImpactHoldSeconds));
    return !Test.HasAnyErrors();
}

USoundCue* CheckCue(FAutomationTestBase& Test)
{
    auto* Wave = LoadObject<USoundWave>(nullptr, GrindWavePath);
    if (!Test.TestNotNull(TEXT("原始 S_Grind SoundWave"), Wave)) return nullptr;
    const bool DirtyBefore = Wave->GetOutermost()->IsDirty();
    const bool LoopBefore = Wave->bLooping != 0;
    const float VolumeBefore = Wave->Volume;
    auto* Cue = LoadObject<USoundCue>(nullptr, GrindCuePath);
    if (!Test.TestNotNull(TEXT("精确路径已保存反馈 SoundCue，测试不创建"), Cue)) return nullptr;
    auto* Player = Cast<USoundNodeWavePlayer>(Cue->FirstNode.Get());
    if (!Test.TestNotNull(TEXT("Cue 根为原生 WavePlayer"), Player)) return nullptr;
    Test.TestTrue(TEXT("唯一循环 WavePlayer 属于本 Cue，无替代声音子图"), Player->GetOuter() == Cue
        && Player->bLooping && Player->ChildNodes.IsEmpty() && Cue->AllNodes.Num() == 1 && Cue->AllNodes[0] == Player);
    // UE5.6 Serialize 的硬声音引用对应 GetSoundWave；不调用 LoadAsset/SetSoundWave 补造引用。
    Test.TestTrue(TEXT("已加载硬引用精确指向原 S_Grind 而不是生成的新波形"), Player->GetSoundWave() == Wave);
    Test.TestTrue(TEXT("原生策略要求硬引用参与保存/Cook"), Player->ShouldHardReferenceAsset(nullptr));
    const FSoftObjectProperty* Soft = FindFProperty<FSoftObjectProperty>(USoundNodeWavePlayer::StaticClass(), TEXT("SoundWaveAssetPtr"));
    if (Test.TestNotNull(TEXT("原生 WavePlayer 的真实软引用属性可只读解析"), Soft))
        Test.TestEqual(TEXT("软路径也指向原 S_Grind"), Soft->GetPropertyValue_InContainer(Player).ToSoftObjectPath().ToString(), FString(GrindWavePath));
    Near(Test, TEXT("新 Cue 音量 .65；不改原 Wave 音量"), Cue->VolumeMultiplier, .65);
    Near(Test, TEXT("新 Cue 音高为1"), Cue->PitchMultiplier, 1);
    Test.TestTrue(TEXT("Cue 保留原声音 Class"), Cue->SoundClassObject == Wave->SoundClassObject);
    Near(Test, TEXT("检查 Cue 不修改原 Wave 音量"), Wave->Volume, VolumeBefore);
    Test.TestEqual(TEXT("检查 Cue 不修改原 Wave looping"), Wave->bLooping != 0, LoopBefore);
    Test.TestEqual(TEXT("检查 Cue 不改变原 Wave package dirty"), Wave->GetOutermost()->IsDirty(), DirtyBefore);
    return Test.HasAnyErrors() ? nullptr : Cue;
}

// 无 MeshDescription 依赖时使用原网格 AABB 的八角点；不宣称是逐三角表面或滤纸内壁验收。
FBox SourceBoundsInBody(const UStaticMeshComponent* Component, const UStaticMeshComponent* Body)
{
    const FBox Source = Component->GetStaticMesh()->GetBoundingBox();
    FBox Result(ForceInit);
    for (int32 Corner = 0; Corner < 8; ++Corner)
    {
        const FVector Point(Corner & 1 ? Source.Max.X : Source.Min.X, Corner & 2 ? Source.Max.Y : Source.Min.Y,
            Corner & 4 ? Source.Max.Z : Source.Min.Z);
        Result += Body->GetComponentTransform().InverseTransformPosition(Component->GetComponentTransform().TransformPosition(Point));
    }
    return Result;
}

void CheckHalfHeight(FAutomationTestBase& Test, const AHandDripperActor* Dripper)
{
    const FBox Bed = SourceBoundsInBody(Dripper->GroundBed.Get(), Dripper->ContainerMesh.Get());
    const FBox Paper = SourceBoundsInBody(Dripper->PaperMesh.Get(), Dripper->ContainerMesh.Get());
    Near(Test, TEXT("实际纸 AABB 底为.8cm"), Paper.Min.Z, .8, BoundsTolerance);
    Near(Test, TEXT("实际纸 AABB 顶为7.1cm"), Paper.Max.Z, 7.1, BoundsTolerance);
    Near(Test, TEXT("实际粉床顶为3.95cm"), Bed.Max.Z, 3.95, BoundsTolerance);
    Near(Test, TEXT("粉床底固定在1.35cm"), Bed.Min.Z, 1.35, BoundsTolerance);
    const double Half = (Paper.Min.Z + Paper.Max.Z) * .5;
    Test.TestTrue(TEXT("15到20g表面不高于滤纸半高，容差.01cm"), Bed.Max.Z <= Half + BoundsTolerance);
    Test.TestTrue(TEXT("粉床 AABB 的中心和 XY 范围仍位于纸的整体 AABB 内；非内壁三角判定"),
        FMath::Abs(Bed.GetCenter().X - Paper.GetCenter().X) <= BoundsTolerance
        && FMath::Abs(Bed.GetCenter().Y - Paper.GetCenter().Y) <= BoundsTolerance
        && Bed.Min.X >= Paper.Min.X - BoundsTolerance && Bed.Max.X <= Paper.Max.X + BoundsTolerance
        && Bed.Min.Y >= Paper.Min.Y - BoundsTolerance && Bed.Max.Y <= Paper.Max.Y + BoundsTolerance);
}

void CheckWetnessMaterial(FAutomationTestBase& Test, const AHandDripperActor* Dripper)
{
    int32 MIDCount = 0;
    for (int32 Index = 0; Index < Dripper->GroundBed->GetNumMaterials(); ++Index)
    {
        auto* MID = Cast<UMaterialInstanceDynamic>(Dripper->GroundBed->GetMaterial(Index));
        if (!Test.TestNotNull(TEXT("真实BeginPlay为原粉床材质创建MID"), MID)) continue;
        ++MIDCount;
        Near(Test, TEXT("真实材质Wetness与权威字段一致，不由测试写参数"),
            MID->K2_GetScalarParameterValue(TEXT("Wetness")), Dripper->Wetness);
    }
    Test.TestTrue(TEXT("粉床至少有一个实际MID，不用空材质数组跳过验证"), MIDCount > 0);
}

bool QueryBody(FAutomationTestBase& Test, UStaticMeshComponent* Component)
{
    if (!Test.TestNotNull(TEXT("原查询网格组件"), Component)
        || !Test.TestNotNull(TEXT("原网格资产"), Component->GetStaticMesh().Get())) return false;
    auto* Setup = Component->GetStaticMesh()->GetBodySetup();
    if (!Test.TestNotNull(TEXT("原 BodySetup"), Setup)) return false;
    // 仅创建内存查询形状；不保存、改碰撞档案或显式触发任何资产/UE编译。
    Setup->CreatePhysicsMeshes();
    Component->SetSimulatePhysics(false);
    Component->RecreatePhysicsState();
    return Test.TestTrue(TEXT("真实查询刚体有效，无模拟"), Component->IsRegistered() && Component->IsQueryCollisionEnabled()
        && Component->GetBodyInstance() && Component->GetBodyInstance()->IsValidBodyInstance()
        && !Component->IsSimulatingPhysics());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackSavedSixChangesStrict,
    "CyberCafe.CoffeeFeedback.SavedSixChangesStrict", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackSavedSixChangesStrict::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    auto* Map = LoadObject<UWorld>(nullptr, MapPath);
    if (!TestNotNull(TEXT("只读保存 L_Cafe"), Map) || !TestNotNull(TEXT("保存 PersistentLevel"), Map->PersistentLevel.Get())) return false;
    TestFalse(TEXT("不启动保存世界 PIE"), Map->HasBegunPlay());
    const bool DirtyBefore = Map->GetOutermost()->IsDirty();
    struct FSnapshot { AActor* Actor; UClass* Class; FTransform Pose; };
    TArray<FSnapshot> Before;
    ACoffeeGrinderActor* SavedGrinder = nullptr;
    auto* AssemblyClass = LoadClass<AHandDripperAssemblyActor>(nullptr, AssemblyPath);
    if (!TestNotNull(TEXT("当前保存组合BP必须存在，不回退旧滤杯"), AssemblyClass)) return false;
    TestTrue(TEXT("指定新BP直接继承组合原生类"), AssemblyClass->GetSuperClass() == AHandDripperAssemblyActor::StaticClass());
    int32 Listed = 0, Jars = 0, Grinders = 0, Drippers = 0, Kettles = 0, Managers = 0, OldStands = 0;
    for (const auto& Entry : Map->PersistentLevel->Actors)
    {
        AActor* Actor = Entry.Get();
        if (!Alive(Actor)) continue;
        Before.Add({Actor, Actor->GetClass(), Actor->GetActorTransform()});
        if (Actor->IsEditable() && Actor->IsListedInSceneOutliner() && !Actor->IsTemplate()
            && !Actor->HasAnyFlags(RF_Transient) && !Actor->IsA<AWorldSettings>() && !FActorEditorUtils::IsABuilderBrush(Actor)) ++Listed;
        if (auto* Jar = Cast<ADirectBeanJarActor>(Actor)) { ++Jars; CheckJarConfig(*this, Jar, A.Lid); }
        if (auto* G = Cast<ACoffeeGrinderActor>(Actor))
        {
            ++Grinders;
            SavedGrinder = G;
            TestTrue(TEXT("保存地图磨豆机仍为原类"), G->GetClass() == A.Grinder);
        }
        OldStands += Actor->GetName() == TEXT("HandDripperStand_Practice");
        if (auto* D = Cast<AHandDripperActor>(Actor))
        {
            ++Drippers; CheckDripperConfig(*this, D);
            TestEqual(TEXT("当前滤杯保留源名称"), D->GetName(), FString(TEXT("HandDripper_Practice")));
            TestTrue(TEXT("当前地图精确新BP且实际IsA组合原生类"), D->GetClass() == AssemblyClass && D->IsA<AHandDripperAssemblyActor>());
            auto* Assembly = Cast<AHandDripperAssemblyActor>(D);
            if (!TestNotNull(TEXT("当前实际组合滤杯，不用任意子类放宽断言"), Assembly)) return false;
            const TArray<UStaticMeshComponent*> Pieces = {Assembly->StandLeftLeg.Get(), Assembly->StandRightLeg.Get(),
                Assembly->StandLeftShelf.Get(), Assembly->StandRightShelf.Get()};
            for (auto* Piece : Pieces)
            {
                if (!TestNotNull(TEXT("当前组合体四个继承支架组件齐全"), Piece)) return false;
                TestTrue(TEXT("四支架同属滤杯并直接挂根且保存可见"), Piece->GetOwner() == D
                    && Piece->GetAttachParent() == D->ContainerMesh.Get() && Piece->IsVisible() && !Piece->bHiddenInGame);
            }
            TestTrue(TEXT("当前滤杯完整根TM仍为(-33,131,67)，非新增坐标"),
                D->GetActorTransform().Equals(FTransform(FVector(-33, 131, 67)), Tolerance));
        }
        if (auto* K = Cast<AHandPourKettleActor>(Actor)) { ++Kettles; CheckSteamConfig(*this, K); }
        if (auto* M = Cast<AGlassRecoveryActor>(Actor))
        {
            ++Managers;
            TestTrue(TEXT("原保存全用品恢复配置不丢失"), M->bUnifiedRestore && M->bRestoreAllSupplies);
            TestEqual(TEXT("保存 replacement16"), M->RestoreTargets.Num(), 16);
            TestEqual(TEXT("保存原地工具3"), M->RestoreToolTargets.Num(), 3);
            int32 AssemblyTools = 0;
            for (const auto& Tool : M->RestoreToolTargets)
                AssemblyTools += Tool && Tool->GetName() == TEXT("HandDripper_Practice") && Tool->GetClass() == AssemblyClass;
            TestEqual(TEXT("当前工具槽精确remap到唯一新组合滤杯"), AssemblyTools, 1);
            for (const auto& Target : M->RestoreTargets)
            {
                if (!TestNotNull(TEXT("保存替换槽引用非空"), Target.Get())) return false;
                TestFalse(TEXT("接液杯仍独立，不附着任何组合滤杯"), Target->IsA<ACupActor>()
                    && Target->GetAttachParentActor() && Target->GetAttachParentActor()->IsA<AHandDripperAssemblyActor>());
            }
        }
    }
    TestEqual(TEXT("源313合并独立支架后的当前保存布局312个Actor"), Listed, 312);
    TestEqual(TEXT("独立旧支架精确名称已消失"), OldStands, 0);
    TestTrue(TEXT("保存豆罐、磨豆机、滤杯、壶和经理各唯一"), Jars == 1 && Grinders == 1 && Drippers == 1 && Kettles == 1 && Managers == 1);
    const auto* JarCDO = A.Jar->GetDefaultObject<ADirectBeanJarActor>();
    CheckJarConfig(*this, JarCDO, A.Lid);
    TestFalse(TEXT("新盖已保存为拉拔，不在测试中写bRequireScrewTwist"), A.Lid->GetDefaultObject<ABottleCapActor>()->bRequireScrewTwist);
    const auto* G = A.Grinder->GetDefaultObject<ACoffeeGrinderActor>();
    const auto* Drawer = G->DrawerClass->GetDefaultObject<AGrinderDrawerActor>();
    TestTrue(TEXT("真实DrawerClass保存Instant=true / Assist=true / ShowPowder=false"),
        Drawer->bInstantGroundPour && Drawer->bAssistInstantGroundTransfer && !Drawer->bShowPowderStream);
    Near(*this, TEXT("旧出粉率配置保留8，不决定瞬时质量"), Drawer->GroundPourRateGramsPerSecond, 8);
    Near(*this, TEXT("保存磨豆容量仍15g"), G->BeanCapacityGrams, 15);
    Near(*this, TEXT("保存六圈克数算法不改变"), G->GramsPerDegree, 15.0 / (6 * 360));
    CheckDripperConfig(*this, A.Dripper->GetDefaultObject<AHandDripperActor>());
    CheckSteamConfig(*this, A.Kettle->GetDefaultObject<AHandPourKettleActor>());
    auto* Cue = CheckCue(*this);
    if (!Cue || !TestNotNull(TEXT("保存实际 GrindSFX 组件"), G->GrindSFX.Get())) return false;
    TestTrue(TEXT("保存 LoadedGrindSound 精确指向新Cue"), G->LoadedGrindSound.Get() == Cue);
    TestTrue(TEXT("保存实际 GrindSFX.Sound 也精确接入新Cue"), G->GrindSFX->Sound.Get() == Cue);
    TestFalse(TEXT("保存 GrindSFX 禁止自动起声"), G->GrindSFX->bAutoActivate != 0);
    if (!TestNotNull(TEXT("原地图实际磨豆机实例"), SavedGrinder)
        || !TestNotNull(TEXT("原地图实际GrindSFX"), SavedGrinder->GrindSFX.Get())) return false;
    TestTrue(TEXT("保存地图实例不被旧声音覆盖，新Cue同时接入Loaded和实际组件"),
        SavedGrinder->LoadedGrindSound.Get() == Cue && SavedGrinder->GrindSFX->Sound.Get() == Cue);
    TestFalse(TEXT("地图实例也不自动起声"), SavedGrinder->GrindSFX->bAutoActivate != 0);
    auto* Body = JarCDO->ContainerMesh->GetStaticMesh().Get();
    if (!TestNotNull(TEXT("保存罐体网格"), Body) || !TestNotNull(TEXT("保存罐体碰撞元数据"), Body->GetBodySetup())
        || !TestNotNull(TEXT("保存破碎profile"), JarCDO->GlassBreakProfile.Get())) return false;
    TestEqual(TEXT("罐体仍12个简单碰撞形状"), Body->GetBodySetup()->AggGeom.GetElementCount(), 12);
    TestEqual(TEXT("破碎仍8片预算"), JarCDO->GlassBreakProfile->MaxPhysicsShards, 8);
    TestEqual(TEXT("实际破碎profile仍8片"), JarCDO->GlassBreakProfile->Shards.Num(), 8);
    for (const auto& Snapshot : Before)
        TestTrue(TEXT("只读配置断言没有改任何地图Actor身份、位置、旋转或缩放"),
            Snapshot.Actor->GetClass() == Snapshot.Class && Snapshot.Actor->GetActorTransform().Equals(Snapshot.Pose, Tolerance));
    TestEqual(TEXT("只读测试不使保存地图变脏"), Map->GetOutermost()->IsDirty(), DirtyBefore);
    AddInfo(TEXT("当前312个Actor的只读前后class与完整pose断言保留；跨修改313→312严格身份基线由AllSupplies/BeanJarSteam及DripperAssembly核对，源素材磁盘SHA仍由主任务核对。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackPullAndReplaceLidActualInput,
    "CyberCafe.CoffeeFeedback.PullAndReplaceLidActualInput", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackPullAndReplaceLidActualInput::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    FWorld F(*this);
    auto* Jar = F.Spawn<ADirectBeanJarActor>(A.Jar);
    if (!Jar || !F.Begin()) return false;
    auto* OriginalCap = Jar->CapActor.Get();
    UMotionControllerComponent* MC = nullptr;
    if (!PullOpen(*this, F, Jar, MC) || !ReleaseLidAway(*this, Jar)) return false;
    auto* Cap = Jar->CapActor.Get();
    TestTrue(TEXT("独立盖仍是原盖，不替换或销毁罐"), Cap == OriginalCap && Alive(Jar) && Cap->OwnerBottle.Get() == Jar);
    MC->SetWorldTransform(Socket(Jar), false, nullptr, ETeleportType::TeleportPhysics);
    Cap->SetActorLocation(Socket(Jar).GetLocation() + FVector(0, 0, .5), false, nullptr, ETeleportType::TeleportPhysics);
    if (!TestTrue(TEXT("独立盖实际重新TryGrab"), Cap->GrabComp->TryGrab(MC))
        || !TestTrue(TEXT("实际TryRelease触发生产回盖回调"), Cap->GrabComp->TryRelease())) return false;
    Quiet(Cap);
    TestTrue(TEXT("实际回盖恢复封口、归属和原Socket位置"), Cap->bIsAttached && Jar->bIsCapped
        && Cap == Jar->CapActor.Get() && Cap->OwnerBottle.Get() == Jar
        && Cap->GetActorLocation().Equals(Socket(Jar).GetLocation(), BoundsTolerance));
    Near(*this, TEXT("回盖旋角清零"), Cap->TwistProgress, 0);
    Jar->SetActorRotation(FRotator(120, 0, 0), ETeleportType::TeleportPhysics);
    Near(*this, TEXT("回盖后的公开PourBeans不能出豆"), Jar->PourBeans(1), 0);
    Near(*this, TEXT("两次真实抓盖不改变150g库存"), Jar->GetBeanGrams(), 150);
    int32 CapCount = 0;
    for (TActorIterator<ABottleCapActor> It(F.World); It; ++It) if (Alive(*It)) ++CapCount;
    TestEqual(TEXT("没有重复生成盖Actor"), CapCount, 1);
    TestFalse(TEXT("原生旧盖默认仍不要求旋转"), GetDefault<ABottleCapActor>()->bRequireScrewTwist);
    Near(*this, TEXT("豆克数账本闭合"), Jar->GetBeanBalanceError(), 0);
    Balance(*this, F);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackLargeBeansKeepMassBudget,
    "CyberCafe.CoffeeFeedback.LargeBeansKeepMassBudget", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackLargeBeansKeepMassBudget::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    FWorld F(*this);
    auto* Jar = F.Spawn<ADirectBeanJarActor>(A.Jar);
    auto* G = F.Spawn<ACoffeeGrinderActor>(A.Grinder);
    if (!Jar || !G || !F.Begin()) return false;
    UMotionControllerComponent* MC = nullptr;
    if (!PullOpen(*this, F, Jar, MC) || !ReleaseLidAway(*this, Jar)) return false;
    CheckJarConfig(*this, Jar, A.Lid);
    AimBeans(Jar, G->BeanEntryPoint->GetComponentLocation(), 20);
    Near(*this, TEXT("公开PourBeans(1)仍实际倒5g"), Jar->PourBeans(1), 5);
    Near(*this, TEXT("源余量145g"), Jar->GetBeanGrams(), 145);
    Near(*this, TEXT("真实磨豆机接收5g"), G->GetCurrentBeanGrams(), 5);
    Near(*this, TEXT("豆累计Accepted5g"), Jar->TotalAcceptedGrams, 5);
    Near(*this, TEXT("豆流放大不改变洒漏账"), Jar->SpilledBeanGrams, 0);
    TestTrue(TEXT("真实流实例为正且最多24"), Jar->BeanStream->GetInstanceCount() > 0 && Jar->BeanStream->GetInstanceCount() <= 24);
    TestTrue(TEXT("运行时真实BeanStream scale仍.4"), Jar->BeanStream->GetComponentScale().Equals(FVector(.4), Tolerance));
    auto* Grain = Jar->BeanStream->GetStaticMesh().Get();
    if (!TestNotNull(TEXT("保存的单豆网格"), Grain)) return false;
    Near(*this, TEXT("原单豆最大边仍约2.3cm"), Grain->GetBoundingBox().GetSize().GetMax(), 2.3, .03);
    Near(*this, TEXT(".4缩放后最大边约.92cm而非改变物理质量"), Grain->GetBoundingBox().GetSize().GetMax() * .4, .92, .02);
    for (int32 I = 0; I < Jar->BeanStream->GetInstanceCount(); ++I)
    {
        FTransform Pose;
        if (!TestTrue(TEXT("读取真正的世界实例变换"), Jar->BeanStream->GetInstanceTransform(I, Pose, true))) return false;
        TestTrue(TEXT("实际单豆实例世界缩放与组件一致，没有重复乘.4"), Pose.GetScale3D().Equals(Jar->BeanStream->GetComponentScale(), Tolerance));
    }
    Near(*this, TEXT("放大仅表现，毫升库存仍0"), Jar->GetPreciseVolumeML(), 0);
    Near(*this, TEXT("豆克数账本闭合"), Jar->GetBeanBalanceError(), 0);
    Balance(*this, F);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackInstantDrawer15gToDryFilter,
    "CyberCafe.CoffeeFeedback.InstantDrawer15gToDryFilter", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackInstantDrawer15gToDryFilter::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    // 每个拒收场景独立World，避免原存量、洒漏或最近目标相互抵消。
    for (int32 Case = 0; Case < 6; ++Case)
    {
        FWorld F(*this);
        auto* G = F.Spawn<ACoffeeGrinderActor>(A.Grinder, FVector(500, 0, 0));
        auto* D = F.Spawn<AHandDripperActor>(A.Dripper, FVector(0, 0, 50));
        if (!G || !D || !F.Begin()) return false;
        auto* Drawer = G->DrawerRef.Get();
        if (!TestNotNull(TEXT("生产BeginPlay生成真实DrawerRef"), Drawer)
            || !TestNotNull(TEXT("真实抽屉粉堆"), Drawer->GroundPileMesh.Get())) return false;
        if (!TestTrue(TEXT("运行时真实子类继承保存瞬时辅助/隐藏粉流配置，不由测试切模式"),
            Drawer->GetClass() == G->DrawerClass.Get() && Drawer->bInstantGroundPour
            && Drawer->bAssistInstantGroundTransfer && !Drawer->bShowPowderStream)) return false;
        const FVector Entry = D->PourEntryPoint->GetComponentLocation();
        float OriginalGrounds = 0, Accepted = 15;
        if (Case == 1) { OriginalGrounds = 18; Accepted = 2; }
        if (Case == 2) { OriginalGrounds = 20; Accepted = 0; }
        if (Case == 3) { OriginalGrounds = 5; Accepted = 0; }
        if (Case >= 4) Accepted = 0;
        if (OriginalGrounds > 0) Near(*this, TEXT("原粉量通过目标AddGrounds注入"), D->AddGrounds(OriginalGrounds, Entry), OriginalGrounds);
        if (Case == 3)
        {
            Near(*this, TEXT("湿粉拒收由真实1mL Water入液触发"), D->AddIngredientLiquid(1, TEXT("Water"), FLinearColor::White), 1);
            TestTrue(TEXT("真实Blooming且Wetness>0"), D->BrewState == ECoffeeBrewState::Blooming && D->Wetness > 0);
        }
        AHandDripperActor* Farther = nullptr;
        if (Case == 2 || Case == 3)
        {
            Farther = F.Spawn<AHandDripperActor>(A.Dripper, D->GetActorLocation() - FVector(0, 0, 8));
            if (!Farther) return false;
        }
        // API允许无持有/状态前提；不伪造State、controller或视觉库存。
        Drawer->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        Near(*this, TEXT("公开AddGrounds向实际抽屉装15g"), Drawer->AddGrounds(15), 15);
        AimGrounds(Drawer, Entry, 20, Case == 5 ? 20 : 0);
        if (Case == 4)
        {
            auto* Block = F.Blocker(Entry + FVector(0, 0, 10));
            if (!Block || !QueryBody(*this, Cast<UStaticMeshComponent>(Block->GetRootComponent()))) return false;
            FHitResult Hit;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(CoffeeFeedbackBlocker), false, Drawer);
            Params.AddIgnoredActor(G);
            TestTrue(TEXT("实际粉路径上的最近物理遮挡先于目标入口"), F.World->LineTraceSingleByChannel(Hit,
                Drawer->GroundPourPoint->GetComponentLocation(), Entry, ECC_Visibility, Params) && Hit.GetActor() == Block);
        }
        // 新模式必须与旧rate解耦；只改本实例的测试rate，不改保存CDO或新开关。
        Drawer->GroundPourRateGramsPerSecond = 0;
        Near(*this, TEXT("辅助首调用只转移目标实际接纳量，即使旧rate=0"), Drawer->PourGrounds(FrameDt), Accepted);
        Near(*this, TEXT("辅助未接纳余量留仓：成功0/容量13/拒收15"), Drawer->GetGroundsGrams(), 15 - Accepted);
        Near(*this, TEXT("目标实际接收容量内粉"), D->GetGroundsGrams(), OriginalGrounds + Accepted);
        Near(*this, TEXT("实际LastPoured精确等于本次转移量"), Drawer->LastPouredGroundGrams, Accepted);
        Near(*this, TEXT("实际LastAccepted"), Drawer->LastAcceptedGroundGrams, Accepted);
        Near(*this, TEXT("辅助容量不足或湿满偏挡均不洒粉"), Drawer->SpilledGroundGrams, 0);
        const TCHAR* Reasons[] = {TEXT("TRANSFERRED"), TEXT("PARTIAL"), TEXT("FULL_FILTER"),
            TEXT("NEEDS_EMPTY_FILTER"), TEXT("BLOCKED"), TEXT("AIM")};
        TestEqual(TEXT("实际辅助结果原因精确匹配当前场景"), Drawer->LastGroundTransferReason, FName(Reasons[Case]));
        Near(*this, TEXT("原粉加本批等于目标余量加源余量和洒漏"), OriginalGrounds + 15,
            D->GetGroundsGrams() + Drawer->GetGroundsGrams() + Drawer->SpilledGroundGrams);
        if (Farther) Near(*this, TEXT("最近满/湿滤杯拒收不得穿透到更远空滤杯"), Farther->GetGroundsGrams(), 0);
        TestEqual(TEXT("粉堆可见性来自真实剩余库存，拒收和部分接纳不能隐藏"), Drawer->GroundPileMesh->IsVisible(), Accepted < 15);
        NoPowder(*this, Drawer);
        if (Case == 0) TestTrue(TEXT("目标真实进入Dry"), D->BrewState == ECoffeeBrewState::Dry && D->Wetness == 0);
        const double SpilledBefore = Drawer->SpilledGroundGrams;
        Near(*this, TEXT("空源或原目标仍拒收的第二次调用不能再次出粉"), Drawer->PourGrounds(FrameDt), 0);
        Near(*this, TEXT("第二次拒收后源库存完整保留"), Drawer->GetGroundsGrams(), 15 - Accepted);
        Near(*this, TEXT("第二次调用LastPoured精确清零"), Drawer->LastPouredGroundGrams, 0);
        Near(*this, TEXT("第二次调用LastAccepted精确清零"), Drawer->LastAcceptedGroundGrams, 0);
        Near(*this, TEXT("空源或拒收调用不擦除累计Spilled"), Drawer->SpilledGroundGrams, SpilledBefore);
        Drawer->Tick(FrameDt);
        NoPowder(*this, Drawer);
        Near(*this, TEXT("无人持有Tick不清掉待重试粉量"), Drawer->GetGroundsGrams(), 15 - Accepted);
        Near(*this, TEXT("后续无人持有Tick也保留累计Spilled"), Drawer->SpilledGroundGrams, SpilledBefore);
        Balance(*this, F);
    }
    // 正Dt和真实+Y朝下是前置条件；负Dt/零Dt/反向倾倒不能为瞬时模式放宽。
    FWorld F(*this);
    auto* D = F.Spawn<AHandDripperActor>(A.Dripper);
    auto* Drawer = F.Spawn<AGrinderDrawerActor>(A.Grinder->GetDefaultObject<ACoffeeGrinderActor>()->DrawerClass.Get(), FVector(200, 0, 0));
    if (!D || !Drawer || !F.Begin()) return false;
    Near(*this, TEXT("前置条件用例装15g"), Drawer->AddGrounds(15), 15);
    const FVector Entry = D->PourEntryPoint->GetComponentLocation();
    AimGrounds(Drawer, Entry, 20, 0, 0);
    Near(*this, TEXT("正立不倒粉"), Drawer->PourGrounds(FrameDt), 0);
    AimGrounds(Drawer, Entry, 20, 0, 65);
    Near(*this, TEXT("反向口沿朝上不倒粉"), Drawer->PourGrounds(FrameDt), 0);
    AimGrounds(Drawer, Entry);
    Near(*this, TEXT("零Dt不出清"), Drawer->PourGrounds(0), 0);
    Near(*this, TEXT("负Dt不出清"), Drawer->PourGrounds(-FrameDt), 0);
    Near(*this, TEXT("无效前置条件保留真实15g"), Drawer->GetGroundsGrams(), 15);
    NoPowder(*this, Drawer);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackCalibratedHalfPaperRuntimeGetBounds,
    "CyberCafe.CoffeeFeedback.CalibratedHalfPaperRuntimeGetBounds", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackCalibratedHalfPaperRuntimeGetBounds::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    const auto* CDO = A.Dripper->GetDefaultObject<AHandDripperActor>();
    if (!CheckDripperConfig(*this, CDO)) return false;
    const FTransform SavedGroundPose = CDO->GroundBed->GetRelativeTransform();
    auto* GroundSource = CDO->GroundBed->GetStaticMesh().Get();
    const FBox Source = GroundSource->GetBoundingBox();
    Near(*this, TEXT("原GroundsV02源minZ约0"), Source.Min.Z, 0, BoundsTolerance);
    Near(*this, TEXT("原GroundsV02源maxZ约3.884277"), Source.Max.Z, 3.884277, BoundsTolerance);
    const bool GroundDirty = GroundSource->GetOutermost()->IsDirty();
    const bool PaperDirty = CDO->PaperMesh->GetStaticMesh()->GetOutermost()->IsDirty();
    FWorld F(*this);
    // Manager先于工具BeginPlay，以真实保存根和空初始库存捕获单个RestoreToolTarget。
    auto* Manager = F.Spawn<AGlassRecoveryActor>(AGlassRecoveryActor::StaticClass(), FVector(500, 0, 0));
    auto* D = F.Spawn<AHandDripperActor>(A.Dripper, FVector(30, 40, 50));
    if (!Manager || !D) return false;
    Manager->bUnifiedRestore = true;
    Manager->bRestoreAllSupplies = true;
    Manager->RestoreToolTargets = {D};
    if (!F.Begin()) return false;
    TestFalse(TEXT("真实0g粉床隐藏"), D->GroundBed->IsVisible());
    Near(*this, TEXT("空粉床预算0"), D->GetCoffeeBudgetML(), 0);
    const FVector Entry = D->PourEntryPoint->GetComponentLocation();
    Near(*this, TEXT("公开入口加5g"), D->AddGrounds(5, Entry), 5);
    const FBox Small = SourceBoundsInBody(D->GroundBed.Get(), D->ContainerMesh.Get());
    const FVector SmallScale = D->GroundBed->GetRelativeScale3D();
    TestTrue(TEXT("5g可见，XY和Z均比15g校准缩放小"), D->GroundBed->IsVisible()
        && SmallScale.X < D->CalibratedGroundBedScale.X && SmallScale.Y < D->CalibratedGroundBedScale.Y
        && SmallScale.Z < D->CalibratedGroundBedScale.Z && Small.Max.Z < 3.95 - BoundsTolerance);
    Near(*this, TEXT("5g粉床源AABB底仍固定1.35cm"), Small.Min.Z, 1.35, BoundsTolerance);
    Near(*this, TEXT("5g真实预算80mL，无视觉加量"), D->GetCoffeeBudgetML(), 80);
    Near(*this, TEXT("公开入口再加10g达到真实15g"), D->AddGrounds(10, Entry), 10);
    Near(*this, TEXT("真实质量15g"), D->GetGroundsGrams(), 15);
    Near(*this, TEXT("15g真实预算240mL"), D->GetCoffeeBudgetML(), 240);
    CheckHalfHeight(*this, D);
    const FTransform FullPose = D->GroundBed->GetRelativeTransform();
    Near(*this, TEXT("公开入口再加5g，视觉饱和不拒绝真实20g"), D->AddGrounds(5, Entry), 5);
    Near(*this, TEXT("真实库存20g不是15g视觉钳制"), D->GetGroundsGrams(), 20);
    Near(*this, TEXT("20g真实预算320mL"), D->GetCoffeeBudgetML(), 320);
    TestTrue(TEXT("15到20g运行时粉床形状保持饱和"), D->GroundBed->GetRelativeTransform().Equals(FullPose, Tolerance));
    CheckHalfHeight(*this, D);
    Near(*this, TEXT("满杯再加5g不人工增粉或增预算"), D->AddGrounds(5, Entry), 0);
    Near(*this, TEXT("满杯预算仍320"), D->GetCoffeeBudgetML(), 320);
    Near(*this, TEXT("干粉Wetness为0"), D->Wetness, 0);
    CheckWetnessMaterial(*this, D);
    Near(*this, TEXT("公开加入1mL水触发真实湿度"), D->AddIngredientLiquid(1, TEXT("Water"), FLinearColor::White), 1);
    D->AdvanceBrew(0);
    TestTrue(TEXT("入水进入真实Blooming并拒收粉"), D->BrewState == ECoffeeBrewState::Blooming && !D->CanReceiveGrounds());
    Near(*this, TEXT("20g加1mL水的权威Wetness=.025"), D->Wetness, .025);
    CheckWetnessMaterial(*this, D);
    TestTrue(TEXT("Wetness更新不改变饱和粉床几何"), D->GroundBed->GetRelativeTransform().Equals(FullPose, Tolerance));
    TestEqual(TEXT("真实工具恢复只捕获单滤杯"), Manager->GetRestoreToolCount(), 1);
    if (!TestTrue(TEXT("公开RestoreWorkbench执行Prepare、ConsumeLiquid及ResetBrew，不调用虚构RestoreToolTarget"), Manager->RestoreWorkbench())) return false;
    TestTrue(TEXT("恢复保留原滤杯身份"), Manager->GetCapturedTool(0) == D && Alive(D));
    Near(*this, TEXT("真实恢复粉量0"), D->GetGroundsGrams(), 0);
    Near(*this, TEXT("真实恢复湿度0"), D->Wetness, 0);
    CheckWetnessMaterial(*this, D);
    Near(*this, TEXT("真实恢复预算0"), D->GetCoffeeBudgetML(), 0);
    Near(*this, TEXT("恢复弃置真实20g，不计为倾倒"), D->DiscardedGroundsGrams, 20);
    Near(*this, TEXT("真实工具余液已清"), D->GetPreciseVolumeML(), 0);
    TestFalse(TEXT("恢复空粉床重新隐藏"), D->GroundBed->IsVisible());
    TestTrue(TEXT("原CDO组件变换不被运行时校准写回"), CDO->GroundBed->GetRelativeTransform().Equals(SavedGroundPose, Tolerance));
    TestEqual(TEXT("原粉床素材不变脏"), GroundSource->GetOutermost()->IsDirty(), GroundDirty);
    TestEqual(TEXT("原滤纸素材不变脏"), CDO->PaperMesh->GetStaticMesh()->GetOutermost()->IsDirty(), PaperDirty);
    Balance(*this, F);
    AddInfo(TEXT("精度为源AABB变换后.01cm；没有MeshDescription依赖，未验证实际内壁各三角的间隙或像素遮挡。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackSteamActualImpactScale,
    "CyberCafe.CoffeeFeedback.SteamActualImpactScale", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackSteamActualImpactScale::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    FWorld F(*this);
    auto* K = F.Spawn<AHandPourKettleActor>(A.Kettle, FVector(300, 0, 80));
    auto* OtherK = F.Spawn<AHandPourKettleActor>(A.Kettle, FVector(500, 0, 80));
    auto* Cup = F.Spawn<ACupActor>(A.Cup, FVector(0, 0, 50));
    if (!K || !OtherK || !Cup) return false;
    Cup->FillAmount = 0;
    if (!CheckSteamConfig(*this, K) || !F.Begin()) return false;
    auto* Steam = K->GetHotWaterSteamComponent();
    auto* OtherSteam = OtherK->GetHotWaterSteamComponent();
    if (!TestNotNull(TEXT("另一个壶真实蒸汽组件"), OtherSteam)) return false;
    const float SavedScale = Steam->SteamScale;
    const float SavedHold = Steam->ImpactHoldSeconds;
    auto* Original = Steam->SteamSystem.Get();
    const bool DirtyBefore = Original->GetOutermost()->IsDirty();
    TestTrue(TEXT("无实际落点前懒创建且无蒸汽请求"), !Steam->GetSteamComponent() && !Steam->IsSteamRequested());
    const FVector Entry = Cup->PourEntryPoint->GetComponentLocation();
    UParticleSystemComponent* OnlySteam = nullptr;
    for (int32 I = 0; I < 3; ++I)
    {
        const double Before = K->GetPreciseVolumeML();
        Near(*this, TEXT("从本壶真实EmitVolume出.5mL水包"), F.Transfer()->EmitVolume(K, .5,
            Entry + FVector(0, 0, 10), FVector(0, 0, -100), FVector::ZeroVector), .5);
        Near(*this, TEXT("实际来源扣.5，不由蒸汽计量"), Before - K->GetPreciseVolumeML(), .5);
        F.Transfer()->Simulate(.2);
        auto* PS = Steam->GetSteamComponent();
        if (!TestNotNull(TEXT("实际水包接触后创建原Cascade组件"), PS)) return false;
        if (!OnlySteam) OnlySteam = PS;
        TestTrue(TEXT("连续水包不替换PS组件身份"), PS == OnlySteam);
        TestTrue(TEXT("实际组件使用保存原模板且归属本壶"), PS->Template == Original && PS->GetOwner() == K);
        TestTrue(TEXT("当前PS世界缩放精确等于已保存配置"), PS->GetComponentScale().Equals(FVector(SavedScale), Tolerance));
        TestTrue(TEXT("蒸汽世界变换绝对，不继承壶身缩放"), PS->IsUsingAbsoluteLocation() && PS->IsUsingAbsoluteRotation() && PS->IsUsingAbsoluteScale());
        TestTrue(TEXT("只有本壶水包能请求本壶蒸汽"), Steam->IsSteamRequested() && !OtherSteam->IsSteamRequested() && !OtherSteam->GetSteamComponent());
        TestEqual(TEXT("连续落点只打开一次请求门，不按包计数"), Steam->GetSteamStartCount(), 1);
        TArray<UParticleSystemComponent*> Components;
        K->GetComponents(Components);
        TestEqual(TEXT("真实壶始终只有一个蒸汽组件"), Components.Num(), 1);
        Steam->TickComponent(SavedHold * .25f, LEVELTICK_All, nullptr);
    }
    auto* PS = Steam->GetSteamComponent();
    const double OwnImpactWater = Steam->GetLastImpactWaterML();
    // 另一壶的真实水包不能刷新本壶蒸汽落点或诊断量；不手动广播Impact。
    Near(*this, TEXT("另一壶公开发出真实1mL水"), F.Transfer()->EmitVolume(OtherK, 1,
        Entry + FVector(0, 0, 10), FVector(0, 0, -100), FVector::ZeroVector), 1);
    F.Transfer()->Simulate(.2);
    Near(*this, TEXT("另一来源水包不刷新本壶LastImpactWaterML"), Steam->GetLastImpactWaterML(), OwnImpactWater);
    TestTrue(TEXT("另一壶仅开启自己的蒸汽"), OtherSteam->IsSteamRequested() && OtherSteam->GetSteamComponent());
    TestTrue(TEXT("另一来源水包不改变本壶PS身份或启动计数"), Steam->GetSteamComponent() == OnlySteam && Steam->GetSteamStartCount() == 1);
    const FVector LastPoint = Steam->GetLastImpactPoint();
    TestTrue(TEXT("真实落点匹配原杯入口+.3法向偏置"), LastPoint.Equals(Entry + FVector(0, 0, .3), BoundsTolerance));
    Near(*this, TEXT("本壶三个.5包加另一壶1mL实际进入杯中2.5mL"), Cup->GetIngredientVolumeML(TEXT("Water")), 2.5);
    Steam->TickComponent(SavedHold + .1f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Hold加.1的真实组件Tick自然撤销请求"), Steam->IsSteamRequested());
    TestTrue(TEXT("自然停发不Destroy组件，不声称尾粒子已渲染完"), Steam->GetSteamComponent() == PS && IsValid(PS));
    TestFalse(TEXT("正常停发关闭蒸汽业务Tick"), Steam->IsComponentTickEnabled());
    TestTrue(TEXT("最后落点和配置保持"), Steam->GetLastImpactPoint().Equals(LastPoint, Tolerance)
        && PS->GetComponentScale().Equals(FVector(SavedScale), Tolerance));
    Steam->StopSteam(true);
    TestTrue(TEXT("强制清粒也不在生命周期外销毁组件"), Steam->GetSteamComponent() == PS);
    TestEqual(TEXT("检查不改原Cascade素材package"), Original->GetOutermost()->IsDirty(), DirtyBefore);
    Balance(*this, F);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackRealSoundCueActiveStop,
    "CyberCafe.CoffeeFeedback.RealSoundCueActiveStop", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackRealSoundCueActiveStop::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    auto* Cue = CheckCue(*this);
    if (!Cue) return false;
    FWorld F(*this, true);
    auto* G = F.Spawn<ACoffeeGrinderActor>(A.Grinder, FVector(0, 0, 80));
    if (!G || !F.Begin()) return false;
    auto* SFX = G->GrindSFX.Get();
    if (!TestNotNull(TEXT("真实GrindSFX不是测试补造组件"), SFX)) return false;
    TestTrue(TEXT("BeginPlay后Loaded与组件实际Sound均解析为反馈Cue"), G->LoadedGrindSound.Get() == Cue && SFX->Sound.Get() == Cue);
    TestFalse(TEXT("原组件autoActivate保持false"), SFX->bAutoActivate != 0);
    TestFalse(TEXT("开始前组件没有播放请求"), SFX->IsPlaying());
    Near(*this, TEXT("公开投10g豆后才磨"), G->TryAddBeans(10), 10);
    G->SetActorTickEnabled(true);
    F.Tick(.05f);
    Near(*this, TEXT("正常20度/.05实际消耗按原g/度"), G->AdvanceGrinding(20 * G->EffectiveDirectionSign, .05f), 20 * G->GramsPerDegree);
    TestTrue(TEXT("非实时设备上实际组件active且IsPlaying；不代表音频被渲染或头显可听"), SFX->IsActive() && SFX->IsPlaying() && G->bGrindSFXActive);
    TestTrue(TEXT("实际播的是新循环Cue"), SFX->Sound.Get() == Cue);
    const float FirstStart = G->GrindSFXStartTime;
    for (int32 I = 0; I < 6; ++I)
    {
        F.Tick(.05f);
        TestTrue(TEXT("普通连续研磨有实际产出"), G->AdvanceGrinding(20 * G->EffectiveDirectionSign, .05f) > 0);
        TestTrue(TEXT("连续调用复用原组件和同一Cue"), G->GrindSFX.Get() == SFX && SFX->Sound.Get() == Cue);
        Near(*this, TEXT("连续调用不重置GrindSFXStartTime"), G->GrindSFXStartTime, FirstStart);
        TestTrue(TEXT("连续普通研磨保持真正IsPlaying"), SFX->IsPlaying() && G->bGrindSFXActive);
    }
    Near(*this, TEXT("实际豆消耗等于累计粉产出和损耗"), G->GetTotalBeanConsumedGrams(),
        G->GetTotalGroundOutputGrams() + G->GetGrindLossGrams());
    Near(*this, TEXT("零角度立即停声且不耗豆"), G->AdvanceGrinding(0, .05f), 0);
    TestFalse(TEXT("零角度后真实IsPlaying立即false，不依赖非实时渲染时钟"), SFX->IsPlaying());
    TestFalse(TEXT("零角度后业务active立即false"), G->bGrindSFXActive);
    TestTrue(TEXT("剩余豆正常恢复研磨可再次起声"), G->AdvanceGrinding(20 * G->EffectiveDirectionSign, .05f) > 0 && SFX->IsPlaying());
    Near(*this, TEXT("反向角度立即停声"), G->AdvanceGrinding(-20 * G->EffectiveDirectionSign, .05f), 0);
    TestFalse(TEXT("反向真实组件停声"), SFX->IsPlaying());
    TestTrue(TEXT("恢复前再起声"), G->AdvanceGrinding(20 * G->EffectiveDirectionSign, .05f) > 0 && SFX->IsPlaying());
    const double BeforeOutput = G->GetTotalGroundOutputGrams();
    if (!TestTrue(TEXT("公开ResetForWorkbench清理实际子件与声音"), G->ResetForWorkbench(G->GetActorTransform()))) return false;
    TestTrue(TEXT("恢复不替换GrindSFX或声音资产"), G->GrindSFX.Get() == SFX && G->LoadedGrindSound.Get() == Cue && SFX->Sound.Get() == Cue);
    TestFalse(TEXT("实际恢复立即停止声音组件"), SFX->IsPlaying());
    TestFalse(TEXT("恢复业务active为false"), G->bGrindSFXActive);
    Near(*this, TEXT("工作台恢复不擦除历史粉产出"), G->GetTotalGroundOutputGrams(), BeforeOutput);
    Near(*this, TEXT("恢复后真实豆0"), G->GetCurrentBeanGrams(), 0);
    TestTrue(TEXT("恢复后真实DrawerRef为空粉"), G->DrawerRef && G->DrawerRef->GetGroundsGrams() == 0);
    Balance(*this, F);
    AddInfo(TEXT("本用例没有推进离线音频渲染、采样峰值、测循环接缝或验证HMD；只验证非实时设备上的真实播放/停止请求。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackNativeLegacyDefaultsAndContinuousPour,
    "CyberCafe.CoffeeFeedback.NativeLegacyDefaultsAndContinuousPour", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackNativeLegacyDefaultsAndContinuousPour::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    TestFalse(TEXT("native旧盖保持拉拔默认"), GetDefault<ABottleCapActor>()->bRequireScrewTwist);
    TestFalse(TEXT("native抽屉不默认瞬时"), GetDefault<AGrinderDrawerActor>()->bInstantGroundPour);
    TestFalse(TEXT("native抽屉辅助默认false，保留旧行为"), GetDefault<AGrinderDrawerActor>()->bAssistInstantGroundTransfer);
    TestTrue(TEXT("native旧连续抽屉仍显示粉流"), GetDefault<AGrinderDrawerActor>()->bShowPowderStream);
    TestFalse(TEXT("native滤杯不默认半高校准"), GetDefault<AHandDripperActor>()->bUseHalfPaperGroundBed);
    TestNull(TEXT("native磨豆机LoadedGrindSound仍空，不全局接新素材"), GetDefault<ACoffeeGrinderActor>()->LoadedGrindSound.Get());
    Near(*this, TEXT("native蒸汽scale原.18不受保存BP反馈影响"), GetDefault<UHotWaterSteamComponent>()->SteamScale, .18);
    Near(*this, TEXT("native蒸汽hold原.18不受保存BP反馈影响"), GetDefault<UHotWaterSteamComponent>()->ImpactHoldSeconds, .18);
    FWorld F(*this);
    auto* Drawer = F.Spawn<AGrinderDrawerActor>(AGrinderDrawerActor::StaticClass(), FVector(200, 0, 0));
    auto* D = F.Spawn<AHandDripperActor>(AHandDripperActor::StaticClass());
    if (!Drawer || !D || !F.Begin()) return false;
    TestFalse(TEXT("native实例确实继承辅助默认false"), Drawer->bAssistInstantGroundTransfer);
    Drawer->bAssistInstantGroundTransfer = false; // 仅旧连续兼容夹具显式关闭，不写保存CDO。
    Near(*this, TEXT("native连续抽屉公开装15g"), Drawer->AddGrounds(15), 15);
    AimGrounds(Drawer, D->PourEntryPoint->GetComponentLocation());
    Drawer->GroundPourRateGramsPerSecond = 0;
    Near(*this, TEXT("旧模式rate0不能变成瞬时倾倒"), Drawer->PourGrounds(FrameDt), 0);
    Drawer->GroundPourRateGramsPerSecond = 8;
    const double TipDown = FVector::DotProduct(Drawer->DrawerMesh->GetComponentQuat().RotateVector(FVector::YAxisVector), FVector::DownVector);
    const double Expected = 8 * FrameDt * TipDown;
    Near(*this, TEXT("旧模式保留8g/s乘Dt乘实际tip连续出粉"), Drawer->PourGrounds(FrameDt), Expected);
    Near(*this, TEXT("旧模式余量不一次清空"), Drawer->GetGroundsGrams(), 15 - Expected);
    Near(*this, TEXT("旧模式真实目标仅收到当帧量"), D->GetGroundsGrams(), Expected);
    Drawer->UpdatePowderStream(FrameDt);
    TestEqual(TEXT("旧模式表现仍至多24个实例且实际24"), Drawer->PowderStream->GetInstanceCount(), 24);
    Balance(*this, F);
    // 旧瞬时miss/overflow专用兼容覆盖；当前辅助场景仍在InstantDrawer中严格保粉。
    FAssets A; if (!A.Check(*this)) return false;
    for (bool Overflow : {false, true})
    {
        FWorld Legacy(*this);
        auto* LegacyDrawer = Legacy.Spawn<AGrinderDrawerActor>(A.Grinder->GetDefaultObject<ACoffeeGrinderActor>()->DrawerClass.Get(), FVector(200, 0, 0));
        auto* LegacyFilter = Legacy.Spawn<AHandDripperActor>(A.Dripper);
        if (!LegacyDrawer || !LegacyFilter || !Legacy.Begin()) return false;
        if (!TestTrue(TEXT("旧瞬时兼容夹具仍读取保存Instant=true"), LegacyDrawer->bInstantGroundPour)) return false;
        LegacyDrawer->bAssistInstantGroundTransfer = false; // 只在旧miss/overflow兼容用例关闭辅助。
        const FVector Entry = LegacyFilter->PourEntryPoint->GetComponentLocation();
        const float OriginalGrounds = Overflow ? 18 : 0, Accepted = Overflow ? 2 : 0;
        if (Overflow) Near(*this, TEXT("旧overflow目标实际装18g"), LegacyFilter->AddGrounds(18, Entry), 18);
        Near(*this, TEXT("旧瞬时兼容源实际装15g"), LegacyDrawer->AddGrounds(15), 15);
        AimGrounds(LegacyDrawer, Entry, 20, Overflow ? 0 : 20);
        Near(*this, TEXT("关闭辅助的旧瞬时仍清出15g"), LegacyDrawer->PourGrounds(FrameDt), 15);
        Near(*this, TEXT("旧瞬时源余量确实0"), LegacyDrawer->GetGroundsGrams(), 0);
        Near(*this, TEXT("旧目标实际仅接纳容量内粉"), LegacyFilter->GetGroundsGrams(), OriginalGrounds + Accepted);
        Near(*this, TEXT("旧LastPoured15不被当前辅助契约改写"), LegacyDrawer->LastPouredGroundGrams, 15);
        Near(*this, TEXT("旧LastAccepted精确0或2"), LegacyDrawer->LastAcceptedGroundGrams, Accepted);
        Near(*this, TEXT("旧miss洒15或overflow洒13仍保留"), LegacyDrawer->SpilledGroundGrams, 15 - Accepted);
        Near(*this, TEXT("旧模式克数仍闭合"), OriginalGrounds + 15,
            LegacyDrawer->GetGroundsGrams() + LegacyFilter->GetGroundsGrams() + LegacyDrawer->SpilledGroundGrams);
        NoPowder(*this, LegacyDrawer); Balance(*this, Legacy);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoffeeFeedbackFullNewRecipeChain,
    "CyberCafe.CoffeeFeedback.FullNewRecipeChain", CyberCafeCoffeeFeedbackTests::Flags)
bool FCoffeeFeedbackFullNewRecipeChain::RunTest(const FString&)
{
    using namespace CyberCafeCoffeeFeedbackTests;
    FAssets A;
    if (!A.Check(*this)) return false;
    FWorld F(*this);
    auto* Jar = F.Spawn<ADirectBeanJarActor>(A.Jar);
    auto* G = F.Spawn<ACoffeeGrinderActor>(A.Grinder);
    auto* D = F.Spawn<AHandDripperActor>(A.Dripper, FVector(200, 0, 50));
    auto* K = F.Spawn<AHandPourKettleActor>(A.Kettle, FVector(400, 0, 50));
    auto* Cup = F.Spawn<ACupActor>(A.Cup, FVector(200, 0, 39.12));
    if (!Jar || !G || !D || !K || !Cup) return false;
    // 只设置BeginPlay前装量；100mL水只能从真实壶初始500mL扣出，不向滤杯/杯补液。
    K->FillAmount = 1;
    D->FillAmount = 0;
    Cup->FillAmount = 0;
    if (!CheckJarConfig(*this, Jar, A.Lid) || !CheckDripperConfig(*this, D)
        || !CheckSteamConfig(*this, K) || !F.Begin()) return false;
    auto* Drawer = G->DrawerRef.Get();
    if (!TestNotNull(TEXT("原保存磨豆机生成真正DrawerRef"), Drawer)
        || !TestNotNull(TEXT("原保存磨豆机生成真正HandleRef"), G->HandleRef.Get())
        || !TestNotNull(TEXT("真实盖"), Jar->CapActor.Get())) return false;
    TestTrue(TEXT("生产子件类、归属和搬粉delegate原样保留"), Drawer->GetClass() == G->DrawerClass.Get()
        && G->HandleRef->GetClass() == G->HandleClass.Get() && Drawer->OwnerGrinder.Get() == G
        && G->OnGrindProgress.Contains(Drawer, FName(TEXT("OnGrinderGrindProgress"))));
    if (!TestTrue(TEXT("链路使用保存瞬时辅助/隐藏粉流开关，不由测试赋值"),
        Drawer->bInstantGroundPour && Drawer->bAssistInstantGroundTransfer && !Drawer->bShowPowderStream)) return false;
    for (auto* Mesh : {Jar->ContainerMesh.Get(), Jar->CapActor->CapMesh.Get(), G->BodyMesh.Get(), Drawer->DrawerMesh.Get(),
        D->ContainerMesh.Get(), K->ContainerMesh.Get(), Cup->ContainerMesh.Get()})
        if (!QueryBody(*this, Mesh)) return false;
    TestTrue(TEXT("真实容器原网格仍是物理根，不替换碰撞或口点"), D->GetRootComponent() == D->ContainerMesh.Get()
        && K->GetRootComponent() == K->ContainerMesh.Get() && Cup->GetRootComponent() == Cup->ContainerMesh.Get());
    const FTransform DripEntry = D->PourEntryPoint->GetComponentTransform();
    const FTransform CupEntry = Cup->PourEntryPoint->GetComponentTransform();
    const float DripRadius = D->PourEntryRadius;
    const float CupRadius = Cup->PourEntryRadius;
    TestTrue(TEXT("真实滤杯出口位于原杯口正上方"), D->GetActorTransform().TransformPosition(D->OutletLocal).Z > CupEntry.GetLocation().Z
        && (D->GetActorTransform().TransformPosition(D->OutletLocal) - CupEntry.GetLocation()).Size2D() < .001);
    Near(*this, TEXT("原滤杯闷蒸仍8秒"), D->BloomSeconds, 8);
    Near(*this, TEXT("原滤杯滴流仍12mL/s"), D->DrainRateMLPerSecond, 12);
    Near(*this, TEXT("真实链路初始Water500"), K->GetIngredientVolumeML(TEXT("Water")), 500);
    Near(*this, TEXT("真实链路Initial500"), F.Transfer()->GetBalance().InitialML, 500);
    Near(*this, TEXT("初始Injected0"), F.Transfer()->GetBalance().InjectedML, 0);

    UMotionControllerComponent* LidHand = nullptr;
    if (!PullOpen(*this, F, Jar, LidHand) || !ReleaseLidAway(*this, Jar)) return false;
    AimBeans(Jar, G->BeanEntryPoint->GetComponentLocation(), 15);
    for (int32 I = 0; I < 3; ++I) Near(*this, TEXT("真实公开倒豆每次5g"), Jar->PourBeans(1), 5);
    Near(*this, TEXT("真实原磨槽收到15g"), G->GetCurrentBeanGrams(), 15);
    Near(*this, TEXT("源豆罐实际余135g"), Jar->GetBeanGrams(), 135);
    Near(*this, TEXT("豆罐实际Accepted15"), Jar->TotalAcceptedGrams, 15);
    Near(*this, TEXT("实际豆Spilled0"), Jar->SpilledBeanGrams, 0);
    for (int32 I = 0; I < 6; ++I)
        Near(*this, TEXT("六圈公共研磨每圈消耗2.5g"), G->AdvanceGrinding(360 * G->EffectiveDirectionSign, .3f), 2.5);
    Near(*this, TEXT("实际累计豆消耗15g"), G->GetTotalBeanConsumedGrams(), 15);
    Near(*this, TEXT("实际累计粉产出15g"), G->GetTotalGroundOutputGrams(), 15);
    Near(*this, TEXT("实际研磨损耗0"), G->GetGrindLossGrams(), 0);
    Near(*this, TEXT("主体真实余豆0"), G->GetCurrentBeanGrams(), 0);
    Near(*this, TEXT("主体真实粉通过delegate搬走"), G->GetCurrentGroundGrams(), 0);
    Near(*this, TEXT("真实子抽屉收到15g"), Drawer->GetGroundsGrams(), 15);
    auto* DrawerHand = F.Hand(Drawer->GetActorTransform(), EControllerHand::Right);
    if (!DrawerHand || !TestTrue(TEXT("真实TryGrab持抽屉"), Drawer->GrabComp->TryGrab(DrawerHand))) return false;
    Drawer->DetachFromGrinder(DrawerHand);
    TestTrue(TEXT("公开Detach使用真正持有手，未直接写State"), Drawer->State == AGrinderDrawerActor::EDrawerState::Detached
        && Drawer->GrabComp->IsHeld() && Drawer->GrabComp->GetHoldingController() == DrawerHand);
    AimGrounds(Drawer, DripEntry.GetLocation());
    Drawer->Tick(FrameDt);
    Near(*this, TEXT("真实持屉首个.016秒Tick调用PourGrounds清出15g"), Drawer->LastPouredGroundGrams, 15);
    Near(*this, TEXT("单帧后实际抽屉Current0"), Drawer->GetGroundsGrams(), 0);
    Near(*this, TEXT("实际滤杯得到15g"), D->GetGroundsGrams(), 15);
    Near(*this, TEXT("粉Spilled0"), Drawer->SpilledGroundGrams, 0);
    TestTrue(TEXT("生产最近入口命中且为Dry，不手动标状态"), Drawer->LastGroundPourEnd.Equals(DripEntry.GetLocation(), .001)
        && D->BrewState == ECoffeeBrewState::Dry && D->Wetness == 0);
    NoPowder(*this, Drawer);
    TestFalse(TEXT("链路真实抽屉粉堆隐藏"), Drawer->GroundPileMesh->IsVisible());
    CheckHalfHeight(*this, D);
    Near(*this, TEXT("15g实际粉床预算240"), D->GetCoffeeBudgetML(), 240);
    Near(*this, TEXT("真实质量闭合到粉床和洒漏"), G->GetTotalGroundOutputGrams(),
        G->GetCurrentGroundGrams() + Drawer->GetGroundsGrams() + D->GetGroundsGrams() + Drawer->SpilledGroundGrams);
    Drawer->SetActorLocation(FVector(400, 100, 50), false, nullptr, ETeleportType::TeleportPhysics);
    DrawerHand->SetWorldLocation(Drawer->GetActorLocation());
    if (!TestTrue(TEXT("退出水流路径后实际松屉"), Drawer->GrabComp->TryRelease())) return false;
    Quiet(Drawer);

    auto* Steam = K->GetHotWaterSteamComponent();
    const float SavedSteamScale = Steam->SteamScale;
    auto* SteamTemplate = Steam->SteamSystem.Get();
    TestTrue(TEXT("尚无实际水落点，不允许预先请求蒸汽"), !Steam->IsSteamRequested() && !Steam->GetSteamComponent());
    K->SetActorRotation(FRotator(-65, 0, 0), ETeleportType::TeleportPhysics);
    const FTransform Spout = K->GetKettleSpoutWorldTransform();
    const FVector Velocity = Spout.GetUnitAxis(EAxis::X) * 35;
    constexpr double Height = 10;
    const double FlightTime = (Velocity.Z + FMath::Sqrt(Velocity.Z * Velocity.Z + 2 * 980 * Height)) / 980;
    const FVector WantedSpout = DripEntry.GetLocation() + FVector(0, 0, Height)
        - FVector(Velocity.X, Velocity.Y, 0) * FlightTime;
    K->SetActorLocation(K->GetActorLocation() + WantedSpout - Spout.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
    TestTrue(TEXT("真实壶嘴35cm/s重力980弹道命中原滤杯入口，不迁就口点"),
        (K->GetKettleSpoutWorldTransform().GetLocation() + Velocity * FlightTime
            + FVector(0, 0, -490) * FlightTime * FlightTime).Equals(DripEntry.GetLocation(), .001));
    const float Tilt = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(K->GetActorUpVector().Z, -1.f, 1.f)));
    const float Rate = K->MaxFlowMLPerSecond * FMath::Clamp((Tilt - K->PourTiltDegrees) / 40.f, 0.f, 1.f);
    if (!Near(*this, TEXT("保存65度实际壶流量17.5，不改水量算法"), Rate, 17.5)) return false;
    constexpr float Dt = .02f;
    constexpr double WantedWater = 100;
    const double InitialWater = K->GetPreciseVolumeML();
    const auto Debit = [K, InitialWater]() { return InitialWater - K->GetPreciseVolumeML(); };
    D->SetActorTickEnabled(true);
    K->SetActorTickEnabled(true);
    Cup->SetActorTickEnabled(true);
    bool SawWaterPackets = false, SawBloom = false, SawCoffeePackets = false, SawSteam = false, SawFlow = false;
    double FirstWet = -1, FirstDrip = -1;
    UParticleSystemComponent* OnlySteam = nullptr;
    const auto Observe = [&]()
    {
        const auto B = F.Transfer()->GetBalance();
        SawWaterPackets |= B.PacketCount > 0 && D->DrippedML == 0;
        SawCoffeePackets |= B.PacketCount > 0 && D->DrippedML > 0;
        SawBloom |= D->BrewState == ECoffeeBrewState::Blooming;
        SawFlow |= K->bIsPouring && K->CurrentFlowMLPerSecond > 0;
        if (D->BrewState == ECoffeeBrewState::Blooming) Near(*this, TEXT("真实闷蒸阶段不能提前滴流"), D->DrippedML, 0);
        if (FirstWet < 0 && D->GetIngredientVolumeML(TEXT("Water")) > 0) FirstWet = F.World->GetTimeSeconds();
        if (FirstDrip < 0 && D->DrippedML > 0) FirstDrip = F.World->GetTimeSeconds();
        if (Steam->IsSteamRequested())
        {
            SawSteam = true;
            TestTrue(TEXT("真实落点是原滤杯入口加.3cm法向偏置"), Steam->GetLastImpactPoint().Equals(
                DripEntry.GetLocation() + DripEntry.GetUnitAxis(EAxis::Z) * .3, .03));
        }
        if (auto* PS = Steam->GetSteamComponent())
        {
            if (!OnlySteam) OnlySteam = PS;
            TestTrue(TEXT("所有水包复用同一原Cascade且尺寸仍为保存值"), PS == OnlySteam && PS->Template == SteamTemplate
                && PS->GetOwner() == K && PS->GetComponentScale().Equals(FVector(SavedSteamScale), Tolerance));
        }
        TArray<UParticleSystemComponent*> Components;
        K->GetComponents(Components);
        TestTrue(TEXT("完整链路始终至多一个蒸汽组件"), Components.Num() <= 1);
        TestTrue(TEXT("真实滤杯存量不超过原容量"), D->GetPreciseVolumeML() <= D->MaxVolumeML + Tolerance);
        Near(*this, TEXT("World Tick全毫升账本Error0"), B.ErrorML, 0);
        Near(*this, TEXT("链路不隔空注水"), B.InjectedML, 0);
        Near(*this, TEXT("真实几何水流无洒漏"), B.SpilledML, 0);
        TestTrue(TEXT("源实际累计debit不越过100mL"), Debit() <= WantedWater + 1.e-9);
    };
    // 常规帧只调用World Tick；不额外Simulate/AdvanceBrew双重积分。
    for (int32 Frame = 0; Frame < 320 && WantedWater - Debit() > static_cast<double>(Rate) * Dt + 1.e-9; ++Frame)
    {
        const double Before = Debit();
        F.Tick(Dt);
        Observe();
        if (!TestTrue(TEXT("真实World Tick驱动壶扣正量"), Debit() > Before)) return false;
        Near(*this, TEXT("真实每帧仅扣Rate*Dt"), Debit() - Before, static_cast<double>(Rate) * Dt);
    }
    K->SetActorTickEnabled(false);
    for (int32 Tail = 0; Tail < 8 && WantedWater - Debit() > 1.e-10; ++Tail)
    {
        const double Remaining = WantedWater - Debit();
        float TailDt = static_cast<float>(Remaining / Rate);
        if (static_cast<double>(Rate) * TailDt > Remaining) TailDt = std::nextafter(TailDt, 0.f);
        if (!TestTrue(TEXT("有界尾帧仍走真实PourWater，未手动扣液"), TailDt > 0 && K->PourWater(TailDt) > 0)) return false;
        F.Tick(Dt);
        Observe();
    }
    Near(*this, TEXT("只从真实壶来源扣100mL"), Debit(), 100);
    K->SetActorRotation(FRotator::ZeroRotator, ETeleportType::TeleportPhysics);
    K->SetActorTickEnabled(true);
    for (int32 Frame = 0; Frame < 1250; ++Frame) { F.Tick(Dt); Observe(); }
    TestTrue(TEXT("世界时钟实际观察到水包、闷蒸、咖啡包、出流和蒸汽"),
        SawWaterPackets && SawBloom && SawCoffeePackets && SawSteam && SawFlow && OnlySteam);
    TestTrue(TEXT("实际世界时钟遵守原8秒闷蒸"), FirstWet >= 0 && FirstDrip - FirstWet >= 7.98);
    Near(*this, TEXT("原杯通过生产路径实际Coffee100"), Cup->GetIngredientVolumeML(TEXT("Coffee")), 100);
    Near(*this, TEXT("实际杯总量100"), Cup->GetPreciseVolumeML(), 100);
    Near(*this, TEXT("实际杯Water0"), Cup->GetIngredientVolumeML(TEXT("Water")), 0);
    Near(*this, TEXT("真实壶剩Water400"), K->GetIngredientVolumeML(TEXT("Water")), 400);
    Near(*this, TEXT("滤杯真实Dripped100"), D->DrippedML, 100);
    Near(*this, TEXT("生产出液钩子实际CoffeeProduced100"), D->CoffeeProducedML, 100);
    Near(*this, TEXT("滤杯实际排空"), D->GetPreciseVolumeML(), 0);
    TestTrue(TEXT("同批真实Spent且壶正立停流"), D->BrewState == ECoffeeBrewState::Spent && !K->bIsPouring);
    TestTrue(TEXT("原源/目标入口和半径全过程保持不变"), D->PourEntryPoint->GetComponentTransform().Equals(DripEntry, .001)
        && Cup->PourEntryPoint->GetComponentTransform().Equals(CupEntry, .001)
        && D->PourEntryRadius == DripRadius && Cup->PourEntryRadius == CupRadius);
    const auto Final = F.Transfer()->GetBalance();
    Near(*this, TEXT("全链Initial500"), Final.InitialML, 500);
    Near(*this, TEXT("全链Injected0"), Final.InjectedML, 0);
    Near(*this, TEXT("全链Removed0"), Final.RemovedML, 0);
    Near(*this, TEXT("全链Spilled0"), Final.SpilledML, 0);
    Near(*this, TEXT("全链InFlight0"), Final.InFlightML, 0);
    Near(*this, TEXT("Water400加Coffee100库存仍500"), Final.ContainerML, 500);
    TestEqual(TEXT("实际在途PacketCount0"), Final.PacketCount, 0);
    TestFalse(TEXT("自然Hold到期由真实World组件Tick关闭蒸汽请求"), Steam->IsSteamRequested());
    Near(*this, TEXT("最终豆罐135g"), Jar->GetBeanGrams(), 135);
    Near(*this, TEXT("最终豆克数账本Error0"), Jar->GetBeanBalanceError(), 0);
    NoPowder(*this, Drawer);
    Balance(*this, F);
    AddInfo(TEXT("独立Game World、NoSim真实碰撞、公共API和World时钟；未运行原地图PIE、HMD、渲染蒸汽或音频验收。"));
    return !HasAnyErrors();
}
#endif
