// @trace CF-02 CF-03 CF-04 RS-02 RS-03
// 待运行：只读保存地图、真实 BP 独立 Game World 与公共 API；不是头显跟踪或视觉验收。
// 不修改源资产、不保存地图、不启动 UE 构建；没有新 BP 时明确失败，绝不回退旧滤杯。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Coffee/Brew/HandDripperAssemblyActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/IceInteraction.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "GrabComponent.h"
#include "ActorEditorUtils.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformMisc.h"
#include "Internationalization/Regex.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "MotionControllerComponent.h"
#include "NiagaraComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <limits>

namespace CyberCafeDripperAssemblyTests
{
constexpr EAutomationTestFlags AssemblyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double Tolerance = 1.e-4;
const TCHAR* AssemblyPath = TEXT("/Game/VRTemplate/CoffeeBrew/Blueprints/BP_HandDripperWithStand.BP_HandDripperWithStand_C");
const TCHAR* GrinderPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Coffee/Grinder/BP_CoffeeGrinder.BP_CoffeeGrinder_C");
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
const FName Water(TEXT("Water")), Coffee(TEXT("Coffee"));

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}

bool PoseEqual(FAutomationTestBase& Test, const FString& Label, const FTransform& Actual, const FTransform& Expected)
{
    return Test.TestTrue(Label, !Actual.ContainsNaN() && Actual.Equals(Expected, Tolerance));
}

void Balance(FAutomationTestBase& Test, ULiquidTransferSubsystem* S)
{
    const FLiquidBalance B = S->GetBalance();
    Near(Test, TEXT("真实液体账本误差为零"), B.ErrorML, 0);
    for (double V : {B.InitialML, B.InjectedML, B.RemovedML, B.ContainerML, B.InFlightML, B.SpilledML})
        Test.TestTrue(TEXT("液体账本各项有限且非负"), FMath::IsFinite(V) && V >= 0);
    Test.TestTrue(TEXT("包计数非负"), B.PacketCount >= 0);
}

void SameBalance(FAutomationTestBase& Test, const FLiquidBalance& A, const FLiquidBalance& B)
{
    Near(Test, TEXT("倒粉不改变初始毫升"), B.InitialML, A.InitialML);
    Near(Test, TEXT("倒粉不注入毫升"), B.InjectedML, A.InjectedML);
    Near(Test, TEXT("倒粉不移除毫升"), B.RemovedML, A.RemovedML);
    Near(Test, TEXT("倒粉不改变容器毫升"), B.ContainerML, A.ContainerML);
    Near(Test, TEXT("倒粉不制造在途液体"), B.InFlightML, A.InFlightML);
    Near(Test, TEXT("倒粉不修改液体撒漏账"), B.SpilledML, A.SpilledML);
    Test.TestEqual(TEXT("倒粉不制造液包"), B.PacketCount, A.PacketCount);
}

TArray<UStaticMeshComponent*> StandPieces(AHandDripperAssemblyActor* D)
{
    return {D->StandLeftLeg.Get(), D->StandRightLeg.Get(), D->StandLeftShelf.Get(), D->StandRightShelf.Get()};
}

bool CheckAssembly(FAutomationTestBase& Test, AHandDripperAssemblyActor* D, bool Begun)
{
    if (!Test.TestNotNull(TEXT("组合滤杯"), D) || !Test.TestNotNull(TEXT("原根网格"), D->ContainerMesh.Get())
        || !Test.TestNotNull(TEXT("唯一根 GrabComp"), D->GrabComp.Get())) return false;
    TArray<UGrabComponent*> Grabs; D->GetComponents(Grabs);
    Test.TestEqual(TEXT("整套滤杯支架只有一个 GrabComp"), Grabs.Num(), 1);
    Test.TestTrue(TEXT("GrabComp 属于原 ContainerMesh 根"), D->GetRootComponent() == D->ContainerMesh.Get()
        && D->GrabComp->GetOwner() == D && D->GrabComp->GetAttachParent() == D->GetRootComponent());
    Test.TestTrue(TEXT("Free/Kinematic 自动激活且松手不模拟"), D->GrabComp->GrabType == EGrabType::Free
        && D->GrabComp->PhysicsMode == EGrabPhysicsMode::Kinematic && D->GrabComp->bAutoActivate
        && !D->GrabComp->bSimulateOnDrop);
    if (Begun) Test.TestTrue(TEXT("实际 BeginPlay 后根抓取已激活"), D->GrabComp->IsActive() && D->HasActorBegunPlay());
    const TCHAR* Names[] = {TEXT("StandLeftLeg"), TEXT("StandRightLeg"), TEXT("StandLeftShelf"), TEXT("StandRightShelf")};
    const TArray<UStaticMeshComponent*> Pieces = StandPieces(D);
    for (int32 I = 0; I < Pieces.Num(); ++I)
    {
        auto* P = Pieces[I]; if (!Test.TestNotNull(TEXT("四件支架齐全"), P)) return false;
        Test.TestEqual(TEXT("支架组件使用实际声明名称"), P->GetName(), FString(Names[I]));
        Test.TestTrue(TEXT("四件支架都属于同 Actor 且直接挂原根"), P->GetOwner() == D && P->GetAttachParent() == D->ContainerMesh.Get());
        Test.TestEqual(TEXT("保留原 Cube 网格"), GetPathNameSafe(P->GetStaticMesh().Get()), FString(TEXT("/Engine/BasicShapes/Cube.Cube")));
        Test.TestEqual(TEXT("支架材质忠于原 WorldGrid"), GetPathNameSafe(P->GetMaterial(0)),
            FString(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")));
        Test.TestTrue(TEXT("支架 PhysicsBody 对象与可移动属性"), P->GetCollisionObjectType() == ECC_PhysicsBody
            && P->Mobility == EComponentMobility::Movable);
        if (I == 2) Test.TestTrue(TEXT("左托板继续 NoCollision，不封杯通道"), P->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        else Test.TestTrue(TEXT("其余支架保留查询阻挡"), P->IsQueryCollisionEnabled()
            && P->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
        const double X = (I == 0 || I == 2) ? -5.2 : 5.2;
        PoseEqual(Test, TEXT("支架补偿原根高度差且保持缩放"), P->GetRelativeTransform(),
            FTransform(FQuat::Identity, FVector(X, 0, I < 2 ? -7.8 : -.5),
                I < 2 ? FVector(.01, .07, .144) : FVector(.015, .1, .01)));
    }
    TArray<UStaticMeshComponent*> Meshes; D->GetComponents(Meshes);
    Test.TestEqual(TEXT("原五件滤杯网格加四件支架，不新增封闭通道代理盒"), Meshes.Num(), 9);
    TArray<UPrimitiveComponent*> Primitives; D->GetComponents(Primitives);
    for (auto* P : Primitives) Test.TestFalse(TEXT("没有任何分件启动刚体模拟"), P->IsSimulatingPhysics());
    return !Test.HasAnyErrors();
}

// 外层严格用 JSON；只解析探针 world_transform 中有明确字段的数值，不扫描整份 JSON 猜姿态。
bool ProbeTransform(FAutomationTestBase& Test, const FString& Text, FTransform& Out)
{
    const FRegexPattern Pattern(TEXT("rotation:\\s*\\{x:\\s*([^,]+),\\s*y:\\s*([^,]+),\\s*z:\\s*([^,]+),\\s*w:\\s*([^}]+)\\},\\s*translation:\\s*\\{x:\\s*([^,]+),\\s*y:\\s*([^,]+),\\s*z:\\s*([^}]+)\\},\\s*scale3d:\\s*\\{x:\\s*([^,]+),\\s*y:\\s*([^,]+),\\s*z:\\s*([^}]+)\\}"));
    FRegexMatcher Match(Pattern, Text);
    if (!Test.TestTrue(TEXT("探针 Transform 含完整 quat/translation/scale 数值"), Match.FindNext())) return false;
    double V[10];
    for (int32 I = 0; I < 10; ++I)
        if (!Test.TestTrue(TEXT("探针 Transform 数值有限"), FDefaultValueHelper::ParseDouble(Match.GetCaptureGroup(I + 1).TrimStartAndEnd(), V[I])
            && FMath::IsFinite(V[I]))) return false;
    const FQuat Q(V[0], V[1], V[2], V[3]);
    if (!Test.TestTrue(TEXT("探针四元数归一且缩放非零"), Q.IsNormalized() && V[7] > 0 && V[8] > 0 && V[9] > 0)) return false;
    Out = FTransform(Q, FVector(V[4], V[5], V[6]), FVector(V[7], V[8], V[9]));
    return true;
}

bool JsonPose(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Object, FTransform& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Test.TestTrue(TEXT("每个 before actor pose 恰好九个数"), Object->TryGetArrayField(TEXT("pose"), Values)
        && Values && Values->Num() == 9)) return false;
    double V[9];
    for (int32 I = 0; I < 9; ++I)
        if (!Test.TestTrue(TEXT("before pose 数值有限"), (*Values)[I]->TryGetNumber(V[I]) && FMath::IsFinite(V[I]))) return false;
    Out = FTransform(FRotator(V[3], V[4], V[5]), FVector(V[0], V[1], V[2]), FVector(V[6], V[7], V[8]));
    return true;
}

struct FAssets
{
    UClass* Assembly = LoadClass<AHandDripperAssemblyActor>(nullptr, AssemblyPath);
    UClass* Grinder = LoadClass<ACoffeeGrinderActor>(nullptr, GrinderPath);
    UClass* Drawer = nullptr;
    bool Ready(FAutomationTestBase& Test)
    {
        if (!Test.TestNotNull(TEXT("必须已保存新的组合 BP，未部署则失败"), Assembly)
            || !Test.TestNotNull(TEXT("实际原研磨器 BP"), Grinder)) return false;
        Test.TestTrue(TEXT("新 BP 直接继承组合原生类"), Assembly->GetSuperClass() == AHandDripperAssemblyActor::StaticClass()
            && Assembly->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
        Drawer = Grinder->GetDefaultObject<ACoffeeGrinderActor>()->DrawerClass.Get();
        return Test.TestNotNull(TEXT("从实际研磨器读取保存 DrawerClass"), Drawer)
            && Test.TestTrue(TEXT("实际抽屉继承原类"), Drawer->IsChildOf(AGrinderDrawerActor::StaticClass()));
    }
};

// 仅隔离声音、粒子、HUD 与非组合件模拟；不改组合抓取设置、业务参数、口点和查询碰撞。
void Quiet(AActor* A)
{
    A->SetActorTickEnabled(false);
    TArray<UNiagaraComponent*> FXs; A->GetComponents(FXs);
    for (auto* FX : FXs) { FX->SetAutoActivate(false); FX->SetAsset(nullptr); FX->DeactivateImmediate(); }
    TArray<UAudioComponent*> Sounds; A->GetComponents(Sounds);
    for (auto* Sound : Sounds) { Sound->Stop(); Sound->SetAutoActivate(false); Sound->SetSound(nullptr); }
    TArray<UGrabComponent*> Grabs; A->GetComponents(Grabs);
    for (auto* Grab : Grabs) { Grab->OnGrabHapticEffect = nullptr; Grab->bCaptureHand = false; Grab->bHideHandWhileHeld = false; }
    if (!Cast<AHandDripperAssemblyActor>(A))
    {
        TArray<UPrimitiveComponent*> Primitives; A->GetComponents(Primitives);
        for (auto* P : Primitives) P->SetSimulatePhysics(false);
    }
    if (auto* Liquid = Cast<ALiquidContainerActor>(A))
    {
        Liquid->LiquidFXTemplate = nullptr; Liquid->SplashEffectTemplate = nullptr;
        Liquid->bEnableSplash = false; Liquid->bEnableDecal = false;
    }
    if (auto* D = Cast<AHandDripperActor>(A)) D->DripSound = nullptr;
    if (auto* Drawer = Cast<AGrinderDrawerActor>(A))
    {
        Drawer->GroundPourSound = nullptr; Drawer->DetachSound = nullptr; Drawer->ReattachSound = nullptr; Drawer->DetachHaptic = nullptr;
    }
    if (auto* Cup = Cast<ACupActor>(A))
    {
        Cup->VolumeWidgetClass = nullptr; Cup->bFaceCamera = false;
        Cup->VolumeWidgetComp->SetWidgetClass(nullptr); Cup->VolumeWidgetComp->SetHiddenInGame(true);
    }
}

struct FWorldFixture
{
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    TArray<AActor*> Actors;
    explicit FWorldFixture(FAutomationTestBase& Test)
    {
        if (!Test.TestNotNull(TEXT("独立世界 Engine"), GEngine)) return;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("独立 Game World"), World)) return;
        FWorldContext& C = GEngine->CreateNewWorldContext(EWorldType::Game); C.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine); C.OwningGameInstance = GI; World->SetGameInstance(GI); GI->Init();
    }
    ~FWorldFixture()
    {
        if (!World) return;
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    template<typename T>
    T* Spawn(UClass* Class, const FTransform& Pose)
    {
        if (!World || !Class) return nullptr;
        auto* A = World->SpawnActorDeferred<T>(Class, Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!A) return nullptr;
        Quiet(A); A->FinishSpawning(Pose); Quiet(A); Actors.Add(A); return A;
    }
    void Begin()
    {
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
        for (auto* A : Actors) Quiet(A);
    }
    ULiquidTransferSubsystem* Transfer() const { return World ? World->GetSubsystem<ULiquidTransferSubsystem>() : nullptr; }
    UMotionControllerComponent* Hand(const FVector& Position, EControllerHand Side)
    {
        auto* Owner = World->SpawnActor<AActor>(); if (!Owner) return nullptr;
        auto* H = NewObject<UMotionControllerComponent>(Owner);
        Owner->SetRootComponent(H); Owner->AddInstanceComponent(H);
        H->SetTrackingMotionSource(Side == EControllerHand::Left ? FName(TEXT("Left")) : FName(TEXT("Right")));
        H->RegisterComponent(); H->SetComponentTickEnabled(false); H->SetWorldLocation(Position); return H;
    }
    AStaticMeshActor* Blocker(const FVector& Position, const FVector& Scale)
    {
        auto* A = World->SpawnActor<AStaticMeshActor>(); if (!A) return nullptr;
        auto* P = A->GetStaticMeshComponent(); P->SetMobility(EComponentMobility::Movable);
        P->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        P->SetCollisionProfileName(TEXT("BlockAllDynamic")); P->SetSimulatePhysics(false);
        A->SetActorTransform(FTransform(FQuat::Identity, Position, Scale)); P->RecreatePhysicsState(); return A;
    }
};

bool QueryBody(FAutomationTestBase& Test, UStaticMeshComponent* P)
{
    if (!Test.TestNotNull(TEXT("真实查询网格"), P) || !Test.TestNotNull(TEXT("真实网格资源"), P->GetStaticMesh().Get())) return false;
    auto* Body = P->GetStaticMesh()->GetBodySetup(); if (!Test.TestNotNull(TEXT("已有 BodySetup"), Body)) return false;
    Body->CreatePhysicsMeshes(); P->RecreatePhysicsState();
    return Test.TestTrue(TEXT("不替换碰撞，实际组件已注册且支持查询"), P->IsRegistered() && P->IsQueryCollisionEnabled());
}

// 保持真实 GroundPourPoint；按当前旋转和实际根缩放解出 Actor 世界位置。
void PlaceLip(AGrinderDrawerActor* Drawer, const FVector& LipWorld, const FQuat& Rotation)
{
    const FVector Local = Drawer->GetActorTransform().InverseTransformPosition(Drawer->GroundPourPoint->GetComponentLocation());
    Drawer->SetActorLocationAndRotation(LipWorld - Rotation.RotateVector(Local * Drawer->GetActorScale3D()), Rotation,
        false, nullptr, ETeleportType::TeleportPhysics);
}

bool SourceOnVisibleLip(FAutomationTestBase& Test, AGrinderDrawerActor* Drawer)
{
    const FTransform M = Drawer->DrawerMesh->GetComponentTransform();
    const FVector LipLocal = M.InverseTransformPosition(Drawer->GroundPourPoint->GetComponentLocation());
    const FVector SelectedLocal = M.InverseTransformPosition(Drawer->LastGroundPourOrigin);
    return Test.TestTrue(TEXT("实际选中的源点在可见口沿局部 X 范围内，不从中心瞬移造粉"),
        FMath::Abs(SelectedLocal.X - LipLocal.X) <= Drawer->GroundLipHalfWidthCM + Tolerance
        && FMath::Abs(SelectedLocal.Y - LipLocal.Y) <= Tolerance && FMath::Abs(SelectedLocal.Z - LipLocal.Z) <= Tolerance);
}

void CheckOpeningGeometry(FAutomationTestBase& Test, AHandDripperActor* D, double ActualRadius)
{
    if (!Test.TestNotNull(TEXT("实际滤纸源网格用于保守开口"), D->PaperMesh->GetStaticMesh().Get())) return;
    const FTransform Entry = D->PourEntryPoint->GetComponentTransform();
    const FTransform Paper = D->PaperMesh->GetComponentTransform();
    const auto Bounds = D->PaperMesh->GetStaticMesh()->GetBounds();
    const FVector N = D->PourEntryPoint->GetUpVector();
    const FVector Offset = Paper.TransformPosition(Bounds.Origin) - Entry.GetLocation();
    const double Alignment = FVector::DotProduct(D->PaperMesh->GetUpVector(), N);
    const double PaperRadius = FMath::Min(Bounds.BoxExtent.X * FMath::Abs(Paper.GetScale3D().X),
        Bounds.BoxExtent.Y * FMath::Abs(Paper.GetScale3D().Y)) * Alignment - (Offset - N * FVector::DotProduct(Offset, N)).Size();
    const double EntryRadius = D->PourEntryRadius * FMath::Min(FMath::Abs(Entry.GetScale3D().X), FMath::Abs(Entry.GetScale3D().Y));
    Near(Test, TEXT("辅助开口取原圆盘和实际源滤纸保守内切圆的较小者"), ActualRadius, FMath::Min(EntryRadius, PaperRadius));
}

void CanonicalInside(FAutomationTestBase& Test, AGrinderDrawerActor* Drawer, AHandDripperActor* D)
{
    const FVector P = D->PourEntryPoint->GetComponentTransform().InverseTransformPosition(Drawer->LastGroundPourEnd);
    Test.TestTrue(TEXT("辅助结束点仍在原入口局部圆盘内"), !P.ContainsNaN() && FMath::Abs(P.Z) <= Tolerance
        && P.SizeSquared2D() <= FMath::Square(static_cast<double>(D->PourEntryRadius)) + Tolerance);
}

void PourMass(FAutomationTestBase& Test, FWorldFixture& W, AGrinderDrawerActor* Drawer, AHandDripperActor* D,
    float Expected, float Dt = .01f)
{
    const double SourceBefore = Drawer->GetGroundsGrams(), TargetBefore = D->GetGroundsGrams(), SpillBefore = Drawer->SpilledGroundGrams;
    const FLiquidBalance Before = W.Transfer()->GetBalance();
    const float Result = Drawer->PourGrounds(Dt);
    Near(Test, TEXT("实际 PourGrounds 返回预期转移克数"), Result, Expected);
    Near(Test, TEXT("返回量等于源真实扣量"), SourceBefore - Drawer->GetGroundsGrams(), Result);
    Near(Test, TEXT("源扣量等于目标实增与撒粉"), Result, D->GetGroundsGrams() - TargetBefore + Drawer->SpilledGroundGrams - SpillBefore);
    Near(Test, TEXT("公开接纳量等于目标实增"), Drawer->LastAcceptedGroundGrams, D->GetGroundsGrams() - TargetBefore);
    Near(Test, TEXT("公开本次出粉量等于真实返回"), Drawer->LastPouredGroundGrams, Result);
    Near(Test, TEXT("整数辅助转移没有撒粉"), Drawer->SpilledGroundGrams, SpillBefore);
    Test.TestEqual(TEXT("瞬时转移不生成粉流实例"), Drawer->PowderStream->GetInstanceCount(), 0);
    Test.TestFalse(TEXT("瞬时转移粉流不可见"), Drawer->PowderStream->IsVisible());
    SameBalance(Test, Before, W.Transfer()->GetBalance()); Balance(Test, W.Transfer());
    if (Expected > 0) { SourceOnVisibleLip(Test, Drawer); CanonicalInside(Test, Drawer, D); }
    Test.AddInfo(FString::Printf(TEXT("实际采样 source=%.9g target=%.9g spill=%.9g reason=%s origin=%s end=%s"),
        Drawer->GetGroundsGrams(), D->GetGroundsGrams(), Drawer->SpilledGroundGrams, *Drawer->LastGroundTransferReason.ToString(),
        *Drawer->LastGroundPourOrigin.ToString(), *Drawer->LastGroundPourEnd.ToString()));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblySavedTest,
    "CyberCafe.DripperAssembly.SavedAssemblyWorldReferencesAndGeometry", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblySavedTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FString RunDir = FPlatformMisc::GetEnvironmentVariable(TEXT("ENV_DRIPPER_RUN_DIR"));
    if (RunDir.IsEmpty()) RunDir = TEXT("C:/Users/cloudcheng/WorkBuddy/2026-11-01-16-35-18/dripper_grab_polish");
    FString Text; TSharedPtr<FJsonObject> Before;
    if (!TestTrue(TEXT("读取本轮 current_before.json，缺失即失败"), FFileHelper::LoadFileToString(Text, *(RunDir / TEXT("current_before.json"))))
        || !TestTrue(TEXT("完整 JSON 解析，不用正则扫描 actor"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Before) && Before.IsValid())) return false;
    const TSharedPtr<FJsonObject>* BeforeActors = nullptr;
    if (!TestTrue(TEXT("基线 actors 存在"), Before->TryGetObjectField(TEXT("actors"), BeforeActors) && BeforeActors && BeforeActors->IsValid())) return false;
    TestEqual(TEXT("基线严格 313 scene actors"), (*BeforeActors)->Values.Num(), 313);
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    auto* Map = LoadObject<UWorld>(nullptr, MapPath);
    if (!TestNotNull(TEXT("只读保存 L_Cafe"), Map) || !TestNotNull(TEXT("保存 PersistentLevel"), Map->PersistentLevel.Get())) return false;
    if (!TestFalse(TEXT("保存地图不运行 BeginPlay"), Map->HasBegunPlay())) return false;
    const double TimeBefore = Map->GetTimeSeconds();
    if (!Map->bIsWorldInitialized)
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        Map->InitWorld(IV);
    }
    Map->UpdateWorldComponents(true, false); // 只注册，不 BeginPlay、不 Tick、不保存。
    TMap<FString, AActor*> Scene; int32 Assemblies = 0, Managers = 0;
    AGlassRecoveryActor* Manager = nullptr; TSet<AActor*> GrabRoots;
    for (const auto& Entry : Map->PersistentLevel->Actors)
    {
        AActor* A = Entry.Get();
        if (!IsValid(A) || !A->IsEditable() || !A->IsListedInSceneOutliner() || A->IsTemplate()
            || A->HasAnyFlags(RF_Transient) || Cast<AWorldSettings>(A) || FActorEditorUtils::IsABuilderBrush(A)) continue;
        Scene.Add(A->GetName(), A);
        if (Cast<AHandDripperAssemblyActor>(A)) ++Assemblies;
        if (auto* M = Cast<AGlassRecoveryActor>(A)) { Manager = M; ++Managers; }
        if (A->FindComponentByClass<UGrabComponent>()) GrabRoots.Add(A);
    }
    TestEqual(TEXT("旧两个 actor 合为一个，313 - 2 + 1 = 312"), Scene.Num(), 312);
    TestEqual(TEXT("保存组合滤杯唯一"), Assemblies, 1); TestEqual(TEXT("恢复经理唯一"), Managers, 1);
    TestFalse(TEXT("旧独立 Stand actor 已移除"), Scene.Contains(TEXT("HandDripperStand_Practice")));
    AActor** Saved = Scene.Find(TEXT("HandDripper_Practice"));
    auto* D = Saved ? Cast<AHandDripperAssemblyActor>(*Saved) : nullptr;
    if (!TestNotNull(TEXT("保留原名的新组合实例"), D) || !TestNotNull(TEXT("实际恢复经理"), Manager)) return false;
    TestTrue(TEXT("场景实例精确引用新生成类"), D->GetClass() == Assets.Assembly);
    CheckAssembly(*this, D, false);
    int32 OtherPreserved = 0;
    for (const auto& Pair : (*BeforeActors)->Values)
    {
        if (Pair.Key == TEXT("HandDripperStand_Practice")) continue;
        AActor** Found = Scene.Find(Pair.Key);
        if (!TestTrue(TEXT("每个保留 actor 名称仍在场景：") + Pair.Key, Found != nullptr)) continue;
        auto Object = Pair.Value->AsObject(); if (!TestTrue(TEXT("基线 actor 是 JSON 对象"), Object.IsValid())) return false;
        FTransform OldPose; if (!JsonPose(*this, Object, OldPose)) return false;
        PoseEqual(*this, TEXT("完整 actor pose 保持：") + Pair.Key, (*Found)->GetActorTransform(), OldPose);
        if (Pair.Key == TEXT("HandDripper_Practice"))
            TestEqual(TEXT("只替换原滤杯 class"), (*Found)->GetClass()->GetPathName(), FString(AssemblyPath));
        else { ++OtherPreserved; TestEqual(TEXT("其余 actor class 不变：") + Pair.Key, (*Found)->GetClass()->GetPathName(), Object->GetStringField(TEXT("class"))); }
        TestEqual(TEXT("actor label 保持：") + Pair.Key, (*Found)->GetActorLabel(), Object->GetStringField(TEXT("label")));
    }
    TestEqual(TEXT("其余 311 actor 原名/class/完整pose保持"), OtherPreserved, 311);
    for (const auto& Pair : Scene) TestTrue(TEXT("没有额外未知 scene actor：") + Pair.Key, (*BeforeActors)->Values.Contains(Pair.Key));
    const auto Filter = Before->GetObjectField(TEXT("filter"));
    FTransform OldRoot;
    if (!ProbeTransform(*this, Filter->GetObjectField(TEXT("root"))->GetStringField(TEXT("world_transform")), OldRoot)) return false;
    PoseEqual(*this, TEXT("ContainerMesh 根完整世界姿态精确等于原滤杯"), D->ContainerMesh->GetComponentTransform(), OldRoot);
    PoseEqual(*this, TEXT("已批准滤杯根(-33,131,67)无旋转且scale1"), OldRoot,
        FTransform(FQuat::Identity, FVector(-33, 131, 67), FVector::OneVector));
    TestEqual(TEXT("仍引用原滤杯源网格"), GetPathNameSafe(D->ContainerMesh->GetStaticMesh().Get()), Filter->GetObjectField(TEXT("root"))->GetStringField(TEXT("mesh")));
    TestEqual(TEXT("仍引用原滤纸源网格"), GetPathNameSafe(D->PaperMesh->GetStaticMesh().Get()), Filter->GetObjectField(TEXT("paper"))->GetStringField(TEXT("mesh")));
    Near(*this, TEXT("原通用入口半径仍为4cm，不扩大接液"), D->PourEntryRadius, Filter->GetNumberField(TEXT("entry_radius")));
    PoseEqual(*this, TEXT("原入口局部位置/旋转/缩放保持"), D->PourEntryPoint->GetRelativeTransform(), FTransform(FVector(0, 0, 6.5)));
    FTransform OldPaperWorld;
    if (!ProbeTransform(*this, Filter->GetObjectField(TEXT("paper"))->GetStringField(TEXT("world_transform")), OldPaperWorld)) return false;
    PoseEqual(*this, TEXT("滤纸完整世界几何不移动"), D->PaperMesh->GetComponentTransform(), OldPaperWorld);
    if (!TestNotNull(TEXT("保存原滤杯源网格非空"), D->ContainerMesh->GetStaticMesh().Get())
        || !TestNotNull(TEXT("保存原滤纸源网格非空"), D->PaperMesh->GetStaticMesh().Get())) return false;
    TestTrue(TEXT("原滤杯源包围盒未被放大替代"), D->ContainerMesh->GetStaticMesh()->GetBounds().Origin.Equals(FVector(0, 0, 2.95), Tolerance)
        && D->ContainerMesh->GetStaticMesh()->GetBounds().BoxExtent.Equals(FVector(5.4, 5.4, 3.55), Tolerance));
    TestTrue(TEXT("原滤纸源包围盒仍为原几何"), D->PaperMesh->GetStaticMesh()->GetBounds().Origin.Equals(FVector(0, 0, 3.95), Tolerance)
        && D->PaperMesh->GetStaticMesh()->GetBounds().BoxExtent.Equals(FVector(4.7, 4.7, 3.15), Tolerance));
    TestEqual(TEXT("原滤杯材质引用不变"), GetPathNameSafe(D->ContainerMesh->GetMaterial(0)),
        Filter->GetObjectField(TEXT("root"))->GetArrayField(TEXT("materials"))[0]->AsString());
    TestEqual(TEXT("原滤纸材质引用不变"), GetPathNameSafe(D->PaperMesh->GetMaterial(0)),
        Filter->GetObjectField(TEXT("paper"))->GetArrayField(TEXT("materials"))[0]->AsString());
    const auto& Stands = Before->GetArrayField(TEXT("stands"));
    if (!TestEqual(TEXT("原快照独立支架只有一个"), Stands.Num(), 1)) return false;
    const auto OldStand = Stands[0]->AsObject(); FTransform OldStandRoot;
    if (!JsonPose(*this, OldStand, OldStandRoot)) return false;
    PoseEqual(*this, TEXT("原支架根(-33,131,52)"), OldStandRoot, FTransform(FVector(-33, 131, 52)));
    const auto& Meshes = OldStand->GetArrayField(TEXT("meshes"));
    if (!TestEqual(TEXT("原支架记录四件网格"), Meshes.Num(), 4)) return false;
    const auto Pieces = StandPieces(D);
    for (int32 I = 0; I < 4; ++I)
    {
        FTransform OldWorld;
        if (!ProbeTransform(*this, Meshes[I]->AsObject()->GetStringField(TEXT("world_transform")), OldWorld)) return false;
        PoseEqual(*this, TEXT("新支架各件世界位置/旋转/缩放等于序列化旧支架"), Pieces[I]->GetComponentTransform(), OldWorld);
    }
    const auto Recovery = Before->GetObjectField(TEXT("recovery"));
    TestEqual(TEXT("恢复经理名保持"), Manager->GetName(), Recovery->GetStringField(TEXT("name")));
    TestTrue(TEXT("全用品模式保留"), Manager->bRestoreAllSupplies);
    TestEqual(TEXT("替换槽仍为16"), Manager->RestoreTargets.Num(), 16);
    TestEqual(TEXT("工具仍为3，组合体不额外增加 Stand 槽"), Manager->RestoreToolTargets.Num(), 3);
    TSet<AActor*> Manifest; int32 AssemblyTools = 0;
    const auto& TargetNames = Recovery->GetArrayField(TEXT("targets"));
    const auto& ToolNames = Recovery->GetArrayField(TEXT("tools"));
    TestEqual(TEXT("before 替换清单长度匹配"), TargetNames.Num(), Manager->RestoreTargets.Num());
    TestEqual(TEXT("before 工具清单长度匹配"), ToolNames.Num(), Manager->RestoreToolTargets.Num());
    for (int32 I = 0; I < Manager->RestoreTargets.Num(); ++I)
    {
        auto* A = Manager->RestoreTargets[I].Get(); if (!TestNotNull(TEXT("替换槽无悬空引用"), A)) continue;
        Manifest.Add(A); TestTrue(TEXT("替换槽仍属于原地图"), A->GetLevel() == Map->PersistentLevel.Get());
        if (TargetNames.IsValidIndex(I)) TestEqual(TEXT("替换槽原身份顺序保持"), A->GetName(), TargetNames[I]->AsString());
        TestTrue(TEXT("替换槽不包含组合工具或旧支架"), A != D && A->GetName() != TEXT("HandDripperStand_Practice"));
    }
    for (int32 I = 0; I < Manager->RestoreToolTargets.Num(); ++I)
    {
        auto* A = Manager->RestoreToolTargets[I].Get(); if (!TestNotNull(TEXT("工具槽无悬空引用"), A)) continue;
        Manifest.Add(A); TestTrue(TEXT("工具仍属于原地图"), A->GetLevel() == Map->PersistentLevel.Get());
        if (ToolNames.IsValidIndex(I)) TestEqual(TEXT("工具名及顺序保持"), A->GetName(), ToolNames[I]->AsString());
        if (A == D) { ++AssemblyTools; TestTrue(TEXT("恢复工具引用就是实际新组合 class"), A->GetClass() == Assets.Assembly); }
        TestTrue(TEXT("工具不残留旧 Stand 引用"), A->GetName() != TEXT("HandDripperStand_Practice"));
    }
    if (TestNotNull(TEXT("独立冰铲引用保持"), Manager->RestoreScoop.Get())) Manifest.Add(Manager->RestoreScoop.Get());
    TestEqual(TEXT("组合体恰好一个工具槽"), AssemblyTools, 1);
    TestEqual(TEXT("16替换+3工具+1冰铲共20供给根"), Manifest.Num(), 20);
    TestEqual(TEXT("可交互抓取根仍为20"), GrabRoots.Num(), 20);
    for (auto* A : GrabRoots) TestTrue(TEXT("所有抓取根均在恢复清单中"), Manifest.Contains(A));
    TestFalse(TEXT("只读核查未启动保存地图"), Map->HasBegunPlay()); Near(*this, TEXT("未推进保存地图时间"), Map->GetTimeSeconds(), TimeBefore);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyGrabTest,
    "CyberCafe.DripperAssembly.FreeGrabByBodyAndStandPreservesOffset", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyGrabTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    for (bool ByLeg : {false, true})
    {
        FWorldFixture W(*this); if (!W.World) return false;
        auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(100, 0, 100)));
        if (!TestNotNull(TEXT("真实保存组合 BP 实例"), D)) return false;
        CheckAssembly(*this, D, false); W.Begin(); if (!CheckAssembly(*this, D, true)) return false;
        auto* Piece = ByLeg ? D->StandLeftLeg.Get() : D->ContainerMesh.Get();
        if (!QueryBody(*this, Piece)) return false;
        const FVector Point = ByLeg ? Piece->GetComponentLocation() : Piece->Bounds.Origin;
        FHitResult Hit;
        const bool Found = W.World->SweepSingleByObjectType(Hit, Point + FVector(0, -12, 0), Point,
            FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody), FCollisionShape::MakeSphere(.3f));
        if (!TestTrue(TEXT("PhysicsBody 小球查询命中真实滤杯/左支架"), Found && Hit.GetActor() == D && Hit.GetComponent() == Piece)) return false;
        TestTrue(TEXT("命中 Stand 与 Body 返回同一根 GrabComp"), Hit.GetActor()->FindComponentByClass<UGrabComponent>() == D->GrabComp.Get());
        auto* H = W.Hand(Point, ByLeg ? EControllerHand::Left : EControllerHand::Right);
        if (!TestNotNull(TEXT("输入手柄夹具，不假称设备跟踪"), H)) return false;
        const FTransform Before = D->GetActorTransform(); const FTransform Offset = Before.GetRelativeTransform(H->GetComponentTransform());
        TArray<FTransform> Relative; for (auto* P : StandPieces(D)) Relative.Add(P->GetRelativeTransform());
        if (!TestTrue(TEXT("调用实际根 TryGrab"), D->GrabComp->TryGrab(H))) return false;
        TestTrue(TEXT("业务持握状态来自实际抓取且根附着手柄"), D->IsHeld() && D->GrabComp->IsHeld() && D->GetRootComponent()->GetAttachParent() == H);
        PoseEqual(*this, TEXT("抓取瞬间 KeepWorld，不吸附跳动"), D->GetActorTransform(), Before);
        PoseEqual(*this, TEXT("根相对手偏移完整保留"), D->GetRootComponent()->GetRelativeTransform(), Offset);
        H->SetWorldTransform(FTransform(FRotator(0, 50, 0), Point + FVector(20, -10, 7), FVector::OneVector));
        H->UpdateComponentToWorld();
        const FTransform Expected = Offset * H->GetComponentTransform();
        PoseEqual(*this, TEXT("真实手柄平移和yaw50驱动整个根"), D->GetActorTransform(), Expected);
        const auto Pieces = StandPieces(D);
        for (int32 I = 0; I < Pieces.Num(); ++I)
        {
            PoseEqual(*this, TEXT("支架相对根不漂移"), Pieces[I]->GetRelativeTransform(), Relative[I]);
            PoseEqual(*this, TEXT("支架世界姿态随整个组合移动"), Pieces[I]->GetComponentTransform(), Relative[I] * Expected);
        }
        if (!TestTrue(TEXT("实际根 TryRelease"), D->GrabComp->TryRelease())) return false;
        TestFalse(TEXT("业务已松手"), D->IsHeld()); TestNull(TEXT("松手根脱离手柄"), D->GetRootComponent()->GetAttachParent());
        PoseEqual(*this, TEXT("松手保持最终位姿，不自动归位"), D->GetActorTransform(), Expected);
        CheckAssembly(*this, D, true);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyHeldDrainTest,
    "CyberCafe.DripperAssembly.WhileHeldDrainPausesAfterDropResumes", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyHeldDrainTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    FWorldFixture W(*this); if (!W.World) return false;
    auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, 100)));
    // 原生杯仅作为液包接收夹具，明确不将此口点冒称保存 Cup BP 的几何。
    auto* Cup = W.Spawn<ACupActor>(ACupActor::StaticClass(), FTransform(FVector(0, 0, 86)));
    if (!TestNotNull(TEXT("真实组合滤杯"), D) || !TestNotNull(TEXT("原生液包接收夹具"), Cup)) return false;
    Cup->FillAmount = 0; Cup->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector); Cup->PourEntryRadius = 4;
    W.Begin(); auto* S = W.Transfer(); if (!TestNotNull(TEXT("真实液体转移系统"), S)) return false;
    Near(*this, TEXT("实际入口接纳15g"), D->AddGrounds(15, D->PourEntryPoint->GetComponentLocation()), 15);
    Near(*this, TEXT("实际注水20mL"), D->AddIngredientLiquid(20, Water, FLinearColor::White), 20);
    auto* H = W.Hand(D->StandLeftLeg->GetComponentLocation(), EControllerHand::Left);
    if (!TestNotNull(TEXT("真实抓取输入"), H) || !TestTrue(TEXT("带水滤杯实际抓起"), D->GrabComp->TryGrab(H))) return false;
    D->AdvanceBrew(8); D->AdvanceBrew(1);
    TestTrue(TEXT("持握期间闷蒸仍完成"), D->BrewState == ECoffeeBrewState::Brewing);
    Near(*this, TEXT("持握期间库存保持20"), D->GetPreciseVolumeML(), 20);
    Near(*this, TEXT("持握期间不出液"), D->DrippedML, 0); TestEqual(TEXT("持握期间没有包"), S->GetBalance().PacketCount, 0);
    Near(*this, TEXT("持握未消耗240预算"), D->GetCoffeeBudgetML(), 240); Balance(*this, S);
    if (!TestTrue(TEXT("松手后恢复滴流"), D->GrabComp->TryRelease())) return false;
    D->AdvanceBrew(1);
    Near(*this, TEXT("一秒滴12，源剩8"), D->GetPreciseVolumeML(), 8);
    Near(*this, TEXT("未模拟前真实在途12"), S->GetBalance().InFlightML, 12);
    S->Simulate(2); Balance(*this, S);
    Near(*this, TEXT("杯从真实液包收到Coffee12"), Cup->GetIngredientVolumeML(Coffee), 12);
    Near(*this, TEXT("恢复滴流未二次注入"), S->GetBalance().InjectedML, 20);
    Near(*this, TEXT("原生杯位于支架通道下方，无撒漏"), S->GetBalance().SpilledML, 0);
    D->AdvanceBrew(1); S->Simulate(2); Balance(*this, S);
    Near(*this, TEXT("最终杯实收Coffee20"), Cup->GetIngredientVolumeML(Coffee), 20);
    Near(*this, TEXT("真实源最终空"), D->GetPreciseVolumeML(), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyRestoreTest,
    "CyberCafe.DripperAssembly.CombinedRestoreHeldRejectAndPoseReapply", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyRestoreTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    FWorldFixture W(*this); if (!W.World) return false;
    const FTransform Initial(FRotator(0, 12, 0), FVector(500, 0, 100), FVector::OneVector);
    auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, Initial);
    auto* M = W.Spawn<AGlassRecoveryActor>(AGlassRecoveryActor::StaticClass(), FTransform(FVector(1000, 0, 0)));
    if (!TestNotNull(TEXT("组合工具夹具"), D) || !TestNotNull(TEXT("独立恢复经理夹具"), M)) return false;
    M->bRestoreAllSupplies = true; M->RestoreToolTargets = {D}; // 不在初始位置放杯，避免外物占位干扰本组持握门禁。
    W.Begin(); auto* S = W.Transfer(); if (!TestNotNull(TEXT("真实账本"), S)) return false;
    TestEqual(TEXT("仅捕获一个工具，不另捕获 Stand"), M->GetRestoreToolCount(), 1);
    TestTrue(TEXT("捕获的就是原组合身份"), M->GetCapturedTool(0) == D);
    PoseEqual(*this, TEXT("捕获组合完整根姿态"), M->GetToolInitialTransform(0), Initial);
    Near(*this, TEXT("公开放粉15g"), D->AddGrounds(15, D->PourEntryPoint->GetComponentLocation()), 15);
    Near(*this, TEXT("公开注水20mL"), D->AddIngredientLiquid(20, Water, FLinearColor::White), 20);
    auto* H = W.Hand(D->StandRightLeg->GetComponentLocation(), EControllerHand::Right);
    if (!TestNotNull(TEXT("实际持握手柄"), H) || !TestTrue(TEXT("真实抓取组合"), D->GrabComp->TryGrab(H))) return false;
    H->SetWorldLocationAndRotation(H->GetComponentLocation() + FVector(40, 0, 15), FRotator(0, 50, 0));
    const FTransform HeldPose = D->GetActorTransform(); const int32 Generation = D->BrewGeneration;
    const FLiquidBalance HeldBalance = S->GetBalance();
    TestFalse(TEXT("经理实际拒绝恢复持握工具"), M->RestoreWorkbench());
    TestEqual(TEXT("预检给出放下供给状态"), M->RestoreStatus.ToString(), FString(TEXT("PUT DOWN SUPPLIES")));
    PoseEqual(*this, TEXT("拒绝恢复不移动根"), D->GetActorTransform(), HeldPose);
    Near(*this, TEXT("拒绝恢复不清粉"), D->GetGroundsGrams(), 15); Near(*this, TEXT("拒绝恢复不清水"), D->GetPreciseVolumeML(), 20);
    TestEqual(TEXT("拒绝恢复不推进滤纸代次"), D->BrewGeneration, Generation);
    SameBalance(*this, HeldBalance, S->GetBalance());
    if (!TestTrue(TEXT("实际松手"), D->GrabComp->TryRelease())) return false;
    PoseEqual(*this, TEXT("松手没有自动回原位"), D->GetActorTransform(), HeldPose);
    if (!TestTrue(TEXT("显式 RestoreWorkbench 恢复整个组合"), M->RestoreWorkbench())) return false;
    TestTrue(TEXT("恢复不替换身份与 class"), M->GetCapturedTool(0) == D && D->GetClass() == Assets.Assembly);
    PoseEqual(*this, TEXT("显式恢复重施完整初始根姿态"), D->GetActorTransform(), Initial);
    Near(*this, TEXT("恢复清空真实粉"), D->GetGroundsGrams(), 0); Near(*this, TEXT("恢复清空真实水"), D->GetPreciseVolumeML(), 0);
    Near(*this, TEXT("清理粉记discard而非洒粉"), D->DiscardedGroundsGrams, 15);
    Near(*this, TEXT("恢复不伪造咖啡预算"), D->GetCoffeeBudgetML(), 0);
    Near(*this, TEXT("恢复排水记真实移除20"), S->GetBalance().RemovedML, 20);
    Near(*this, TEXT("恢复不记液体撒漏"), S->GetBalance().SpilledML, 0); Balance(*this, S);
    TestEqual(TEXT("成功恢复推进一次"), M->GetRestoreGeneration(), 1);
    CheckAssembly(*this, D, true);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyLipOffsetsTest,
    "CyberCafe.DripperAssembly.AssistedLipOffsetsAndLowerTilt", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyLipOffsetsTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    const FVector Offsets[] = {FVector(0, 0, 10), FVector(6, 0, 10), FVector(-6, 0, 10), FVector(0, 4.6, 10), FVector(0, 0, -.5)};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Offsets); ++I)
    {
        FWorldFixture W(*this); if (!W.World) return false;
        auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, 100)));
        auto* Drawer = W.Spawn<AGrinderDrawerActor>(Assets.Drawer, FTransform(FQuat::Identity, FVector(100, 0, 100), FVector(.5)));
        if (!TestNotNull(TEXT("实际组合 BP"), D) || !TestNotNull(TEXT("实际半缩放抽屉 BP"), Drawer)) return false;
        W.Begin(); if (!TestNotNull(TEXT("真实账本"), W.Transfer())) return false;
        TestTrue(TEXT("保存 BP 明确启用辅助瞬时倒粉且关闭粉流"), Drawer->bInstantGroundPour && Drawer->bAssistInstantGroundTransfer && !Drawer->bShowPowderStream);
        Near(*this, TEXT("实际世界抽屉缩放为0.5，不依赖CDO默认1"), Drawer->DrawerMesh->GetComponentScale().X, .5);
        Near(*this, TEXT("局部口沿半宽8"), Drawer->GroundLipHalfWidthCM, 8);
        Near(*this, TEXT("实际世界口沿半宽4"), Drawer->GroundLipHalfWidthCM * Drawer->DrawerMesh->GetComponentScale().X, 4);
        Near(*this, TEXT("辅助倾角30"), Drawer->AssistedPourTiltDegrees, 30);
        Near(*this, TEXT("原连续倾角45不变"), Drawer->PourTiltDegrees, 45);
        Near(*this, TEXT("原圆盘外边距只1cm"), Drawer->GroundsAssistMarginCM, 1);
        Near(*this, TEXT("只允许低于口沿1.5cm"), Drawer->GroundsAssistBelowRimCM, 1.5);
        Near(*this, TEXT("辅助最大高度25cm"), Drawer->GroundsAssistMaxHeightCM, 25);
        double Radius = 0, Top = 0;
        if (!TestTrue(TEXT("从实际滤纸几何得到辅助开口"), D->GetGroundsAssistOpening(Radius, Top))) return false;
        Near(*this, TEXT("scale1原滤杯实际辅助半径4"), Radius, 4);
        CheckOpeningGeometry(*this, D, Radius);
        // 沿用旧测试绕+X的负轴角：+Y朝下；等价 UE Roll +35，而不是 Roll -35。
        const FQuat Tilt(FVector::ForwardVector, FMath::DegreesToRadians(-35.0));
        const FVector LipWorld = D->PourEntryPoint->GetComponentLocation() + Offsets[I];
        PlaceLip(Drawer, LipWorld, Tilt);
        TestTrue(TEXT("实际局部+Y朝下且35度低于旧45阈值"), FVector::DotProduct(Drawer->DrawerMesh->GetComponentQuat().RotateVector(FVector::YAxisVector), FVector::DownVector) > .3);
        TestTrue(TEXT("逆变换实际口点准确"), Drawer->GroundPourPoint->GetComponentLocation().Equals(LipWorld, Tolerance));
        const FTransform MouthBefore = D->PourEntryPoint->GetRelativeTransform();
        const FTransform DrawerMouthBefore = Drawer->GroundPourPoint->GetRelativeTransform();
        Near(*this, TEXT("公开源库存15g"), Drawer->AddGrounds(15), 15);
        Drawer->State = AGrinderDrawerActor::EDrawerState::Free;
        auto* SourceHand = W.Hand(Drawer->GetActorLocation(), EControllerHand::Right);
        if (!TestNotNull(TEXT("实际抽屉输入手柄"), SourceHand) || !TestTrue(TEXT("实际抓起自由抽屉"), Drawer->GrabComp->TryGrab(SourceHand))) return false;
        TestTrue(TEXT("实际回调转为Detached且Held"), Drawer->State == AGrinderDrawerActor::EDrawerState::Detached && Drawer->GrabComp->IsHeld());
        if (I == 0)
        {
            auto* TargetHand = W.Hand(D->StandLeftLeg->GetComponentLocation(), EControllerHand::Left);
            if (!TestNotNull(TEXT("另一只手握滤杯"), TargetHand) || !TestTrue(TEXT("实际另一手抓住直立滤杯"), D->GrabComp->TryGrab(TargetHand))) return false;
            TestTrue(TEXT("支持直立且干燥的持握滤杯接粉"), D->IsHeld() && D->CanReceiveGrounds());
        }
        PourMass(*this, W, Drawer, D, 15);
        Near(*this, TEXT("一次抽屉归零"), Drawer->GetGroundsGrams(), 0); Near(*this, TEXT("实际滤杯15g"), D->GetGroundsGrams(), 15);
        Near(*this, TEXT("实际15g只产生240mL有限预算"), D->GetCoffeeBudgetML(), 240);
        const FVector SelectedOrigin = Drawer->LastGroundPourOrigin;
        const FVector SelectedEnd = Drawer->LastGroundPourEnd;
        if (FMath::Abs(Offsets[I].X) > Radius + Drawer->GroundsAssistMarginCM)
        {
            Near(*this, TEXT("偏心6cm实际源口沿最近点距中心2cm"), FMath::Abs(SelectedOrigin.X - D->PourEntryPoint->GetComponentLocation().X), 2);
            Near(*this, TEXT("偏心6cm实际源点沿世界口沿移动4cm"), FMath::Abs(SelectedOrigin.X - LipWorld.X), 4);
            TestTrue(TEXT("选中起点不是凭空移到滤杯中心"), !SelectedOrigin.Equals(D->PourEntryPoint->GetComponentLocation(), Tolerance));
        }
        if (I == 3)
            TestTrue(TEXT("1cm边距只桥接到原圆盘内，源Y仍在真实4.6cm口沿"),
                FMath::Abs(SelectedOrigin.Y - LipWorld.Y) <= Tolerance && FMath::Abs(SelectedEnd.Y - SelectedOrigin.Y) > Tolerance);
        const int32 Generation = D->BrewGeneration;
        PourMass(*this, W, Drawer, D, 0); Near(*this, TEXT("重复空倒不追加预算"), D->GetCoffeeBudgetML(), 240);
        TestEqual(TEXT("重复调用不新增滤纸代次"), D->BrewGeneration, Generation);
        PoseEqual(*this, TEXT("没有修改目标原始口点"), D->PourEntryPoint->GetRelativeTransform(), MouthBefore);
        PoseEqual(*this, TEXT("没有移动真实抽屉口点"), Drawer->GroundPourPoint->GetRelativeTransform(), DrawerMouthBefore);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyRejectedTest,
    "CyberCafe.DripperAssembly.AssistedRejectedInputsPreserveSourceAndNearestTarget", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyRejectedTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    const TCHAR* Cases[] = {TEXT("MISS"), TEXT("HEIGHT"), TEXT("BELOW"), TEXT("ANGLE"), TEXT("EMPTY_SOURCE"), TEXT("WET"), TEXT("FULL"), TEXT("BLOCKED"), TEXT("COVERED"), TEXT("INVALID_DT")};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Cases); ++I)
    {
        AddInfo(FString(TEXT("拒绝输入：")) + Cases[I]);
        FWorldFixture W(*this); if (!W.World) return false;
        auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, 100)));
        auto* Behind = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, 95)));
        auto* Drawer = W.Spawn<AGrinderDrawerActor>(Assets.Drawer, FTransform(FQuat::Identity, FVector(100, 0, 100), FVector(.5)));
        if (!TestNotNull(TEXT("最近真实组合"), D) || !TestNotNull(TEXT("后面的真实组合"), Behind) || !TestNotNull(TEXT("真实源抽屉"), Drawer)) return false;
        W.Begin(); if (!TestNotNull(TEXT("真实账本"), W.Transfer())) return false;
        if (!TestTrue(TEXT("拒收测试使用部署的辅助模式而非测试强行启用"), Drawer->bAssistInstantGroundTransfer && Drawer->bInstantGroundPour)) return false;
        FVector Offset(0, 0, 10); double Roll = -35;
        // 实际可达 X 上界是半口沿4 + 原半径4 + 边距1 = 9cm，不把抽屉中心当唯一源点。
        if (I == 0) Offset.X = 9.2;
        if (I == 1) Offset.Z = 30;
        if (I == 2) { Offset.Z = -2; Behind->SetActorLocation(FVector(100, 0, 95)); }
        if (I == 3) Roll = -20;
        if (I != 4) Near(*this, TEXT("公开加入真实源15g"), Drawer->AddGrounds(15), 15);
        if (I == 5) Near(*this, TEXT("最近杯实际注水1导致湿滤纸"), D->AddIngredientLiquid(1, Water, FLinearColor::White), 1);
        if (I == 6) Near(*this, TEXT("最近杯实际容量填到20，不改私有粉量"), D->AddGrounds(100, D->PourEntryPoint->GetComponentLocation()), 20);
        if (I == 7 || I == 8)
        {
            auto* Obstacle = W.Blocker(D->PourEntryPoint->GetComponentLocation() + FVector(0, 0, I == 7 ? 5 : .5), FVector(.12, .12, I == 7 ? .02 : .005));
            if (!TestNotNull(TEXT("真实外物 Cube 阻挡"), Obstacle) || !QueryBody(*this, Obstacle->GetStaticMeshComponent())) return false;
        }
        PlaceLip(Drawer, D->PourEntryPoint->GetComponentLocation() + Offset, FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Roll)));
        const double SourceBefore = Drawer->GetGroundsGrams(), TargetBefore = D->GetGroundsGrams(), VolumeBefore = D->GetPreciseVolumeML();
        PourMass(*this, W, Drawer, D, 0, I == 9 ? std::numeric_limits<float>::quiet_NaN() : .01f);
        Near(*this, TEXT("失败全部保留源粉，不记洒粉"), Drawer->GetGroundsGrams(), SourceBefore);
        Near(*this, TEXT("失败不改最近杯粉"), D->GetGroundsGrams(), TargetBefore);
        Near(*this, TEXT("失败不改最近杯液量"), D->GetPreciseVolumeML(), VolumeBefore);
        Near(*this, TEXT("最近杯湿/满/阻挡不穿透改投后杯"), Behind->GetGroundsGrams(), 0);
        if (I == 5) TestEqual(TEXT("湿滤纸原因"), Drawer->LastGroundTransferReason, FName(TEXT("NEEDS_EMPTY_FILTER")));
        if (I == 6) TestEqual(TEXT("满滤杯原因"), Drawer->LastGroundTransferReason, FName(TEXT("FULL_FILTER")));
        if (I == 7 || I == 8) TestEqual(TEXT("外物阻挡原因"), Drawer->LastGroundTransferReason, FName(TEXT("BLOCKED")));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyPartialTest,
    "CyberCafe.DripperAssembly.PartialCapacityAndRepeatedFullHaveNoDuplicateCredit", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyPartialTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    FWorldFixture W(*this); if (!W.World) return false;
    auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, 100)));
    auto* Drawer = W.Spawn<AGrinderDrawerActor>(Assets.Drawer, FTransform(FQuat::Identity, FVector(100, 0, 100), FVector(.5)));
    if (!TestNotNull(TEXT("实际组合 BP"), D) || !TestNotNull(TEXT("实际抽屉 BP"), Drawer)) return false;
    W.Begin(); if (!TestNotNull(TEXT("真实账本"), W.Transfer())) return false;
    if (!TestTrue(TEXT("保存辅助瞬时模式"), Drawer->bAssistInstantGroundTransfer && Drawer->bInstantGroundPour)) return false;
    Near(*this, TEXT("实际硬容量20g"), D->GetGroundsCapacityGrams(), 20);
    Near(*this, TEXT("公开初始目标18g"), D->AddGrounds(18, D->PourEntryPoint->GetComponentLocation()), 18);
    Near(*this, TEXT("公开源15g"), Drawer->AddGrounds(15), 15);
    PlaceLip(Drawer, D->PourEntryPoint->GetComponentLocation() + FVector(6, 0, 10), FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-35.0)));
    PourMass(*this, W, Drawer, D, 2);
    Near(*this, TEXT("目标实际20g"), D->GetGroundsGrams(), 20); Near(*this, TEXT("容量余量后源仍13g"), Drawer->GetGroundsGrams(), 13);
    TestEqual(TEXT("报告部分接纳"), Drawer->LastGroundTransferReason, FName(TEXT("PARTIAL")));
    Near(*this, TEXT("20g有限预算320，无重复信用"), D->GetCoffeeBudgetML(), 320);
    for (int32 I = 0; I < 5; ++I) PourMass(*this, W, Drawer, D, 0);
    Near(*this, TEXT("反复对满杯源仍13g"), Drawer->GetGroundsGrams(), 13);
    Near(*this, TEXT("反复对满杯预算仍320"), D->GetCoffeeBudgetML(), 320);
    Near(*this, TEXT("18+15=目标20+源13，无粉损失"), D->GetGroundsGrams() + Drawer->GetGroundsGrams() + Drawer->SpilledGroundGrams, 33);
    if (!TestTrue(TEXT("真实空水滤杯公开换纸以测试float容量尾差"), D->ReplacePaper())) return false;
    const float AlmostFull = std::nextafter(20.f, 0.f);
    TestEqual(TEXT("公开填入可表示的临满粉量"), D->AddGrounds(AlmostFull, D->PourEntryPoint->GetComponentLocation()), AlmostFull);
    const float Space = 20.f - AlmostFull;
    PourMass(*this, W, Drawer, D, Space);
    TestEqual(TEXT("float请求确实转移容量尾差，而非容差把零当通过"), Drawer->LastAcceptedGroundGrams, Space);
    TestEqual(TEXT("float源只扣同一可表示尾差"), Drawer->GetGroundsGrams(), 13.f - Space);
    TestEqual(TEXT("float目标严格不超过实际20g容量"), D->GetGroundsGrams(), 20.f);
    TestEqual(TEXT("float尾差不凭空给目标信用或记撒粉"), Drawer->SpilledGroundGrams, 0.0);
    PourMass(*this, W, Drawer, D, 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyScaleAndLegacyTest,
    "CyberCafe.DripperAssembly.ScaleHalfTiltedOpeningAndNativeLegacyOptOut", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyScaleAndLegacyTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    for (bool Outside : {false, true})
    {
        FWorldFixture W(*this); if (!W.World) return false;
        auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FRotator(20, 0, 0), FVector(0, 0, 100), FVector(.5)));
        auto* Drawer = W.Spawn<AGrinderDrawerActor>(Assets.Drawer, FTransform(FQuat::Identity, FVector(100, 0, 100), FVector(.5)));
        if (!TestNotNull(TEXT("半缩放且Pitch20组合 BP"), D) || !TestNotNull(TEXT("真实半缩放抽屉"), Drawer)) return false;
        W.Begin(); if (!TestNotNull(TEXT("实际账本"), W.Transfer())) return false;
        double Radius = 0, Top = 0;
        if (!TestTrue(TEXT("倾斜滤杯读取实际滤纸保守开口"), D->GetGroundsAssistOpening(Radius, Top))) return false;
        Near(*this, TEXT("实际缩放后世界半径2，不误当4"), Radius, 2);
        CheckOpeningGeometry(*this, D, Radius);
        // Y 与口沿 X 正交；超界3.2cm不能靠世界4cm半口沿吸过去。
        const FVector InPlane = D->PourEntryPoint->GetRightVector() * (Outside ? 3.2 : 1.5);
        PlaceLip(Drawer, D->PourEntryPoint->GetComponentLocation() + InPlane + FVector(0, 0, 10), FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-35.0)));
        Near(*this, TEXT("真实源15g"), Drawer->AddGrounds(15), 15);
        PourMass(*this, W, Drawer, D, Outside ? 0 : 15);
        if (Outside) Near(*this, TEXT("半缩放实际开口加1cm外拒收，源保留"), Drawer->GetGroundsGrams(), 15);
        else { CanonicalInside(*this, Drawer, D); Near(*this, TEXT("缩放/倾斜后仍实际接纳15"), D->GetGroundsGrams(), 15); }
    }
    // 兼容路径使用默认原生类，不覆盖保存 BP opt-in；几何也保持原生口点与真实0.5缩放。
    FWorldFixture W(*this); if (!W.World) return false;
    auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, 100)));
    auto* Drawer = W.Spawn<AGrinderDrawerActor>(AGrinderDrawerActor::StaticClass(), FTransform(FQuat::Identity, FVector(100, 0, 100), FVector(.5)));
    if (!TestNotNull(TEXT("兼容目标实际组合"), D) || !TestNotNull(TEXT("默认连续抽屉原生夹具"), Drawer)) return false;
    W.Begin(); if (!TestNotNull(TEXT("兼容真实账本"), W.Transfer())) return false;
    TestFalse(TEXT("原生辅助默认关闭"), Drawer->bAssistInstantGroundTransfer); TestFalse(TEXT("原生瞬时默认关闭"), Drawer->bInstantGroundPour);
    Near(*this, TEXT("公开兼容源10g"), Drawer->AddGrounds(10), 10);
    PlaceLip(Drawer, D->PourEntryPoint->GetComponentLocation() + FVector(0, 0, 10), FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-35.0)));
    Near(*this, TEXT("原生35度仍不倒，不偷偷启用辅助"), Drawer->PourGrounds(.5f), 0);
    Near(*this, TEXT("原生低倾角完整保留10"), Drawer->GetGroundsGrams(), 10);
    PlaceLip(Drawer, D->PourEntryPoint->GetComponentLocation() + FVector(0, 0, 10), FQuat(FVector::ForwardVector, -PI / 2));
    Near(*this, TEXT("原连续路径0.5秒只倒4g"), Drawer->PourGrounds(.5f), 4);
    Near(*this, TEXT("原连续目标实际收到4g"), D->GetGroundsGrams(), 4); Near(*this, TEXT("原连续源仍6g"), Drawer->GetGroundsGrams(), 6);
    Near(*this, TEXT("原连续命中无洒粉"), Drawer->SpilledGroundGrams, 0); Balance(*this, W.Transfer());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyDecimalGroundsTest,
    "CyberCafe.DripperAssembly.DecimalGroundsDoNotRejectWholeBatch", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyDecimalGroundsTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    const auto Close = [this](const FString& Label, double Actual, double Expected)
    {
        return TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
            FMath::IsFinite(Actual) && FMath::IsFinite(Expected) && FMath::Abs(Actual - Expected) <= 1.e-5);
    };
    const float InitialTargets[] = {.1f, 1.3f, 18.f};
    for (int32 I = 0; I < UE_ARRAY_COUNT(InitialTargets); ++I)
    {
        AddInfo(FString::Printf(TEXT("独立世界目标初始粉量 %.9g"), InitialTargets[I]));
        FWorldFixture W(*this); if (!W.World) return false;
        auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, 100)));
        auto* Drawer = W.Spawn<AGrinderDrawerActor>(Assets.Drawer, FTransform(FQuat::Identity, FVector(100, 0, 100), FVector(.5)));
        if (!TestNotNull(TEXT("真实保存组合 BP"), D) || !TestNotNull(TEXT("原研磨器保存 DrawerClass"), Drawer)) return false;
        W.Begin(); if (!TestNotNull(TEXT("真实液体账本"), W.Transfer())) return false;
        if (!TestTrue(TEXT("保存 BP 已启用辅助瞬时倒粉，不由测试强制设置"), Drawer->bInstantGroundPour && Drawer->bAssistInstantGroundTransfer
            && !Drawer->bShowPowderStream)) return false;
        TestEqual(TEXT("每个子场景目标从零开始"), D->GetGroundsGrams(), 0.f);
        TestEqual(TEXT("每个子场景源从零开始"), Drawer->GetGroundsGrams(), 0.f);
        Close(TEXT("实际抽屉世界缩放0.5"), Drawer->DrawerMesh->GetComponentScale().X, .5);
        Close(TEXT("实际目标容量20g"), D->GetGroundsCapacityGrams(), 20);
        Close(TEXT("真实每克预算16mL"), D->CoffeeYieldMLPerGram, 16);
        TestEqual(TEXT("公开 API 建立小数或部分满目标"), D->AddGrounds(InitialTargets[I], D->PourEntryPoint->GetComponentLocation()), InitialTargets[I]);
        TestEqual(TEXT("公开 API 加入真实源15g"), Drawer->AddGrounds(15.f), 15.f);
        const FTransform TargetMouthBefore = D->PourEntryPoint->GetRelativeTransform();
        const FTransform SourceMouthBefore = Drawer->GroundPourPoint->GetRelativeTransform();
        const FVector LipWorld = D->PourEntryPoint->GetComponentLocation() + FVector(0, 0, 10);
        // 保持局部口沿 X 水平，绕 +X 转 -35 度使倾倒方向 +Y 朝下。
        PlaceLip(Drawer, LipWorld, FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-35.0)));
        if (!TestTrue(TEXT("真实口点高10cm且无平面偏移"), !Drawer->GroundPourPoint->GetComponentTransform().ContainsNaN()
            && Drawer->GroundPourPoint->GetComponentLocation().Equals(LipWorld, 1.e-8))) return false;
        TestTrue(TEXT("35度实际倾倒方向朝下"), FVector::DotProduct(Drawer->DrawerMesh->GetComponentQuat().RotateVector(FVector::YAxisVector), FVector::DownVector) > .3);
        const double SourceBefore = Drawer->GetGroundsGrams(), TargetBefore = D->GetGroundsGrams();
        const double SpillBefore = Drawer->SpilledGroundGrams, BudgetBefore = D->GetCoffeeBudgetML();
        const FLiquidBalance LiquidBefore = W.Transfer()->GetBalance();
        const float Dropped = Drawer->PourGrounds(.016f);
        const double TargetAfter = D->GetGroundsGrams(), SourceAfter = Drawer->GetGroundsGrams();
        const double TargetIncrease = TargetAfter - TargetBefore, SpillIncrease = Drawer->SpilledGroundGrams - SpillBefore;
        TestEqual(TEXT("真实返回整批15或容量部分2，零不能靠容差通过"), Dropped, I < 2 ? 15.f : 2.f);
        TestEqual(TEXT("公开本次出粉量等于返回"), Drawer->LastPouredGroundGrams, Dropped);
        Close(TEXT("返回量等于源实际扣量"), Dropped, SourceBefore - SourceAfter);
        Close(TEXT("公开接纳量与目标实际增量一致"), Drawer->LastAcceptedGroundGrams, TargetIncrease);
        Close(TEXT("扣源等于目标实增加正微尾差"), SourceBefore - SourceAfter, TargetIncrease + SpillIncrease);
        Close(TEXT("粉账本前后守恒"), SourceBefore + TargetBefore + SpillBefore,
            SourceAfter + TargetAfter + Drawer->SpilledGroundGrams);
        Close(TEXT("预算增量来自实际 API 接纳量乘16"), static_cast<double>(D->GetCoffeeBudgetML()) - BudgetBefore,
            static_cast<double>(Drawer->LastAcceptedGroundGrams) * 16.0);
        TestTrue(TEXT("预算有限非负且不超过实际粉量对应上限"), FMath::IsFinite(D->GetCoffeeBudgetML())
            && D->GetCoffeeBudgetML() >= 0.f && static_cast<double>(D->GetCoffeeBudgetML()) <= TargetAfter * 16.0 + 1.e-5);
        if (I < 2)
        {
            TestEqual(TEXT("小数目标仍一次出清源库存"), Drawer->GetGroundsGrams(), 0.f);
            Close(TEXT("小数目标实收原粉量加15"), TargetAfter, TargetBefore + 15.0);
            TestTrue(TEXT("小数舍入仅记有限正微尾差，不要求严格零撒粉"), FMath::IsFinite(SpillIncrease)
                && SpillIncrease > 0.0 && SpillIncrease < 1.e-5);
            TestEqual(TEXT("小数目标不会误报满杯"), Drawer->LastGroundTransferReason, FName(TEXT("TRANSFERRED")));
        }
        else
        {
            TestEqual(TEXT("18g目标部分接纳后严格20g"), D->GetGroundsGrams(), 20.f);
            TestEqual(TEXT("部分接纳后严格留源13g"), Drawer->GetGroundsGrams(), 13.f);
            TestEqual(TEXT("整数部分接纳不记撒粉"), Drawer->SpilledGroundGrams, SpillBefore);
            TestEqual(TEXT("部分接纳原因"), Drawer->LastGroundTransferReason, FName(TEXT("PARTIAL")));
        }
        SourceOnVisibleLip(*this, Drawer); CanonicalInside(*this, Drawer, D);
        AddInfo(FString::Printf(TEXT("实际首倒 dropped=%.9g source=%.9g target=%.12g accepted=%.9g spill=%.12g budget=%.12g reason=%s"),
            Dropped, SourceAfter, TargetAfter, Drawer->LastAcceptedGroundGrams, Drawer->SpilledGroundGrams,
            D->GetCoffeeBudgetML(), *Drawer->LastGroundTransferReason.ToString()));
        const float BudgetAfter = D->GetCoffeeBudgetML();
        const double SpillAfter = Drawer->SpilledGroundGrams;
        const int32 GenerationAfter = D->BrewGeneration;
        for (int32 Repeat = 0; Repeat < 5; ++Repeat)
        {
            TestEqual(TEXT("空源或满目标重复倒粉严格返回零"), Drawer->PourGrounds(.016f), 0.f);
            TestEqual(TEXT("重复不产生接纳信用"), Drawer->LastAcceptedGroundGrams, 0.f);
            TestEqual(TEXT("重复保留源库存"), static_cast<double>(Drawer->GetGroundsGrams()), SourceAfter);
            TestEqual(TEXT("重复不增加目标粉"), static_cast<double>(D->GetGroundsGrams()), TargetAfter);
            TestEqual(TEXT("重复不追加预算"), D->GetCoffeeBudgetML(), BudgetAfter);
            TestEqual(TEXT("重复不追加撒粉"), Drawer->SpilledGroundGrams, SpillAfter);
            TestEqual(TEXT("重复不新增滤纸代次"), D->BrewGeneration, GenerationAfter);
            if (I == 2) TestEqual(TEXT("部分接纳后的满杯拒收"), Drawer->LastGroundTransferReason, FName(TEXT("FULL_FILTER")));
        }
        if (I == 0)
        {
            // 同一张滤纸续加下一批真实15g，只能用剩余容量，不能恢复整批空间或预算。
            TestEqual(TEXT("小数场景公开加入下一批15g"), Drawer->AddGrounds(15.f), 15.f);
            const double NextSourceBefore = Drawer->GetGroundsGrams();
            const float NextDropped = Drawer->PourGrounds(.016f);
            TestTrue(TEXT("下一批只部分出粉，不虚构整批15g空间"), FMath::IsFinite(NextDropped) && NextDropped > 0.f && NextDropped < 15.f);
            TestEqual(TEXT("下一批接纳后目标严格20g"), D->GetGroundsGrams(), 20.f);
            TestTrue(TEXT("下一批未接纳的粉仍留源"), FMath::IsFinite(Drawer->GetGroundsGrams()) && Drawer->GetGroundsGrams() > 10.f);
            Close(TEXT("下一批返回与真实扣源一致"), NextDropped, NextSourceBefore - Drawer->GetGroundsGrams());
            Close(TEXT("下一批仍保粉账本"), NextSourceBefore + TargetAfter + SpillAfter,
                static_cast<double>(Drawer->GetGroundsGrams()) + D->GetGroundsGrams() + Drawer->SpilledGroundGrams);
            Close(TEXT("下一批预算仅来自实际接纳量"), static_cast<double>(D->GetCoffeeBudgetML()) - BudgetAfter,
                static_cast<double>(Drawer->LastAcceptedGroundGrams) * 16.0);
            Close(TEXT("同一滤纸预算封顶320"), D->GetCoffeeBudgetML(), 320);
            TestEqual(TEXT("下一批仍属同一滤纸"), D->BrewGeneration, GenerationAfter);
            TestEqual(TEXT("下一批报告部分接纳"), Drawer->LastGroundTransferReason, FName(TEXT("PARTIAL")));
            const float Retained = Drawer->GetGroundsGrams(), FullBudget = D->GetCoffeeBudgetML();
            const double FullSpill = Drawer->SpilledGroundGrams;
            for (int32 Repeat = 0; Repeat < 5; ++Repeat)
            {
                TestEqual(TEXT("下一批满杯后重复返回零"), Drawer->PourGrounds(.016f), 0.f);
                TestEqual(TEXT("满杯后下一批余粉保留"), Drawer->GetGroundsGrams(), Retained);
                TestEqual(TEXT("满杯重复不增加目标"), D->GetGroundsGrams(), 20.f);
                TestEqual(TEXT("满杯重复不增加预算"), D->GetCoffeeBudgetML(), FullBudget);
                TestEqual(TEXT("满杯重复不记额外撒粉"), Drawer->SpilledGroundGrams, FullSpill);
                TestEqual(TEXT("满杯重复不给信用"), Drawer->LastAcceptedGroundGrams, 0.f);
                TestEqual(TEXT("下一批满杯拒收原因"), Drawer->LastGroundTransferReason, FName(TEXT("FULL_FILTER")));
            }
        }
        PoseEqual(*this, TEXT("目标原口点保持"), D->PourEntryPoint->GetRelativeTransform(), TargetMouthBefore);
        PoseEqual(*this, TEXT("源原口点保持"), Drawer->GroundPourPoint->GetRelativeTransform(), SourceMouthBefore);
        SameBalance(*this, LiquidBefore, W.Transfer()->GetBalance()); Balance(*this, W.Transfer());
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDripperAssemblyCoplanarLipBlockedTest,
    "CyberCafe.DripperAssembly.CoplanarLipBlockedByForeignObjectKeepsPowder", CyberCafeDripperAssemblyTests::AssemblyTestFlags)
bool FDripperAssemblyCoplanarLipBlockedTest::RunTest(const FString&)
{
    using namespace CyberCafeDripperAssemblyTests;
    FAssets Assets; if (!Assets.Ready(*this)) return false;
    FWorldFixture W(*this); if (!W.World) return false;
    // 原入口相对根仍为6.5cm，仅摆放真实 Actor 使世界入口为零，避免抵消误差掩盖零长度路径。
    auto* D = W.Spawn<AHandDripperAssemblyActor>(Assets.Assembly, FTransform(FVector(0, 0, -6.5)));
    auto* Drawer = W.Spawn<AGrinderDrawerActor>(Assets.Drawer, FTransform(FQuat::Identity, FVector(100, 0, 100), FVector(.5)));
    if (!TestNotNull(TEXT("真实保存组合 BP"), D) || !TestNotNull(TEXT("实际保存抽屉 BP"), Drawer)) return false;
    W.Begin(); if (!TestNotNull(TEXT("真实液体账本"), W.Transfer())) return false;
    if (!TestTrue(TEXT("保存 BP 辅助瞬时倒粉已开启，不手动设置"), Drawer->bInstantGroundPour && Drawer->bAssistInstantGroundTransfer
        && !Drawer->bShowPowderStream)) return false;
    Near(*this, TEXT("真实源世界缩放0.5"), Drawer->DrawerMesh->GetComponentScale().X, .5);
    const FVector Entry = D->PourEntryPoint->GetComponentLocation();
    const FTransform TargetMouthBefore = D->PourEntryPoint->GetRelativeTransform();
    const FTransform SourceMouthBefore = Drawer->GroundPourPoint->GetRelativeTransform();
    PlaceLip(Drawer, Entry, FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-35.0)));
    const FVector SourcePoint = Drawer->GroundPourPoint->GetComponentLocation();
    const FVector Normal = D->PourEntryPoint->GetUpVector();
    const double Height = FVector::DotProduct(SourcePoint - Entry, Normal) / FVector::DotProduct(Normal, FVector::UpVector);
    const FVector Projected = SourcePoint - FVector::UpVector * Height;
    double Radius = 0, Top = 0;
    if (!TestTrue(TEXT("读取真实滤纸开口"), D->GetGroundsAssistOpening(Radius, Top))) return false;
    const FVector Canonical = Entry + (Projected - Entry).GetClampedToMaxSize(Radius * .85);
    if (!TestTrue(TEXT("入口、源点、投影和规范点有限且落在运行时零长度分支阈值内"), !Entry.ContainsNaN() && !SourcePoint.ContainsNaN()
        && !Projected.ContainsNaN() && !Canonical.ContainsNaN() && FMath::IsFinite(Height)
        && Entry.IsNearlyZero(UE_SMALL_NUMBER) && SourcePoint.Equals(Entry, UE_SMALL_NUMBER)
        && FMath::Abs(Height) <= UE_SMALL_NUMBER
        && FVector::DistSquared(SourcePoint, Projected) <= UE_SMALL_NUMBER * UE_SMALL_NUMBER
        && FVector::DistSquared(Projected, Canonical) <= UE_SMALL_NUMBER * UE_SMALL_NUMBER)) return false;
    TestTrue(TEXT("保持实际35度倾倒，不以角度拒收代替碰撞"), FVector::DotProduct(Drawer->DrawerMesh->GetComponentQuat().RotateVector(FVector::YAxisVector), FVector::DownVector) > .3);
    if (!QueryBody(*this, D->ContainerMesh.Get())) return false;
    TestEqual(TEXT("目标真实初始粉量零"), D->GetGroundsGrams(), 0.f);
    TestEqual(TEXT("公开加入真实源15g"), Drawer->AddGrounds(15.f), 15.f);
    auto* Obstacle = W.Blocker(Entry, FVector(.03, .03, .002));
    if (!TestNotNull(TEXT("入口中心独立3×3×0.2cm横板 Cube"), Obstacle)) return false;
    auto* P = Obstacle->GetStaticMeshComponent();
    P->SetCollisionObjectType(ECC_PhysicsBody);
    P->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    P->SetCollisionResponseToAllChannels(ECR_Ignore);
    P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    if (!QueryBody(*this, P)) return false;
    TestTrue(TEXT("外物只以PhysicsBody对象参加查询，Visibility阻挡且不模拟"), P->GetCollisionObjectType() == ECC_PhysicsBody
        && P->GetCollisionEnabled() == ECollisionEnabled::QueryOnly && P->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
        && !P->IsSimulatingPhysics());
    TestNull(TEXT("外物不是目标或源的附属组件"), P->GetAttachParent());
    TestTrue(TEXT("外物属于独立Actor而非组合或抽屉"), P->GetOwner() == Obstacle && P->GetOwner() != D && P->GetOwner() != Drawer);
    FCollisionQueryParams ForeignOnly;
    ForeignOnly.AddIgnoredActor(D); ForeignOnly.AddIgnoredActor(Drawer);
    const auto ForeignOverlap = [&]()
    {
        return W.World->OverlapAnyTestByObjectType(Entry, FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody),
            FCollisionShape::MakeSphere(.05f), ForeignOnly);
    };
    if (!TestTrue(TEXT("真实0.05cm球确实重叠独立PhysicsBody挡板"), ForeignOverlap())) return false;
    const FTransform DrawerPose = Drawer->GetActorTransform(), TargetPose = D->GetActorTransform();
    const float BudgetBefore = D->GetCoffeeBudgetML();
    const int32 GenerationBefore = D->BrewGeneration;
    const FLiquidBalance LiquidBefore = W.Transfer()->GetBalance();
    for (int32 Repeat = 0; Repeat < 2; ++Repeat)
    {
        TestEqual(TEXT("共面出口被外物阻挡，真实倒粉严格返回零"), Drawer->PourGrounds(.016f), 0.f);
        TestEqual(TEXT("拒收完整保留源15g"), Drawer->GetGroundsGrams(), 15.f);
        TestEqual(TEXT("拒收目标仍为零"), D->GetGroundsGrams(), 0.f);
        TestEqual(TEXT("阻挡不记撒粉"), Drawer->SpilledGroundGrams, 0.0);
        TestEqual(TEXT("阻挡不给接纳信用"), Drawer->LastAcceptedGroundGrams, 0.f);
        TestEqual(TEXT("阻挡不给出粉量"), Drawer->LastPouredGroundGrams, 0.f);
        TestEqual(TEXT("拒收明确来自碰撞"), Drawer->LastGroundTransferReason, FName(TEXT("BLOCKED")));
        TestEqual(TEXT("阻挡不增加预算"), D->GetCoffeeBudgetML(), BudgetBefore);
        TestEqual(TEXT("阻挡不新增滤纸代次"), D->BrewGeneration, GenerationBefore);
        TestTrue(TEXT("实际查询选中严格共面的源点与规范终点"), Drawer->LastGroundPourOrigin.Equals(Entry, UE_SMALL_NUMBER) && Drawer->LastGroundPourEnd.Equals(Entry, UE_SMALL_NUMBER));
        PoseEqual(*this, TEXT("阻挡不移动源Actor"), Drawer->GetActorTransform(), DrawerPose);
        PoseEqual(*this, TEXT("阻挡不移动目标Actor"), D->GetActorTransform(), TargetPose);
    }
    if (!TestTrue(TEXT("只销毁独立挡板，不改Actor口点与查询布尔"), W.World->DestroyActor(Obstacle))) return false;
    if (!TestFalse(TEXT("移除挡板后真实PhysicsBody外物重叠已消失"), ForeignOverlap())) return false;
    PourMass(*this, W, Drawer, D, 15.f, .016f);
    TestEqual(TEXT("同一共面位置解除阻挡后源归零"), Drawer->GetGroundsGrams(), 0.f);
    TestEqual(TEXT("同一共面位置真实目标收到15g"), D->GetGroundsGrams(), 15.f);
    TestEqual(TEXT("整数转移无撒粉"), Drawer->SpilledGroundGrams, 0.0);
    TestEqual(TEXT("移除外物后明确转移成功"), Drawer->LastGroundTransferReason, FName(TEXT("TRANSFERRED")));
    TestTrue(TEXT("成功仍使用原严格共面源点与终点"), Drawer->LastGroundPourOrigin.Equals(Entry, UE_SMALL_NUMBER) && Drawer->LastGroundPourEnd.Equals(Entry, UE_SMALL_NUMBER));
    TestEqual(TEXT("15g只产生有限240mL预算"), D->GetCoffeeBudgetML(), 240.f);
    PourMass(*this, W, Drawer, D, 0.f, .016f);
    TestEqual(TEXT("空源重复不增加目标粉"), D->GetGroundsGrams(), 15.f);
    TestEqual(TEXT("空源重复不追加预算"), D->GetCoffeeBudgetML(), 240.f);
    PoseEqual(*this, TEXT("全过程源Actor位置不变"), Drawer->GetActorTransform(), DrawerPose);
    PoseEqual(*this, TEXT("全过程目标Actor位置不变"), D->GetActorTransform(), TargetPose);
    PoseEqual(*this, TEXT("保留目标真实口点"), D->PourEntryPoint->GetRelativeTransform(), TargetMouthBefore);
    PoseEqual(*this, TEXT("保留源真实口点"), Drawer->GroundPourPoint->GetRelativeTransform(), SourceMouthBefore);
    SameBalance(*this, LiquidBefore, W.Transfer()->GetBalance()); Balance(*this, W.Transfer());
    return !HasAnyErrors();
}
#endif
