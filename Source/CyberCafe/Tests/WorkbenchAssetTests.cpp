// @trace GBX-01 RS-01 RS-02 RS-03 RS-04
// 状态：writtenpendingrun。资产 apply 后由外部全量构建、执行；本文件不启动 UE。
// 保存地图仅只读加载；独立 Game World 的 Chaos 采样不代表原地图或头显验收。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/BottleActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/IceInteraction.h"
#include "GrabComponent.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialStep.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/ServingSpotActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Components/StaticMeshComponent.h"
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
#include "NiagaraComponent.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

namespace WorkbenchAssetTests
{
constexpr float StepSeconds = .01f;
constexpr double VolumeTolerance = 1.e-6;
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
struct FVariant { const TCHAR* Label; const TCHAR* ClassPath; };
const FVariant Variants[] = {
    {TEXT("Bottle01"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle01/BP_Bottle01.BP_Bottle01_C")},
    {TEXT("Bottle02"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle02/BP_Bottle02.BP_Bottle02_C")},
    {TEXT("Bottle03"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle03/BP_Bottle03.BP_Bottle03_C")},
    {TEXT("Bottle04"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle04/BP_Bottle04_Milk.BP_Bottle04_Milk_C")},
    {TEXT("Bottle05"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle05/BP_Bottle05.BP_Bottle05_C")},
    {TEXT("Bottle06"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle06/BP_Bottle06.BP_Bottle06_C")},
    {TEXT("Cup01"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup01.BP_Cup01_C")},
    {TEXT("Cup02"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup02.BP_Cup02_C")},
    {TEXT("Cup03"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup03.BP_Cup03_C")},
    {TEXT("Cup04"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup04.BP_Cup04_C")},
    {TEXT("Cup05"), TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup05.BP_Cup05_C")}
};

FString ProfilePath(const FVariant& V)
{
    if (FString(V.Label) == TEXT("Cup01")) return TEXT("/Game/VRTemplate/Breakage/DA_CupBreak.DA_CupBreak");
    if (FString(V.Label) == TEXT("Bottle04")) return TEXT("/Game/VRTemplate/Breakage/DA_MilkBottleBreak.DA_MilkBottleBreak");
    return FString::Printf(TEXT("/Game/VRTemplate/Breakage/Expanded/%s/DA_%sBreak.DA_%sBreak"), V.Label, V.Label, V.Label);
}

void FinishCompilation()
{
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
}

FString Quote(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\")); Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r")); Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return TEXT("\"") + Value + TEXT("\"");
}

// 不增加 Json 模块依赖；不存在的测量不填零、不捏造成功行。
struct FRow
{
    TArray<FString> Fields;
    void Text(const TCHAR* Key, const FString& Value) { Fields.Add(Quote(Key) + TEXT(":") + Quote(Value)); }
    void Number(const TCHAR* Key, double Value)
    {
        Fields.Add(Quote(Key) + TEXT(":") + (FMath::IsFinite(Value) ? FString::Printf(TEXT("%.17g"), Value) : FString(TEXT("null"))));
    }
    void Bool(const TCHAR* Key, bool Value) { Fields.Add(Quote(Key) + (Value ? TEXT(":true") : TEXT(":false"))); }
    FString Json() const { return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}"); }
};

bool SaveRows(FAutomationTestBase& Test, const TCHAR* FileName, const TCHAR* Group,
    const TCHAR* SampleKind, const TArray<FString>& Rows)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Workbench");
    if (!Test.TestTrue(TEXT("创建本次运行采样目录"), IFileManager::Get().MakeDirectory(*Directory, true))) return false;
    const FString Json = TEXT("{\"group\":") + Quote(Group) + TEXT(",\"sampleKind\":") + Quote(SampleKind)
        + TEXT(",\"actualSceneRun\":false,\"screenshotsCaptured\":false,\"rows\":[\n")
        + FString::Join(Rows, TEXT(",\n")) + TEXT("\n]}\n");
    return Test.TestTrue(TEXT("只写本组实际执行采样，不合并旧结果"), FFileHelper::SaveStringToFile(Json,
        *(Directory / FileName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

AGlassRecoveryActor* SavedManager(FAutomationTestBase& Test, UWorld*& Map)
{
    Map = LoadObject<UWorld>(nullptr, MapPath);
    if (!Test.TestNotNull(TEXT("只读加载保存地图，不启动原地图世界"), Map)
        || !Test.TestNotNull(TEXT("保存地图 PersistentLevel"), Map->PersistentLevel.Get())) return nullptr;
    AGlassRecoveryActor* Result = nullptr;
    int32 Count = 0;
    for (AActor* A : Map->PersistentLevel->Actors)
        if (AGlassRecoveryActor* M = Cast<AGlassRecoveryActor>(A))
        {
            ++Count;
            if (A->GetFName() == TEXT("GlassRecovery_Tutorial")) Result = M;
        }
    Test.TestEqual(TEXT("保存地图只有一个恢复经理"), Count, 1);
    Test.TestNotNull(TEXT("原 GlassRecovery_Tutorial 存在"), Result);
    return Result;
}

void ConfigureGrab(UGrabComponent* Grab)
{
    Grab->GrabType = EGrabType::Free; Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
    Grab->bSimulateOnDrop = false; Grab->bCaptureHand = false;
    Grab->HandAnimLayer = nullptr; Grab->OnGrabHapticEffect = nullptr;
}

void IsolateVessel(ALiquidContainerActor* A)
{
    // 热身只禁模拟/重力，保留实际容器网格和 QueryAndPhysics，绝不关闭掉落碰撞。
    A->ContainerMesh->SetSimulatePhysics(false); A->ContainerMesh->SetEnableGravity(false);
    A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    A->bCanPour = false; A->bEnableSplash = false;
    A->LiquidFXTemplate = nullptr; A->SplashEffectTemplate = nullptr;
    for (UNiagaraComponent* FX : {A->LiquidFX.Get(), A->PourFX.Get()})
        if (FX) { FX->SetAutoActivate(false); FX->SetAsset(nullptr); FX->DeactivateImmediate(); }
    ConfigureGrab(A->GrabComp.Get());
    if (ACupActor* Cup = Cast<ACupActor>(A))
    {
        Cup->VolumeWidgetClass = nullptr; Cup->VolumeWidgetComp->SetWidgetClass(nullptr); Cup->bFaceCamera = false;
    }
    if (ABottleActor* Bottle = Cast<ABottleActor>(A))
    {
        Bottle->PourHaptic = nullptr;
        Bottle->bStartCapped = true; // 保留保存的 CapClass，不用删盖绕过碰撞或业务清理。
    }
}

// 复制既有夹具的最小 GI/Context 生命周期；不包含其他测试 cpp，不安装或改写破碎组件。
struct FTestWorld
{
    UWorld* World;
    UGameInstance* GI;
    TArray<ALiquidContainerActor*> Vessels;
    int32 Frames = 0;
    explicit FTestWorld(bool bPhysics)
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(bPhysics);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine); Context.OwningGameInstance = GI;
        World->SetGameInstance(GI); GI->Init();
    }
    ~FTestWorld()
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    ALiquidContainerActor* Spawn(UClass* Class, const FVector& Location)
    {
        const auto* CDO = Class->GetDefaultObject<ALiquidContainerActor>();
        const FTransform Pose(FQuat::Identity, Location, CDO->GetActorScale3D());
        auto* A = World->SpawnActorDeferred<ALiquidContainerActor>(Class, Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!A) return nullptr;
        IsolateVessel(A); A->FinishSpawning(Pose); IsolateVessel(A);
        A->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
        Vessels.Add(A); return A;
    }
    UStaticMeshComponent* Floor()
    {
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!Cube || !Cube->GetBodySetup()) return nullptr;
        FinishCompilation(); Cube->GetBodySetup()->CreatePhysicsMeshes();
        AActor* A = World->SpawnActor<AActor>();
        if (!A) return nullptr;
        auto* Mesh = NewObject<UStaticMeshComponent>(A); A->SetRootComponent(Mesh); A->AddInstanceComponent(Mesh);
        Mesh->SetMobility(EComponentMobility::Static); Mesh->SetStaticMesh(Cube);
        Mesh->SetWorldScale3D(FVector(10, 10, .2)); Mesh->SetWorldLocation(FVector(0, 0, -10));
        Mesh->SetCollisionObjectType(ECC_WorldStatic); Mesh->SetCollisionResponseToAllChannels(ECR_Block);
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Mesh->SetSimulatePhysics(false);
        Mesh->SetEnableGravity(false); Mesh->RegisterComponent(); return Mesh;
    }
    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
        for (auto* A : Vessels) IsolateVessel(A);
    }
    void Tick(int32 Count = 1)
    {
        for (int32 I = 0; I < Count; ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, StepSeconds); ++Frames; }
    }
    ULiquidTransferSubsystem* Transfers() const { return World->GetSubsystem<ULiquidTransferSubsystem>(); }
};

struct FDebrisCounts { int32 Batches = 0, Fragments = 0, Simulated = 0; };
FDebrisCounts Debris(UWorld* World, const ALiquidContainerActor* Source, const UGlassBreakProfile* Profile)
{
    FDebrisCounts Counts;
    for (TActorIterator<AGlassDebrisActor> It(World); It; ++It)
    {
        if (It->SourceContainer.Get() != Source) continue;
        ++Counts.Batches;
        TArray<UStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
        for (const auto* Mesh : Meshes)
        {
            bool bShard = false;
            for (const auto& Spec : Profile->Shards) bShard |= Spec.Mesh && Spec.Mesh.Get() == Mesh->GetStaticMesh();
            if (bShard) { ++Counts.Fragments; if (Mesh->IsSimulatingPhysics()) ++Counts.Simulated; }
        }
    }
    return Counts; // Cylinder 湿迹不能算作玻璃碎片。
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchSavedAllProfilesTest,
    "CyberCafe.Workbench.SavedAllProfilesShapeAndRestoreMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkbenchSavedAllProfilesTest::RunTest(const FString&)
{
    TArray<FString> Rows;
    TSet<UGlassBreakProfile*> Profiles;
    TSet<UStaticMesh*> Shards;
    for (const FVariant& V : Variants)
    {
        UClass* Class = LoadClass<ALiquidContainerActor>(nullptr, V.ClassPath);
        UGlassBreakProfile* Profile = LoadObject<UGlassBreakProfile>(nullptr, *ProfilePath(V));
        if (!TestNotNull(FString(V.Label) + TEXT(" 保存生成类"), Class)
            || !TestNotNull(FString(V.Label) + TEXT(" 保存 profile"), Profile)) continue;
        const auto* CDO = Class->GetDefaultObject<ALiquidContainerActor>();
        TestNotNull(FString(V.Label) + TEXT(" BPCDO.GlassBreakProfile 非空"), CDO->GlassBreakProfile.Get());
        TestTrue(FString(V.Label) + TEXT(" CDO 引用对应保存 profile"), CDO->GlassBreakProfile.Get() == Profile);
        TestFalse(TEXT("每个型号使用独立 profile"), Profiles.Contains(Profile)); Profiles.Add(Profile);
        TestTrue(TEXT("实际杯瓶类而非原生占位"), Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
            && (Class->IsChildOf(ACupActor::StaticClass()) || Class->IsChildOf(ABottleActor::StaticClass())));
        TestNotNull(TEXT("保存原容器根网格非空"), CDO->ContainerMesh->GetStaticMesh().Get());
        TestTrue(TEXT("根组件仍为原容器网格组件"), CDO->GetRootComponent() == CDO->ContainerMesh.Get());
        TestTrue(TEXT("未使用基础 Cube 替换原杯瓶"), GetPathNameSafe(CDO->ContainerMesh->GetStaticMesh().Get()) != TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (FString(V.Label) == TEXT("Cup01")) TestEqual(TEXT("旧 Cup01 原网格未改"), GetPathNameSafe(CDO->ContainerMesh->GetStaticMesh().Get()),
            FString(TEXT("/Game/LiquidMaterials_VFXPack/Meshes/Glass05/SM_Glass_Glass_05.SM_Glass_Glass_05")));
        if (FString(V.Label) == TEXT("Bottle04")) TestEqual(TEXT("旧奶瓶原网格未改"), GetPathNameSafe(CDO->ContainerMesh->GetStaticMesh().Get()),
            FString(TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle04/SM_Bottle04.SM_Bottle04")));
        TestFalse(TEXT("保存 CDO 没有冻结"), CDO->IsLiquidFrozen());
        TestEqual(TEXT("保存八块玻璃"), Profile->Shards.Num(), 8);
        TestEqual(TEXT("保存八块物理预算"), Profile->MaxPhysicsShards, 8);
        TestTrue(TEXT("保存破碎阈值为有限正数"), FMath::IsFinite(Profile->BreakSpeedCms) && Profile->BreakSpeedCms > 0);
        FinishCompilation();
        for (const FGlassShardSpec& Spec : Profile->Shards)
        {
            UStaticMesh* Mesh = Spec.Mesh.Get();
            if (!TestNotNull(TEXT("保存碎片实际网格"), Mesh)) continue;
            TestFalse(TEXT("八十八块保存碎片无重复引用"), Shards.Contains(Mesh)); Shards.Add(Mesh);
            TestTrue(TEXT("碎片局部变换有效"), Spec.LocalTransform.IsValid());
            TestTrue(TEXT("碎片有真实 LOD0 几何"), Mesh->GetNumLODs() > 0 && Mesh->GetNumVertices(0) > 6 && Mesh->GetNumTriangles(0) > 0);
            const FBox Box = Mesh->GetBoundingBox();
            TestTrue(TEXT("碎片三维边界有效"), Box.IsValid && !Box.Min.ContainsNaN() && !Box.Max.ContainsNaN() && Box.GetSize().GetMin() > KINDA_SMALL_NUMBER);
            UBodySetup* Body = Mesh->GetBodySetup();
            if (!TestNotNull(TEXT("碎片 BodySetup 有效"), Body)) continue;
            TestTrue(TEXT("碎片简单碰撞有凸体"), Body->AggGeom.ConvexElems.Num() > 0);
            TestTrue(TEXT("碎片可模拟，不用 ComplexAsSimple"), Body->CollisionTraceFlag != CTF_UseComplexAsSimple);
            for (const FKConvexElem& Hull : Body->AggGeom.ConvexElems)
            {
                const FBox HullBox(Hull.VertexData);
                TestTrue(TEXT("每个凸体至少四点且三维有效"), Hull.VertexData.Num() >= 4 && HullBox.IsValid
                    && !HullBox.Min.ContainsNaN() && !HullBox.Max.ContainsNaN() && HullBox.GetSize().GetMin() > KINDA_SMALL_NUMBER);
            }
            FRow Row; Row.Text(TEXT("model"), V.Label); Row.Text(TEXT("mesh"), Mesh->GetPathName());
            Row.Number(TEXT("convexCount"), Body->AggGeom.ConvexElems.Num()); Rows.Add(Row.Json());
        }
    }
    TestEqual(TEXT("实际发现全十一份保存 profile"), Profiles.Num(), 11);
    TestEqual(TEXT("实际发现八十八块独立保存碎片"), Shards.Num(), 88);
    UWorld* Map = nullptr; auto* Manager = SavedManager(*this, Map);
    if (!Manager) return false;
    const bool bBegunBefore = Map->HasBegunPlay();
    TestTrue(TEXT("保存经理使用统一恢复模式"), Manager->bUnifiedRestore);
    TestEqual(TEXT("保存 RestoreTargets 六项"), Manager->RestoreTargets.Num(), 6);
    TestEqual(TEXT("保存统一长按一秒"), Manager->HoldSeconds, 1.f);
    TestNotNull(TEXT("保存经理绑定原冰铲"), Manager->RestoreScoop.Get());
    TestTrue(TEXT("原经理仍为原生类"), Manager->GetClass() == AGlassRecoveryActor::StaticClass());
    TSet<ALiquidContainerActor*> Targets;
    for (const auto& Target : Manager->RestoreTargets)
    {
        if (!TestNotNull(TEXT("六项保存目标引用有效"), Target.Get())) continue;
        TestTrue(TEXT("目标仍归属原 PersistentLevel"), Target->GetLevel() == Map->PersistentLevel.Get());
        Targets.Add(Target.Get());
    }
    TestEqual(TEXT("六个保存目标互不重复"), Targets.Num(), 6);
    TestTrue(TEXT("六项包括原杯与奶瓶"), Targets.Contains(Manager->PrototypeCup.Get()) && Targets.Contains(Manager->PrototypeBottle.Get()));
    TestEqual(TEXT("原杯身份保留"), GetNameSafe(Manager->PrototypeCup.Get()), FString(TEXT("BP_Cup01_C_1")));
    TestEqual(TEXT("原奶瓶身份保留"), GetNameSafe(Manager->PrototypeBottle.Get()), FString(TEXT("BP_Bottle04_Milk_Tutorial")));
    TestEqual(TEXT("原杯 profile 保留"), GetPathNameSafe(Manager->CupProfile.Get()), ProfilePath(Variants[6]));
    TestEqual(TEXT("奶瓶 profile 保留"), GetPathNameSafe(Manager->BottleProfile.Get()), ProfilePath(Variants[3]));
    ATutorialDirector* Director = nullptr; AOrderHologramActor* Hologram = nullptr; AIceStationActor* Station = nullptr;
    int32 Directors = 0, Holograms = 0, Stations = 0, Grinders = 0;
    for (AActor* A : Map->PersistentLevel->Actors)
    {
        if (!A) continue;
        if (auto* D = Cast<ATutorialDirector>(A)) { ++Directors; if (A->GetFName() == TEXT("BP_TutorialDirecter_C_0")) Director = D; }
        if (auto* H = Cast<AOrderHologramActor>(A)) { ++Holograms; if (A->GetFName() == TEXT("BP_OrderHologram_Tutorial")) Hologram = H; }
        if (auto* S = Cast<AIceStationActor>(A)) { ++Stations; if (A->GetFName() == TEXT("IceStation_Practice")) Station = S; }
        if (Cast<ACoffeeGrinderActor>(A) || A->GetClass()->GetName().Contains(TEXT("BeanGrinder"))) ++Grinders;
    }
    if (!TestNotNull(TEXT("原六步导演存在"), Director) || !TestNotNull(TEXT("原全息实例存在"), Hologram)
        || !TestNotNull(TEXT("原冰站存在"), Station)) return false;
    TestEqual(TEXT("原导演唯一"), Directors, 1); TestEqual(TEXT("原全息唯一"), Holograms, 1); TestEqual(TEXT("冰站唯一"), Stations, 1);
    TestTrue(TEXT("BeanGrinder 仍存在"), Grinders > 0);
    TestEqual(TEXT("旧步骤仍为六步"), Director->Steps.Num(), 6);
    const TCHAR* Names[] = {TEXT("TutorialStep_ShowMessage_0"), TEXT("TutorialStep_ShowMessage_1"), TEXT("TutorialStep_ShowMessage_2"),
        TEXT("TutorialStep_ShowMessage_3"), TEXT("TutorialStep_WaitForServe_0"), TEXT("TutorialStep_ConsumeServedDrink_0")};
    for (int32 I = 0; I < 6 && Director->Steps.IsValidIndex(I); ++I)
    {
        auto* Step = Director->Steps[I].Get();
        if (!TestNotNull(TEXT("旧步骤内联对象有效"), Step)) continue;
        TestEqual(TEXT("旧步骤名称和顺序保留"), Step->GetName(), FString(Names[I]));
        TestTrue(TEXT("旧步骤仍归属原导演"), Step->GetOuter() == Director);
        const FString Type = I < 4 ? TEXT("TutorialStep_ShowMessage") : (I == 4 ? TEXT("TutorialStep_WaitForServe") : TEXT("TutorialStep_ConsumeServedDrink"));
        TestEqual(TEXT("旧步骤类型保留"), Step->GetClass()->GetPathName(), TEXT("/Script/CyberCafe.") + Type);
    }
    auto* Wait = Director->Steps.IsValidIndex(4) ? Cast<UTutorialStep_WaitForServe>(Director->Steps[4].Get()) : nullptr;
    if (TestNotNull(TEXT("原上菜步骤"), Wait))
    {
        TestTrue(TEXT("旧全息引用保持"), Wait->OrderHologram.Get() == Hologram);
        TestEqual(TEXT("原拿铁配方引用保持"), GetPathNameSafe(Wait->RequiredRecipe.Get()), FString(TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
        TestEqual(TEXT("原上菜点引用保持"), GetPathNameSafe(Wait->RequiredSpot.Get()), FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_ServingSpot_C_1")));
    }
    TestTrue(TEXT("冰站仍引用经理冰铲"), Station->Scoop.Get() == Manager->RestoreScoop.Get());
    TestNotNull(TEXT("原冰桶引用存在"), Station->Bucket.Get()); TestNotNull(TEXT("原练习杯引用存在"), Station->Cup.Get());
    if (Station->Cup) TestEqual(TEXT("原冰站练习杯仍为 Cup03"), Station->Cup->GetClass()->GetPathName(), FString(Variants[8].ClassPath));
    TestEqual(TEXT("只读加载没有启动或推进原地图世界"), Map->HasBegunPlay(), bBegunBefore);
    // 全部旧文本、变换及配置的 prevbaselineHash 差分由外部主流程复核，不伪造基线。
    FRow Row; Row.Text(TEXT("map"), Map->GetPathName()); Row.Text(TEXT("manager"), Manager->GetPathName());
    Row.Number(TEXT("targetCount"), Targets.Num()); Row.Number(TEXT("oldStepCount"), Director->Steps.Num()); Rows.Add(Row.Json());
    return SaveRows(*this, TEXT("saved_profiles_map.json"), TEXT("SavedAllProfilesShapeAndRestoreMap"), TEXT("readOnlySavedAssets"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchAllSavedImpactTest,
    "CyberCafe.Workbench.AllSavedVariantsActualImpact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkbenchAllSavedImpactTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实物理世界需要 Engine"), GEngine)) return false;
    TArray<FString> Rows; int32 Measured = 0, Broken = 0;
    for (const FVariant& V : Variants)
    {
        FRow Row; Row.Text(TEXT("group"), TEXT("PhysicsDropGroup")); Row.Text(TEXT("model"), V.Label);
        UClass* Class = LoadClass<ALiquidContainerActor>(nullptr, V.ClassPath);
        if (!TestNotNull(FString(V.Label) + TEXT(" 加载真实 BP"), Class))
        { Row.Text(TEXT("stage"), TEXT("classLoadFailed")); Rows.Add(Row.Json()); continue; }
        const auto* CDO = Class->GetDefaultObject<ALiquidContainerActor>();
        UGlassBreakProfile* Profile = CDO->GlassBreakProfile.Get();
        if (!TestNotNull(FString(V.Label) + TEXT(" CDO 自动安装所需保存 profile"), Profile))
        { Row.Text(TEXT("stage"), TEXT("savedProfileMissing")); Rows.Add(Row.Json()); continue; }
        TestEqual(FString(V.Label) + TEXT(" 使用指定保存 profile 路径"), Profile->GetPathName(), ProfilePath(V));
        TestEqual(TEXT("掉落组独立核对保存八片配置"), Profile->Shards.Num(), 8);
        FinishCompilation();
        FTestWorld T(true); // 每个型号独立 GI、Context、Chaos Scene、账本，规避六批碎片预算互相淘汰。
        UStaticMeshComponent* Floor = T.Floor();
        ALiquidContainerActor* A = T.Spawn(Class, FVector(0, 0, 120));
        if (!TestNotNull(TEXT("真实静态地板"), Floor) || !TestNotNull(TEXT("真实保存杯瓶实例"), A))
        { Row.Text(TEXT("stage"), TEXT("spawnFailed")); Rows.Add(Row.Json()); continue; }
        T.BeginPlay();
        auto* Comp = A->FindComponentByClass<UGlassBreakableComponent>();
        auto* S = T.Transfers();
        if (!TestNotNull(FString(V.Label) + TEXT(" BeginPlay 自动安装组件，测试未手工添加"), Comp)
            || !TestNotNull(TEXT("真实世界账本"), S))
        { Row.Text(TEXT("stage"), TEXT("automaticComponentOrLedgerMissing")); Rows.Add(Row.Json()); continue; }
        TArray<UGlassBreakableComponent*> Components; A->GetComponents(Components);
        TestEqual(TEXT("恰好一个自动组件"), Components.Num(), 1);
        TestTrue(TEXT("自动组件绑定本 BP 保存 profile"), Comp->Profile.Get() == Profile && A->GlassBreakProfile.Get() == Profile);
        TestTrue(TEXT("组件已注册并正常 BeginPlay"), Comp->IsRegistered() && Comp->HasBegunPlay() && Comp->IsComponentTickEnabled());
        TestEqual(TEXT("保留默认六百毫秒启动保护"), Comp->StartupGraceSeconds, .6f);
        TestTrue(TEXT("正常绑定生产 OnComponentHit"), A->ContainerMesh->OnComponentHit.Contains(Comp, FName(TEXT("HandleHit"))));
        TestTrue(TEXT("生产 BeginPlay 已启用 CCD"), A->ContainerMesh->GetBodyInstance() && A->ContainerMesh->GetBodyInstance()->bUseCCD);
        TestTrue(TEXT("真实 GI 与事件系统"), T.World->GetGameInstance() == T.GI && T.GI->GetSubsystem<UCyberCafeEventSubsystem>() != nullptr);
        TestTrue(TEXT("真实 Chaos 模拟及向下重力"), T.World->bShouldSimulatePhysics && T.World->GetPhysicsScene() && T.World->GetGravityZ() < 0);
        TestTrue(TEXT("保留 BP 原容器网格、容量和初始原料"), A->ContainerMesh->GetStaticMesh() == CDO->ContainerMesh->GetStaticMesh()
            && A->MaxVolumeML == CDO->MaxVolumeML && A->InitialIngredientId == CDO->InitialIngredientId);
        TestTrue(TEXT("平台静态且顶面为零"), Floor->GetCollisionObjectType() == ECC_WorldStatic && !Floor->IsSimulatingPhysics()
            && FMath::IsNearlyZero(Floor->GetComponentLocation().Z + Floor->GetStaticMesh()->GetBoundingBox().Max.Z * Floor->GetComponentScale().Z, .001));
        TestTrue(TEXT("原容器仍阻挡静态世界"), A->ContainerMesh->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
        TestFalse(TEXT("未持有、未拉取、未冻结、无自动倒液"), A->GrabComp->IsHeld() || A->GrabComp->IsPulled() || A->IsLiquidFrozen() || A->bCanPour);
        T.Tick(100); // .01 x 100 正常世界 Tick，超过 .6 秒启动保护；此时仍不模拟物理。
        TestFalse(TEXT("热身未抢跑破碎"), A->IsBroken());
        TestTrue(TEXT("热身仍保持 Z120"), FMath::IsNearlyEqual(A->GetActorLocation().Z, 120.0, .001));
        const TWeakObjectPtr<ABottleCapActor> Cap(Cast<ABottleActor>(A) ? Cast<ABottleActor>(A)->CapActor.Get() : nullptr);
        if (auto* Bottle = Cast<ABottleActor>(A))
        {
            TestTrue(TEXT("保存盖类及初始封口保留"), Bottle->CapClass == CDO->GetClass()->GetDefaultObject<ABottleActor>()->CapClass && Bottle->bStartCapped);
            if (Bottle->CapClass) TestNotNull(TEXT("实际原盖存在，未删盖绕过掉落"), Cap.Get());
            if (Cap.IsValid()) TestTrue(TEXT("原盖附着且未持有，旧弹盖 Hit 路径不会触发"), Cap->bIsAttached && !Cap->GrabComp->IsHeld());
        }
        const FLiquidBalance Before = S->GetBalance(); const double Volume = A->GetPreciseVolumeML();
        UStaticMesh* Mesh = A->ContainerMesh->GetStaticMesh();
        if (!TestNotNull(TEXT("实际源网格"), Mesh) || !TestNotNull(TEXT("实际源 BodySetup"), Mesh ? Mesh->GetBodySetup() : nullptr))
        { Row.Text(TEXT("stage"), TEXT("bodyMissing")); Rows.Add(Row.Json()); continue; }
        FinishCompilation(); Mesh->GetBodySetup()->CreatePhysicsMeshes();
        A->ContainerMesh->SetMobility(EComponentMobility::Movable); A->SetActorEnableCollision(true);
        A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); A->ContainerMesh->RecreatePhysicsState();
        TestTrue(TEXT("重新创建实际有效刚体"), A->ContainerMesh->GetBodyInstance() && A->ContainerMesh->GetBodyInstance()->IsValidBodyInstance());
        A->ContainerMesh->SetSimulatePhysics(true); A->ContainerMesh->SetEnableGravity(true);
        A->ContainerMesh->SetPhysicsLinearVelocity(FVector(0, 0, -400), false);
        A->ContainerMesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector, false); A->ContainerMesh->WakeAllRigidBodies();
        TestTrue(TEXT("源网格真的开启模拟与重力"), A->ContainerMesh->IsSimulatingPhysics() && A->ContainerMesh->IsGravityEnabled());
        const FVector StartVelocity = A->ContainerMesh->GetPhysicsLinearVelocity();
        TestTrue(TEXT("实际初速度为 -400 cm/s"), FMath::IsNearlyEqual(StartVelocity.Z, -400.0, .1));
        int32 DropTicks = 0; double MinZ = A->GetActorLocation().Z; FVector LastVelocity = StartVelocity;
        while (DropTicks < 200 && !A->IsBroken())
        {
            LastVelocity = A->ContainerMesh->GetPhysicsLinearVelocity();
            T.Tick(); ++DropTicks; MinZ = FMath::Min(MinZ, A->GetActorLocation().Z);
        } // 不移动掉落中的容器，不调用 TryImpact/业务破碎，不广播 Hit，不手工 Tick 组件。
        ++Measured; if (A->IsBroken()) ++Broken;
        const FDebrisCounts D = Debris(T.World, A, Profile); const FLiquidBalance After = S->GetBalance();
        TestTrue(FString(V.Label) + TEXT(" 两百真实帧内因实际掉落破碎"), A->IsBroken());
        TestTrue(TEXT("物理实际造成 Z 下降而非仅推进 Age"), MinZ < 119.0);
        TestEqual(TEXT("本容器只有一批碎片"), D.Batches, 1); TestEqual(TEXT("实际玻璃碎片八块"), D.Fragments, 8);
        TestEqual(TEXT("八块实际开启物理"), D.Simulated, 8);
        TestFalse(TEXT("破碎无手持或拉取残留"), A->GrabComp->IsHeld() || A->GrabComp->IsPulled());
        TestNull(TEXT("破碎控制器引用为空"), A->GrabComp->GetHoldingController());
        TestNull(TEXT("破碎主抓取引用为空"), A->GrabComp->PrimaryGrabComponent.Get());
        TestFalse(TEXT("破碎停止容器显示和物理"), A->ContainerMesh->IsVisible() || A->ContainerMesh->IsSimulatingPhysics());
        TestFalse(TEXT("破碎停止采样"), Comp->IsComponentTickEnabled());
        TestEqual(TEXT("无无关液体在途包"), After.PacketCount, Before.PacketCount);
        TestEqual(TEXT("无额外注入"), After.InjectedML, Before.InjectedML); TestEqual(TEXT("无额外消耗"), After.RemovedML, Before.RemovedML);
        TestTrue(TEXT("破碎结算等于掉落前实际余液"), FMath::Abs((After.SpilledML - Before.SpilledML) - Volume) < VolumeTolerance);
        TestTrue(TEXT("实际守恒误差小于 1e-6 mL"), FMath::IsFinite(After.ErrorML) && FMath::Abs(After.ErrorML) < VolumeTolerance);
        if (auto* Bottle = Cast<ABottleActor>(A))
        {
            TestNull(TEXT("碎瓶清空盖引用"), Bottle->CapActor.Get()); TestFalse(TEXT("碎瓶解开封口标记"), Bottle->bIsCapped);
            if (Cap.IsValid() && !Cap->IsActorBeingDestroyed())
            { TestNull(TEXT("残存盖无旧碎瓶归属"), Cap->OwnerBottle.Get()); TestFalse(TEXT("残存盖未持有"), Cap->GrabComp->IsHeld()); }
        }
        Row.Text(TEXT("stage"), TEXT("actualChaosDropMeasured")); Row.Text(TEXT("class"), Class->GetPathName());
        Row.Text(TEXT("sourceMesh"), Mesh->GetPathName()); Row.Text(TEXT("profile"), Profile->GetPathName());
        Row.Bool(TEXT("broken"), A->IsBroken()); Row.Number(TEXT("fragments"), D.Fragments); Row.Number(TEXT("simulatedFragments"), D.Simulated);
        Row.Bool(TEXT("held"), A->GrabComp->IsHeld()); Row.Bool(TEXT("pulled"), A->GrabComp->IsPulled()); Row.Bool(TEXT("frozen"), A->IsLiquidFrozen());
        Row.Bool(TEXT("holdingReferenceCleared"), A->GrabComp->GetHoldingController() == nullptr);
        Row.Number(TEXT("error"), After.ErrorML); Row.Number(TEXT("volumeActualML"), Volume);
        Row.Number(TEXT("lastSpilledVolumeML"), After.SpilledML - Before.SpilledML);
        Row.Number(TEXT("classInitialVolumeML"), CDO->GetPreciseVolumeML()); Row.Number(TEXT("thresholdCms"), Profile->BreakSpeedCms);
        Row.Number(TEXT("dropTicks"), DropTicks); Row.Number(TEXT("dtSeconds"), StepSeconds); Row.Number(TEXT("minZCM"), MinZ);
        Row.Number(TEXT("initialVelocityZCms"), StartVelocity.Z); Row.Number(TEXT("lastPreBreakVelocityZCms"), LastVelocity.Z);
        Rows.Add(Row.Json());
        const int32 Revision = A->GetLiquidRevision(); const double Spilled = After.SpilledML;
        T.Tick(20);
        TestEqual(TEXT("后续 Tick 不重复洒出"), S->GetBalance().SpilledML, Spilled);
        TestEqual(TEXT("后续 Tick 不重复破碎推进 revision"), A->GetLiquidRevision(), Revision);
        TestEqual(TEXT("后续 Tick 不重复生成碎片"), Debris(T.World, A, Profile).Batches, 1);
    }
    TestEqual(TEXT("全十一型号都有真实掉落测量"), Measured, 11);
    TestEqual(TEXT("全十一型号实际破碎"), Broken, 11);
    return SaveRows(*this, TEXT("all_variants.json"), TEXT("AllSavedVariantsActualImpact"), TEXT("independentChaosPhysicsDropWorlds"), Rows);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchSavedRestoreIntegrationTest,
    "CyberCafe.Workbench.SavedMapWorkbenchRestoreIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkbenchSavedRestoreIntegrationTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("独立集成世界需要 Engine"), GEngine)) return false;
    UWorld* Map = nullptr; auto* Saved = SavedManager(*this, Map);
    if (!Saved) return false;
    TArray<ALiquidContainerActor*> Originals;
    if (Saved->PrototypeCup) Originals.AddUnique(Saved->PrototypeCup.Get());
    if (Saved->PrototypeBottle) Originals.AddUnique(Saved->PrototypeBottle.Get());
    for (const auto& A : Saved->RestoreTargets) if (A) Originals.AddUnique(A.Get());
    if (!TestEqual(TEXT("保存原型及目标去重后六槽"), Originals.Num(), 6)) return false;
    FTestWorld T(false); TArray<ALiquidContainerActor*> Old; TArray<FTransform> Poses;
    TArray<double> InitialVolumes; TArray<UClass*> Classes; TArray<UGlassBreakProfile*> Profiles;
    int32 Cup03Slot = INDEX_NONE, Bottle01Slot = INDEX_NONE;
    for (int32 I = 0; I < Originals.Num(); ++I)
    {
        UClass* Class = Originals[I]->GetClass();
        auto* A = T.Spawn(Class, FVector(50 + I * 200, 0, 120));
        if (!TestNotNull(TEXT("按保存目标类生成真实 BP，不使用 Cube 替代杯瓶"), A)) return false;
        Old.Add(A); Poses.Add(A->GetActorTransform()); Classes.Add(Class);
        Profiles.Add(Class->GetDefaultObject<ALiquidContainerActor>()->GlassBreakProfile.Get());
        if (Class->GetPathName() == Variants[8].ClassPath) Cup03Slot = I;
        if (Class->GetPathName() == Variants[0].ClassPath) Bottle01Slot = I;
    }
    if (!TestTrue(TEXT("保存六目标包含 Cup03 和 Bottle01"), Cup03Slot != INDEX_NONE && Bottle01Slot != INDEX_NONE)) return false;
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("仅原生冰铲夹具使用基础 Cube"), Cube)) return false;
    const FTransform ScoopPose(FQuat::Identity, FVector(1500, 0, 120), FVector(.06));
    auto* Scoop = T.World->SpawnActorDeferred<AIceScoopActor>(AIceScoopActor::StaticClass(), ScoopPose,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
    if (!TestNotNull(TEXT("原生冰铲夹具"), Scoop)) return false;
    Scoop->Mesh->SetStaticMesh(Cube); Scoop->ScoopSound = nullptr; ConfigureGrab(Scoop->Grab.Get()); Scoop->FinishSpawning(ScoopPose);
    auto* Station = T.World->SpawnActor<AIceStationActor>();
    auto* Manager = T.World->SpawnActor<AGlassRecoveryActor>();
    if (!TestNotNull(TEXT("原生冰站夹具"), Station) || !TestNotNull(TEXT("原生统一经理"), Manager)) return false;
    Station->Cup = Cast<ACupActor>(Old[Cup03Slot]); Station->Scoop = Scoop; Station->ClinkSound = nullptr;
    Station->CupInteriorRadiusCM = 4.1f; Station->CupInteriorDepthCM = 8.8f;
    Manager->bUnifiedRestore = true; Manager->HoldSeconds = Saved->HoldSeconds; Manager->RestoreScoop = Scoop;
    Manager->BindPrototype(Old[0], Old[1], Profiles[0], Profiles[1]);
    for (auto* A : Old) Manager->RestoreTargets.Add(A);
    T.BeginPlay(); Scoop->Mesh->SetSimulatePhysics(false); Scoop->Mesh->SetEnableGravity(false); ConfigureGrab(Scoop->Grab.Get());
    auto* S = T.Transfers(); if (!TestNotNull(TEXT("集成世界真实液体账本"), S)) return false;
    TestEqual(TEXT("原经理捕获六槽"), Manager->GetRestoreSlotCount(), 6);
    for (int32 I = 0; I < Old.Num(); ++I)
    {
        TestTrue(TEXT("初始槽位顺序与保存原型/目标去重一致"), Manager->GetCurrent(I) == Old[I]);
        InitialVolumes.Add(Old[I]->GetPreciseVolumeML());
        auto* Comp = Old[I]->FindComponentByClass<UGlassBreakableComponent>();
        if (!TestNotNull(TEXT("真实 BP 组件已安装"), Comp) || !TestNotNull(TEXT("保存目标 profile 有效"), Profiles[I])) return false;
        TestTrue(TEXT("组件使用各自保存 profile"), Comp->Profile.Get() == Profiles[I]);
    }
    auto* Cup03 = Cast<ACupActor>(Old[Cup03Slot]); auto* CupIce = Cup03->FindComponentByClass<UCupIceComponent>();
    if (!TestNotNull(TEXT("冰站正常安装杯冰组件"), CupIce)) return false;
    TestTrue(TEXT("杯冰组件绑定真实 Cup03"), CupIce->GetCup() == Cup03);
    TestEqual(TEXT("消耗实际 Bottle01 十毫升"), Old[Bottle01Slot]->ConsumeLiquid(10.f), 10.f);
    TestEqual(TEXT("旧 Cup03 实际注入二十毫升"), Cup03->AddIngredientLiquid(20.f, TEXT("Milk"), FLinearColor::White), 20.f);
    const FVector Mouth = Old[0]->PourEntryPoint->GetComponentLocation();
    TestEqual(TEXT("Bottle01 向旧杯发射真实延迟尾流"), S->EmitVolume(Old[Bottle01Slot], 5.0,
        Mouth + FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector, .5), 5.0);
    // 本组明确验证业务守卫与恢复，公开业务 API 不冒充上组的实际物理掉落。
    TestTrue(TEXT("Cup03 公开业务破碎成立"), Cup03->BreakContainerForImpact());
    TestFalse(TEXT("已破碎 Cup03 重复业务破碎拒绝"), Cup03->BreakContainerForImpact());
    Old[0]->SetActorLocation(Poses[0].GetLocation() + FVector(0, 50, 0));
    Scoop->SetActorLocation(ScoopPose.GetLocation() + FVector(0, 50, 0));
    const auto* GrabIdentity = Scoop->Grab.Get(); const auto IceClass = Scoop->IceClass;
    const FLiquidBalance Before = S->GetBalance();
    double RemovedExpected = 0, InjectedExpected = 0;
    for (int32 I = 0; I < Old.Num(); ++I)
    {
        RemovedExpected += Old[I]->GetPreciseVolumeML();
        if (Cast<ABottleActor>(Old[I])) InjectedExpected += InitialVolumes[I];
    }
    if (!TestTrue(TEXT("保存类、profile 与六槽真实统一恢复一次"), Manager->RestoreWorkbench())) return false;
    TestEqual(TEXT("成功只推进一代"), Manager->GetRestoreGeneration(), 1); TestEqual(TEXT("恢复仍为六槽"), Manager->GetRestoreSlotCount(), 6);
    TSet<ALiquidContainerActor*> CurrentRefs; TArray<FString> Rows;
    for (int32 I = 0; I < Old.Num(); ++I)
    {
        auto* A = Manager->GetCurrent(I); if (!TestNotNull(TEXT("恢复六个有效当前引用"), A)) return false;
        CurrentRefs.Add(A);
        TestTrue(TEXT("新身份保持保存 BP 类且回捕获供给位置"), A != Old[I] && A->GetClass() == Classes[I] && A->GetActorTransform().Equals(Poses[I]));
        auto* Comp = A->FindComponentByClass<UGlassBreakableComponent>();
        TestTrue(TEXT("新供给仍绑定原保存 profile"), Comp && Comp->Profile.Get() == Profiles[I] && A->GlassBreakProfile.Get() == Profiles[I]);
        TestTrue(TEXT("新供给未破碎、未冻结、可见可抓"), !A->IsBroken() && !A->IsLiquidFrozen() && A->ContainerMesh->IsVisible() && A->GrabComp->IsActive());
        TestTrue(TEXT("新供给没有持有悬挂引用"), !A->GrabComp->IsHeld() && !A->GrabComp->IsPulled() && !A->GrabComp->GetHoldingController());
        const double Expected = Cast<ACupActor>(A) ? 0.0 : InitialVolumes[I];
        TestTrue(TEXT("杯归零、瓶恢复初始权威液量"), FMath::Abs(A->GetPreciseVolumeML() - Expected) < VolumeTolerance);
        TestTrue(TEXT("槽位引用与 RestoreTargets 同步迁移"), Manager->RestoreTargets[I].Get() == A);
        FRow Row; Row.Number(TEXT("slot"), I); Row.Text(TEXT("class"), A->GetClass()->GetPathName());
        Row.Text(TEXT("profile"), GetPathNameSafe(Comp ? Comp->Profile.Get() : nullptr)); Row.Number(TEXT("volumeActualML"), A->GetPreciseVolumeML());
        Row.Number(TEXT("xCM"), A->GetActorLocation().X); Row.Number(TEXT("yCM"), A->GetActorLocation().Y); Rows.Add(Row.Json());
        IsolateVessel(A); // 先断言业务结果，再固定无地板的集成夹具；不改变恢复身份或液体结果。
    }
    TestEqual(TEXT("六个新当前身份互不重复"), CurrentRefs.Num(), 6);
    TestTrue(TEXT("原型公开引用迁移"), Manager->PrototypeCup.Get() == Manager->GetCurrent(0) && Manager->PrototypeBottle.Get() == Manager->GetCurrent(1));
    auto* NewCup03 = Cast<ACupActor>(Manager->GetCurrent(Cup03Slot));
    if (!TestNotNull(TEXT("恢复后的 Cup03 类型有效"), NewCup03)) return false;
    auto* NewIce = NewCup03->FindComponentByClass<UCupIceComponent>();
    TestTrue(TEXT("冰站引用与新杯冰绑定迁移"), Station->Cup.Get() == NewCup03 && NewIce && NewIce->GetCup() == NewCup03);
    if (NewIce) { TestEqual(TEXT("杯冰半径保留"), NewIce->InteriorRadiusCM, 4.1f); TestEqual(TEXT("杯冰深度保留"), NewIce->InteriorDepthCM, 8.8f); TestEqual(TEXT("新杯没有遗留冰"), NewIce->GetIceCount(), 0); }
    TestTrue(TEXT("冰铲及 Grab 身份不变且返回捕获位姿"), Manager->RestoreScoop.Get() == Scoop && Scoop->Grab.Get() == GrabIdentity && Scoop->GetActorTransform().Equals(ScoopPose));
    TestTrue(TEXT("冰铲冰类未改"), Scoop->IceClass == IceClass); TestEqual(TEXT("冰铲装载归零"), Scoop->GetLoadedCount(), 0);
    const FLiquidBalance After = S->GetBalance();
    TestEqual(TEXT("历史 Initial 不重置"), After.InitialML, Before.InitialML);
    TestTrue(TEXT("仅瓶初始供给重新注入"), FMath::Abs(After.InjectedML - Before.InjectedML - InjectedExpected) < VolumeTolerance);
    TestTrue(TEXT("旧完整容器余液只移除一次"), FMath::Abs(After.RemovedML - Before.RemovedML - RemovedExpected) < VolumeTolerance);
    TestTrue(TEXT("相关旧尾流五毫升只洒一次"), FMath::Abs(After.SpilledML - Before.SpilledML - 5.0) < VolumeTolerance);
    TestEqual(TEXT("恢复无旧尾流"), After.PacketCount, 0);
    TestTrue(TEXT("保存资产类集成账本闭合"), FMath::IsFinite(After.ErrorML) && FMath::Abs(After.ErrorML) < VolumeTolerance);
    // 同一保存杯类的 Frozen 保护，仅验证公开业务守卫，不声称实际掉落。
    auto* Frozen = T.Spawn(Classes[0], FVector(50, 300, 120));
    if (!TestNotNull(TEXT("保存 Cup01 冻结守卫实例"), Frozen)) return false;
    TestEqual(TEXT("冻结前实际加入二十毫升，避免仅验证空杯账本"), Frozen->AddIngredientLiquid(20.f, TEXT("Milk"), FLinearColor::White), 20.f);
    Frozen->FreezeLiquidForServing(); const double FrozenVolume = Frozen->GetPreciseVolumeML(); const int32 FrozenRevision = Frozen->GetLiquidRevision();
    const FLiquidBalance FrozenBefore = S->GetBalance();
    TestFalse(TEXT("Frozen 拒绝公开业务破碎"), Frozen->BreakContainerForImpact());
    TestFalse(TEXT("Frozen 没有变成 broken"), Frozen->IsBroken()); TestTrue(TEXT("Frozen 仍被冻结"), Frozen->IsLiquidFrozen());
    TestEqual(TEXT("Frozen 余液不变"), Frozen->GetPreciseVolumeML(), FrozenVolume); TestEqual(TEXT("Frozen revision 不变"), Frozen->GetLiquidRevision(), FrozenRevision);
    TestEqual(TEXT("Frozen 拒绝不修改洒出账本"), S->GetBalance().SpilledML, FrozenBefore.SpilledML);
    return SaveRows(*this, TEXT("saved_restore_integration.json"), TEXT("SavedMapWorkbenchRestoreIntegration"), TEXT("independentWorldSavedClassPublicBusinessAPI"), Rows);
}
}
#endif // WITH_DEV_AUTOMATION_TESTS
