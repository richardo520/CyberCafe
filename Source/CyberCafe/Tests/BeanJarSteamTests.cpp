// writtenpendingrun：UE5.6 独立 Game World / 保存资产只读测试；不代表原地图、HMD、声音设备或像素验收。
// 源 SM 与 Cascade 是否未改由本轮 before.json 的磁盘 SHA 核对，不在 C++ 中猜资产版本。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Regex.h"
#if WITH_EDITOR
#include "ActorEditorUtils.h"
#include "AssetCompilingManager.h"
#endif
#include "Coffee/Bean/DirectBeanJarActor.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "Coffee/Brew/HandDripperAssemblyActor.h"
#include "Misc/DefaultValueHelper.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Coffee/Brew/HotWaterSteamComponent.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/IceInteraction.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "GrabComponent.h"
#include "CollisionQueryParams.h"
#include "CoreGlobals.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
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
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include <cmath>
#include <limits>

namespace BeanJarSteamTests
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double Tolerance = 1.e-5;
const TCHAR* JarPath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/Blueprints/BP_DirectBeanJar.BP_DirectBeanJar_C");
const TCHAR* LidPath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/Blueprints/BP_ScrewBeanJarLid.BP_ScrewBeanJarLid_C");
const TCHAR* BodyPath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/Meshes/SM_BeanJar_Body.SM_BeanJar_Body");
const TCHAR* LidMeshPath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/Meshes/SM_BeanJar_Lid.SM_BeanJar_Lid");
const TCHAR* BeansPath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/Meshes/SM_BeanJar_Beans.SM_BeanJar_Beans");
const TCHAR* ProfilePath = TEXT("/Game/VRTemplate/CoffeeBrew/BeanJar/DA_BeanJarBreak.DA_BeanJarBreak");
const TCHAR* SteamPath = TEXT("/Game/VRTemplate/CoffeeBrew/Particles/P_Steam_Pour_Feedback.P_Steam_Pour_Feedback");
const TCHAR* GrinderPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Coffee/Grinder/BP_CoffeeGrinder.BP_CoffeeGrinder_C");
const TCHAR* KettlePath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandPourKettle.BP_HandPourKettle_C");
const TCHAR* CupPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup03.BP_Cup03_C");
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
const TCHAR* DripperBeforePath = TEXT("C:/Users/cloudcheng/WorkBuddy/2026-11-01-16-35-18/dripper_grab_polish/current_before.json");
const TCHAR* AssemblyPath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandDripperWithStand.BP_HandDripperWithStand_C");
const TCHAR* LegacyDripperPath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandDripper.BP_HandDripper_C");
const TCHAR* FilterName = TEXT("HandDripper_Practice");
const TCHAR* OldStandName = TEXT("HandDripperStand_Practice");

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}
bool Alive(const AActor* Actor) { return IsValid(Actor) && !Actor->IsActorBeingDestroyed(); }
UMaterialInterface* OriginalMaterial(UMaterialInterface* Material)
{
    while (auto* MID = Cast<UMaterialInstanceDynamic>(Material)) Material = MID->Parent;
    return Material;
}

// 只隔离刚体模拟、HUD、手部反馈与非蒸汽表现；不改抓取类型、碰撞响应、网格、口点或业务库存。
void Quiet(AActor* Actor)
{
    if (!Alive(Actor)) return;
    Actor->SetActorTickEnabled(false);
    TArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes);
    for (auto* Mesh : Meshes) Mesh->SetSimulatePhysics(false);
    TArray<UNiagaraComponent*> Niagara; Actor->GetComponents(Niagara);
    for (auto* FX : Niagara) { FX->SetAutoActivate(false); FX->DeactivateImmediate(); FX->SetComponentTickEnabled(false); }
    TArray<UAudioComponent*> Audio; Actor->GetComponents(Audio);
    for (auto* Sound : Audio) { Sound->Stop(); Sound->SetAutoActivate(false); }
    TArray<UGrabComponent*> Grabs; Actor->GetComponents(Grabs);
    for (auto* Grab : Grabs)
    {
        Grab->bCaptureHand = false; Grab->bHideHandWhileHeld = false; Grab->OnGrabHapticEffect = nullptr;
    }
    if (auto* Cup = Cast<ACupActor>(Actor))
    {
        Cup->VolumeWidgetClass = nullptr; Cup->bFaceCamera = false;
        Cup->VolumeWidgetComp->SetWidgetClass(nullptr); Cup->VolumeWidgetComp->SetComponentTickEnabled(false);
    }
    if (auto* Liquid = Cast<ALiquidContainerActor>(Actor))
    {
        Liquid->LiquidFXTemplate = nullptr; Liquid->SplashEffectTemplate = nullptr;
        Liquid->bEnableSplash = false; Liquid->bEnableDecal = false;
    }
}

struct FJarAssets
{
    UClass* JarClass = LoadClass<ADirectBeanJarActor>(nullptr, JarPath);
    UClass* LidClass = LoadClass<ABottleCapActor>(nullptr, LidPath);
    UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, BodyPath);
    UStaticMesh* Lid = LoadObject<UStaticMesh>(nullptr, LidMeshPath);
    UStaticMesh* Beans = LoadObject<UStaticMesh>(nullptr, BeansPath);
    UGlassBreakProfile* Profile = LoadObject<UGlassBreakProfile>(nullptr, ProfilePath);
    bool Check(FAutomationTestBase& Test) const
    {
        bool Good = Test.TestNotNull(TEXT("保存豆罐 BP，缺失不使用原生兜底"), JarClass);
        Good &= Test.TestNotNull(TEXT("保存盖 BP（保留兼容资产名称）"), LidClass);
        if (LidClass) Good &= Test.TestFalse(TEXT("保存盖 CDO 严格为拉拔 false，runtime opt-in 不得掩盖保存字段"),
            LidClass->GetDefaultObject<ABottleCapActor>()->bRequireScrewTwist);
        Good &= Test.TestNotNull(TEXT("分离玻璃体"), Body);
        Good &= Test.TestNotNull(TEXT("分离金属盖"), Lid);
        Good &= Test.TestNotNull(TEXT("分离豆堆"), Beans);
        Good &= Test.TestNotNull(TEXT("保存豆罐破碎配置"), Profile);
        return Good;
    }
};

struct FWorld
{
    FAutomationTestBase& Test;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    explicit FWorld(FAutomationTestBase& InTest) : Test(InTest)
    {
        if (!Test.TestNotNull(TEXT("Engine 可用"), GEngine)) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("独立 Game World"), World)) return;
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine); Context.OwningGameInstance = GI;
        World->SetGameInstance(GI); GI->Init();
    }
    ~FWorld()
    {
        if (!World) return;
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    template <typename T>
    T* Spawn(UClass* Class, const FTransform& Pose = FTransform::Identity)
    {
        if (!World || !Test.TestNotNull(TEXT("真实待生成 class"), Class)) return nullptr;
        auto* Actor = World->SpawnActorDeferred<T>(Class, Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!Test.TestNotNull(TEXT("Deferred 生成"), Actor)) return nullptr;
        Quiet(Actor); Actor->FinishSpawning(Pose); Quiet(Actor);
        Actor->SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
        return Actor;
    }
    ADirectBeanJarActor* Jar(const FJarAssets& Assets, bool Native = false)
    {
        auto* Actor = Spawn<ADirectBeanJarActor>(Native ? ADirectBeanJarActor::StaticClass() : Assets.JarClass);
        if (Actor && Native)
        {
            Actor->ContainerMesh->SetStaticMesh(Assets.Body); Actor->BeanPileMesh->SetStaticMesh(Assets.Beans);
            Actor->CapClass = Assets.LidClass; Actor->GlassBreakProfile = Assets.Profile;
        }
        return Actor;
    }
    AGlassRecoveryActor* ManagerFirst()
    {
        auto* Manager = Spawn<AGlassRecoveryActor>(AGlassRecoveryActor::StaticClass(),
            FTransform(FVector(500, 0, 0)));
        if (Manager) { Manager->bUnifiedRestore = true; Manager->bRestoreAllSupplies = true; }
        return Manager;
    }
    bool Begin()
    {
        if (!World) return false;
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
        for (TActorIterator<AActor> It(World); It; ++It) Quiet(*It);
        return Test.TestNotNull(TEXT("World-local 实际液体子系统"), Transfer());
    }
    ULiquidTransferSubsystem* Transfer() const { return World ? World->GetSubsystem<ULiquidTransferSubsystem>() : nullptr; }
    UMotionControllerComponent* Hand(const FTransform& Pose, EControllerHand Side = EControllerHand::Left)
    {
        auto* Pawn = World->SpawnActor<APawn>();
        if (!Test.TestNotNull(TEXT("手柄 Pawn"), Pawn)) return nullptr;
        auto* MC = NewObject<UMotionControllerComponent>(Pawn);
        Pawn->SetRootComponent(MC); Pawn->AddInstanceComponent(MC);
        MC->SetTrackingSource(Side); MC->RegisterComponent(); MC->SetComponentTickEnabled(false);
        Pawn->SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
        return MC;
    }
    AActor* Cube(const FVector& Center, const FVector& Size)
    {
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!Test.TestNotNull(TEXT("真实阻挡 Cube 网格"), Mesh)) return nullptr;
        auto* Actor = World->SpawnActor<AActor>();
        if (!Test.TestNotNull(TEXT("实际阻挡 Actor"), Actor)) return nullptr;
        auto* Comp = NewObject<UStaticMeshComponent>(Actor);
        Actor->SetRootComponent(Comp); Actor->AddInstanceComponent(Comp);
        Comp->SetStaticMesh(Mesh); Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Comp->SetCollisionObjectType(ECC_WorldStatic); Comp->SetCollisionResponseToAllChannels(ECR_Block);
        Comp->SetSimulatePhysics(false); Comp->RegisterComponent();
        Comp->SetWorldLocation(Center); Comp->SetWorldScale3D(Size / 100.0);
        return Actor;
    }
};

FTransform Socket(const ADirectBeanJarActor* Jar)
{
    return Jar->ContainerMesh->GetSocketTransform(Jar->CapSocketName, RTS_World);
}
void Sample(ABottleCapActor* Cap, UMotionControllerComponent* MC, double Degrees, const FVector& Offset = FVector::ZeroVector, float Dt = .03f)
{
    const FTransform Mouth = Cap->OwnerBottle->ContainerMesh->GetSocketTransform(Cap->CapSocketName, RTS_World);
    const FTransform Pose(Mouth.GetRotation() * FQuat(FVector::UpVector, FMath::DegreesToRadians(Degrees)),
        Mouth.GetLocation() + Mouth.TransformVectorNoScale(Offset));
    MC->SetWorldTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
    // 实际公共有效姿态入口仍检查 Held/controller/socket；不假冒 IsTracked/HMD。
    Cap->SampleScrewHandPose(MC->GetComponentTransform(), Dt);
}
bool GrabCap(FAutomationTestBase& Test, FWorld& Fixture, ADirectBeanJarActor* Jar,
    UMotionControllerComponent*& MC, EControllerHand Side = EControllerHand::Left)
{
    auto* Cap = Jar->CapActor.Get();
    if (!Test.TestNotNull(TEXT("BeginPlay 实际生成盖"), Cap) || !Test.TestNotNull(TEXT("实际盖 GrabComp"), Cap->GrabComp.Get())) return false;
    MC = Fixture.Hand(Socket(Jar), Side);
    if (!MC || !Test.TestTrue(TEXT("真实 Custom TryGrab"), Cap->GrabComp->TryGrab(MC))) return false;
    Test.TestTrue(TEXT("GrabType 保持 Custom"), Cap->GrabComp->GrabType == EGrabType::Custom);
    Test.TestTrue(TEXT("实际持盖且 controller 身份一致"), Cap->GrabComp->IsHeld() && Cap->GrabComp->GetHoldingController() == MC);
    Test.TestEqual(TEXT("实际持有手分类"), static_cast<int32>(Cap->GrabComp->GetHeldByHand()), static_cast<int32>(Side));
    if (Cap->bRequireScrewTwist) Sample(Cap, MC, 0);
    return Test.TestTrue(TEXT("真实抓盖后仍附着封口且进度零；仅旧旋盖 opt-in 建立采样基线"),
        Cap->bIsAttached && Jar->bIsCapped && FMath::IsNearlyZero(Cap->TwistProgress));
}
bool Open(FAutomationTestBase& Test, FWorld& Fixture, ADirectBeanJarActor* Jar,
    UMotionControllerComponent*& MC, EControllerHand Side = EControllerHand::Left)
{
    if (!GrabCap(Test, Fixture, Jar, MC, Side)) return false;
    auto* Cap = Jar->CapActor.Get();
    if (Cap->bRequireScrewTwist)
    {
        // 旧旋盖 opt-in 兼容非当前交互：保持原120度真实采样路径。
        for (int32 Angle = 5; Angle <= 120; Angle += 5) Sample(Cap, MC, Angle);
    }
    else
    {
        const FTransform Mouth = Socket(Jar);
        if (!Test.TestTrue(TEXT("保存拉拔阈值允许沿 Socket Z 移动4cm，不改实际配置"),
            FMath::IsFinite(Cap->DetachPullDistance) && Cap->DetachPullDistance > 0 && Cap->DetachPullDistance <= 4)) return false;
        MC->SetWorldTransform(FTransform(Mouth.GetRotation(), Mouth.GetLocation() + Mouth.GetUnitAxis(EAxis::Z) * 4),
            false, nullptr, ETeleportType::TeleportPhysics);
        // 当前交互只调用生产 Tick 拉拔分支，不直接 Detach 或调用旋盖采样。
        Cap->Tick(.01f);
        Near(Test, TEXT("真实拉拔不累计旋盖进度"), Cap->TwistProgress, 0);
    }
    return Test.TestTrue(Cap->bRequireScrewTwist
        ? TEXT("旧旋盖 opt-in 兼容非当前交互：实际120度开盖且身份不误改")
        : TEXT("当前实际 TryGrab/沿 Socket Z 拉4cm/Tick .01 开盖且身份不误改"),
        !Cap->bIsAttached && !Jar->bIsCapped && Cap->OwnerBottle == Jar && Jar->CapActor == Cap
        && !Jar->bCanPour && Cap->GrabComp->IsHeld() && Cap->GrabComp->GetHoldingController() == MC);
}
bool ReleaseAway(FAutomationTestBase& Test, ADirectBeanJarActor* Jar)
{
    auto* Cap = Jar->CapActor.Get();
    Cap->SetActorLocation(Socket(Jar).GetLocation() + FVector(0, 100, 0), false, nullptr, ETeleportType::TeleportPhysics);
    const bool Released = Test.TestTrue(TEXT("实际 TryRelease 松开独立盖"), Cap->GrabComp->TryRelease());
    Quiet(Cap);
    return Released && Test.TestFalse(TEXT("远处释放不自动盖回"), Cap->bIsAttached);
}
FVector BeanStart(const ADirectBeanJarActor* Jar)
{
    const FTransform Body = Jar->ContainerMesh->GetComponentTransform();
    const FVector Up = Body.GetUnitAxis(EAxis::Z);
    const FVector RimDown = (FVector::DownVector - Up * FVector::DotProduct(FVector::DownVector, Up)).GetSafeNormal();
    const FVector Scale = Body.GetScale3D().GetAbs();
    return Body.TransformPosition(Jar->MouthLocal) + RimDown * Jar->OpeningRadiusCM * FMath::Min(Scale.X, Scale.Y);
}
FVector BeanDirection(const ADirectBeanJarActor* Jar)
{
    return (Jar->ContainerMesh->GetUpVector() * 8.0 + FVector::DownVector * 20.0).GetSafeNormal();
}
void Aim(ADirectBeanJarActor* Jar, const FVector& Entry, double Height = 15.0, double SideMiss = 0.0)
{
    Jar->SetActorRotation(FRotator(120, 0, 0), ETeleportType::TeleportPhysics);
    const FVector Direction = BeanDirection(Jar);
    const FVector WantedStart = Entry + FVector(0, SideMiss, 0) - Direction * (Height / -Direction.Z);
    // 由真实口沿、8/20射向推导起点；不移动磨豆机入口或扩大接收半径。
    Jar->SetActorLocation(Jar->GetActorLocation() + WantedStart - BeanStart(Jar), false, nullptr, ETeleportType::TeleportPhysics);
}
void Balance(FAutomationTestBase& Test, FWorld& Fixture, ADirectBeanJarActor* Jar)
{
    Near(Test, TEXT("克数守恒误差"), Jar->GetBeanBalanceError(), 0);
    Near(Test, TEXT("实际液体账本误差"), Fixture.Transfer()->GetBalance().ErrorML, 0);
}
int32 CapsOutside(FWorld& Fixture, ABottleCapActor* Active)
{
    int32 Count = 0;
    for (TActorIterator<ABottleCapActor> It(Fixture.World); It; ++It) if (Alive(*It) && *It != Active) ++Count;
    return Count;
}

struct FSteamFixture
{
    FWorld Runtime;
    AHandPourKettleActor* Kettle = nullptr;
    ACupActor* Cup = nullptr;
    UHotWaterSteamComponent* Steam = nullptr;
    UParticleSystem* Template = nullptr;
    float SavedSteamScale = 0;
    explicit FSteamFixture(FAutomationTestBase& Test) : Runtime(Test) {}
    bool Init()
    {
        auto& Test = Runtime.Test;
        Template = LoadObject<UParticleSystem>(nullptr, SteamPath);
        if (!Test.TestNotNull(TEXT("原P_Steam_Lit的倒水专用Cascade副本"), Template)) return false;
        Kettle = Runtime.Spawn<AHandPourKettleActor>(LoadClass<AHandPourKettleActor>(nullptr, KettlePath), FTransform(FVector(300, 0, 100)));
        Cup = Runtime.Spawn<ACupActor>(LoadClass<ACupActor>(nullptr, CupPath), FTransform(FVector(0, 0, 100)));
        if (!Kettle || !Cup) return false;
        Cup->FillAmount = 0;
        Steam = Kettle->GetHotWaterSteamComponent();
        if (!Test.TestNotNull(TEXT("保存壶自带原生蒸汽组件，测试不补造组件"), Steam)) return false;
        if (!Test.TestTrue(TEXT("保存壶配置原模板而非测试赋值"), Steam->SteamSystem == Template)) return false;
        const auto* SavedSteam = Kettle->GetClass()->GetDefaultObject<AHandPourKettleActor>()->GetHotWaterSteamComponent();
        if (!Test.TestNotNull(TEXT("实际保存壶 CDO 的蒸汽配置"), SavedSteam)) return false;
        SavedSteamScale = SavedSteam->SteamScale;
        if (!Test.TestTrue(TEXT("实读保存蒸汽缩放有效，视觉增大不改变原模板或液体"),
            FMath::IsFinite(SavedSteamScale) && SavedSteamScale >= .4f)
            || !Near(Test, TEXT("蒸汽实例继承实际保存缩放，不写旧 .12 常量"), Steam->SteamScale, SavedSteamScale)) return false;
        if (!Runtime.Begin()) return false;
        return Test.TestTrue(TEXT("落点前无请求且未创建PS组件"), !Steam->IsSteamRequested() && !Steam->GetSteamComponent());
    }
    FVector Entry() const { return Cup->PourEntryPoint->GetComponentLocation(); }
    double Emit(ALiquidContainerActor* Source = nullptr, double ML = .5)
    {
        return Runtime.Transfer()->EmitVolume(Source ? Source : Kettle, ML, Entry() + FVector(0, 0, 10),
            FVector(0, 0, -100), FVector::ZeroVector);
    }
    void Settle() { Runtime.Transfer()->Simulate(.2); }
    int32 ParticleComponents() const
    {
        TArray<UParticleSystemComponent*> Components; Kettle->GetComponents(Components);
        int32 Count = 0; for (auto* C : Components) if (IsValid(C) && !C->IsBeingDestroyed()) ++Count;
        return Count;
    }
};
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamAssets, "CyberCafe.BeanJarSteam.SavedAssetsSeparatedAndConfigured", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamAssets::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    auto* Glass = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Coffee_Equipment/Meshes/Jar/SM_Jar_Glass.SM_Jar_Glass"));
    auto* Combined = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Coffee_Equipment/Meshes/Jar/SM_Jar_Coffee_Beans_Nanite_1.SM_Jar_Coffee_Beans_Nanite_1"));
    if (!TestNotNull(TEXT("原玻璃源网格"), Glass) || !TestNotNull(TEXT("原盖豆组合源网格"), Combined)) return false;
    TestTrue(TEXT("三个新资产身份彼此分离且不是原SM"), A.Body != A.Lid && A.Lid != A.Beans && A.Body != A.Beans && A.Body != Glass && A.Lid != Combined && A.Beans != Combined);
    if (!TestEqual(TEXT("原玻璃材质槽"), Glass->GetStaticMaterials().Num(), 1)
        || !TestEqual(TEXT("原组合材质槽"), Combined->GetStaticMaterials().Num(), 2)) return false;
    const TArray<UStaticMesh*> Parts = {A.Body, A.Lid, A.Beans};
    const TArray<UMaterialInterface*> Materials = {Glass->GetStaticMaterials()[0].MaterialInterface.Get(), Combined->GetStaticMaterials()[0].MaterialInterface.Get(), Combined->GetStaticMaterials()[1].MaterialInterface.Get()};
    for (int32 I = 0; I < Parts.Num(); ++I)
    {
        auto* Part = Parts[I];
        if (!TestTrue(TEXT("LOD0有真实section和材质槽"), Part->GetNumLODs() > 0 && Part->GetNumSections(0) > 0 && Part->GetStaticMaterials().Num() > 0)) return false;
        for (int32 Section = 0; Section < Part->GetNumSections(0); ++Section)
        {
            const int32 MaterialIndex = Part->GetSectionInfoMap().Get(0, Section).MaterialIndex;
            if (!TestTrue(TEXT("实际section材质索引有效"), Part->GetStaticMaterials().IsValidIndex(MaterialIndex))) return false;
            TestTrue(TEXT("glass/metal/coffee表面复用对应原材质，不把保留的未使用槽当成表面"), Part->GetStaticMaterials()[MaterialIndex].MaterialInterface == Materials[I]);
        }
        TestTrue(TEXT("LOD0 有三角且无巨大错误导入"), Part->GetNumTriangles(0) > 0 && Part->GetNumTriangles(0) <= 10000);
        TestTrue(TEXT("部件包围盒有限且尺寸合理"), !Part->GetBoundingBox().GetCenter().ContainsNaN() && Part->GetBoundingBox().GetSize().GetMax() < 50);
    }
    TestEqual(TEXT("body 对应原玻璃几何三角数"), A.Body->GetNumTriangles(0), Glass->GetNumTriangles(0));
    TestEqual(TEXT("盖豆分离对应原SOURCE_MODEL的6560三角，不误用Nanite fallback的3726三角"), A.Lid->GetNumTriangles(0) + A.Beans->GetNumTriangles(0), 6560);
    TestEqual(TEXT("盖子材质选择获得原1312三角"), A.Lid->GetNumTriangles(0), 1312);
    TestEqual(TEXT("豆堆材质选择获得原5248三角"), A.Beans->GetNumTriangles(0), 5248);
    TestTrue(TEXT("body 包围盒对应原玻璃几何"), A.Body->GetBoundingBox().Min.Equals(Glass->GetBoundingBox().Min, .02) && A.Body->GetBoundingBox().Max.Equals(Glass->GetBoundingBox().Max, .02));
    TestTrue(TEXT("盖中心已移至局部原点"), A.Lid->GetBoundingBox().GetCenter().IsNearlyZero(.02));
    auto* Socket = A.Body->FindSocket(TEXT("CapSocket"));
    if (!TestNotNull(TEXT("body 真实 CapSocket"), Socket)) return false;
    TestTrue(TEXT("CapSocket 高于19cm且不在玻璃中段"), Socket->RelativeLocation.Z > 19 && Socket->RelativeLocation.Z < 25);
    for (auto* Mesh : {A.Body, A.Lid})
    {
        auto* Setup = Mesh->GetBodySetup(); if (!TestNotNull(TEXT("body/lid 保留碰撞元数据"), Setup)) return false;
        TestTrue(TEXT("body/lid 有简单碰撞"), Setup->AggGeom.GetElementCount() > 0);
    }
    TestEqual(TEXT("真实 profile 恰有八片"), A.Profile->Shards.Num(), 8);
    TestEqual(TEXT("物理碎片预算八片"), A.Profile->MaxPhysicsShards, 8);
    FBox ShardBounds(ForceInit);
    for (const auto& Shard : A.Profile->Shards)
    {
        if (!TestNotNull(TEXT("碎片是保存真实网格"), Shard.Mesh.Get())) return false;
        TestTrue(TEXT("碎片不是原整罐假几何"), Shard.Mesh != Glass && Shard.Mesh != A.Body);
        TestTrue(TEXT("碎片几何和局部变换有效"), Shard.Mesh->GetNumTriangles(0) > 0 && Shard.Mesh->GetNumTriangles(0) < 10000 && !Shard.LocalTransform.ContainsNaN());
        ShardBounds += Shard.Mesh->GetBoundingBox().TransformBy(Shard.LocalTransform);
    }
    TestTrue(TEXT("八片合并包围盒对应原玻璃轮廓"), ShardBounds.Min.Equals(Glass->GetBoundingBox().Min, 1.0) && ShardBounds.Max.Equals(Glass->GetBoundingBox().Max, 1.0));
    BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A);
    if (!Jar) return false;
    TestTrue(TEXT("保存配置本身是150g零液体适配，不依赖BeginPlay修补"), Jar->InitialBeanGrams == 150 && Jar->BeanCapacityGrams == 150 && Jar->FillAmount == 0 && Jar->InitialIngredientId == FName(TEXT("Unknown")) && !Jar->bEnablePourAudio && !Jar->bAcceptLiquidFromOthers);
    if (!F.Begin()) return false;
    auto* Cap = Jar->CapActor.Get(); if (!TestNotNull(TEXT("保存类生成配套盖"), Cap)) return false;
    TestTrue(TEXT("保存body/beans/lid组件用分离网格"), Jar->ContainerMesh->GetStaticMesh() == A.Body && Jar->BeanPileMesh->GetStaticMesh() == A.Beans && Cap->CapMesh->GetStaticMesh() == A.Lid);
    TestFalse(TEXT("保存新盖实例严格拉拔 false，兼容 BP_ScrewBeanJarLid 名称不改"), Cap->bRequireScrewTwist);
    TestTrue(TEXT("保存类身份与闲置旧旋盖120度正向参数保留，非当前交互要求"),
        Cap->GetClass() == A.LidClass && Jar->CapClass.Get() == A.LidClass
        && Cap->UnlockAngleDegrees == 120 && Cap->OpeningDirection == 1);
    TestTrue(TEXT("保存豆流仅视觉缩放迁移到 .4，克数契约不变"), Jar->BeanStream->GetRelativeScale3D().Equals(FVector(.4), BeanJarSteamTests::Tolerance));
    auto* OldBottleClass = LoadClass<ABottleActor>(nullptr,
        TEXT("/Game/VRTemplate/MenuIngredients/Bottles/BP_Ingredient_Water.BP_Ingredient_Water_C"));
    if (!TestNotNull(TEXT("原保存水瓶资产，旧盖开盖方式不使用原生兜底"), OldBottleClass)) return false;
    const auto* OldBottle = OldBottleClass->GetDefaultObject<ABottleActor>();
    if (!TestNotNull(TEXT("原水瓶保存真实 CapClass"), OldBottle->CapClass.Get())) return false;
    TestTrue(TEXT("原盖资产保持独立身份且实际 CDO 仍为拉拔 false"),
        OldBottle->CapClass.Get() != A.LidClass && !OldBottle->CapClass->GetDefaultObject<ABottleCapActor>()->bRequireScrewTwist);
    TestTrue(TEXT("旧原生盖仍默认拉拔"), !GetDefault<ABottleCapActor>()->bRequireScrewTwist);
    TestTrue(TEXT("保存profile绑定到真实Glass组件"), Jar->GlassBreakProfile == A.Profile && Jar->FindComponentByClass<UGlassBreakableComponent>() && Jar->FindComponentByClass<UGlassBreakableComponent>()->Profile == A.Profile);
    for (int32 I = 0; I < 3; ++I)
    {
        UStaticMeshComponent* Comp = I == 0 ? Jar->ContainerMesh.Get() : I == 1 ? Cap->CapMesh.Get() : Jar->BeanPileMesh.Get();
        for (int32 Section = 0; Section < Parts[I]->GetNumSections(0); ++Section)
        {
            const int32 MaterialIndex = Parts[I]->GetSectionInfoMap().Get(0, Section).MaterialIndex;
            TestTrue(TEXT("运行组件表面仍对应原始材质"), BeanJarSteamTests::OriginalMaterial(Comp->GetMaterial(MaterialIndex)) == Materials[I]);
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamSavedMap, "CyberCafe.BeanJarSteam.SavedMap313AndUnified20", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamSavedMap::RunTest(const FString&)
{
    using namespace BeanJarSteamTests;
    FString Before;
    if (!TestTrue(TEXT("读取本轮已包含豆罐的313源布局，不回退旧bean快照"), FFileHelper::LoadFileToString(Before, DripperBeforePath))) return false;
    const int32 Start = Before.Find(TEXT("\"actors\""));
    if (!TestTrue(TEXT("源基线含actors区块"), Start != INDEX_NONE)) return false;
    struct FOriginal { FString Path, Class, Label; FTransform Pose; };
    TMap<FString, FOriginal> Originals;
    const FRegexPattern Pattern(TEXT("\"([^\"]+)\"\\s*:\\s*\\{\\s*\"path\"\\s*:\\s*\"([^\"]+)\"\\s*,\\s*\"class\"\\s*:\\s*\"([^\"]+)\"\\s*,\\s*\"pose\"\\s*:\\s*\\[([^\\]]+)\\]\\s*,\\s*\"label\"\\s*:\\s*\"([^\"]*)\""));
    const FString ActorText = Before.Mid(Start);
    FRegexMatcher Matcher(Pattern, ActorText);
    while (Matcher.FindNext())
    {
        TArray<FString> Values; Matcher.GetCaptureGroup(4).ParseIntoArray(Values, TEXT(","), false);
        if (!TestEqual(TEXT("源每个pose恰好九个数"), Values.Num(), 9)) return false;
        double V[9];
        for (int32 I = 0; I < 9; ++I)
            if (!TestTrue(TEXT("源pose数值可解析且有限"), FDefaultValueHelper::ParseDouble(Values[I].TrimStartAndEnd(), V[I]) && FMath::IsFinite(V[I]))) return false;
        const FString Name = Matcher.GetCaptureGroup(1);
        if (!TestFalse(TEXT("源身份名不重复"), Originals.Contains(Name))) return false;
        Originals.Add(Name, {Matcher.GetCaptureGroup(2), Matcher.GetCaptureGroup(3), Matcher.GetCaptureGroup(5),
            FTransform(FRotator(V[3], V[4], V[5]), FVector(V[0], V[1], V[2]), FVector(V[6], V[7], V[8]))});
    }
    if (!TestEqual(TEXT("源布局严格313个完整身份"), Originals.Num(), 313)) return false;
    const auto* OldFilter = Originals.Find(FilterName); const auto* OldStand = Originals.Find(OldStandName);
    if (!TestNotNull(TEXT("源精确旧滤杯"), OldFilter) || !TestNotNull(TEXT("源精确唯一旧支架"), OldStand)) return false;
    TestEqual(TEXT("源滤杯是固定旧BP"), OldFilter->Class, FString(LegacyDripperPath));
    TestEqual(TEXT("源独立支架是原Actor"), OldStand->Class, FString(TEXT("/Script/Engine.Actor")));
    auto* Map = LoadObject<UWorld>(nullptr, BeanJarSteamTests::MapPath);
    if (!TestNotNull(TEXT("只读保存地图"), Map) || !TestNotNull(TEXT("保存PersistentLevel"), Map->PersistentLevel.Get())) return false;
    if (!TestFalse(TEXT("不开始保存地图的游戏"), Map->HasBegunPlay())) return false;
    if (!Map->bIsWorldInitialized)
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        Map->InitWorld(IV);
    }
    Map->UpdateWorldComponents(true, false); // 只注册以读取真实组件世界TM，不BeginPlay、Tick或保存。
    TMap<FString, AActor*> Actors;
    ADirectBeanJarActor* Jar = nullptr; AGlassRecoveryActor* Manager = nullptr;
    int32 Jars = 0, Managers = 0, Added = 0;
    for (AActor* Actor : Map->PersistentLevel->Actors)
    {
        if (!BeanJarSteamTests::Alive(Actor) || !Actor->IsEditable() || !Actor->IsListedInSceneOutliner() || Actor->IsTemplate() || Actor->HasAnyFlags(RF_Transient) || Cast<AWorldSettings>(Actor) || FActorEditorUtils::IsABuilderBrush(Actor)) continue;
        Actors.Add(Actor->GetName(), Actor);
        if (auto* J = Cast<ADirectBeanJarActor>(Actor)) { ++Jars; Jar = J; }
        if (auto* M = Cast<AGlassRecoveryActor>(Actor)) { ++Managers; Manager = M; }
        if (!Originals.Contains(Actor->GetName())) ++Added;
        TestFalse(TEXT("未引入BeanScoopActor根"), Actor->GetClass()->GetName().Contains(TEXT("BeanScoopActor")));
    }
    TestEqual(TEXT("当前保存地图是源313减一个独立支架"), Actors.Num(), Originals.Num() - 1);
    TestEqual(TEXT("源已包含豆罐，不新增任何身份"), Added, 0); TestEqual(TEXT("豆罐唯一"), Jars, 1); TestEqual(TEXT("经理唯一"), Managers, 1);
    TestFalse(TEXT("旧独立支架已移除，不仅比较数量"), Actors.Contains(OldStandName));
    int32 Preserved = 0, OtherClasses = 0, ChangedFilter = 0;
    for (const auto& Pair : Originals)
    {
        if (Pair.Key == OldStandName) continue;
        auto* const* Found = Actors.Find(Pair.Key);
        if (!TestNotNull(Pair.Key + TEXT(" 源保留身份必须存在"), Found)) return false;
        ++Preserved;
        if (Pair.Key == FilterName)
        {
            ++ChangedFilter;
            TestEqual(TEXT("滤杯仅允许精确新组合BP"), (*Found)->GetClass()->GetPathName(), FString(AssemblyPath));
            TestTrue(TEXT("实际新滤杯继承组合原生类"), (*Found)->IsA<AHandDripperAssemblyActor>());
        }
        else
        {
            ++OtherClasses;
            TestEqual(Pair.Key + TEXT(" 原身份class未改变"), (*Found)->GetClass()->GetPathName(), Pair.Value.Class);
        }
        TestEqual(Pair.Key + TEXT(" 保存Actor路径不变"), (*Found)->GetPathName(), Pair.Value.Path);
        TestEqual(Pair.Key + TEXT(" 源label不变"), (*Found)->GetActorLabel(), Pair.Value.Label);
        TestTrue(Pair.Key + TEXT(" 源完整位置旋转缩放不变"), (*Found)->GetActorTransform().Equals(Pair.Value.Pose, .001));
    }
    TestEqual(TEXT("原名完整保留312个Actor"), Preserved, 312);
    TestEqual(TEXT("其余311个Actor含豆罐class和pose不变"), OtherClasses, 311);
    TestEqual(TEXT("只有一个指定滤杯class授权变化"), ChangedFilter, 1);
    if (!TestNotNull(TEXT("源豆罐"), Jar) || !TestNotNull(TEXT("统一经理"), Manager)) return false;
    TestTrue(TEXT("豆罐使用源已有精确保存类"), Originals.Contains(Jar->GetName()) && Jar->GetClass()->GetPathName() == JarPath);
    auto* D = Cast<AHandDripperAssemblyActor>(Actors.FindRef(FilterName));
    if (!TestNotNull(TEXT("实际保存组合滤杯，无旧类兜底"), D)) return false;
    TestTrue(TEXT("滤杯原ContainerMesh仍是root，完整世界TM未动"), D->GetRootComponent() == D->ContainerMesh.Get()
        && D->ContainerMesh->GetComponentTransform().Equals(OldFilter->Pose, Tolerance)
        && OldFilter->Pose.Equals(FTransform(FVector(-33, 131, 67)), Tolerance));
    const TArray<UStaticMeshComponent*> Pieces = {D->StandLeftLeg.Get(), D->StandRightLeg.Get(), D->StandLeftShelf.Get(), D->StandRightShelf.Get()};
    const TCHAR* PieceNames[] = {TEXT("StandLeftLeg"), TEXT("StandRightLeg"), TEXT("StandLeftShelf"), TEXT("StandRightShelf")};
    for (int32 I = 0; I < Pieces.Num(); ++I)
    {
        auto* Piece = Pieces[I]; if (!TestNotNull(TEXT("继承四件支架组件齐全"), Piece)) return false;
        TestEqual(TEXT("继承支架声明名准确"), Piece->GetName(), FString(PieceNames[I]));
        TestTrue(TEXT("四件支架直接附属同一滤杯根且可见"), Piece->GetOwner() == D
            && Piece->GetAttachParent() == D->ContainerMesh.Get() && Piece->IsVisible() && !Piece->bHiddenInGame);
    }
    const int32 StandStart = Before.Find(TEXT("\"stands\""));
    const int32 DrawerStart = Before.Find(TEXT("\"drawer\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, StandStart);
    if (!TestTrue(TEXT("源支架元数据独立存在且位于drawer之前"), StandStart != INDEX_NONE && DrawerStart > StandStart)) return false;
    const FString StandText = Before.Mid(StandStart, DrawerStart - StandStart);
    const FRegexPattern WorldPattern(TEXT("\"world_transform\"\\s*:\\s*\"[^\"]*rotation:\\s*\\{x:\\s*([^,]+),\\s*y:\\s*([^,]+),\\s*z:\\s*([^,]+),\\s*w:\\s*([^}]+)\\},\\s*translation:\\s*\\{x:\\s*([^,]+),\\s*y:\\s*([^,]+),\\s*z:\\s*([^}]+)\\},\\s*scale3d:\\s*\\{x:\\s*([^,]+),\\s*y:\\s*([^,]+),\\s*z:\\s*([^}]+)\\}"));
    FRegexMatcher WorldMatch(WorldPattern, StandText);
    int32 PieceIndex = 0;
    while (WorldMatch.FindNext())
    {
        if (!TestTrue(TEXT("源支架不能超过四件"), Pieces.IsValidIndex(PieceIndex))) return false;
        double V[10];
        for (int32 I = 0; I < 10; ++I)
            if (!TestTrue(TEXT("源支架worldTM各数值有限"), FDefaultValueHelper::ParseDouble(WorldMatch.GetCaptureGroup(I + 1).TrimStartAndEnd(), V[I]) && FMath::IsFinite(V[I]))) return false;
        const FQuat Q(V[0], V[1], V[2], V[3]);
        if (!TestTrue(TEXT("源支架四元数有效且缩放非零"), Q.IsNormalized() && V[7] > 0 && V[8] > 0 && V[9] > 0)) return false;
        const FTransform OldWorld(Q, FVector(V[4], V[5], V[6]), FVector(V[7], V[8], V[9]));
        TestTrue(TEXT("继承四支架的完整世界TM与原源网格逐件相同"), Pieces[PieceIndex]->GetComponentTransform().Equals(OldWorld, Tolerance));
        ++PieceIndex;
    }
    TestEqual(TEXT("源恰好解析四个旧支架世界TM，不用空循环跳过"), PieceIndex, 4);
    TestTrue(TEXT("保存统一全用品opt-in"), Manager->bUnifiedRestore && Manager->bRestoreAllSupplies);
    TestEqual(TEXT("保存replacement16"), Manager->RestoreTargets.Num(), 16);
    TestEqual(TEXT("保存tool3"), Manager->RestoreToolTargets.Num(), 3);
    TSet<AActor*> Roots;
    for (const auto& Target : Manager->RestoreTargets) { if (!TestNotNull(TEXT("replacement非空"), Target.Get())) return false; Roots.Add(Target.Get()); }
    TestEqual(TEXT("replacement无重复"), Roots.Num(), 16); TestTrue(TEXT("豆罐在replacement内"), Roots.Contains(Jar));
    TestTrue(TEXT("原型首杯首瓶属于replacement集合"), Roots.Contains(Manager->PrototypeCup.Get()) && Roots.Contains(Manager->PrototypeBottle.Get()));
    int32 AssemblyTools = 0;
    for (const auto& Tool : Manager->RestoreToolTargets)
    {
        if (!TestNotNull(TEXT("tool非空"), Tool.Get())) return false;
        Roots.Add(Tool.Get()); AssemblyTools += Tool.Get() == D;
        TestFalse(TEXT("工具不能残留独立支架引用"), Tool->GetName() == OldStandName);
    }
    TestEqual(TEXT("原滤杯工具槽精确remap为当前组合体且仅一次"), AssemblyTools, 1);
    for (const auto& Target : Manager->RestoreTargets)
        TestFalse(TEXT("接液杯不附着组合体"), Target->IsA<ACupActor>() && Target->IsAttachedTo(D));
    const int32 RecoveryStart = Before.Find(TEXT("\"recovery\""));
    const int32 FilterStart = Before.Find(TEXT("\"filter\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, RecoveryStart);
    if (!TestTrue(TEXT("源recovery区块存在且先于filter元数据"), RecoveryStart != INDEX_NONE && FilterStart > RecoveryStart)) return false;
    const FString RecoveryText = Before.Mid(RecoveryStart, FilterStart - RecoveryStart);
    for (const auto* Key : {TEXT("targets"), TEXT("tools")})
    {
        const FRegexPattern ArrayPattern(FString::Printf(TEXT("\"%s\"\\s*:\\s*\\[([^\\]]*)\\]"), Key));
        FRegexMatcher ArrayMatch(ArrayPattern, RecoveryText);
        if (!TestTrue(TEXT("源恢复成员数组必须存在"), ArrayMatch.FindNext())) return false;
        const FString NamesText = ArrayMatch.GetCaptureGroup(1);
        const FRegexPattern NamePattern(TEXT("\"([^\"]+)\""));
        FRegexMatcher NameMatch(NamePattern, NamesText);
        TArray<FString> Names; while (NameMatch.FindNext()) Names.Add(NameMatch.GetCaptureGroup(1));
        const bool IsTools = FString(Key) == TEXT("tools");
        const int32 Count = IsTools ? Manager->RestoreToolTargets.Num() : Manager->RestoreTargets.Num();
        if (!TestEqual(TEXT("源与当前恢复成员数量一致"), Names.Num(), Count)) return false;
        for (int32 I = 0; I < Count; ++I)
        {
            AActor* A = IsTools ? Manager->RestoreToolTargets[I].Get() : static_cast<AActor*>(Manager->RestoreTargets[I].Get());
            TestEqual(TEXT("源恢复成员名称和顺序保持，仅同名滤杯class换新"), A->GetName(), Names[I]);
            TestTrue(TEXT("恢复成员必须来自当前保存地图"), A->GetLevel() == Map->PersistentLevel.Get());
        }
    }
    if (!TestNotNull(TEXT("独立ice scoop"), Manager->RestoreScoop.Get())) return false;
    Roots.Add(Manager->RestoreScoop.Get()); TestEqual(TEXT("16replacement+3tool+1ice严格20根"), Roots.Num(), 20);
    // 原 NPC 起始偏移不在本测试职责内；不修改、不测其阈值，也不宣称通过。
    return !HasAnyErrors();
}

#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamZeroLiquid, "CyberCafe.BeanJarSteam.NativeZeroLiquidAdapter", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamZeroLiquid::RunTest(const FString&)
{
    const auto* Defaults = GetDefault<ADirectBeanJarActor>();
    TestTrue(TEXT("原生默认液体呈现本就关闭，非夹具Quiet伪造"), !Defaults->LiquidFX->bAutoActivate && !Defaults->PourFX->bAutoActivate && !Defaults->bEnablePourAudio && Defaults->FillAmount == 0);
    BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Spawn<ADirectBeanJarActor>(ADirectBeanJarActor::StaticClass());
    if (!Jar || !F.Begin()) return false;
    BeanJarSteamTests::Near(*this, TEXT("原生FillAmount零"), Jar->FillAmount, 0);
    TestEqual(TEXT("原生InitialUnknown"), Jar->InitialIngredientId, FName(TEXT("Unknown")));
    TestFalse(TEXT("不接受液体"), Jar->bAcceptLiquidFromOthers); TestFalse(TEXT("无倒液声音权限"), Jar->bEnablePourAudio);
    TestFalse(TEXT("不请求倒液声音"), Jar->IsPourAudioRequested()); TestFalse(TEXT("不进入液体出流"), Jar->bCanPour || Jar->bIsPouring);
    BeanJarSteamTests::Near(*this, TEXT("原生库存150g"), Jar->GetBeanGrams(), 150);
    Jar->InitializeBeans(); BeanJarSteamTests::Near(*this, TEXT("重复初始化不加库存"), Jar->GetBeanGrams(), 150);
    BeanJarSteamTests::Near(*this, TEXT("实际容器毫升零"), Jar->GetPreciseVolumeML(), 0);
    BeanJarSteamTests::Near(*this, TEXT("EmitVolume不能把豆克数当液体"), F.Transfer()->EmitVolume(Jar, 5, FVector::ZeroVector, FVector::DownVector, FVector::ZeroVector), 0);
    TestEqual(TEXT("未生成液体包"), F.Transfer()->GetBalance().PacketCount, 0);
    BeanJarSteamTests::Near(*this, TEXT("世界液体Initial零"), F.Transfer()->GetBalance().InitialML, 0);
    BeanJarSteamTests::Near(*this, TEXT("世界液体Container零"), F.Transfer()->GetBalance().ContainerML, 0);
    TestTrue(TEXT("液体Niagara未自动激活且隐藏"), !Jar->LiquidFX->bAutoActivate && !Jar->LiquidFX->IsVisible() && !Jar->PourFX->bAutoActivate && !Jar->PourFX->IsVisible());
    BeanJarSteamTests::Balance(*this, F, Jar); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamTwistUnlock, "CyberCafe.BeanJarSteam.ActualTwistUnlockUsingController.LegacyScrewOptInCompatibilityNotCurrentInteraction", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamTwistUnlock::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    for (bool Native : {false, true}) for (auto Side : {EControllerHand::Left, EControllerHand::Right})
    {
        BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A, Native); if (!Jar || !F.Begin()) return false;
        auto* Cap = Jar->CapActor.Get(); if (!TestNotNull(TEXT("旧旋盖 opt-in 的真实盖"), Cap)) return false;
        // 旧旋盖 opt-in 兼容非当前交互：仅改本 fixture 实例，保存 CDO 必须仍为 false。
        Cap->bRequireScrewTwist = true;
        AddInfo(TEXT("旧旋盖 opt-in 兼容非当前交互：保留不足120度、逐5度、阈值开盖与左右手覆盖。"));
        UMotionControllerComponent* MC = nullptr; if (!BeanJarSteamTests::GrabCap(*this, F, Jar, MC, Side)) return false;
        TestFalse(TEXT("CPU夹具不是伪装Tracked控制器"), MC->IsTracked());
        for (int32 Angle = 5; Angle < 120; Angle += 5)
        {
            BeanJarSteamTests::Sample(Cap, MC, Angle);
            TestTrue(TEXT("不足120度保持附着和本体封口"), Cap->bIsAttached && Jar->bIsCapped);
            BeanJarSteamTests::Near(*this, TEXT("每5度真实相对角累积"), Cap->TwistProgress, Angle);
        }
        BeanJarSteamTests::Sample(Cap, MC, 120);
        TestTrue(TEXT("阈值自动调用Detach真实开盖"), !Cap->bIsAttached && !Jar->bIsCapped && Cap->OwnerBottle == Jar && Jar->CapActor == Cap);
        TestTrue(TEXT("开盖仍held并进入原地抓取"), Cap->GrabComp->IsHeld() && Cap->GrabComp->IsGrabbingInPlace());
        BeanJarSteamTests::Near(*this, TEXT("阈值进度"), Cap->TwistProgress, 120);
        TestFalse(TEXT("解锁不能恢复豆罐倒液权限"), Jar->bCanPour);
        TestTrue(TEXT("本体碰撞元数据未移除"), Jar->ContainerMesh->GetStaticMesh() == A.Body && A.Body->GetBodySetup());
        BeanJarSteamTests::Balance(*this, F, Jar);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamTwistFailClosed, "CyberCafe.BeanJarSteam.TwistPullReverseJumpFailClosed.LegacyScrewOptInCompatibilityNotCurrentInteraction", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamTwistFailClosed::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A); if (!Jar || !F.Begin()) return false;
    auto* Cap = Jar->CapActor.Get(); if (!TestNotNull(TEXT("旧旋盖 opt-in 的真实盖"), Cap)) return false;
    // 旧旋盖 opt-in 兼容非当前交互：只设置本 fixture 的实际实例，不改保存资产。
    Cap->bRequireScrewTwist = true;
    AddInfo(TEXT("旧旋盖 opt-in 兼容非当前交互：原失败护栏、相对角或回盖重开断言保持。"));
    UMotionControllerComponent* MC = nullptr; if (!BeanJarSteamTests::GrabCap(*this, F, Jar, MC)) return false;
    BeanJarSteamTests::Sample(Cap, MC, 0, FVector(0, 0, 10)); Cap->Tick(.03f); Cap->DetachFromBottle(MC);
    TestTrue(TEXT("直接拉10cm和直接Detach均fail-closed"), Cap->bIsAttached && Jar->bIsCapped);
    BeanJarSteamTests::Near(*this, TEXT("仅拉动无旋角"), Cap->TwistProgress, 0);
    BeanJarSteamTests::Sample(Cap, MC, 0);
    for (int32 Angle = 5; Angle <= 20; Angle += 5) BeanJarSteamTests::Sample(Cap, MC, Angle);
    BeanJarSteamTests::Near(*this, TEXT("正转20"), Cap->TwistProgress, 20);
    for (int32 Angle = 15; Angle >= 0; Angle -= 5) BeanJarSteamTests::Sample(Cap, MC, Angle);
    BeanJarSteamTests::Near(*this, TEXT("反转20抵消，净角零"), Cap->TwistProgress, 0);
    for (int32 Angle = -5; Angle >= -20; Angle -= 5) BeanJarSteamTests::Sample(Cap, MC, Angle);
    for (int32 Angle = -15; Angle <= 0; Angle += 5) BeanJarSteamTests::Sample(Cap, MC, Angle);
    // 零处反向被钳制，随后正向20是20而非数学负库存；回到零后独立测跳变。
    BeanJarSteamTests::Near(*this, TEXT("零处不存负进度"), Cap->TwistProgress, 20);
    for (int32 Angle = -5; Angle >= -20; Angle -= 5) BeanJarSteamTests::Sample(Cap, MC, Angle);
    BeanJarSteamTests::Near(*this, TEXT("回撤后零"), Cap->TwistProgress, 0);
    BeanJarSteamTests::Sample(Cap, MC, 120); // 从-20跳140度，绝不补计。
    BeanJarSteamTests::Near(*this, TEXT("跳140度忽略"), Cap->TwistProgress, 0);
    BeanJarSteamTests::Sample(Cap, MC, 125); BeanJarSteamTests::Near(*this, TEXT("异常后的首帧重建基线"), Cap->TwistProgress, 0);
    BeanJarSteamTests::Sample(Cap, MC, 130, FVector::ZeroVector, .001f);
    BeanJarSteamTests::Near(*this, TEXT("超720度每秒速率忽略"), Cap->TwistProgress, 0);
    Jar->SetActorRotation(FRotator(120, 0, 0), ETeleportType::TeleportPhysics);
    BeanJarSteamTests::Near(*this, TEXT("封口倾斜120度仍不能倒豆"), Jar->PourBeans(.2f), 0);
    BeanJarSteamTests::Near(*this, TEXT("封口失败路径库存不变"), Jar->GetBeanGrams(), 150);
    TestTrue(TEXT("异常姿态仍封闭"), Cap->bIsAttached && Jar->bIsCapped);
    BeanJarSteamTests::Balance(*this, F, Jar); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamReclose, "CyberCafe.BeanJarSteam.WholeJarMotionAndActualReclose.LegacyScrewOptInCompatibilityNotCurrentInteraction", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamReclose::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A); if (!Jar || !F.Begin()) return false;
    auto* Cap = Jar->CapActor.Get(); if (!TestNotNull(TEXT("旧旋盖 opt-in 的真实盖"), Cap)) return false;
    // 旧旋盖 opt-in 兼容非当前交互：只设置本 fixture 的实际实例，不改保存资产。
    Cap->bRequireScrewTwist = true;
    AddInfo(TEXT("旧旋盖 opt-in 兼容非当前交互：原失败护栏、相对角或回盖重开断言保持。"));
    UMotionControllerComponent* MC = nullptr; if (!BeanJarSteamTests::GrabCap(*this, F, Jar, MC)) return false;
    for (int32 Angle = 5; Angle <= 150; Angle += 5)
    {
        Jar->SetActorRotation(FRotator(Angle * .3, Angle, Angle * .2), ETeleportType::TeleportPhysics);
        BeanJarSteamTests::Sample(Cap, MC, 0);
        BeanJarSteamTests::Near(*this, TEXT("罐和手同转、相对姿态不变，进度零"), Cap->TwistProgress, 0);
        TestTrue(TEXT("整体旋转不解锁"), Cap->bIsAttached);
    }
    for (int32 Angle = 5; Angle <= 120; Angle += 5) BeanJarSteamTests::Sample(Cap, MC, Angle);
    if (!TestFalse(TEXT("第一次真实旋开"), Cap->bIsAttached) || !BeanJarSteamTests::ReleaseAway(*this, Jar)) return false;
    MC->SetWorldTransform(BeanJarSteamTests::Socket(Jar));
    if (!TestTrue(TEXT("真实重新抓独立盖"), Cap->GrabComp->TryGrab(MC))) return false;
    Cap->SetActorLocation(BeanJarSteamTests::Socket(Jar).GetLocation() + FVector(0, 0, .5), false, nullptr, ETeleportType::TeleportPhysics);
    if (!TestTrue(TEXT("近Socket实际TryRelease触发回盖"), Cap->GrabComp->TryRelease())) return false;
    BeanJarSteamTests::Quiet(Cap);
    TestTrue(TEXT("回盖重附着、封口和同归属"), Cap->bIsAttached && Jar->bIsCapped && Cap->OwnerBottle == Jar);
    BeanJarSteamTests::Near(*this, TEXT("回盖进度重置零"), Cap->TwistProgress, 0);
    TestTrue(TEXT("实际吸附位置是Socket"), Cap->GetActorLocation().Equals(BeanJarSteamTests::Socket(Jar).GetLocation(), .01));
    UMotionControllerComponent* Second = nullptr;
    if (!BeanJarSteamTests::Open(*this, F, Jar, Second, EControllerHand::Right)) return false;
    TestTrue(TEXT("再次旋开复用同一盖身份"), Jar->CapActor == Cap);
    TestEqual(TEXT("不存在额外盖"), BeanJarSteamTests::CapsOutside(F, Cap), 0);
    BeanJarSteamTests::Balance(*this, F, Jar); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamGrinder, "CyberCafe.BeanJarSteam.OpenJarDirectPourRealGrinder", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamGrinder::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A);
    auto* Grinder = F.Spawn<ACoffeeGrinderActor>(LoadClass<ACoffeeGrinderActor>(nullptr, BeanJarSteamTests::GrinderPath));
    if (!Jar || !Grinder || !F.Begin()) return false;
    if (!TestNotNull(TEXT("真实Handle子件"), Grinder->HandleRef.Get()) || !TestNotNull(TEXT("真实Drawer子件"), Grinder->DrawerRef.Get()) || !TestNotNull(TEXT("真实BeanEntryPoint"), Grinder->BeanEntryPoint.Get())) return false;
    BeanJarSteamTests::Near(*this, TEXT("fixture研磨器原点不改保存入口"), Grinder->GetActorLocation().Size(), 0);
    BeanJarSteamTests::Near(*this, TEXT("保存BP入口世界z38"), Grinder->BeanEntryPoint->GetComponentLocation().Z, 38);
    TestTrue(TEXT("主体网格查询碰撞保留，不绕过hopper阻挡"), Grinder->BodyMesh->GetStaticMesh() && Grinder->BodyMesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
    UMotionControllerComponent* MC = nullptr; if (!BeanJarSteamTests::Open(*this, F, Jar, MC) || !BeanJarSteamTests::ReleaseAway(*this, Jar)) return false;
    BeanJarSteamTests::Aim(Jar, Grinder->BeanEntryPoint->GetComponentLocation());
    BeanJarSteamTests::Near(*this, TEXT("dt1出5g"), Jar->PourBeans(1), 5);
    BeanJarSteamTests::Near(*this, TEXT("真实场景入口接5g，碰撞阻挡会明确失败"), Grinder->GetCurrentBeanGrams(), 5);
    BeanJarSteamTests::Near(*this, TEXT("豆罐库存145g"), Jar->GetBeanGrams(), 145);
    BeanJarSteamTests::Near(*this, TEXT("accepted5g"), Jar->TotalAcceptedGrams, 5);
    BeanJarSteamTests::Near(*this, TEXT("spilled0g"), Jar->SpilledBeanGrams, 0);
    for (int32 I = 0; I < 2; ++I) BeanJarSteamTests::Near(*this, TEXT("后续每次出5g"), Jar->PourBeans(1), 5);
    BeanJarSteamTests::Near(*this, TEXT("三次装满15g"), Grinder->GetCurrentBeanGrams(), 15);
    BeanJarSteamTests::Near(*this, TEXT("满仓第四次仍出5g"), Jar->PourBeans(1), 5);
    BeanJarSteamTests::Near(*this, TEXT("不在机器创造20g"), Grinder->GetCurrentBeanGrams(), 15);
    BeanJarSteamTests::Near(*this, TEXT("多余实际洒5g"), Jar->SpilledBeanGrams, 5);
    BeanJarSteamTests::Near(*this, TEXT("累计接15g"), Jar->TotalAcceptedGrams, 15);
    TestEqual(TEXT("倒豆不调用液体发包"), F.Transfer()->GetBalance().PacketCount, 0);
    BeanJarSteamTests::Near(*this, TEXT("液体账本Initial仍零"), F.Transfer()->GetBalance().InitialML, 0);
    BeanJarSteamTests::Balance(*this, F, Jar); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamPourGuards, "CyberCafe.BeanJarSteam.PourMissObstructionGuardsAndVisualBudget", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamPourGuards::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    for (int32 Scenario = 0; Scenario < 4; ++Scenario)
    {
        BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A);
        auto* Grinder = F.Spawn<ACoffeeGrinderActor>(LoadClass<ACoffeeGrinderActor>(nullptr, BeanJarSteamTests::GrinderPath));
        if (!Jar || !Grinder || !F.Begin()) return false;
        UMotionControllerComponent* MC = nullptr; if (!BeanJarSteamTests::Open(*this, F, Jar, MC) || !BeanJarSteamTests::ReleaseAway(*this, Jar)) return false;
        const FVector Entry = Grinder->BeanEntryPoint->GetComponentLocation();
        BeanJarSteamTests::Aim(Jar, Entry, Scenario == 2 ? 55 : 15, Scenario == 0 ? 20 : 0);
        if (Scenario == 1)
        {
            const FVector Start = BeanJarSteamTests::BeanStart(Jar), Direction = BeanJarSteamTests::BeanDirection(Jar);
            auto* Obstruction = F.Cube(Start + Direction * ((Start.Z - Entry.Z) / -Direction.Z) * .25, FVector(10, 10, 1));
            if (!Obstruction) return false;
            FHitResult Hit; FCollisionQueryParams Params; Params.AddIgnoredActor(Jar); Params.AddIgnoredActor(Jar->CapActor.Get());
            TestTrue(TEXT("真实Cube在口沿到入口之间阻挡"), F.World->LineTraceSingleByChannel(Hit, Start, Entry, ECC_Visibility, Params) && Hit.GetActor() == Obstruction);
        }
        if (Scenario == 3)
        {
            Jar->bWorkbenchRestoreEmissionBlocked = true;
            BeanJarSteamTests::Near(*this, TEXT("公共恢复护栏拒绝出豆"), Jar->PourBeans(1), 0);
            Jar->bWorkbenchRestoreEmissionBlocked = false; Jar->SetBeanRestoreBlocked(true);
            BeanJarSteamTests::Near(*this, TEXT("豆恢复护栏拒绝出豆"), Jar->PourBeans(1), 0); Jar->SetBeanRestoreBlocked(false);
            const float Rate = Jar->BeanPourRateGramsPerSecond;
            Jar->BeanPourRateGramsPerSecond = 0; BeanJarSteamTests::Near(*this, TEXT("zeroRate不出豆"), Jar->PourBeans(1), 0);
            Jar->BeanPourRateGramsPerSecond = std::numeric_limits<float>::quiet_NaN(); BeanJarSteamTests::Near(*this, TEXT("NaN rate不出豆"), Jar->PourBeans(1), 0);
            Jar->BeanPourRateGramsPerSecond = Rate;
            BeanJarSteamTests::Near(*this, TEXT("NaN dt不出豆"), Jar->PourBeans(std::numeric_limits<float>::quiet_NaN()), 0);
            Jar->SetActorRotation(FRotator(30, 0, 0), ETeleportType::TeleportPhysics);
            BeanJarSteamTests::Near(*this, TEXT("低倾角即使开盖也不出豆"), Jar->PourBeans(.2f), 0);
            BeanJarSteamTests::Near(*this, TEXT("所有guard库存150不变"), Jar->GetBeanGrams(), 150);
            BeanJarSteamTests::Aim(Jar, Entry, 15, 20);
        }
        int32 ActorsBefore = 0;
        for (TActorIterator<AActor> It(F.World); It; ++It) if (BeanJarSteamTests::Alive(*It)) ++ActorsBefore;
        const FVector InitialScale = Jar->BeanPileMesh->GetRelativeScale3D();
        BeanJarSteamTests::Near(*this, TEXT("真实miss仍出5g"), Jar->PourBeans(1), 5);
        BeanJarSteamTests::Near(*this, TEXT("偏瞄/阻挡/超过40cm不得最近吸附"), Grinder->GetCurrentBeanGrams(), 0);
        BeanJarSteamTests::Near(*this, TEXT("miss按实际g记洒落"), Jar->SpilledBeanGrams, 5);
        TestTrue(TEXT("真实豆堆高度随库存减小"), Jar->BeanPileMesh->GetRelativeScale3D().Z < InitialScale.Z);
        TestTrue(TEXT("视觉Stream至多24且没有独立豆物理"), Jar->BeanStream->GetInstanceCount() <= 24 && !Jar->BeanStream->IsSimulatingPhysics() && Jar->BeanStream->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        if (Jar->BeanStream->GetStaticMesh()) TestEqual(TEXT("有配置网格时实际创建24个视觉实例"), Jar->BeanStream->GetInstanceCount(), 24);
        TestTrue(TEXT("豆堆也没有物理或查询碰撞"), !Jar->BeanPileMesh->IsSimulatingPhysics() && Jar->BeanPileMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        Jar->BeanPourRateGramsPerSecond = 1000;
        BeanJarSteamTests::Near(*this, TEXT("剩余145克真实全部洒落"), Jar->PourBeans(1), 145);
        TestTrue(TEXT("库存零真实豆堆隐藏"), !Jar->BeanPileMesh->IsVisible() && Jar->BeanPileMesh->bHiddenInGame);
        Jar->PourBeans(.2f); TestEqual(TEXT("空罐清空Stream"), Jar->BeanStream->GetInstanceCount(), 0);
        int32 ActorsAfter = 0;
        for (TActorIterator<AActor> It(F.World); It; ++It) if (BeanJarSteamTests::Alive(*It)) ++ActorsAfter;
        TestEqual(TEXT("倒豆没有生成独立粒子物理Actor"), ActorsAfter, ActorsBefore);
        TestEqual(TEXT("倒豆没有液体包"), F.Transfer()->GetBalance().PacketCount, 0);
        BeanJarSteamTests::Balance(*this, F, Jar);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamBreakRestore, "CyberCafe.BeanJarSteam.BreakAndRestoreChain", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamBreakRestore::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    for (bool Native : {false, true})
    {
        BeanJarSteamTests::FWorld F(*this); auto* Manager = F.ManagerFirst(); auto* Jar = F.Jar(A, Native);
        if (!Manager || !Jar) return false;
        Manager->RestoreTargets = {Jar}; const FTransform Initial = Jar->GetActorTransform();
        UClass* Class = Jar->GetClass(); if (!F.Begin()) return false;
        TestEqual(TEXT("仅捕获一个Jar供给槽，不测试其他20槽闭环"), Manager->GetRestoreSlotCount(), 1);
        TestTrue(TEXT("prototype空时Jar实际槽索引为2"), Manager->GetCurrent(2) == Jar);
        auto* Breakable = Jar->FindComponentByClass<UGlassBreakableComponent>();
        if (!TestNotNull(TEXT("真实GlassBreak组件"), Breakable)) return false;
        auto* Floor = F.Cube(FVector(0, 0, -1), FVector(100, 100, 2)); if (!Floor) return false;
        auto* Cap = Jar->CapActor.Get(); UMotionControllerComponent* MC = nullptr;
        if (!BeanJarSteamTests::GrabCap(*this, F, Jar, MC)) return false;
        TestFalse(TEXT("启动保护期间真实冲击拒绝"), Breakable->TryImpact(FVector::ZeroVector, FVector::UpVector, FVector(0, 0, -400), Floor));
        // 保留原StartupGrace/追踪保护：连续.05采样覆盖默认.6，而非改阈值或一次大帧绕过。
        for (int32 I = 0; I < 20; ++I) Breakable->TickComponent(.05f, LEVELTICK_All, nullptr);
        if (!TestTrue(TEXT("400cm/s闭合法向速度真实TryImpact破碎"), Breakable->TryImpact(FVector::ZeroVector, FVector::UpVector, FVector(0, 0, -400), Floor))) return false;
        TestTrue(TEXT("源罐真实broken"), Jar->IsBroken());
        BeanJarSteamTests::Near(*this, TEXT("全部150g洒落"), Jar->SpilledBeanGrams, 150);
        TestTrue(TEXT("附着持盖已释放且归属失效，不能回盖"), !BeanJarSteamTests::Alive(Cap) && (!IsValid(Cap) || (!Cap->GrabComp->IsHeld() && !Cap->OwnerBottle)));
        if (IsValid(Cap)) { Cap->ReattachToBottle(); TestFalse(TEXT("破碎盖不能重新附着"), Cap->bIsAttached); }
        BeanJarSteamTests::Balance(*this, F, Jar);
        if (!TestTrue(TEXT("真实统一RestoreWorkbench"), Manager->RestoreWorkbench())) return false;
        auto* Fresh = Cast<ADirectBeanJarActor>(Manager->GetCurrent(2));
        if (!TestNotNull(TEXT("恢复真实新Jar"), Fresh)) return false;
        BeanJarSteamTests::Quiet(Fresh); BeanJarSteamTests::Quiet(Fresh->CapActor.Get());
        TestTrue(TEXT("新身份同class和初始TM"), Fresh != Jar && Fresh->GetClass() == Class && Fresh->GetActorTransform().Equals(Initial, .01));
        TestTrue(TEXT("恢复新罐不碎、保存拉拔 false 和闲置120度正向参数保留且封口"), !Fresh->IsBroken() && Fresh->CapActor && !Fresh->CapActor->bRequireScrewTwist && Fresh->CapActor->GetClass() == A.LidClass && Fresh->CapActor->UnlockAngleDegrees == 120 && Fresh->CapActor->OpeningDirection == 1 && Fresh->CapActor->bIsAttached && Fresh->bIsCapped);
        TestTrue(TEXT("profile真实重绑定"), Fresh->FindComponentByClass<UGlassBreakableComponent>() && Fresh->FindComponentByClass<UGlassBreakableComponent>()->Profile == A.Profile);
        BeanJarSteamTests::Near(*this, TEXT("新库存150"), Fresh->GetBeanGrams(), 150);
        auto Ledger = Fresh->GetBeanLedger();
        BeanJarSteamTests::Near(*this, TEXT("历史initial仅150"), Ledger.InitialGrams, 150);
        BeanJarSteamTests::Near(*this, TEXT("新供给injected150"), Ledger.InjectedGrams, 150);
        BeanJarSteamTests::Near(*this, TEXT("保留历史spill150"), Ledger.SpilledGrams, 150);
        UMotionControllerComponent* NewMC = nullptr;
        if (!BeanJarSteamTests::Open(*this, F, Fresh, NewMC) || !BeanJarSteamTests::ReleaseAway(*this, Fresh)) return false;
        Fresh->SetActorRotation(FRotator(120, 0, 0), ETeleportType::TeleportPhysics);
        BeanJarSteamTests::Near(*this, TEXT("真实倒70g令库存80"), Fresh->PourBeans(14), 70);
        BeanJarSteamTests::Near(*this, TEXT("第二次恢复前余量80"), Fresh->GetBeanGrams(), 80);
        if (!TestTrue(TEXT("第二次真实恢复"), Manager->RestoreWorkbench())) return false;
        auto* Second = Cast<ADirectBeanJarActor>(Manager->GetCurrent(2)); if (!TestNotNull(TEXT("第二轮新Jar"), Second)) return false;
        BeanJarSteamTests::Quiet(Second); BeanJarSteamTests::Quiet(Second->CapActor.Get()); Ledger = Second->GetBeanLedger();
        BeanJarSteamTests::Near(*this, TEXT("余豆80只记Discarded"), Ledger.DiscardedGrams, 80);
        BeanJarSteamTests::Near(*this, TEXT("initial不重复150"), Ledger.InitialGrams, 150);
        BeanJarSteamTests::Near(*this, TEXT("两次供给injected300"), Ledger.InjectedGrams, 300);
        BeanJarSteamTests::Near(*this, TEXT("第二轮库存150"), Second->GetBeanGrams(), 150);
        TestEqual(TEXT("配套盖全部回收，额外零"), BeanJarSteamTests::CapsOutside(F, Second->CapActor.Get()), 0);
        BeanJarSteamTests::Balance(*this, F, Second);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamHeldRestore, "CyberCafe.BeanJarSteam.DetachedHeldCapBlocksRestore", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamHeldRestore::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    BeanJarSteamTests::FWorld F(*this); auto* Manager = F.ManagerFirst(); auto* Jar = F.Jar(A);
    if (!Manager || !Jar) return false; Manager->RestoreTargets = {Jar}; if (!F.Begin()) return false;
    UMotionControllerComponent* MC = nullptr; if (!BeanJarSteamTests::Open(*this, F, Jar, MC)) return false;
    auto* Detached = Jar->CapActor.Get();
    TestFalse(TEXT("真实独立盖Held预检拒绝恢复"), Manager->RestoreWorkbench());
    TestTrue(TEXT("拒绝不替换身份"), Manager->GetCurrent(2) == Jar); TestEqual(TEXT("拒绝不增generation"), Manager->GetRestoreGeneration(), 0);
    if (!BeanJarSteamTests::ReleaseAway(*this, Jar)) return false;
    if (!TestTrue(TEXT("松盖后真实恢复"), Manager->RestoreWorkbench())) return false;
    auto* Fresh = Cast<ADirectBeanJarActor>(Manager->GetCurrent(2)); if (!TestNotNull(TEXT("新罐"), Fresh)) return false;
    TestFalse(TEXT("远处释放配套盖也被回收"), BeanJarSteamTests::Alive(Detached));
    TestEqual(TEXT("除新活动盖外零额外盖"), BeanJarSteamTests::CapsOutside(F, Fresh->CapActor.Get()), 0);
    BeanJarSteamTests::Near(*this, TEXT("未倒出的旧150记弃置"), Fresh->DiscardedBeanGrams, 150);
    BeanJarSteamTests::Balance(*this, F, Fresh); return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamActualImpact, "CyberCafe.BeanJarSteam.SteamActualImpact", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamActualImpact::RunTest(const FString&)
{
    BeanJarSteamTests::FSteamFixture F(*this); if (!F.Init()) return false;
    struct FObserved { int32 Count = 0; FHotWaterImpact Last; bool Committed = true; bool RestoreRejected = false; } Observed;
    const TWeakObjectPtr<ULiquidTransferSubsystem> WeakTransfer(F.Runtime.Transfer());
    const TWeakObjectPtr<AHandPourKettleActor> WeakSource(F.Kettle);
    const FDelegateHandle Handle = F.Runtime.Transfer()->OnWaterImpactNative.AddLambda([&Observed, WeakTransfer, WeakSource](const FHotWaterImpact& Impact)
    {
        if (Impact.Source.Get() != WeakSource.Get()) return;
        ++Observed.Count; Observed.Last = Impact;
        if (auto* S = WeakTransfer.Get())
        {
            const auto B = S->GetBalance(); Observed.Committed &= FMath::Abs(B.ErrorML) <= BeanJarSteamTests::Tolerance && B.PacketCount == 0;
            Observed.RestoreRejected = !S->PrepareWorkbenchRestore({WeakSource.Get()});
        }
    });
    BeanJarSteamTests::Near(*this, TEXT("真实EmitVolume发0.5mL水"), F.Emit(), .5);
    TestFalse(TEXT("发包后未Simulate不能预先蒸汽"), F.Steam->IsSteamRequested());
    TestEqual(TEXT("发包尚无impact事件"), Observed.Count, 0); TestEqual(TEXT("真实在途一个包"), F.Runtime.Transfer()->GetBalance().PacketCount, 1);
    F.Settle();
    TestTrue(TEXT("真实结算落杯后请求蒸汽"), F.Steam->IsSteamRequested());
    TestEqual(TEXT("一次落点开始计数1"), F.Steam->GetSteamStartCount(), 1);
    TestTrue(TEXT("落杯点匹配入口加原API0.3cm法向偏置"), F.Steam->GetLastImpactPoint().Equals(F.Entry() + FVector(0, 0, .3), .01));
    BeanJarSteamTests::Near(*this, TEXT("通知实际WaterML0.5"), F.Steam->GetLastImpactWaterML(), .5);
    BeanJarSteamTests::Near(*this, TEXT("cup实收0.5mL"), F.Cup->GetPreciseVolumeML(), .5);
    TestTrue(TEXT("queue回调已离开包迭代且账本提交，并保留重入保护"), Observed.Committed && Observed.RestoreRejected && Observed.Count == 1 && Observed.Last.Target.Get() == F.Cup);
    auto* PS = F.Steam->GetSteamComponent();
    if (!TestNotNull(TEXT("首次真实impact才创建Cascade组件"), PS)) { F.Runtime.Transfer()->OnWaterImpactNative.Remove(Handle); return false; }
    TestTrue(TEXT("模板是原Steam的倒水专用副本，归属为该壶"), PS->Template == F.Template && PS->GetOwner() == F.Kettle);
    TestTrue(TEXT("实际蒸汽 PS 尺寸使用实读保存配置，而非旧 .12"),
        PS->GetComponentScale().Equals(FVector(F.SavedSteamScale), BeanJarSteamTests::Tolerance));
    TestEqual(TEXT("只有一个PS组件"), F.ParticleComponents(), 1);
    for (int32 I = 0; I < 3; ++I) { F.Emit(); F.Settle(); }
    TestEqual(TEXT("连续包不逐包启动计数"), F.Steam->GetSteamStartCount(), 1);
    TestTrue(TEXT("连续包复用同一PS"), F.Steam->GetSteamComponent() == PS && F.ParticleComponents() == 1);
    F.Steam->TickComponent(.1f, LEVELTICK_All, nullptr); TestTrue(TEXT("Hold内保留请求"), F.Steam->IsSteamRequested());
    F.Steam->TickComponent(F.Steam->ImpactHoldSeconds + .1f, LEVELTICK_All, nullptr);
    TestFalse(TEXT("Hold过期关请求，非立即Kill尾粒子"), F.Steam->IsSteamRequested());
    TestTrue(TEXT("普通停止保留同一组件"), F.Steam->GetSteamComponent() == PS);
    F.Emit(); F.Settle(); TestEqual(TEXT("新落点恢复计数2"), F.Steam->GetSteamStartCount(), 2);
    TestTrue(TEXT("恢复仍同一PS"), F.Steam->GetSteamComponent() == PS && F.ParticleComponents() == 1);
    F.Runtime.Transfer()->OnWaterImpactNative.Remove(Handle);
    // 另走真实PourWater切片入口：只推导壶姿态，既不广播假impact也不直接调用HandleImpact。
    F.Kettle->SetActorRotation(FRotator(-65, 0, 0), ETeleportType::TeleportPhysics);
    const FVector Spout = F.Kettle->GetKettleSpoutWorldTransform().GetLocation();
    const FVector Velocity = F.Kettle->GetKettleSpoutWorldTransform().GetUnitAxis(EAxis::X) * 35;
    const double FallTime = (Velocity.Z + FMath::Sqrt(Velocity.Z * Velocity.Z + 2 * 980 * 10)) / 980;
    const FVector WantedSpout = F.Entry() + FVector(0, 0, 10) - FVector(Velocity.X, Velocity.Y, 0) * FallTime;
    F.Kettle->SetActorLocation(F.Kettle->GetActorLocation() + WantedSpout - Spout, false, nullptr, ETeleportType::TeleportPhysics);
    const double Before = F.Cup->GetPreciseVolumeML();
    TestTrue(TEXT("真实PourWater发量"), F.Kettle->PourWater(.02f) > 0); F.Settle();
    TestTrue(TEXT("实际壶出水命中真实杯入口"), F.Cup->GetPreciseVolumeML() > Before && F.Steam->IsSteamRequested());
    BeanJarSteamTests::Near(*this, TEXT("蒸汽不改变真实液体守恒"), F.Runtime.Transfer()->GetBalance().ErrorML, 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamNegativeImpact, "CyberCafe.BeanJarSteam.SteamMissCompositionRestoreAndCleanup", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamNegativeImpact::RunTest(const FString&)
{
    for (int32 Scenario = 0; Scenario < 5; ++Scenario)
    {
        BeanJarSteamTests::FSteamFixture F(*this); if (!F.Init()) return false;
        if (Scenario == 0)
        {
            auto* Floor = F.Runtime.Cube(FVector(100, 0, 100), FVector(20, 20, 2)); if (!Floor) return false;
            FHitResult Expected; FCollisionQueryParams Params; Params.AddIgnoredActor(F.Kettle);
            if (!TestTrue(TEXT("miss fixture真实命中Cube"), F.Runtime.World->LineTraceSingleByChannel(Expected, FVector(100, 0, 111), FVector(100, 0, 91), ECC_Visibility, Params))) return false;
            F.Runtime.Transfer()->EmitVolume(F.Kettle, .5, FVector(100, 0, 111), FVector(0, 0, -100), FVector::ZeroVector); F.Settle();
            TestTrue(TEXT("普通表面真实水落点也请求蒸汽"), F.Steam->IsSteamRequested() && F.Steam->GetLastImpactPoint().Equals(Expected.ImpactPoint, .01));
            BeanJarSteamTests::Near(*this, TEXT("miss实际洒0.5"), F.Runtime.Transfer()->GetBalance().SpilledML, .5);
        }
        else if (Scenario == 1)
        {
            F.Runtime.Transfer()->EmitVolume(F.Kettle, .5, FVector(5000, 0, 5000), FVector::ZeroVector, FVector::ZeroVector);
            F.Runtime.Transfer()->Simulate(3.1);
            TestTrue(TEXT("无地面超龄包不生成蒸汽组件"), !F.Steam->IsSteamRequested() && !F.Steam->GetSteamComponent());
            TestEqual(TEXT("超龄包已结清"), F.Runtime.Transfer()->GetBalance().PacketCount, 0);
        }
        else if (Scenario == 2)
        {
            auto* Cold = F.Runtime.Spawn<ABottleActor>(ABottleActor::StaticClass(), FTransform(FVector(1000, 0, 100)));
            if (!Cold) return false;
            Cold->ConsumeLiquid(TNumericLimits<float>::Max()); Cold->AddIngredientLiquid(2, TEXT("Water"), FLinearColor::White);
            TestFalse(TEXT("冷瓶无HotWaterSteam组件"), Cold->FindComponentByClass<UHotWaterSteamComponent>() != nullptr);
            F.Emit(Cold); F.Settle();
            TestTrue(TEXT("其他Source真实水落点不启动本壶组件"), !F.Steam->IsSteamRequested() && !F.Steam->GetSteamComponent());
            BeanJarSteamTests::Near(*this, TEXT("冷水仍真实进杯"), F.Cup->GetIngredientVolumeML(TEXT("Water")), .5);
        }
        else if (Scenario == 3)
        {
            F.Kettle->ConsumeLiquid(TNumericLimits<float>::Max());
            for (const FName Ingredient : {FName(TEXT("Coffee")), FName(TEXT("Milk"))})
            {
                F.Kettle->AddIngredientLiquid(.5f, Ingredient, FLinearColor::White);
                BeanJarSteamTests::Near(*this, TEXT("非水portion保持Water零"), F.Kettle->GetIngredientVolumeML(TEXT("Water")), 0);
                F.Emit(); F.Settle();
                TestTrue(TEXT("coffee/milk真实包不触发水蒸汽通知"), !F.Steam->IsSteamRequested() && !F.Steam->GetSteamComponent());
            }
        }
        else
        {
            F.Emit(); F.Settle(); auto* PS = F.Steam->GetSteamComponent();
            if (!TestNotNull(TEXT("清理前真实PS"), PS)) return false;
            const TWeakObjectPtr<UParticleSystemComponent> WeakPS(PS);
            F.Kettle->SetActorRotation(FRotator(-65, 0, 0), ETeleportType::TeleportPhysics);
            TestTrue(TEXT("恢复前保持合法倒水姿态和正库存"), F.Kettle->GetPreciseVolumeML() > 0 && FVector::DotProduct(F.Kettle->GetKettleSpoutWorldTransform().GetUnitAxis(EAxis::X), FVector::DownVector) > .3);
            F.Emit(); TestEqual(TEXT("恢复前真实pending包"), F.Runtime.Transfer()->GetBalance().PacketCount, 1);
            F.Kettle->bWorkbenchRestoreEmissionBlocked = true;
            if (!TestTrue(TEXT("实际PrepareRestore取消来源在途包"), F.Runtime.Transfer()->PrepareWorkbenchRestore({F.Kettle}))) return false;
            const int32 Starts = F.Steam->GetSteamStartCount(); F.Settle();
            TestEqual(TEXT("取消包不得补发impact"), F.Steam->GetSteamStartCount(), Starts);
            TestFalse(TEXT("恢复护栏立即关蒸汽请求"), F.Steam->IsSteamRequested());
            BeanJarSteamTests::Near(*this, TEXT("恢复护栏真实PourWater拒绝且声音关闭"), F.Kettle->PourWater(.02f), 0);
            TestFalse(TEXT("无倒水音频请求"), F.Kettle->IsPourAudioRequested());
            TestEqual(TEXT("取消后无在途包"), F.Runtime.Transfer()->GetBalance().PacketCount, 0);
            F.Kettle->bWorkbenchRestoreEmissionBlocked = false;
            F.Emit(); F.Settle(); TestTrue(TEXT("解护栏新impact可恢复且仍一PS"), F.Steam->IsSteamRequested() && F.Steam->GetSteamComponent() == PS && F.ParticleComponents() == 1);
            F.Kettle->Destroy();
            TestTrue(TEXT("DestroyActor生命周期回收弱PS"), !WeakPS.IsValid() || WeakPS->IsBeingDestroyed());
        }
        BeanJarSteamTests::Near(*this, TEXT("各隔离world真实液体账本守恒"), F.Runtime.Transfer()->GetBalance().ErrorML, 0);
    }
    // CPU/NullRHI只断言请求门、原模板、数量与生命周期，不用IsActive承诺渲染或尾粒子可见。
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamZeroLiquidInventory, "CyberCafe.BeanJarSteam.ZeroLiquidInventoryCannotBeCreditedOrEmitted", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamZeroLiquidInventory::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A);
    if (!Jar || !F.Begin()) return false;
    ALiquidContainerActor* Base = Cast<ALiquidContainerActor>(Jar);
    if (!TestNotNull(TEXT("保存豆罐的真实基类指针"), Base)) return false;
    TestTrue(TEXT("保存豆罐已走实际BeginPlay"), Jar->HasActorBegunPlay());
    TestFalse(TEXT("经基类虚调用拒绝液体库存能力"), Base->SupportsLiquidInventory());
    const auto Before = F.Transfer()->GetBalance();
    const int32 Revision = Base->GetLiquidRevision();
    BeanJarSteamTests::Near(*this, TEXT("经基类AddIngredientLiquid拒绝1mL水"), Base->AddIngredientLiquid(1, TEXT("Water"), FLinearColor::White), 0);
    BeanJarSteamTests::Near(*this, TEXT("拒绝入账后精确库存零"), Base->GetPreciseVolumeML(), 0);
    BeanJarSteamTests::Near(*this, TEXT("拒绝入账后Water成分零"), Base->GetIngredientVolumeML(TEXT("Water")), 0);
    BeanJarSteamTests::Near(*this, TEXT("拒绝入账不增加InjectedML"), F.Transfer()->GetBalance().InjectedML, Before.InjectedML);
    BeanJarSteamTests::Near(*this, TEXT("拒绝入账后液体账本误差零"), F.Transfer()->GetBalance().ErrorML, 0);

    // 只污染公开的表现比例，不用容量setter伪装液位，也不调用Tick修补为零。
    Base->FillAmount = .5f;
    Base->InitializeLiquidState();
    BeanJarSteamTests::Near(*this, TEXT("半满表现比例尚未被呈现层修补"), Base->FillAmount, .5);
    BeanJarSteamTests::Near(*this, TEXT("半满表现不能创造CurrentVolumeML"), Base->GetCurrentVolumeML(), 0);
    BeanJarSteamTests::Near(*this, TEXT("半满表现仍拒绝经基类加水"), Base->AddIngredientLiquid(1, TEXT("Water"), FLinearColor::White), 0);
    BeanJarSteamTests::Near(*this, TEXT("经基类ConsumeLiquid也不能扣出毫升"), Base->ConsumeLiquid(1), 0);
    int32 SourceImpacts = 0;
    const TWeakObjectPtr<ALiquidContainerActor> WeakSource(Base);
    const FDelegateHandle Handle = F.Transfer()->OnWaterImpactNative.AddLambda([&SourceImpacts, WeakSource](const FHotWaterImpact& Impact)
    {
        if (Impact.Source.Get() == WeakSource.Get()) ++SourceImpacts;
    });
    BeanJarSteamTests::Near(*this, TEXT("实际Source发射器拒绝1mL水"), F.Transfer()->EmitVolume(Base, 1,
        FVector(100, 0, 100), FVector(0, 0, -100), FVector::ZeroVector), 0);
    TestEqual(TEXT("发射尝试不生成液体包"), F.Transfer()->GetBalance().PacketCount, 0);
    F.Transfer()->Simulate(.2);
    F.Transfer()->OnWaterImpactNative.Remove(Handle);
    TestEqual(TEXT("豆罐Source不发布可触发蒸汽的水落点"), SourceImpacts, 0);
    TestFalse(TEXT("豆罐没有热水蒸汽请求组件"), Jar->FindComponentByClass<UHotWaterSteamComponent>() != nullptr);
    TArray<UParticleSystemComponent*> SteamParticles; Jar->GetComponents(SteamParticles);
    TestEqual(TEXT("豆罐Source未创建Cascade发射组件"), SteamParticles.Num(), 0);
    TestEqual(TEXT("拒绝入账扣账和发射不改液体revision"), Base->GetLiquidRevision(), Revision);
    BeanJarSteamTests::Near(*this, TEXT("全部拒绝后精确液体库存零"), Base->GetPreciseVolumeML(), 0);
    BeanJarSteamTests::Near(*this, TEXT("全部拒绝后液体成分总量零"), Base->GetComposition().Total(), 0);
    const auto After = F.Transfer()->GetBalance();
    BeanJarSteamTests::Near(*this, TEXT("InitialML保持基线"), After.InitialML, Before.InitialML);
    BeanJarSteamTests::Near(*this, TEXT("InjectedML保持基线"), After.InjectedML, Before.InjectedML);
    BeanJarSteamTests::Near(*this, TEXT("RemovedML保持基线"), After.RemovedML, Before.RemovedML);
    BeanJarSteamTests::Near(*this, TEXT("ContainerML保持基线"), After.ContainerML, Before.ContainerML);
    BeanJarSteamTests::Near(*this, TEXT("InFlightML保持基线"), After.InFlightML, Before.InFlightML);
    BeanJarSteamTests::Near(*this, TEXT("SpilledML保持基线"), After.SpilledML, Before.SpilledML);
    TestEqual(TEXT("结算后仍无液体包"), After.PacketCount, 0);
    BeanJarSteamTests::Near(*this, TEXT("液体能力拒绝路径误差零"), After.ErrorML, 0);
    BeanJarSteamTests::Near(*this, TEXT("拒绝毫升入口不动150g豆库存"), Jar->GetBeanGrams(), 150);
    BeanJarSteamTests::Balance(*this, F, Jar);

    // 合成原生瓶只作旧毫升API对照，不替代上面的保存豆罐资产。
    TestTrue(TEXT("旧原生瓶默认仍支持液体库存"), GetDefault<ABottleActor>()->SupportsLiquidInventory());
    BeanJarSteamTests::FWorld Legacy(*this);
    auto* Bottle = Legacy.Spawn<ABottleActor>(ABottleActor::StaticClass(), FTransform(FVector(300, 0, 100)));
    if (!Bottle) return false;
    Bottle->ContainerMesh->SetStaticMesh(A.Body); Bottle->FillAmount = 0;
    if (!Legacy.Begin()) return false;
    ALiquidContainerActor* LegacyBase = Cast<ALiquidContainerActor>(Bottle);
    TestTrue(TEXT("旧原生瓶的基类虚调用仍支持库存"), LegacyBase->SupportsLiquidInventory());
    BeanJarSteamTests::Near(*this, TEXT("旧原生瓶保持显式入账1mL的原行为"), LegacyBase->AddIngredientLiquid(1, TEXT("Water"), FLinearColor::White), 1);
    BeanJarSteamTests::Near(*this, TEXT("旧原生瓶真实库存1mL"), LegacyBase->GetPreciseVolumeML(), 1);
    BeanJarSteamTests::Near(*this, TEXT("旧原生瓶真实Water成分1mL"), LegacyBase->GetIngredientVolumeML(TEXT("Water")), 1);
    BeanJarSteamTests::Near(*this, TEXT("旧原生瓶实际InjectedML为1"), Legacy.Transfer()->GetBalance().InjectedML, 1);
    BeanJarSteamTests::Near(*this, TEXT("旧原生瓶账本误差零"), Legacy.Transfer()->GetBalance().ErrorML, 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamConfiguredTiltThreshold, "CyberCafe.BeanJarSteam.ConfiguredTiltThresholdIsNotHiddenByMouthNormal", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamConfiguredTiltThreshold::RunTest(const FString&)
{
    BeanJarSteamTests::FJarAssets A; if (!A.Check(*this)) return false;
    BeanJarSteamTests::FWorld F(*this); auto* Jar = F.Jar(A);
    if (!Jar || !F.Begin()) return false;
    UMotionControllerComponent* MC = nullptr;
    if (!BeanJarSteamTests::Open(*this, F, Jar, MC) || !BeanJarSteamTests::ReleaseAway(*this, Jar)) return false;
    Jar->SetActorLocation(Jar->GetActorLocation() + FVector(100, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
    BeanJarSteamTests::Near(*this, TEXT("保存配置倾角仍是65度，不调阈值绕过"), Jar->PourBeanTiltDegrees, 65);
    BeanJarSteamTests::Near(*this, TEXT("保存配置出豆率仍是5g每秒"), Jar->BeanPourRateGramsPerSecond, 5);
    auto* Cap = Jar->CapActor.Get();
    TestTrue(TEXT("真实拉拔开盖并远处释放后才测倾角"), !Cap->bIsAttached && !Jar->bIsCapped && !Cap->GrabComp->IsHeld());
    // 读取当前ContainerMesh轴的真实角度；这里只测试罐体姿态，不代表真实HMD或追踪手柄。
    const auto TiltDegrees = [Jar]()
    {
        const double UpDot = FVector::DotProduct(Jar->ContainerMesh->GetUpVector(), FVector::UpVector);
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(UpDot, -1.0, 1.0)));
    };
    Jar->SetActorRotation(FRotator(60, 0, 0), ETeleportType::TeleportPhysics);
    BeanJarSteamTests::Near(*this, TEXT("当前罐体实测倾角60度"), TiltDegrees(), 60);
    BeanJarSteamTests::Near(*this, TEXT("60小于配置65度不出豆"), Jar->PourBeans(1), 0);
    BeanJarSteamTests::Near(*this, TEXT("60度库存仍150g"), Jar->GetBeanGrams(), 150);
    BeanJarSteamTests::Near(*this, TEXT("60度没有洒豆"), Jar->SpilledBeanGrams, 0);

    for (const double Angle : {65.0, 70.0})
    {
        Jar->SetActorRotation(FRotator(Angle, 0, 0), ETeleportType::TeleportPhysics);
        BeanJarSteamTests::Near(*this, FString::Printf(TEXT("当前罐体实测倾角%.0f度"), Angle), TiltDegrees(), Angle);
        TestTrue(TEXT("配置65度边界采用大于等于"), TiltDegrees() + 1.e-4 >= Jar->PourBeanTiltDegrees);
        TestTrue(TEXT("口外法向仍不满足旧DownDot大于0.2，不得隐藏配置阈值"),
            FVector::DotProduct(Jar->ContainerMesh->GetUpVector(), FVector::DownVector) <= .2);
        const double BeforeGrams = Jar->GetBeanGrams(), BeforeSpilled = Jar->SpilledBeanGrams;
        BeanJarSteamTests::Near(*this, FString::Printf(TEXT("%.0f度dt1实际出5g"), Angle), Jar->PourBeans(1), 5);
        BeanJarSteamTests::Near(*this, TEXT("每次只扣实际5g库存"), Jar->GetBeanGrams(), BeforeGrams - 5);
        BeanJarSteamTests::Near(*this, TEXT("无磨豆机时每次实际洒5g"), Jar->SpilledBeanGrams, BeforeSpilled + 5);
        BeanJarSteamTests::Near(*this, TEXT("无接收方不凭空接纳"), Jar->TotalAcceptedGrams, 0);
        BeanJarSteamTests::Balance(*this, F, Jar);
    }
    Jar->SetActorRotation(FRotator::ZeroRotator, ETeleportType::TeleportPhysics);
    BeanJarSteamTests::Near(*this, TEXT("返回直立后当前罐体倾角零"), TiltDegrees(), 0);
    BeanJarSteamTests::Near(*this, TEXT("返回直立停止出豆"), Jar->PourBeans(1), 0);
    BeanJarSteamTests::Near(*this, TEXT("直立库存保持140g"), Jar->GetBeanGrams(), 140);
    BeanJarSteamTests::Near(*this, TEXT("直立洒落保持10g"), Jar->SpilledBeanGrams, 10);

    MC->SetWorldTransform(BeanJarSteamTests::Socket(Jar));
    if (!TestTrue(TEXT("封口门禁前真实重新抓盖"), Cap->GrabComp->TryGrab(MC))) return false;
    Cap->SetActorLocation(BeanJarSteamTests::Socket(Jar).GetLocation() + FVector(0, 0, .5), false, nullptr, ETeleportType::TeleportPhysics);
    if (!TestTrue(TEXT("近Socket真实释放触发回盖"), Cap->GrabComp->TryRelease())) return false;
    BeanJarSteamTests::Quiet(Cap);
    if (!TestTrue(TEXT("回盖真实附着并封口，不直接写bIsCapped"), Cap->bIsAttached && Jar->bIsCapped && Cap->OwnerBottle == Jar)) return false;
    Jar->SetActorRotation(FRotator(70, 0, 0), ETeleportType::TeleportPhysics);
    BeanJarSteamTests::Near(*this, TEXT("封口时当前罐体仍实测70度"), TiltDegrees(), 70);
    BeanJarSteamTests::Near(*this, TEXT("满足倾角也被真实封口门禁拒绝"), Jar->PourBeans(1), 0);
    BeanJarSteamTests::Near(*this, TEXT("封口后库存保持140g"), Jar->GetBeanGrams(), 140);
    BeanJarSteamTests::Near(*this, TEXT("封口后洒落保持10g"), Jar->SpilledBeanGrams, 10);
    BeanJarSteamTests::Near(*this, TEXT("累计只出过10g"), Jar->TotalPouredGrams, 10);
    TestEqual(TEXT("倾角测试不生成液体包"), F.Transfer()->GetBalance().PacketCount, 0);
    BeanJarSteamTests::Balance(*this, F, Jar);
    return !HasAnyErrors();
}

namespace BeanJarSteamTests
{
// 只供新增查询夹具使用；等待资产编译与实际碰撞 cook，不改保存碰撞响应或默认 NoSim World。
bool CookQueryBody(FAutomationTestBase& Test, UStaticMeshComponent* Mesh)
{
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    if (!Test.TestNotNull(TEXT("实际查询网格组件"), Mesh)
        || !Test.TestNotNull(TEXT("实际保存网格"), Mesh->GetStaticMesh().Get())) return false;
    auto* Setup = Mesh->GetStaticMesh()->GetBodySetup();
    if (!Test.TestNotNull(TEXT("实际保存 BodySetup"), Setup)) return false;
    Setup->CreatePhysicsMeshes(); Mesh->SetSimulatePhysics(false); Mesh->RecreatePhysicsState();
    return Test.TestTrue(TEXT("cook 后真实查询刚体有效且不模拟，不强改碰撞档案"),
        Mesh->IsRegistered() && Mesh->IsQueryCollisionEnabled() && !Mesh->IsSimulatingPhysics()
        && Mesh->GetBodyInstance() && Mesh->GetBodyInstance()->IsValidBodyInstance());
}
void TickWorld(FWorld& F, float Dt)
{
    // 与 CoffeeAssetRuntimeTests 相同的真实世界调度；不另调用 Transfer::Simulate 或 AdvanceBrew。
    ++GFrameCounter; F.World->Tick(LEVELTICK_All, Dt);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamOpenMouthCollision, "CyberCafe.BeanJarSteam.OpenMouthAndBodyCollision", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamOpenMouthCollision::RunTest(const FString&)
{
    using namespace BeanJarSteamTests;
    FJarAssets A; if (!A.Check(*this)) return false;
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    for (bool Native : {false, true})
    {
        FWorld F(*this); auto* Jar = F.Jar(A, Native);
        auto* Floor = F.Cube(FVector(0, 0, -5), FVector(500, 500, 10));
        if (!Jar || !Floor || !F.Begin()) return false;
        auto* Cap = Jar->CapActor.Get();
        if (!TestNotNull(TEXT("真实配套盖"), Cap)
            || !CookQueryBody(*this, Jar->ContainerMesh.Get()) || !CookQueryBody(*this, Cap->CapMesh.Get())
            || !CookQueryBody(*this, Cast<UStaticMeshComponent>(Floor->GetRootComponent()))) return false;
        auto* Body = Jar->ContainerMesh.Get(); auto* Setup = A.Body->GetBodySetup();
        if (!TestNotNull(TEXT("保存分离本体的真实 BodySetup"), Setup)) return false;
        TestTrue(TEXT("saved/native 本体都使用保存分离网格与其实际 BodySetup"),
            Body->GetStaticMesh() == A.Body && Body->GetBodySetup() == Setup);
        TestEqual(TEXT("真实罐体总简单形状是12而非48或封口单 hull"), Setup->AggGeom.GetElementCount(), 12);
        TestEqual(TEXT("12个真实角向 convex hull"), Setup->AggGeom.ConvexElems.Num(), 12);
        TestTrue(TEXT("查询采用简单物理形状，不用源三角网格代替"), Setup->CollisionTraceFlag != CTF_UseComplexAsSimple);
        TestEqual(TEXT("原玻璃形状1764三角保持"), A.Body->GetNumTriangles(0), 1764);
        TestEqual(TEXT("当前绑定 profile 八片"), A.Profile->Shards.Num(), 8);
        TestTrue(TEXT("实际碎片物理预算不超过八且原绑定未变"), A.Profile->MaxPhysicsShards <= 8
            && Jar->GlassBreakProfile == A.Profile && Jar->FindComponentByClass<UGlassBreakableComponent>()
            && Jar->FindComponentByClass<UGlassBreakableComponent>()->Profile == A.Profile);
        TestTrue(TEXT("Body 仍阻挡 Visibility 和地面 WorldStatic"), Body->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
            && Body->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
        TestTrue(TEXT("豆堆及豆流保留 NoCollision，不用 IgnoreComponent 掩盖"),
            Jar->BeanPileMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision
            && Jar->BeanStream->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        TestTrue(TEXT("闭盖保留真实 QueryOnly/Overlap/PhysicsBody，而不是强制 Visibility Block"),
            Cap->bIsAttached && Jar->bIsCapped && Cap->GetRootComponent() == Cap->CapMesh.Get()
            && Cap->CapMesh->GetCollisionEnabled() == ECollisionEnabled::QueryOnly
            && Cap->CapMesh->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Overlap
            && Cap->CapMesh->GetCollisionObjectType() == ECC_PhysicsBody);
        const FTransform TM = Body->GetComponentTransform();
        const FVector Above = TM.TransformPosition(FVector(0, 0, 30));
        const FVector Inside = TM.TransformPosition(FVector(0, 0, 12));
        FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        FCollisionQueryParams All(SCENE_QUERY_STAT(BeanJarClosedMouthObjects), false);
        TArray<FHitResult> ClosedHits;
        F.World->LineTraceMultiByObjectType(ClosedHits, Above, Inside, Objects, All);
        if (!TestTrue(TEXT("不忽略罐和盖的对象查询确有闭盖命中"), !ClosedHits.IsEmpty())) return false;
        const FHitResult* First = &ClosedHits[0];
        for (const auto& Hit : ClosedHits) if (Hit.Distance < First->Distance) First = &Hit;
        TestTrue(TEXT("闭盖路径第一个确切 Actor/Root 是该 Jar.Cap，不是 Body"),
            First->GetActor() == Cap && First->GetComponent() == Cap->CapMesh.Get());

        UMotionControllerComponent* MC = nullptr;
        if (!Open(*this, F, Jar, MC) || !ReleaseAway(*this, Jar) || !CookQueryBody(*this, Cap->CapMesh.Get())) return false;
        TestTrue(TEXT("实际拉拔和远处释放，独立盖不模拟、不加力"),
            !Cap->bIsAttached && !Jar->bIsCapped && !Cap->GrabComp->IsHeld() && !Cap->CapMesh->IsSimulatingPhysics());
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BeanJarOpenMouthBody), false);
        Query.AddIgnoredActor(Cap); // 唯一忽略项是独立盖；绝不忽略 Jar 本体或其豆组件。
        FHitResult Hit;
        TestFalse(TEXT("本体局部 Z30 到 Z12 穿过真实开口，无封口 hull"),
            F.World->LineTraceSingleByChannel(Hit, Above, Inside, ECC_Visibility, Query));
        TestFalse(TEXT("内部 Z12 向 Z30 出口也畅通，豆堆不挡出流"),
            F.World->LineTraceSingleByChannel(Hit, Inside, Above, ECC_Visibility, Query));
        TArray<FHitResult> OpenHits;
        F.World->LineTraceMultiByObjectType(OpenHits, Above, Inside, Objects, All);
        TestTrue(TEXT("开盖后对象查询不忽略任何 Actor，原闭盖路径也无命中"), OpenHits.IsEmpty());
        TestTrue(TEXT("局部 X-12 到 X12、Z10 仍首先命中真实罐壁"),
            F.World->LineTraceSingleByChannel(Hit, TM.TransformPosition(FVector(-12, 0, 10)),
                TM.TransformPosition(FVector(12, 0, 10)), ECC_Visibility, Query)
            && Hit.GetActor() == Jar && Hit.GetComponent() == Body);
        TestTrue(TEXT("内部向外到 X12 的罐壁仍阻挡，不仅测到外表面"),
            F.World->LineTraceSingleByChannel(Hit, TM.TransformPosition(FVector(0, 0, 10)),
                TM.TransformPosition(FVector(12, 0, 10)), ECC_Visibility, Query)
            && Hit.GetActor() == Jar && Hit.GetComponent() == Body);
        TestTrue(TEXT("向下穿罐底先命中 Jar Body 而不是穿到底下地面"),
            F.World->LineTraceSingleByChannel(Hit, Inside, TM.TransformPosition(FVector(0, 0, -10)), ECC_Visibility, Query)
            && Hit.GetActor() == Jar && Hit.GetComponent() == Body);
        TestTrue(TEXT("罐外向下实际查询命中地面，查询场景未被关掉"),
            F.World->LineTraceSingleByChannel(Hit, TM.TransformPosition(FVector(20, 0, 30)),
                TM.TransformPosition(FVector(20, 0, -10)), ECC_Visibility, Query)
            && Hit.GetActor() == Floor);
        Jar->SetActorLocationAndRotation(FVector(0, 0, 100), FRotator(120, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        const FVector ActualLip = BeanStart(Jar);
        TestFalse(TEXT("实际倾倒口沿沿真实8/20射向出发1cm不撞自身罐体，未忽略 Jar"),
            F.World->LineTraceSingleByChannel(Hit, ActualLip, ActualLip + BeanDirection(Jar), ECC_Visibility, Query));
        TestTrue(TEXT("所有形状查询后仍是默认 NoSim fixture"), !F.World->bShouldSimulatePhysics && !Body->IsSimulatingPhysics());
        Near(*this, TEXT("碰撞查询不动150g库存"), Jar->GetBeanGrams(), 150);
        Balance(*this, F, Jar);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeanJarSteamCoffeeChain, "CyberCafe.BeanJarSteam.JarPourToGrindGroundsToCoffee100", BeanJarSteamTests::TestFlags)
bool FBeanJarSteamCoffeeChain::RunTest(const FString&)
{
    using namespace BeanJarSteamTests;
    FJarAssets A; if (!A.Check(*this)) return false;
    UClass* DripperClass = LoadClass<AHandDripperActor>(nullptr,
        TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandDripper.BP_HandDripper_C"));
    UClass* KettleClass = LoadClass<AHandPourKettleActor>(nullptr, KettlePath);
    UClass* CupClass = LoadClass<ACupActor>(nullptr, CupPath);
    UClass* GrinderClass = LoadClass<ACoffeeGrinderActor>(nullptr, GrinderPath);
    if (!TestNotNull(TEXT("保存滤杯 BP，缺失不原生兜底"), DripperClass)
        || !TestNotNull(TEXT("保存壶 BP"), KettleClass) || !TestNotNull(TEXT("原 Cup03 BP"), CupClass)
        || !TestNotNull(TEXT("原磨豆机 BP"), GrinderClass)) return false;
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    const auto SavedPose = [](UClass* Class, const FVector& Center)
    {
        return FTransform(FQuat::Identity, Center, Class->GetDefaultObject<AActor>()->GetActorScale3D());
    };
    FWorld F(*this); auto* Jar = F.Jar(A);
    auto* G = F.Spawn<ACoffeeGrinderActor>(GrinderClass);
    auto* D = F.Spawn<AHandDripperActor>(DripperClass, SavedPose(DripperClass, FVector(200, 0, 50)));
    auto* K = F.Spawn<AHandPourKettleActor>(KettleClass, SavedPose(KettleClass, FVector(400, 0, 50)));
    auto* Cup = F.Spawn<ACupActor>(CupClass, SavedPose(CupClass, FVector(200, 0, 39.12)));
    if (!Jar || !G || !D || !K || !Cup) return false;
    // 只设置 BeginPlay 前初始装量；全部100mL只能从壶的真实初始500mL扣出。
    K->FillAmount = 1; D->FillAmount = 0; Cup->FillAmount = 0;
    if (!F.Begin()) return false;
    auto* S = F.Transfer(); auto* Drawer = G->DrawerRef.Get();
    if (!TestNotNull(TEXT("闭环豆罐真实生成的盖"), Jar->CapActor.Get())
        || !TestNotNull(TEXT("原 BP 真实生成的柄"), G->HandleRef.Get())
        || !TestNotNull(TEXT("原 BP 真实生成的抽屉"), Drawer)
        || !TestNotNull(TEXT("真实 BeanEntryPoint"), G->BeanEntryPoint.Get())
        || !TestNotNull(TEXT("真实 GroundPourPoint"), Drawer->GroundPourPoint.Get())
        || !TestNotNull(TEXT("真实抽屉 Custom Grab"), Drawer->GrabComp.Get())) return false;
    for (auto* Mesh : {Jar->ContainerMesh.Get(), Jar->CapActor->CapMesh.Get(), G->BodyMesh.Get(), Drawer->DrawerMesh.Get(),
        D->ContainerMesh.Get(), K->ContainerMesh.Get(), Cup->ContainerMesh.Get()})
        if (!CookQueryBody(*this, Mesh)) return false;
    TestTrue(TEXT("真实保存容器网格就是物理根，不换合成口点或几何"), D->GetRootComponent() == D->ContainerMesh.Get()
        && K->GetRootComponent() == K->ContainerMesh.Get() && Cup->GetRootComponent() == Cup->ContainerMesh.Get());
    TestTrue(TEXT("原类和实际 BeginPlay 子件及搬粉 delegate 保留"), Drawer->GetClass() == G->DrawerClass.Get()
        && G->HandleRef->GetClass() == G->HandleClass.Get() && Drawer->OwnerGrinder == G
        && Drawer->State == AGrinderDrawerActor::EDrawerState::Attached && Drawer->HasActorBegunPlay()
        && G->OnGrindProgress.Contains(Drawer, FName(TEXT("OnGrinderGrindProgress"))));
    TestTrue(TEXT("滤纸粉床水面只展示，保存 NoCollision 不由夹具补关"),
        D->PaperMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision
        && D->GroundBed->GetCollisionEnabled() == ECollisionEnabled::NoCollision
        && D->WaterSurface->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    const FTransform DripEntry = D->PourEntryPoint->GetComponentTransform();
    const FTransform CupEntry = Cup->PourEntryPoint->GetComponentTransform();
    const float DripRadius = D->PourEntryRadius, CupRadius = Cup->PourEntryRadius;
    TestTrue(TEXT("真实出口在原杯口上方且相同中心，保留源和杯入口局部姿态"),
        D->GetActorTransform().TransformPosition(D->OutletLocal).Z > CupEntry.GetLocation().Z
        && (D->GetActorTransform().TransformPosition(D->OutletLocal) - CupEntry.GetLocation()).Size2D() < .001);
    Near(*this, TEXT("原滤杯闷蒸仍8秒"), D->BloomSeconds, 8);
    Near(*this, TEXT("原滤杯排液仍12mL/s"), D->DrainRateMLPerSecond, 12);
    Near(*this, TEXT("原磨豆入口仍z38"), G->BeanEntryPoint->GetComponentLocation().Z, 38);
    Near(*this, TEXT("原磨豆机起始豆槽零"), G->GetCurrentBeanGrams(), 0);
    Near(*this, TEXT("原抽屉起始粉零"), Drawer->GetGroundsGrams(), 0);
    Near(*this, TEXT("壶实际初始 Water500"), K->GetIngredientVolumeML(TEXT("Water")), 500);
    Near(*this, TEXT("账本 Initial500"), S->GetBalance().InitialML, 500);
    Near(*this, TEXT("全链初始 Injected0"), S->GetBalance().InjectedML, 0);
    TestTrue(TEXT("声音配置仍来自实际 Source SFX，不补玩家音频 hook"),
        (G->LoadedGrindSound || G->GrindSFX->Sound) && Drawer->GroundPourSound && D->DripSound && K->PourLoopSound);
    auto* GrindCue = LoadObject<USoundCue>(nullptr,
        TEXT("/Game/VRTemplate/CoffeeBrew/Audio/SC_Grind_Feedback_Loop.SC_Grind_Feedback_Loop"));
    auto* GrindWave = LoadObject<USoundWave>(nullptr, TEXT("/Game/coffee_grinder_scene/Sound/S_Grind.S_Grind"));
    if (!TestNotNull(TEXT("保存的新研磨 Cue，测试不补造"), GrindCue)
        || !TestNotNull(TEXT("保留原研磨 Wave 资产"), GrindWave)) return false;
    auto* WavePlayer = Cast<USoundNodeWavePlayer>(GrindCue->FirstNode.Get());
    if (!TestNotNull(TEXT("新 Cue 的实际 WavePlayer"), WavePlayer)) return false;
    TestTrue(TEXT("actual Loaded 读取保存新 Cue，循环复用原 Wave；不改 Wave/材质/Quiet 或全局状态"),
        G->LoadedGrindSound == GrindCue && WavePlayer->GetSoundWave() == GrindWave && WavePlayer->bLooping);
    Near(*this, TEXT("新 Cue 的保存音量 .65，不写原 Wave 音量"), GrindCue->VolumeMultiplier, .65);

    UMotionControllerComponent* MC = nullptr;
    if (!Open(*this, F, Jar, MC) || !ReleaseAway(*this, Jar)) return false;
    Aim(Jar, G->BeanEntryPoint->GetComponentLocation(), 15);
    // 豆/研磨/抽屉使用公开输入 API，不冒称 HMD 手部输入；库存搬运全部走生产路径。
    for (int32 I = 0; I < 3; ++I) Near(*this, TEXT("实际公开倒豆每次5g"), Jar->PourBeans(1), 5);
    Near(*this, TEXT("真实原槽收到15g"), G->GetCurrentBeanGrams(), 15);
    Near(*this, TEXT("源豆罐余135g"), Jar->GetBeanGrams(), 135);
    Near(*this, TEXT("豆罐实际 accepted15g"), Jar->TotalAcceptedGrams, 15);
    Near(*this, TEXT("豆无洒漏"), Jar->SpilledBeanGrams, 0);
    for (int32 I = 0; I < 6; ++I) Near(*this, TEXT("每圈公共360度/.3秒实际研磨2.5g"), G->AdvanceGrinding(360, .3f), 2.5);
    Near(*this, TEXT("六圈实际累计消耗15g"), G->GetTotalBeanConsumedGrams(), 15);
    Near(*this, TEXT("真实粉累计产出15g"), G->GetTotalGroundOutputGrams(), 15);
    Near(*this, TEXT("研磨损耗零"), G->GetGrindLossGrams(), 0);
    Near(*this, TEXT("磨豆机豆归零"), G->GetCurrentBeanGrams(), 0);
    Near(*this, TEXT("主体粉已由原 delegate 搬走"), G->GetCurrentGroundGrams(), 0);
    Near(*this, TEXT("实际 DrawerRef 得到15g"), Drawer->GetGroundsGrams(), 15);
    Balance(*this, F, Jar);

    auto* DrawerHand = F.Hand(Drawer->GetActorTransform(), EControllerHand::Right);
    if (!DrawerHand || !TestTrue(TEXT("真实 Custom 抓屉而不直接写 State"),
        Drawer->GrabComp->GrabType == EGrabType::Custom && Drawer->GrabComp->TryGrab(DrawerHand))) return false;
    Drawer->DetachFromGrinder(DrawerHand);
    if (!TestTrue(TEXT("公开 Detach 使用真实 holding controller，抽屉独立"),
        Drawer->State == AGrinderDrawerActor::EDrawerState::Detached && Drawer->GrabComp->GetHoldingController() == DrawerHand
        && Drawer->GrabComp->IsHeld() && !Drawer->GetAttachParentActor())) return false;
    // 公共输入控制位姿；关抓取组件位姿跟随，避免它在后续 World Tick 把夹具口点移回磨豆机。
    Drawer->GrabComp->SetComponentTickEnabled(false);
    const FQuat DrawerTilt(FVector::ForwardVector, FMath::DegreesToRadians(-65.0));
    const FVector LipLocal = Drawer->GetActorTransform().InverseTransformPosition(Drawer->GroundPourPoint->GetComponentLocation());
    const FVector WantedLip = DripEntry.GetLocation() + FVector(0, 0, 20);
    Drawer->SetActorLocationAndRotation(WantedLip - DrawerTilt.RotateVector(LipLocal * Drawer->GetActorScale3D()),
        DrawerTilt, false, nullptr, ETeleportType::TeleportPhysics);
    TestTrue(TEXT("真实出粉口在原入口正上方20cm，最大射程未扩大"),
        Drawer->GroundPourPoint->GetComponentLocation().Equals(WantedLip, .001) && Drawer->GroundPourTraceDistanceCM <= 40);
    Near(*this, TEXT("原抽屉出粉率仍8g/s"), Drawer->GroundPourRateGramsPerSecond, 8);
    const auto* SavedDrawer = G->DrawerClass->GetDefaultObject<AGrinderDrawerActor>();
    if (!TestTrue(TEXT("实际DrawerClass保存Instant=true/Assist=true，实例继承且隐藏粉流，不由测试切模式"),
        SavedDrawer->bInstantGroundPour && SavedDrawer->bAssistInstantGroundTransfer && !SavedDrawer->bShowPowderStream
        && Drawer->bInstantGroundPour && Drawer->bAssistInstantGroundTransfer && !Drawer->bShowPowderStream)) return false;
    TestTrue(TEXT("出粉 API 前真实持有该抽屉与 controller，未人为改 State 或克数"),
        Drawer->GrabComp->IsHeld() && Drawer->GrabComp->GetHoldingController() == DrawerHand);
    // 严格迁移到保存 instant 契约：保留旧两秒调用，首 transfer 实际15g，不再期待14.50g。
    Near(*this, TEXT("保存 instant 模式首个两秒公共出粉实际15g"), Drawer->PourGrounds(2), 15);
    Near(*this, TEXT("instant 首次实际 accepted15g"), Drawer->LastAcceptedGroundGrams, 15);
    const FVector AcceptedGroundPoint = Drawer->LastGroundPourEnd;
    Near(*this, TEXT("instant 已出完，后续尾帧不得重复转移"), Drawer->PourGrounds(.1f), 0);
    Drawer->UpdatePowderStream(.01f);
    TestTrue(TEXT("保存 instant 全链无视觉粉流，实际粉质量不受表现开关影响"),
        Drawer->PowderStream->GetInstanceCount() == 0 && !Drawer->PowderStream->IsVisible() && Drawer->PowderStream->bHiddenInGame);
    Near(*this, TEXT("实际抽屉出完15g"), Drawer->GetGroundsGrams(), 0);
    Near(*this, TEXT("原入口真实接纳15g粉"), D->GetGroundsGrams(), 15);
    Near(*this, TEXT("粉无洒漏"), Drawer->SpilledGroundGrams, 0);
    TestTrue(TEXT("实际最近入口命中且进入 Dry，不直接加粉或写状态"),
        AcceptedGroundPoint.Equals(DripEntry.GetLocation(), .001) && D->BrewState == ECoffeeBrewState::Dry);
    Near(*this, TEXT("实际产粉质量闭合到粉床"), G->GetTotalGroundOutputGrams(),
        G->GetCurrentGroundGrams() + Drawer->GetGroundsGrams() + D->GetGroundsGrams() + Drawer->SpilledGroundGrams);
    Drawer->SetActorLocation(FVector(400, 100, 50), false, nullptr, ETeleportType::TeleportPhysics);
    DrawerHand->SetWorldLocation(Drawer->GetActorLocation());
    if (!TestTrue(TEXT("远离磨豆机真实松屉"), Drawer->GrabComp->TryRelease())) return false;
    Quiet(Drawer); // 只关闭刚体模拟，保留原碰撞，抽屉退出真实水流路径。

    auto* Steam = K->GetHotWaterSteamComponent(); auto* Template = LoadObject<UParticleSystem>(nullptr, SteamPath);
    if (!TestNotNull(TEXT("保存壶原生蒸汽组件"), Steam) || !TestNotNull(TEXT("原 Cascade 模板"), Template)) return false;
    TestTrue(TEXT("蒸汽原模板来自保存配置，不由测试赋值"), Steam->SteamSystem == Template);
    const auto* SavedSteam = KettleClass->GetDefaultObject<AHandPourKettleActor>()->GetHotWaterSteamComponent();
    if (!TestNotNull(TEXT("全链实际保存蒸汽配置"), SavedSteam)) return false;
    const float SavedSteamScale = SavedSteam->SteamScale;
    TestTrue(TEXT("全链保存蒸汽视觉缩放已增大，不猜固定新尺寸"), FMath::IsFinite(SavedSteamScale) && SavedSteamScale >= .4f);
    Near(*this, TEXT("全链蒸汽实例使用实际保存配置"), Steam->SteamScale, SavedSteamScale);
    TestTrue(TEXT("尚无实际水落点，不能预先请求蒸汽"), !Steam->IsSteamRequested() && !Steam->GetSteamComponent());
    // 仅剥离非蒸汽 FX 的测试表现资产；网格根、口点、碰撞、SFX 与业务参数均保留。
    for (auto* Actor : {static_cast<AActor*>(D), static_cast<AActor*>(K), static_cast<AActor*>(Cup)})
    {
        TArray<UNiagaraComponent*> FXs; Actor->GetComponents(FXs);
        for (auto* FX : FXs) { FX->DeactivateImmediate(); FX->SetAsset(nullptr); FX->SetComponentTickEnabled(false); }
    }
    K->SetActorRotation(FRotator(-65, 0, 0), ETeleportType::TeleportPhysics);
    const FTransform Spout = K->GetKettleSpoutWorldTransform();
    const FVector Velocity = Spout.GetUnitAxis(EAxis::X) * 35;
    const double Height = 10;
    const double FlightTime = (Velocity.Z + FMath::Sqrt(Velocity.Z * Velocity.Z + 2 * 980 * Height)) / 980;
    // 当前 PourWater 发35cm/s、重力980的包，未传 RibbonTransform；不把表现 Ribbon 曲线当业务弹道。
    const FVector WantedSpout = DripEntry.GetLocation() + FVector(0, 0, Height)
        - FVector(Velocity.X, Velocity.Y, 0) * FlightTime;
    K->SetActorLocation(K->GetActorLocation() + WantedSpout - Spout.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
    TestTrue(TEXT("平移真实根使实际嘴心弹道命中原滤杯入口，不搬口点"),
        (K->GetKettleSpoutWorldTransform().GetLocation() + Velocity * FlightTime
            + FVector(0, 0, -490) * FlightTime * FlightTime).Equals(DripEntry.GetLocation(), .001));
    const float Tilt = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(K->GetActorUpVector().Z, -1.f, 1.f)));
    const float Rate = K->MaxFlowMLPerSecond * FMath::Clamp((Tilt - K->PourTiltDegrees) / 40.f, 0.f, 1.f);
    if (!Near(*this, TEXT("实际65度壶流量17.5mL/s，保留原率"), Rate, 17.5)) return false;
    constexpr float Dt = .02f;
    constexpr double WantedWater = 100;
    const double InitialWater = K->GetPreciseVolumeML();
    const auto Debit = [K, InitialWater]() { return InitialWater - K->GetPreciseVolumeML(); };
    D->SetActorTickEnabled(true); Cup->SetActorTickEnabled(true); K->SetActorTickEnabled(true);
    bool SawWaterPackets = false, SawBloom = false, SawCoffeePackets = false, SawSteam = false, SawFlow = false;
    double FirstWet = -1, FirstDrip = -1;
    UParticleSystemComponent* OnlySteam = nullptr;
    const auto Observe = [&]()
    {
        const auto B = S->GetBalance();
        SawWaterPackets |= B.PacketCount > 0 && D->DrippedML == 0;
        SawCoffeePackets |= B.PacketCount > 0 && D->DrippedML > 0;
        SawBloom |= D->BrewState == ECoffeeBrewState::Blooming;
        if (D->BrewState == ECoffeeBrewState::Blooming) Near(*this, TEXT("实际闷蒸阶段不提前滴流"), D->DrippedML, 0);
        TestTrue(TEXT("实际滤杯存量不超过原120mL容量"), D->GetPreciseVolumeML() <= D->MaxVolumeML + Tolerance);
        SawFlow |= K->bIsPouring && K->CurrentFlowMLPerSecond > 0;
        if (FirstWet < 0 && D->GetIngredientVolumeML(TEXT("Water")) > 0) FirstWet = F.World->GetTimeSeconds();
        if (FirstDrip < 0 && D->DrippedML > 0) FirstDrip = F.World->GetTimeSeconds();
        if (Steam->IsSteamRequested())
        {
            SawSteam = true;
            TestTrue(TEXT("真实蒸汽落点是原滤杯入口加0.3cm法向偏置"),
                Steam->GetLastImpactPoint().Equals(DripEntry.GetLocation() + DripEntry.GetUnitAxis(EAxis::Z) * .3, .03));
        }
        TArray<UParticleSystemComponent*> Components; K->GetComponents(Components);
        TestTrue(TEXT("真实水落点始终至多一个 PS 组件"), Components.Num() <= 1);
        if (auto* PS = Steam->GetSteamComponent())
        {
            if (!OnlySteam) OnlySteam = PS;
            TestTrue(TEXT("全链复用同一壶的原 Cascade 和实际保存缩放，不按包新建"),
                PS == OnlySteam && PS->Template == Template && PS->GetOwner() == K
                && PS->GetComponentScale().Equals(FVector(SavedSteamScale), Tolerance));
        }
        Near(*this, TEXT("每次 World Tick 全毫升账本误差零"), B.ErrorML, 0);
        Near(*this, TEXT("每帧无隔空注液或额外注入"), B.InjectedML, 0);
        Near(*this, TEXT("每帧真实几何水流无洒漏"), B.SpilledML, 0);
        TestTrue(TEXT("总实际出水不会离散越过100mL"), Debit() <= WantedWater + 1.e-9);
    };
    for (int32 Frame = 0; Frame < 320 && WantedWater - Debit() > static_cast<double>(Rate) * Dt + 1.e-9; ++Frame)
    {
        const double Before = Debit(); TickWorld(F, Dt); Observe();
        if (!TestTrue(TEXT("正常 World Tick 调用真实壶 Tick 并扣正水量"), Debit() > Before)) return false;
        Near(*this, TEXT("World Tick 未重复调用出水或钳改小帧"), Debit() - Before, static_cast<double>(Rate) * Dt);
    }
    K->SetActorTickEnabled(false);
    // 尾帧按剩余真实 debit 截断；公开 PourWater 只驱动源，结算及冲泡仍由 World Tick 完成。
    for (int32 Tail = 0; Tail < 8 && WantedWater - Debit() > 1.e-10; ++Tail)
    {
        const double Remaining = WantedWater - Debit();
        float TailDt = static_cast<float>(Remaining / Rate);
        if (static_cast<double>(Rate) * TailDt > Remaining) TailDt = std::nextafter(TailDt, 0.f);
        if (!TestTrue(TEXT("有界尾帧仍由真实 PourWater 发正量"), TailDt > 0 && K->PourWater(TailDt) > 0)) return false;
        TickWorld(F, Dt); Observe();
    }
    Near(*this, TEXT("只从500mL来源实际扣100mL"), Debit(), 100);
    K->SetActorRotation(FRotator::ZeroRotator, ETeleportType::TeleportPhysics);
    K->SetActorTickEnabled(true); // 真实直立 Tick 停流，不能因400mL余水再次发包。
    for (int32 Frame = 0; Frame < 1250; ++Frame) { TickWorld(F, Dt); Observe(); }
    TestTrue(TEXT("真实 World Tick 观察到水包/闷蒸/咖啡包/壶出流和落点蒸汽"),
        SawWaterPackets && SawBloom && SawCoffeePackets && SawFlow && SawSteam && OnlySteam);
    TestTrue(TEXT("实际世界时钟保持原8秒闷蒸"), FirstWet >= 0 && FirstDrip - FirstWet >= 7.98);
    Near(*this, TEXT("杯中实际 Coffee100，非手动标完成或加成分"), Cup->GetIngredientVolumeML(TEXT("Coffee")), 100);
    Near(*this, TEXT("杯中真实总量100"), Cup->GetPreciseVolumeML(), 100);
    Near(*this, TEXT("杯中 Water0"), Cup->GetIngredientVolumeML(TEXT("Water")), 0);
    Near(*this, TEXT("杯中 Milk0"), Cup->GetIngredientVolumeML(TEXT("Milk")), 0);
    Near(*this, TEXT("原壶真实 Water400"), K->GetIngredientVolumeML(TEXT("Water")), 400);
    Near(*this, TEXT("原壶真实余400"), K->GetPreciseVolumeML(), 400);
    Near(*this, TEXT("滤杯真实滴出100"), D->DrippedML, 100);
    Near(*this, TEXT("生产出液钩子实际转换 Coffee100"), D->CoffeeProducedML, 100);
    Near(*this, TEXT("滤杯完全排空"), D->GetPreciseVolumeML(), 0);
    TestTrue(TEXT("同批自然 Spent 且不再出水，不改状态或预算"), D->BrewState == ECoffeeBrewState::Spent && !K->bIsPouring);
    TestTrue(TEXT("源入口/目标入口及原半径全过程不搬移不扩大"), D->PourEntryPoint->GetComponentTransform().Equals(DripEntry, .001)
        && Cup->PourEntryPoint->GetComponentTransform().Equals(CupEntry, .001)
        && D->PourEntryRadius == DripRadius && Cup->PourEntryRadius == CupRadius);
    const auto Final = S->GetBalance();
    Near(*this, TEXT("全链 Initial 保持500"), Final.InitialML, 500);
    Near(*this, TEXT("全链 Injected0"), Final.InjectedML, 0); Near(*this, TEXT("全链 Removed0"), Final.RemovedML, 0);
    Near(*this, TEXT("全链 Spilled0"), Final.SpilledML, 0); Near(*this, TEXT("全部尾包结算 InFlight0"), Final.InFlightML, 0);
    Near(*this, TEXT("真实库存400Water加100Coffee保持500"), Final.ContainerML, 500);
    TestEqual(TEXT("完整排空后真实 PacketCount0"), Final.PacketCount, 0);
    TestFalse(TEXT("Hold 到期通过实际组件 Tick 关蒸汽请求"), Steam->IsSteamRequested());
    Near(*this, TEXT("闭环后豆罐仍135g"), Jar->GetBeanGrams(), 135);
    Near(*this, TEXT("闭环后豆罐 accepted15g不重复改写"), Jar->TotalAcceptedGrams, 15);
    Balance(*this, F, Jar);
    // writtenpendingrun：NoSim 碰撞/实际 World Tick 的逻辑闭环；不承诺渲染、HMD 或声音设备验收。
    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
