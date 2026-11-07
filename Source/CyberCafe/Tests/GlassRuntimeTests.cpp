// @trace GB-01 GB-02 GB-03 GB-04
// 保存资产发现、真实运动 sweep 与 Chaos 掉落；不调用 TryImpact 或手工广播 Hit。
// 仅实际执行测试时输出采样，不把 NullRHI 数据称为截图或硬件验收。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/CupActor.h"
#include "Liquid/BottleActor.h"
#include "Liquid/BottleCapActor.h"
#include "GrabComponent.h"
#include "Character/VRPawn.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialStep.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/ServingSpotActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "MotionControllerComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "NiagaraComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "CoreGlobals.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

namespace GlassRuntimeTests
{
constexpr float StepSeconds = .01f;
constexpr int32 WarmFrames = 100;
constexpr double VolumeTolerance = 1.e-6;
const TCHAR* CupClassPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup01.BP_Cup01_C");
const TCHAR* MilkClassPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle04/BP_Bottle04_Milk.BP_Bottle04_Milk_C");
const TCHAR* CupProfilePath = TEXT("/Game/VRTemplate/Breakage/DA_CupBreak.DA_CupBreak");
const TCHAR* MilkProfilePath = TEXT("/Game/VRTemplate/Breakage/DA_MilkBottleBreak.DA_MilkBottleBreak");

FString Quote(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return TEXT("\"") + Value + TEXT("\"");
}

// 不引入新的模块依赖；每行只含本次发现或 getter 实测值。
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

bool SaveSamples(FAutomationTestBase& Test, const TCHAR* Group, const TArray<FString>& Rows)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("GlassRuntime");
    if (!Test.TestTrue(TEXT("创建本次 GlassRuntime 采样目录"),
        IFileManager::Get().MakeDirectory(*Directory, true))) return false;
    const FString Json = TEXT("{\"sampleKind\":\"savedAssetsAndActualWorldTick\",\"group\":")
        + Quote(Group) + TEXT(",\"screenshotsCaptured\":false,\"rows\":[\n")
        + FString::Join(Rows, TEXT(",\n")) + TEXT("\n]}\n");
    // 每组独立覆盖，不聚合旧测试或上次运行的结果。
    return Test.TestTrue(TEXT("保存本组真实采样 JSON"), FFileHelper::SaveStringToFile(Json,
        *(Directory / (FString(Group) + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

struct FDebrisCounts
{
    int32 Batches = 0, Shards = 0, SimulatedShards = 0, OtherMeshes = 0;
};

FDebrisCounts DiscoverDebris(UWorld* World, const UGlassBreakProfile* Profile)
{
    FDebrisCounts Result;
    for (TActorIterator<AGlassDebrisActor> It(World); It; ++It)
    {
        ++Result.Batches;
        TArray<UStaticMeshComponent*> Components;
        It->GetComponents(Components);
        for (const UStaticMeshComponent* Component : Components)
        {
            bool bShard = false;
            for (const FGlassShardSpec& Spec : Profile->Shards)
                bShard |= Spec.Mesh && Spec.Mesh.Get() == Component->GetStaticMesh();
            if (bShard)
            {
                ++Result.Shards;
                if (Component->IsSimulatingPhysics()) ++Result.SimulatedShards;
            }
            else ++Result.OtherMeshes; // 液体湿迹 Cylinder 不能算成第九块玻璃。
        }
    }
    return Result;
}

// 沿用 GlassBreakageTests 的 WorldContext/GI 生命周期；不包含旧 cpp，避免重复注册测试。
struct FRuntimeWorld
{
    UWorld* World;
    UGameInstance* GI;
    TArray<ALiquidContainerActor*> Containers;
    int32 Frames = 0;

    explicit FRuntimeWorld(bool bPhysics)
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(bPhysics);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
            true, ERHIFeatureLevel::SM5, &IV);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        GI->Init();
    }

    ~FRuntimeWorld()
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    static void IsolateVessel(ALiquidContainerActor* A)
    {
        A->ContainerMesh->SetSimulatePhysics(false);
        A->ContainerMesh->SetEnableGravity(false);
        A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        A->ContainerMesh->SetGenerateOverlapEvents(false);
        A->bCanPour = false; // 禁止无关倒液影响破碎余量账本。
        A->bEnableSplash = false;
        A->LiquidFXTemplate = nullptr;
        A->SplashEffectTemplate = nullptr;
        for (UNiagaraComponent* FX : {A->LiquidFX.Get(), A->PourFX.Get()})
        {
            FX->SetAutoActivate(false);
            FX->SetAsset(nullptr);
            FX->DeactivateImmediate();
        }
        A->GrabComp->GrabType = EGrabType::Free;
        A->GrabComp->PhysicsMode = EGrabPhysicsMode::Kinematic;
        A->GrabComp->bSimulateOnDrop = false;
        A->GrabComp->bCaptureHand = false;
        A->GrabComp->HandAnimLayer = nullptr;
        A->GrabComp->OnGrabHapticEffect = nullptr;
        if (ACupActor* Cup = Cast<ACupActor>(A))
        {
            Cup->VolumeWidgetClass = nullptr;
            Cup->VolumeWidgetComp->SetWidgetClass(nullptr);
            Cup->bFaceCamera = false;
        }
        if (ABottleActor* Bottle = Cast<ABottleActor>(A)) Bottle->PourHaptic = nullptr;
    }

    ALiquidContainerActor* Vessel(UClass* Class, UGlassBreakProfile* Profile, const FVector& Location)
    {
        const ALiquidContainerActor* CDO = Class->GetDefaultObject<ALiquidContainerActor>();
        const FTransform Transform(FQuat::Identity, Location, CDO->GetActorScale3D());
        ALiquidContainerActor* A = World->SpawnActorDeferred<ALiquidContainerActor>(Class,
            Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!A) return nullptr;
        IsolateVessel(A);
        A->FinishSpawning(Transform);
        IsolateVessel(A); // SCS 完成后再隔离，但不替换保存的容器网格/容量/Fill/瓶盖配置。
        A->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
        UGlassBreakableComponent* Comp = A->FindComponentByClass<UGlassBreakableComponent>();
        if (!Comp)
        {
            Comp = NewObject<UGlassBreakableComponent>(A);
            A->AddInstanceComponent(Comp);
            Comp->Profile = Profile;
            Comp->RegisterComponent();
        }
        else Comp->Profile = Profile;
        Containers.Add(A);
        return A;
    }

    UStaticMeshComponent* Plane()
    {
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!Cube) return nullptr;
        AActor* Floor = World->SpawnActor<AActor>();
        if (!Floor) return nullptr;
        UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Floor);
        Floor->SetRootComponent(Mesh);
        Floor->AddInstanceComponent(Mesh);
        Mesh->SetMobility(EComponentMobility::Static);
        Mesh->SetStaticMesh(Cube);
        Mesh->SetWorldScale3D(FVector(20, 20, .1));
        Mesh->SetWorldLocation(FVector(0, 0, -Cube->GetBoundingBox().Max.Z * .1));
        Mesh->SetCollisionObjectType(ECC_WorldStatic);
        Mesh->SetCollisionResponseToAllChannels(ECR_Block);
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Mesh->SetSimulatePhysics(false);
        Mesh->SetEnableGravity(false);
        Mesh->RegisterComponent();
        return Mesh; // 顶面 Z=0，静态大平台，不用物理模拟的 Cube 冒充容器。
    }

    AVRPawn* GripPawn()
    {
        const FTransform Transform(FVector(0, 1000, 200));
        AVRPawn* Pawn = World->SpawnActorDeferred<AVRPawn>(AVRPawn::StaticClass(),
            Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Pawn) return nullptr;
        Pawn->AutoPossessPlayer = EAutoReceiveInput::Disabled;
        Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
        Pawn->HoverHapticEffect = nullptr;
        Pawn->FinishSpawning(Transform);
        Pawn->SetActorTickEnabled(false);
        // 只隔离硬件追踪、自动扫描和 UI，杯/瓶及破碎组件仍由真实 World Tick 调度。
        for (UActorComponent* Component : Pawn->GetComponents()) Component->SetComponentTickEnabled(false);
        return Pawn;
    }

    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        for (ALiquidContainerActor* A : Containers) IsolateVessel(A);
    }

    void Tick(int32 Count = 1)
    {
        for (int32 I = 0; I < Count; ++I)
        {
            ++GFrameCounter; // 实际 World Tick 的逐帧门控，不能靠大 Dt 或手工组件 Tick 热身。
            World->Tick(LEVELTICK_All, StepSeconds);
            ++Frames;
        }
    }

    ULiquidTransferSubsystem* Transfers() const { return World->GetSubsystem<ULiquidTransferSubsystem>(); }
};

bool CheckFixture(FAutomationTestBase& Test, FRuntimeWorld& T, ALiquidContainerActor* A,
    UGlassBreakProfile* Profile, UStaticMeshComponent* Floor)
{
    UGlassBreakableComponent* Comp = A->FindComponentByClass<UGlassBreakableComponent>();
    if (!Test.TestNotNull(TEXT("真实组件在 BeginPlay 前安装"), Comp) ||
        !Test.TestNotNull(TEXT("源容器采用实际保存网格"), A->ContainerMesh->GetStaticMesh().Get()) ||
        !Test.TestNotNull(TEXT("真实世界转移账本"), T.Transfers()) ||
        !Test.TestNotNull(TEXT("真实 GI 事件系统"), T.GI->GetSubsystem<UCyberCafeEventSubsystem>())) return false;
    Test.TestTrue(TEXT("容器与组件均已正常 BeginPlay"), A->HasActorBegunPlay() && Comp->HasBegunPlay());
    Test.TestTrue(TEXT("组件已注册并由世界调度 Tick"), Comp->IsRegistered() && Comp->IsComponentTickEnabled());
    Test.TestTrue(TEXT("使用保存的 profile，未构造测试替代 profile"), Comp->Profile.Get() == Profile);
    Test.TestEqual(TEXT("保留组件默认启动保护 .6 秒"), Comp->StartupGraceSeconds, .6f);
    Test.TestFalse(TEXT("单元运动关闭无关倒液"), A->bCanPour);
    const ALiquidContainerActor* CDO = A->GetClass()->GetDefaultObject<ALiquidContainerActor>();
    Test.TestTrue(TEXT("源网格保持保存的 BP CDO 配置"), A->ContainerMesh->GetStaticMesh() == CDO->ContainerMesh->GetStaticMesh());
    Test.TestEqual(TEXT("源容量保持保存配置"), A->MaxVolumeML, CDO->MaxVolumeML);
    Test.TestEqual(TEXT("源原料身份保持保存配置"), A->InitialIngredientId, CDO->InitialIngredientId);
    Test.TestTrue(TEXT("平台 WorldStatic"), Floor->GetCollisionObjectType() == ECC_WorldStatic);
    Test.TestFalse(TEXT("平台自身不模拟物理"), Floor->IsSimulatingPhysics());
    FHitResult Hit;
    const FVector P = A->GetActorLocation();
    const bool bHit = T.World->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, 50),
        FVector(P.X, P.Y, -50), ECC_Visibility, FCollisionQueryParams(NAME_None, false, A));
    Test.TestTrue(TEXT("真实查询命中平台顶面 Z=0"), bHit && Hit.GetComponent() == Floor &&
        FMath::IsNearlyZero(Hit.ImpactPoint.Z, .001) && Hit.ImpactNormal.Z > .99);
    return true;
}

void CaptureVessel(TArray<FString>& Rows, const TCHAR* Stage, const FRuntimeWorld& T,
    const ALiquidContainerActor* A, const UGlassBreakProfile* Profile)
{
    const FLiquidBalance B = T.Transfers()->GetBalance();
    const FDebrisCounts D = DiscoverDebris(T.World, Profile);
    const ALiquidContainerActor* CDO = A->GetClass()->GetDefaultObject<ALiquidContainerActor>();
    FRow Row;
    Row.Text(TEXT("stage"), Stage); Row.Text(TEXT("sourceClass"), A->GetClass()->GetPathName());
    Row.Text(TEXT("sourceMesh"), A->ContainerMesh->GetStaticMesh()->GetPathName());
    Row.Text(TEXT("profile"), Profile->GetPathName()); Row.Text(TEXT("ingredient"), A->InitialIngredientId.ToString());
    Row.Number(TEXT("savedCapacityML"), CDO->MaxVolumeML); Row.Number(TEXT("savedFill"), CDO->FillAmount);
    Row.Number(TEXT("capacityML"), A->MaxVolumeML); Row.Number(TEXT("volumeML"), A->GetPreciseVolumeML());
    Row.Number(TEXT("revision"), A->GetLiquidRevision()); Row.Number(TEXT("worldFrames"), T.Frames);
    Row.Number(TEXT("worldTimeSeconds"), T.World->GetTimeSeconds()); Row.Number(TEXT("dtSeconds"), StepSeconds);
    Row.Number(TEXT("thresholdCms"), Profile->BreakSpeedCms); Row.Number(TEXT("debrisBatches"), D.Batches);
    Row.Number(TEXT("glassFragments"), D.Shards); Row.Number(TEXT("simulatedGlassFragments"), D.SimulatedShards);
    Row.Number(TEXT("nonShardMeshes"), D.OtherMeshes); Row.Number(TEXT("initialML"), B.InitialML);
    Row.Number(TEXT("injectedML"), B.InjectedML); Row.Number(TEXT("removedML"), B.RemovedML);
    Row.Number(TEXT("containerML"), B.ContainerML); Row.Number(TEXT("inFlightML"), B.InFlightML);
    Row.Number(TEXT("lastSpilledML"), B.SpilledML); Row.Number(TEXT("errorML"), B.ErrorML);
    Row.Bool(TEXT("broken"), A->IsBroken()); Row.Bool(TEXT("held"), A->GrabComp->IsHeld());
    Row.Bool(TEXT("canPour"), A->bCanPour); Row.Bool(TEXT("visible"), A->ContainerMesh->IsVisible());
    Row.Bool(TEXT("simulatingPhysics"), A->ContainerMesh->IsSimulatingPhysics());
    Row.Vector(TEXT("positionCM"), A->GetActorLocation());
    Row.Vector(TEXT("physicsVelocityCms"), A->ContainerMesh->GetPhysicsLinearVelocity());
    Rows.Add(Row.Json());
}

void CheckBroken(FAutomationTestBase& Test, FRuntimeWorld& T, ALiquidContainerActor* A,
    UGlassBreakProfile* Profile, double VolumeBefore, const FLiquidBalance& Before)
{
    const FDebrisCounts D = DiscoverDebris(T.World, Profile);
    const FLiquidBalance After = T.Transfers()->GetBalance();
    Test.TestTrue(TEXT("检测器通过实际运动触发 broken"), A->IsBroken());
    Test.TestEqual(TEXT("只生成一个实际 debris 批次"), D.Batches, 1);
    Test.TestEqual(TEXT("实际保存网格玻璃碎片恰好八块，排除湿迹"), D.Shards, 8);
    Test.TestEqual(TEXT("八块玻璃实际开启物理"), D.SimulatedShards, 8);
    Test.TestFalse(TEXT("破碎后不再持有"), A->GrabComp->IsHeld());
    Test.TestNull(TEXT("破碎释放控制器引用"), A->GrabComp->GetHoldingController());
    Test.TestFalse(TEXT("破碎关闭原容器物理与显示"), A->ContainerMesh->IsSimulatingPhysics() || A->ContainerMesh->IsVisible());
    Test.TestFalse(TEXT("破碎组件停止采样"), A->FindComponentByClass<UGlassBreakableComponent>()->IsComponentTickEnabled());
    Test.TestTrue(TEXT("破碎只洒出运动前实测余量"),
        FMath::Abs((After.SpilledML - Before.SpilledML) - VolumeBefore) < VolumeTolerance);
    Test.TestEqual(TEXT("未引入倒液在途包"), After.PacketCount, Before.PacketCount);
    Test.TestEqual(TEXT("破碎不作额外注入"), After.InjectedML, Before.InjectedML);
    Test.TestEqual(TEXT("破碎不作消耗"), After.RemovedML, Before.RemovedML);
    Test.TestTrue(TEXT("实际破碎账本闭合"), FMath::IsFinite(After.ErrorML) && FMath::Abs(After.ErrorML) < VolumeTolerance);
}

// 独立诊断查询只记录实际 Sweep 结果，不向检测器注入接触或速度。
void CaptureMotion(TArray<FString>& Rows, FRuntimeWorld& T, ALiquidContainerActor* A,
    const FTransform& Previous, const TCHAR* Stage, int32& FloorCrossings)
{
    const FTransform Current = A->ContainerMesh->GetComponentTransform();
    const FBox Box = A->ContainerMesh->GetStaticMesh()->GetBoundingBox();
    const FVector Local = Box.GetCenter() + FVector(0, 0, FMath::Lerp(-Box.GetExtent().Z, Box.GetExtent().Z, .12));
    const FVector Scale = Current.GetScale3D().GetAbs();
    const float Radius = FMath::Max(.3f, static_cast<float>(FMath::Min(Box.GetExtent().X * Scale.X,
        Box.GetExtent().Y * Scale.Y) * .8));
    const FVector Start = Previous.TransformPosition(Local), End = Current.TransformPosition(Local);
    const FVector Velocity = (End - Start) / StepSeconds;
    FHitResult Hit;
    FCollisionQueryParams Params(NAME_None, false, A);
    for (TActorIterator<APawn> It(T.World); It; ++It) Params.AddIgnoredActor(*It);
    if (ABottleActor* Bottle = Cast<ABottleActor>(A)) if (Bottle->CapActor) Params.AddIgnoredActor(Bottle->CapActor);
    const bool bHit = T.World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
        ECC_Visibility, FCollisionShape::MakeSphere(Radius), Params);
    if (bHit && !Hit.bStartPenetrating && Hit.GetComponent() &&
        Hit.GetComponent()->GetCollisionObjectType() == ECC_WorldStatic) ++FloorCrossings;
    FRow Row;
    Row.Text(TEXT("stage"), Stage); Row.Number(TEXT("worldFramesBeforeTick"), T.Frames);
    Row.Vector(TEXT("actualStartCM"), Start); Row.Vector(TEXT("actualEndCM"), End);
    Row.Vector(TEXT("measuredVelocityCms"), Velocity); Row.Number(TEXT("sphereRadiusCM"), Radius);
    Row.Bool(TEXT("probeBlockingSweep"), bHit); Row.Bool(TEXT("startPenetrating"), bHit && Hit.bStartPenetrating);
    Row.Bool(TEXT("heldBeforeTick"), A->GrabComp->IsHeld());
    if (bHit)
    {
        Row.Text(TEXT("probeHitActor"), GetPathNameSafe(Hit.GetActor()));
        Row.Vector(TEXT("probeImpactNormal"), Hit.ImpactNormal);
        Row.Number(TEXT("probeClosingCms"), UGlassBreakableComponent::ClosingSpeed(Velocity, Hit.ImpactNormal));
    }
    Rows.Add(Row.Json());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassSavedProfilesShardShapesAndMapRefTest,
    "CyberCafe.Glass.SavedProfilesShardShapesAndMapRef",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassSavedProfilesShardShapesAndMapRefTest::RunTest(const FString&)
{
    TArray<FString> Rows;
    UGlassBreakProfile* CupProfile = LoadObject<UGlassBreakProfile>(nullptr, CupProfilePath);
    UGlassBreakProfile* MilkProfile = LoadObject<UGlassBreakProfile>(nullptr, MilkProfilePath);
    if (!TestNotNull(TEXT("加载已保存原生杯 profile"), CupProfile) ||
        !TestNotNull(TEXT("加载已保存原生奶瓶 profile"), MilkProfile)) return false;
    TSet<UStaticMesh*> UniqueMeshes;
    for (int32 Group = 0; Group < 2; ++Group)
    {
        UGlassBreakProfile* Profile = Group == 0 ? CupProfile : MilkProfile;
        const TCHAR* Kind = Group == 0 ? TEXT("Cup") : TEXT("Bottle");
        TestTrue(TEXT("profile 为原生 UGlassBreakProfile，非蓝图占位"), Profile->GetClass() == UGlassBreakProfile::StaticClass());
        TestEqual(TEXT("保存 profile 恰好八块玻璃"), Profile->Shards.Num(), 8);
        TestEqual(TEXT("保存 profile 八块物理预算"), Profile->MaxPhysicsShards, 8);
        TestTrue(TEXT("保存阈值是有限正数"), FMath::IsFinite(Profile->BreakSpeedCms) && Profile->BreakSpeedCms > 0);
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const FString Name = FString::Printf(TEXT("SM_%sShard_%02d"), Kind, Index);
            const FString Path = FString::Printf(TEXT("/Game/VRTemplate/Breakage/Meshes/%s/%s.%s"), Kind, *Name, *Name);
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
            if (!TestNotNull(TEXT("从磁盘加载指定保存碎片网格"), Mesh)) return false;
            TestFalse(TEXT("两组十六块碎片引用互不重复"), UniqueMeshes.Contains(Mesh));
            UniqueMeshes.Add(Mesh);
            if (Profile->Shards.IsValidIndex(Index))
            {
                TestTrue(TEXT("profile 顺序引用真实对应碎片"), Profile->Shards[Index].Mesh.Get() == Mesh);
                TestTrue(TEXT("碎片局部变换有效"), Profile->Shards[Index].LocalTransform.IsValid());
            }
            const FBox Box = Mesh->GetBoundingBox();
            TestTrue(TEXT("碎片包围盒有限且三维非空"), Box.IsValid && !Box.Min.ContainsNaN() &&
                !Box.Max.ContainsNaN() && Box.GetSize().GetMin() > KINDA_SMALL_NUMBER);
            if (!TestTrue(TEXT("保存碎片有实际 LOD0"), Mesh->GetNumLODs() > 0 &&
                Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.IsValidIndex(0))) return false;
            const FStaticMeshLODResources& LOD = Mesh->GetLODForExport(0);
            const int32 Vertices = Mesh->GetNumVertices(0);
            const int32 Triangles = Mesh->GetNumTriangles(0);
            TestTrue(TEXT("碎片有非占位几何，顶点多于六个"), Vertices > 6 && Triangles > 0);
            TestEqual(TEXT("原生 LOD 顶点计数一致"), static_cast<int32>(LOD.GetNumVertices()), Vertices);
            UBodySetup* Body = Mesh->GetBodySetup();
            if (!TestNotNull(TEXT("保存碎片有 BodySetup"), Body)) return false;
            TestTrue(TEXT("碎片有凸体简单碰撞"), Body->AggGeom.ConvexElems.Num() > 0);
            TestTrue(TEXT("可模拟碎片不使用 ComplexAsSimple"), Body->CollisionTraceFlag != CTF_UseComplexAsSimple);
            int32 ConvexVertices = 0;
            for (const FKConvexElem& Convex : Body->AggGeom.ConvexElems)
            {
                ConvexVertices += Convex.VertexData.Num();
                const FBox ConvexBox(Convex.VertexData);
                TestTrue(TEXT("每个凸体至少四点且体积非空"), Convex.VertexData.Num() >= 4 &&
                    ConvexBox.IsValid && ConvexBox.GetSize().GetMin() > KINDA_SMALL_NUMBER);
            }
            FRow Row;
            Row.Text(TEXT("stage"), TEXT("savedShardDiscovery")); Row.Text(TEXT("profile"), Profile->GetPathName());
            Row.Text(TEXT("mesh"), Mesh->GetPathName()); Row.Number(TEXT("index"), Index);
            Row.Number(TEXT("lod0Vertices"), Vertices); Row.Number(TEXT("lod0Triangles"), Triangles);
            Row.Number(TEXT("convexCount"), Body->AggGeom.ConvexElems.Num()); Row.Number(TEXT("convexVertices"), ConvexVertices);
            Row.Vector(TEXT("boundsMinCM"), Box.Min); Row.Vector(TEXT("boundsMaxCM"), Box.Max);
            Rows.Add(Row.Json());
        }
    }
    UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
    if (!TestNotNull(TEXT("加载保存地图，不开关卡或 BeginPlay"), Map) ||
        !TestNotNull(TEXT("保存地图 PersistentLevel"), Map->PersistentLevel.Get())) return false;
    AGlassRecoveryActor* Manager = nullptr;
    ALiquidContainerActor* Cup = nullptr;
    ABottleActor* Milk = nullptr;
    ATutorialDirector* Director = nullptr;
    AOrderHologramActor* Hologram = nullptr;
    int32 ManagerCount = 0, HologramCount = 0, DirectorCount = 0;
    for (AActor* Actor : Map->PersistentLevel->Actors)
    {
        if (!Actor) continue;
        if (AGlassRecoveryActor* Candidate = Cast<AGlassRecoveryActor>(Actor))
        {
            ++ManagerCount;
            if (Actor->GetFName() == TEXT("GlassRecovery_Tutorial")) Manager = Candidate;
        }
        if (Actor->GetFName() == TEXT("BP_Cup01_C_1")) Cup = Cast<ALiquidContainerActor>(Actor);
        if (Actor->GetFName() == TEXT("BP_Bottle04_Milk_Tutorial")) Milk = Cast<ABottleActor>(Actor);
        if (ATutorialDirector* Candidate = Cast<ATutorialDirector>(Actor))
        {
            ++DirectorCount;
            if (Actor->GetFName() == TEXT("BP_TutorialDirecter_C_0")) Director = Candidate;
        }
        if (AOrderHologramActor* Candidate = Cast<AOrderHologramActor>(Actor))
        {
            ++HologramCount;
            if (Actor->GetFName() == TEXT("BP_OrderHologram_Tutorial")) Hologram = Candidate;
        }
    }
    if (!TestNotNull(TEXT("PersistentActors 发现保存的恢复经理"), Manager) ||
        !TestNotNull(TEXT("保留原杯实例"), Cup) || !TestNotNull(TEXT("保留原奶瓶实例"), Milk) ||
        !TestNotNull(TEXT("保留原教学导演"), Director) || !TestNotNull(TEXT("保留已保存全息实例"), Hologram)) return false;
    TestEqual(TEXT("恢复经理唯一，没有重复服务"), ManagerCount, 1);
    TestEqual(TEXT("原教学导演唯一"), DirectorCount, 1);
    TestEqual(TEXT("原全息实例唯一"), HologramCount, 1);
    TestTrue(TEXT("恢复经理为原生 class，不依赖新 BP"), Manager->GetClass() == AGlassRecoveryActor::StaticClass());
    TestTrue(TEXT("经理引用正确两个原实例及保存 profiles"), Manager->PrototypeCup.Get() == Cup &&
        Manager->PrototypeBottle.Get() == Milk && Manager->CupProfile.Get() == CupProfile && Manager->BottleProfile.Get() == MilkProfile);
    TestEqual(TEXT("原杯 class 未替换"), Cup->GetClass()->GetPathName(), FString(CupClassPath));
    TestEqual(TEXT("原奶瓶 class 未替换"), Milk->GetClass()->GetPathName(), FString(MilkClassPath));
    TestEqual(TEXT("原杯保存网格未换为基础 Cube"), GetPathNameSafe(Cup->ContainerMesh->GetStaticMesh().Get()),
        FString(TEXT("/Game/LiquidMaterials_VFXPack/Meshes/Glass05/SM_Glass_Glass_05.SM_Glass_Glass_05")));
    TestEqual(TEXT("原奶瓶保存网格未替换"), GetPathNameSafe(Milk->ContainerMesh->GetStaticMesh().Get()),
        FString(TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle04/SM_Bottle04.SM_Bottle04")));
    TestEqual(TEXT("菜单插入后教学恰好八步"), Director->Steps.Num(), 8);
    // 原步骤身份/类型保持；文本基线更新为已保存的 yanxia_dialogue/copy.json。
    const TCHAR* StepNames[] = {TEXT("TutorialStep_ShowMessage_0"), TEXT("TutorialStep_ShowMessage_1"),
        TEXT("TutorialStep_ShowMessage_2"), TEXT("TutorialStep_ShowMessage_3"),
        TEXT("TutorialStep_WaitForServe_0"), TEXT("TutorialStep_ConsumeServedDrink_0")};
    const TCHAR* Hints[] = {TEXT("你就是新来的店长？我叫时零。\n这儿能坐吗？柱梯口挤得连转身都难。"),
        TEXT("先来杯拿铁吧，咖啡和牛奶一半一半。\n一只手拿稳瓶身，另一只手抓住瓶盖，向上拔开。"),
        TEXT("空杯在你左手边，先用那只。\n这儿卡在上下城中间，往上赶的、往下赶的，都从门口过。"),
        TEXT("够不到就别硬探身子，瓶子又不会跑。\n【操作提示】左手 X / 右手 A：远程抓取。"),
        TEXT("原味拿铁：咖啡 100 mL＋牛奶 100 mL（本杯容量 200 mL）。\n瓶口对准杯口，慢慢倾倒；看杯旁的 mL 读数。做好后放到吧台前的交付位置，再松手。\n验收：总量 180–220 mL，两种原料各占 45%–55%，不加其他原料。不合适可拿回调整。"),
        TEXT("时零准备尝一口，稍等一下。")};
    const int32 OriginalStepIndices[] = {0, 1, 2, 3, 6, 7};
    for (int32 Index = 0; Index < 6 && Director->Steps.IsValidIndex(OriginalStepIndices[Index]); ++Index)
    {
        UTutorialStep* Step = Director->Steps[OriginalStepIndices[Index]].Get();
        if (!TestNotNull(TEXT("原六步实例均有效"), Step)) return false;
        const FString Type = Index < 4 ? TEXT("TutorialStep_ShowMessage") :
            (Index == 4 ? TEXT("TutorialStep_WaitForServe") : TEXT("TutorialStep_ConsumeServedDrink"));
        TestEqual(TEXT("原六步名称与顺序保留"), Step->GetName(), FString(StepNames[Index]));
        TestEqual(TEXT("原六步类型保留"), Step->GetClass()->GetPathName(), TEXT("/Script/CyberCafe.") + Type);
        TestTrue(TEXT("原步骤仍内联归属原导演"), Step->GetOuter() == Director);
        TestEqual(TEXT("原步骤提示文本保留"), Step->HintText.ToString(), FString(Hints[Index]));
        const FString Completed = Index < 4 ? TEXT("") : (Index == 4 ?
            TEXT("拿铁已收下。") : TEXT("嗯，奶没盖住咖啡味。\n我今天不赶柱梯。再坐一会儿，可以吧？"));
        TestEqual(TEXT("原步骤完成文本保留"), Step->CompletedText.ToString(), Completed);
        FRow Row; Row.Text(TEXT("stage"), TEXT("savedOriginalStep")); Row.Number(TEXT("index"), OriginalStepIndices[Index]);
        Row.Text(TEXT("path"), Step->GetPathName()); Row.Text(TEXT("class"), Step->GetClass()->GetPathName());
        Row.Text(TEXT("hint"), Step->HintText.ToString()); Row.Text(TEXT("completed"), Step->CompletedText.ToString());
        Rows.Add(Row.Json());
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
    }
    UTutorialStep_WaitForServe* Wait = Director->Steps.IsValidIndex(6)
        ? Cast<UTutorialStep_WaitForServe>(Director->Steps[6].Get()) : nullptr;
    if (!TestNotNull(TEXT("原 Step[6] 上菜步骤"), Wait)) return false;
    TestTrue(TEXT("原上菜步骤仍引用保存的全息"), Wait->OrderHologram.Get() == Hologram);
    TestEqual(TEXT("原配方引用保留"), GetPathNameSafe(Wait->RequiredRecipe.Get()),
        FString(TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
    TestEqual(TEXT("原上菜点引用保留"), GetPathNameSafe(Wait->RequiredSpot.Get()),
        FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_ServingSpot_C_1")));
    TestTrue(TEXT("全息与原容器都属于保存地图"), Hologram->GetLevel() == Map->PersistentLevel &&
        Cup->GetLevel() == Map->PersistentLevel && Milk->GetLevel() == Map->PersistentLevel);
    FRow Row; Row.Text(TEXT("stage"), TEXT("savedMapReferences")); Row.Text(TEXT("map"), Map->GetPathName());
    Row.Text(TEXT("manager"), Manager->GetPathName()); Row.Text(TEXT("managerClass"), Manager->GetClass()->GetPathName());
    Row.Text(TEXT("cup"), GetPathNameSafe(Manager->PrototypeCup.Get())); Row.Text(TEXT("milk"), GetPathNameSafe(Manager->PrototypeBottle.Get()));
    Row.Text(TEXT("cupProfile"), GetPathNameSafe(Manager->CupProfile.Get())); Row.Text(TEXT("milkProfile"), GetPathNameSafe(Manager->BottleProfile.Get()));
    Row.Text(TEXT("hologram"), Hologram->GetPathName()); Row.Number(TEXT("managerCount"), ManagerCount);
    Row.Number(TEXT("stepCount"), Director->Steps.Num()); Row.Number(TEXT("uniqueShardMeshes"), UniqueMeshes.Num());
    Rows.Add(Row.Json());
    return SaveSamples(*this, TEXT("SavedProfilesShardShapesAndMapRef"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassActualKinematicSweepAndHoldingReleaseTest,
    "CyberCafe.Glass.ActualKinematicSweepAndHoldingRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassActualKinematicSweepAndHoldingReleaseTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("实际世界需要 Engine"), GEngine)) return false;
    UClass* Class = LoadClass<ACupActor>(nullptr, CupClassPath);
    UGlassBreakProfile* Profile = LoadObject<UGlassBreakProfile>(nullptr, CupProfilePath);
    if (!TestNotNull(TEXT("加载原 BP_Cup01 生成类"), Class) || !TestNotNull(TEXT("加载真实杯 profile"), Profile)) return false;
    if (!TestTrue(TEXT("保存阈值区分 50 cm/s 轻放与 400 cm/s 快速运动"),
        FMath::IsFinite(Profile->BreakSpeedCms) && Profile->BreakSpeedCms > 50.f && Profile->BreakSpeedCms <= 400.f)) return false;
    FRuntimeWorld T(false);
    UStaticMeshComponent* Floor = T.Plane();
    ALiquidContainerActor* Gentle = T.Vessel(Class, Profile, FVector(-100, 0, 80));
    ALiquidContainerActor* Cup = T.Vessel(Class, Profile, FVector(100, 0, 80));
    AVRPawn* Pawn = T.GripPawn();
    if (!TestNotNull(TEXT("创建真实平台"), Floor) || !TestNotNull(TEXT("生成真实轻放杯"), Gentle) ||
        !TestNotNull(TEXT("生成真实抓取杯"), Cup) || !TestNotNull(TEXT("创建原生 GripPawn"), Pawn)) return false;
    T.BeginPlay();
    if (!CheckFixture(*this, T, Gentle, Profile, Floor) || !CheckFixture(*this, T, Cup, Profile, Floor)) return false;
    T.Tick(WarmFrames);
    TestFalse(TEXT("实际 .01 x 100 热身后杯未破碎"), Gentle->IsBroken() || Cup->IsBroken());
    TArray<FString> Rows;
    CaptureVessel(Rows, TEXT("warmUnheldKinematicCup"), T, Gentle, Profile);
    const FLiquidBalance BeforeGentle = T.Transfers()->GetBalance();
    const double GentleVolume = Gentle->GetPreciseVolumeML();
    int32 GentleCrossings = 0;
    // 无持有的真实 kinematic 位移：每 .02 秒下降 1 cm，逐 .01 Tick 实测 50 cm/s。
    for (int32 I = 0; I < 160 && !Gentle->IsBroken(); ++I)
    {
        const FTransform Previous = Gentle->ContainerMesh->GetComponentTransform();
        Gentle->SetActorLocation(Gentle->GetActorLocation() + FVector(0, 0, -.5), false);
        CaptureMotion(Rows, T, Gentle, Previous, TEXT("gentleUnheldKinematicMotion"), GentleCrossings);
        T.Tick();
    }
    TestTrue(TEXT("轻放轨迹实际跨过平台接触面"), GentleCrossings > 0);
    TestFalse(TEXT("50 cm/s 轻放接触不破碎"), Gentle->IsBroken());
    TestEqual(TEXT("轻放不洒出"), T.Transfers()->GetBalance().SpilledML, BeforeGentle.SpilledML);
    TestEqual(TEXT("轻放保留实测余量"), Gentle->GetPreciseVolumeML(), GentleVolume);
    CaptureVessel(Rows, TEXT("gentleFloorContact"), T, Gentle, Profile);

    Pawn->MotionControllerLeftGrip->SetWorldLocation(Gentle->GetActorLocation());
    Pawn->MotionControllerRightGrip->SetWorldLocation(Cup->GetActorLocation());
    Cup->GrabComp->bHideHandWhileHeld = true;
    if (!TestTrue(TEXT("真实左手持有另一个完整杯"), Pawn->TryGrabLeft(Gentle->GrabComp)) ||
        !TestTrue(TEXT("真实右手 TryGrab 成功，登记 Pawn 引用"), Pawn->TryGrabRight(Cup->GrabComp))) return false;
    TestTrue(TEXT("持有杯真实附着 Grip"), Cup->GetRootComponent()->GetAttachParent() == Pawn->MotionControllerRightGrip.Get());
    TestTrue(TEXT("右手引用真实持有组件"), Pawn->HeldComponentRight.Get() == Cup->GrabComp.Get());
    TestFalse(TEXT("实际抓取隐藏原生手部"), Pawn->HandRight->IsVisible());
    T.Tick(WarmFrames); // Held 状态切换再次越过首样本 .12 保护。
    Pawn->TargetGrabComponentRight = Cup->GrabComp; // 留下待破碎清理的悬停引用。
    const FLiquidBalance BeforeBreak = T.Transfers()->GetBalance();
    const double VolumeBefore = Cup->GetPreciseVolumeML();
    int32 FastCrossings = 0, MotionFrames = 0;
    CaptureVessel(Rows, TEXT("warmActualHeldCup"), T, Cup, Profile);
    // 真实手柄跟手：每 .02 秒下降 8 cm，逐 .01 Tick 实测 400 cm/s，不直接移动附着杯。
    for (int32 I = 0; I < 20 && !Cup->IsBroken(); ++I)
    {
        const FTransform Previous = Cup->ContainerMesh->GetComponentTransform();
        Pawn->MotionControllerRightGrip->AddWorldOffset(FVector(0, 0, -4), false);
        CaptureMotion(Rows, T, Cup, Previous, TEXT("fastActualHeldMotion"), FastCrossings);
        T.Tick();
        ++MotionFrames;
    }
    TestTrue(TEXT("快速跟手实际跨过平台接触面"), FastCrossings > 0);
    CheckBroken(*this, T, Cup, Profile, VolumeBefore, BeforeBreak);
    TestNull(TEXT("破碎清除 Pawn 右手持有引用"), Pawn->HeldComponentRight.Get());
    TestNull(TEXT("破碎清除右手悬停引用"), Pawn->TargetGrabComponentRight.Get());
    TestNull(TEXT("破碎清除主抓取引用"), Cup->GrabComp->PrimaryGrabComponent.Get());
    TestTrue(TEXT("破碎恢复右手可见"), Pawn->HandRight->IsVisible());
    TestTrue(TEXT("不清除另一只手持有的完整杯"), Pawn->HeldComponentLeft.Get() == Gentle->GrabComp.Get() && Gentle->GrabComp->IsHeld());
    TestFalse(TEXT("真实抓取 API 拒绝重新抓碎杯"), Pawn->TryGrabRight(Cup->GrabComp));
    CaptureVessel(Rows, TEXT("actualHeldSweepBroken"), T, Cup, Profile);
    const FLiquidBalance BrokenBalance = T.Transfers()->GetBalance();
    const int32 Revision = Cup->GetLiquidRevision();
    T.Tick(20);
    TestEqual(TEXT("后续实际 Tick 不重复生成"), DiscoverDebris(T.World, Profile).Batches, 1);
    TestEqual(TEXT("后续实际 Tick 不重复洒出"), T.Transfers()->GetBalance().SpilledML, BrokenBalance.SpilledML);
    TestEqual(TEXT("后续实际 Tick 不推进破碎 revision"), Cup->GetLiquidRevision(), Revision);
    FRow Row; Row.Text(TEXT("stage"), TEXT("actualMotionAndReferenceDiscovery"));
    Row.Number(TEXT("gentleFloorCrossings"), GentleCrossings); Row.Number(TEXT("fastFloorCrossings"), FastCrossings);
    Row.Number(TEXT("fastMotionFrames"), MotionFrames); Row.Bool(TEXT("rightHeldReferenceCleared"), Pawn->HeldComponentRight == nullptr);
    Row.Bool(TEXT("rightHoverReferenceCleared"), Pawn->TargetGrabComponentRight == nullptr);
    Row.Bool(TEXT("otherHandStillHeld"), Pawn->HeldComponentLeft.Get() == Gentle->GrabComp.Get() && Gentle->GrabComp->IsHeld());
    Rows.Add(Row.Json());
    return SaveSamples(*this, TEXT("ActualKinematicSweepAndHoldingRelease"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassActualPhysicsDropTest,
    "CyberCafe.Glass.ActualPhysicsDrop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGlassActualPhysicsDropTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实物理世界需要 Engine"), GEngine)) return false;
    UClass* Class = LoadClass<ABottleActor>(nullptr, MilkClassPath);
    UGlassBreakProfile* Profile = LoadObject<UGlassBreakProfile>(nullptr, MilkProfilePath);
    if (!TestNotNull(TEXT("加载原奶瓶 BP 生成类"), Class) || !TestNotNull(TEXT("加载保存奶瓶 profile"), Profile)) return false;
    FRuntimeWorld T(true);
    UStaticMeshComponent* Floor = T.Plane();
    ABottleActor* Milk = Cast<ABottleActor>(T.Vessel(Class, Profile, FVector(0, 0, 80)));
    if (!TestNotNull(TEXT("创建实际掉落平台"), Floor) || !TestNotNull(TEXT("生成真实奶瓶"), Milk)) return false;
    T.BeginPlay();
    if (!CheckFixture(*this, T, Milk, Profile, Floor)) return false;
    UGlassBreakableComponent* Comp = Milk->FindComponentByClass<UGlassBreakableComponent>();
    TestTrue(TEXT("真实 Chaos 世界开启模拟"), T.World->bShouldSimulatePhysics && T.World->GetPhysicsScene() != nullptr);
    TestTrue(TEXT("真实世界重力向下"), T.World->GetGravityZ() < 0);
    TestTrue(TEXT("BeginPlay 已绑定正常 OnComponentHit"), Milk->ContainerMesh->OnComponentHit.Contains(Comp, FName(TEXT("HandleHit"))));
    T.Tick(WarmFrames); // 先禁物理/重力热身，避免启动保护期间已经落地。
    TestFalse(TEXT("热身时仍完整，不抢跑落地"), Milk->IsBroken());
    TestTrue(TEXT("热身保持实际源位置 Z80"), FMath::IsNearlyEqual(Milk->GetActorLocation().Z, 80.0, .001));
    const TWeakObjectPtr<ABottleCapActor> OriginalCap(Milk->CapActor.Get());
    if (!TestNotNull(TEXT("保存奶瓶配置实际生成原瓶盖"), OriginalCap.Get())) return false;
    TestFalse(TEXT("掉落前原瓶盖未被持有"), OriginalCap->GrabComp->IsHeld());
    TArray<FString> Rows;
    CaptureVessel(Rows, TEXT("warmMilkBeforePhysics"), T, Milk, Profile);
    const FLiquidBalance Before = T.Transfers()->GetBalance();
    const double VolumeBefore = Milk->GetPreciseVolumeML();
    TestTrue(TEXT("真实奶瓶初始权威余量大于零"), VolumeBefore > 0);
    Milk->ContainerMesh->SetEnableGravity(true);
    // The fixture repeatedly toggles simulation during deferred spawn; recreate the
    // body after the isolation stage before starting the actual drop.
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    Milk->ContainerMesh->GetStaticMesh()->GetBodySetup()->CreatePhysicsMeshes();
    Milk->ContainerMesh->SetMobility(EComponentMobility::Movable);
    Milk->SetActorEnableCollision(true);
    Milk->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Milk->ContainerMesh->RecreatePhysicsState();
    TestTrue(TEXT("掉落组件注册且允许物理碰撞"), Milk->ContainerMesh->IsRegistered() && Milk->ContainerMesh->IsCollisionEnabled());
    TestTrue(TEXT("真实掉落需要有效物理刚体"), Milk->ContainerMesh->GetBodyInstance() && Milk->ContainerMesh->GetBodyInstance()->IsValidBodyInstance());
    Milk->ContainerMesh->SetSimulatePhysics(true);
    Milk->ContainerMesh->SetPhysicsLinearVelocity(FVector(0, 0, -350), false);
    Milk->ContainerMesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
    Milk->ContainerMesh->WakeAllRigidBodies();
    TestTrue(TEXT("源网格真正开启物理和重力"), Milk->ContainerMesh->IsSimulatingPhysics() && Milk->ContainerMesh->IsGravityEnabled());
    const FVector InitialVelocity = Milk->ContainerMesh->GetPhysicsLinearVelocity();
    TestTrue(TEXT("真实物理初速度 -350 cm/s"), FMath::IsNearlyEqual(InitialVelocity.Z, -350.0, .1));
    int32 FirstBrokenFrame = -1, Transitions = 0;
    double MinZ = Milk->GetActorLocation().Z;
    FVector LastPreBreakVelocity = InitialVelocity;
    bool bPreviousBroken = Milk->IsBroken();
    for (int32 I = 0; I < 200; ++I)
    {
        const FVector BeforeVelocity = Milk->ContainerMesh->GetPhysicsLinearVelocity();
        T.Tick(); // 不手工 Tick 破碎组件，不广播 Hit，不强制调用业务破碎。
        MinZ = FMath::Min(MinZ, Milk->GetActorLocation().Z);
        if (!bPreviousBroken && Milk->IsBroken())
        {
            ++Transitions;
            FirstBrokenFrame = I + 1;
            LastPreBreakVelocity = BeforeVelocity;
            CheckBroken(*this, T, Milk, Profile, VolumeBefore, Before);
            CaptureVessel(Rows, TEXT("actualPhysicsFirstBreak"), T, Milk, Profile);
        }
        bPreviousBroken = Milk->IsBroken();
        if (I < 30 || (I + 1) % 20 == 0) CaptureVessel(Rows, TEXT("actualPhysicsTick"), T, Milk, Profile);
    }
    TestTrue(TEXT("实际模拟移动了奶瓶，而非只推进组件 Age"), MinZ < 79.0);
    TestTrue(TEXT("200 个真实 .01 Tick 内检测到破碎"), FirstBrokenFrame > 0 && FirstBrokenFrame <= 200);
    TestEqual(TEXT("实际 broken 状态只转换一次"), Transitions, 1);
    TestEqual(TEXT("200 帧后仍只有八块玻璃"), DiscoverDebris(T.World, Profile).Shards, 8);
    TestEqual(TEXT("200 帧后仍只有一个 debris 批次"), DiscoverDebris(T.World, Profile).Batches, 1);
    TestNull(TEXT("实际奶瓶破碎后清空瓶盖引用"), Milk->CapActor.Get());
    TestFalse(TEXT("实际奶瓶破碎后解开封口标记"), Milk->bIsCapped);
    if (ABottleCapActor* Cap = OriginalCap.Get())
    {
        TestFalse(TEXT("残存原瓶盖不被持有"), Cap->GrabComp->IsHeld());
        TestNull(TEXT("残存原瓶盖不引用碎瓶"), Cap->OwnerBottle.Get());
    }
    const FLiquidBalance After = T.Transfers()->GetBalance();
    TestTrue(TEXT("200 帧只结算一次实测奶量，没有 unrelated pour"),
        FMath::Abs((After.SpilledML - Before.SpilledML) - VolumeBefore) < VolumeTolerance);
    TestTrue(TEXT("掉落后实际账本闭合"), FMath::IsFinite(After.ErrorML) && FMath::Abs(After.ErrorML) < VolumeTolerance);
    FRow Row; Row.Text(TEXT("stage"), TEXT("actualPhysicsAndCapDiscovery"));
    Row.Number(TEXT("dropWorldTicks"), 200); Row.Number(TEXT("firstBrokenDropFrame"), FirstBrokenFrame);
    Row.Number(TEXT("brokenTransitionsObserved"), Transitions); Row.Number(TEXT("minActorZCM"), MinZ);
    Row.Number(TEXT("worldGravityCms2"), T.World->GetGravityZ()); Row.Vector(TEXT("actualInitialVelocityCms"), InitialVelocity);
    Row.Vector(TEXT("lastPreBreakPhysicsVelocityCms"), LastPreBreakVelocity);
    Row.Bool(TEXT("originalCapStillValid"), OriginalCap.IsValid());
    Row.Bool(TEXT("originalCapHeldAfterDrop"), OriginalCap.IsValid() && OriginalCap->GrabComp->IsHeld());
    Row.Bool(TEXT("bottleCapReferenceCleared"), Milk->CapActor == nullptr);
    Rows.Add(Row.Json());
    CaptureVessel(Rows, TEXT("actualPhysicsFinalLedger"), T, Milk, Profile);
    return SaveSamples(*this, TEXT("ActualPhysicsDrop"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGlassDebrisCleanupTest,"CyberCafe.Glass.DebrisBudgetSleepAndCleanup",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGlassDebrisCleanupTest::RunTest(const FString&)
{
    FRuntimeWorld T(true);
    UGlassBreakProfile* Saved=LoadObject<UGlassBreakProfile>(nullptr,CupProfilePath);
    if(!TestNotNull(TEXT("真实杯碎片配置"),Saved))return false;
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    UGlassBreakProfile* Profile=NewObject<UGlassBreakProfile>(T.World);
    Profile->Shards=Saved->Shards;Profile->Shards.Append(Saved->Shards);
    Profile->MaxPhysicsShards=16;Profile->LifetimeSeconds=3.f;
    T.BeginPlay();
    AGlassDebrisActor* A=T.World->SpawnActor<AGlassDebrisActor>();
    A->Initialize(Profile,FTransform(FVector(0,0,20)),FVector(0,0,10),FVector(0,0,-300),FVector::UpVector);
    const auto Started=DiscoverDebris(T.World,Profile);
    TestEqual(TEXT("视觉片上限16"),Started.Shards,16);
    TestTrue(TEXT("错误配置16也最多8片物理"),Started.SimulatedShards<=8);
    TWeakObjectPtr<AGlassDebrisActor> Weak=A;
    for(int I=0;I<230;++I)T.Tick();
    TestEqual(TEXT("两秒后关闭所有玻璃物理"),DiscoverDebris(T.World,Profile).SimulatedShards,0);
    for(int I=0;I<100;++I)T.Tick();
    TestFalse(TEXT("配置寿命后批次已经销毁"),Weak.IsValid());
    TestEqual(TEXT("清理后无残留片"),DiscoverDebris(T.World,Profile).Shards,0);
    for(int I=0;I<7;++I){auto* Batch=T.World->SpawnActor<AGlassDebrisActor>();Batch->Initialize(Profile,FTransform(FVector(I*40,0,20)),FVector(I*40,0,0),FVector::ZeroVector,FVector::UpVector);}
    const auto Limited=DiscoverDebris(T.World,Profile);
    TestEqual(TEXT("场景最多六组碎片"),Limited.Batches,6);
    TestTrue(TEXT("场景最多48个模拟玻璃刚体"),Limited.SimulatedShards<=48);
    return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
