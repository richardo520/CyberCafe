// @trace IC-01 IC-02 IC-03 IC-04
// 保存地图只读发现；独立 Game World 中由 Chaos 与正常 World Tick 完成装冰、倾倒、穿口。
// JSON 只在实际运行时生成，不将 NullRHI 采样称为画面或 VR 硬件验收。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Liquid/IceInteraction.h"
#include "Liquid/CupActor.h"
#include "Liquid/BottleActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
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
#include "StaticMeshResources.h"
#include "NiagaraComponent.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

namespace IceAssetRuntimeTests
{
constexpr float StepSeconds = .01f;
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
const TCHAR* CubePath = TEXT("/Game/VRTemplate/Ice/Blueprints/BP_IceCube.BP_IceCube_C");
const TCHAR* ScoopPath = TEXT("/Game/VRTemplate/Ice/Blueprints/BP_IceScoop.BP_IceScoop_C");
const TCHAR* BucketPath = TEXT("/Game/VRTemplate/Ice/Blueprints/BP_IceBucket.BP_IceBucket_C");
const TCHAR* CupPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup03.BP_Cup03_C");
const TCHAR* PrimaryCupClassPath = TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C");
const TCHAR* PrimaryCupMeshPath = TEXT("/Game/VRTemplate/MenuIngredients/Meshes/SM_Highball_Cup.SM_Highball_Cup");
const TCHAR* PrimaryCupProfilePath = TEXT("/Game/VRTemplate/MenuIngredients/Breakage/cup_Highball/DA_cup_Highball_Break.DA_cup_Highball_Break");
const FName MilkId(TEXT("Milk"));

FString Quote(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return TEXT("\"") + Value + TEXT("\"");
}

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
    void Raw(const TCHAR* Key, const FString& Value) { Fields.Add(Quote(Key) + TEXT(":") + Value); }
    void Vector(const TCHAR* Key, const FVector& Value)
    {
        FRow V; V.Number(TEXT("x"), Value.X); V.Number(TEXT("y"), Value.Y); V.Number(TEXT("z"), Value.Z);
        Raw(Key, V.Json());
    }
    FString Json() const { return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}"); }
};

bool SaveSamples(FAutomationTestBase& Test, const TCHAR* Group, const TArray<FString>& Rows)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("IceAssetRuntime");
    if (!Test.TestTrue(TEXT("创建本次真实资产采样目录"), IFileManager::Get().MakeDirectory(*Directory, true))) return false;
    const FString Json = TEXT("{\"sampleKind\":\"savedAssetsAndActualChaosWorldTick\",\"screenshotsCaptured\":false,\"group\":")
        + Quote(Group) + TEXT(",\"rows\":[\n") + FString::Join(Rows, TEXT(",\n")) + TEXT("\n]}\n");
    return Test.TestTrue(TEXT("保存本组实际状态、物理位置及账本采样"), FFileHelper::SaveStringToFile(Json,
        *(Directory / (FString(Group) + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

void FinishCompilation()
{
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
}

struct FAssets
{
    UClass* Cube = LoadClass<AIceCubeActor>(nullptr, CubePath);
    UClass* Scoop = LoadClass<AIceScoopActor>(nullptr, ScoopPath);
    UClass* Bucket = LoadClass<AIceBucketActor>(nullptr, BucketPath);
    UClass* Cup = LoadClass<ACupActor>(nullptr, CupPath);
    bool Check(FAutomationTestBase& Test) const
    {
        return Test.TestNotNull(TEXT("保存的冰块 BP 生成类"), Cube)
            && Test.TestNotNull(TEXT("保存的冰铲 BP 生成类"), Scoop)
            && Test.TestNotNull(TEXT("保存的冰桶 BP 生成类"), Bucket)
            && Test.TestNotNull(TEXT("已有 Cup03 BP 生成类"), Cup);
    }
};

bool CheckMesh(FAutomationTestBase& Test, UStaticMeshComponent* Component,
    const TCHAR* Path, int32 Hulls, const FVector& Size, TArray<FString>& Rows)
{
    if (!Test.TestNotNull(TEXT("保存的根网格组件"), Component)) return false;
    UStaticMesh* Mesh = Component->GetStaticMesh();
    if (!Test.TestNotNull(TEXT("保存资产不是空网格"), Mesh)) return false;
    Test.TestEqual(TEXT("根组件引用指定项目网格而非 Basic Cube"), Mesh->GetPathName(), FString(Path));
    Test.TestTrue(TEXT("厘米模型根组件相对缩放为一"), Component->GetRelativeScale3D().Equals(FVector::OneVector, .0001));
    Test.TestEqual(TEXT("保存网格恰好三个 LOD"), Mesh->GetNumLODs(), 3);
    const FBox Box = Mesh->GetBoundingBox();
    Test.TestTrue(TEXT("模型边界有限且非空"), Box.IsValid && !Box.Min.ContainsNaN()
        && !Box.Max.ContainsNaN() && Box.GetSize().GetMin() > 0);
    const FVector ActualSize = Box.GetSize() * Component->GetRelativeScale3D().GetAbs();
    Test.TestTrue(TEXT("真实厘米长宽符合导入尺寸"), FMath::IsNearlyEqual(ActualSize.X, Size.X, .15)
        && FMath::IsNearlyEqual(ActualSize.Y, Size.Y, .15));
    if (Size.Z > 0) Test.TestTrue(TEXT("真实厘米高度符合导入尺寸"), FMath::IsNearlyEqual(ActualSize.Z, Size.Z, .15));
    UBodySetup* Body = Mesh->GetBodySetup();
    if (!Test.TestNotNull(TEXT("实际保存 BodySetup"), Body)) return false;
    Test.TestEqual(TEXT("保存 UCX 凸体数量"), Body->AggGeom.ConvexElems.Num(), Hulls);
    Test.TestTrue(TEXT("物理网格不使用 ComplexAsSimple"), Body->CollisionTraceFlag != CTF_UseComplexAsSimple);
    for (const FKConvexElem& Hull : Body->AggGeom.ConvexElems)
    {
        const FBox HullBox(Hull.VertexData);
        Test.TestTrue(TEXT("每个 UCX 是有效三维凸体"), Hull.VertexData.Num() >= 4
            && HullBox.IsValid && !HullBox.Min.ContainsNaN() && !HullBox.Max.ContainsNaN()
            && HullBox.GetSize().GetMin() > KINDA_SMALL_NUMBER);
    }
    for (int32 LOD = 0; LOD < 3 && LOD < Mesh->GetNumLODs(); ++LOD)
    {
        Test.TestTrue(TEXT("每个 LOD 有实际几何"), Mesh->GetNumVertices(LOD) > 0 && Mesh->GetNumTriangles(LOD) > 0);
        if (LOD > 0) Test.TestTrue(TEXT("后级 LOD 三角形严格减少"), Mesh->GetNumTriangles(LOD) < Mesh->GetNumTriangles(LOD - 1));
        FRow Row; Row.Text(TEXT("stage"), TEXT("savedMeshProfile")); Row.Text(TEXT("mesh"), Mesh->GetPathName());
        Row.Number(TEXT("lod"), LOD); Row.Number(TEXT("vertices"), Mesh->GetNumVertices(LOD));
        Row.Number(TEXT("triangles"), Mesh->GetNumTriangles(LOD)); Row.Number(TEXT("convexCount"), Body->AggGeom.ConvexElems.Num());
        Row.Vector(TEXT("boundsMinCM"), Box.Min); Row.Vector(TEXT("boundsMaxCM"), Box.Max);
        Row.Vector(TEXT("physicalSizeCM"), ActualSize); Row.Vector(TEXT("relativeScale"), Component->GetRelativeScale3D());
        Rows.Add(Row.Json());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceSavedStationAndAssetProfilesTest,
    "CyberCafe.Ice.SavedStationAndAssetProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceSavedStationAndAssetProfilesTest::RunTest(const FString&)
{
    FAssets Assets;
    if (!Assets.Check(*this)) return false;
    FinishCompilation();
    TestTrue(TEXT("冰 BP 直接继承对应 native base"), Assets.Cube->GetSuperClass() == AIceCubeActor::StaticClass()
        && Assets.Scoop->GetSuperClass() == AIceScoopActor::StaticClass() && Assets.Bucket->GetSuperClass() == AIceBucketActor::StaticClass());
    const AIceCubeActor* Cube = Assets.Cube->GetDefaultObject<AIceCubeActor>();
    const AIceScoopActor* Scoop = Assets.Scoop->GetDefaultObject<AIceScoopActor>();
    const AIceBucketActor* Bucket = Assets.Bucket->GetDefaultObject<AIceBucketActor>();
    TArray<FString> Rows;
    if (!CheckMesh(*this, Cube->Mesh.Get(), TEXT("/Game/VRTemplate/Ice/Meshes/SM_IceCube.SM_IceCube"), 1, FVector(2.4), Rows)
        || !CheckMesh(*this, Scoop->Mesh.Get(), TEXT("/Game/VRTemplate/Ice/Meshes/SM_IceScoop.SM_IceScoop"), 8, FVector(28, 8, 0), Rows)
        || !CheckMesh(*this, Bucket->Mesh.Get(), TEXT("/Game/VRTemplate/Ice/Meshes/SM_IceBucket.SM_IceBucket"), 17, FVector(24, 24, 18), Rows)) return false;
    TestTrue(TEXT("冰铲 CDO 实际生成保存冰 BP"), Scoop->IceClass.Get() == Assets.Cube);
    TestTrue(TEXT("桶展示冰引用同一个真实 2.4cm 网格"), Bucket->IceMesh.Get() == Cube->Mesh->GetStaticMesh());
    const FBox ScoopBox = Scoop->Mesh->GetStaticMesh()->GetBoundingBox();
    TestTrue(TEXT("28cm 铲的握柄与碗分居 pivot 两侧"), ScoopBox.Min.X < 0 && ScoopBox.Max.X > 0);
    TestTrue(TEXT("保存 BowlCenter 和 ReleaseLip 与厘米 pivot 匹配"), Scoop->BowlCenter.Equals(FVector(10, 0, 0), .001)
        && Scoop->ReleaseLip.Equals(FVector(16, 0, 0), .001) && ScoopBox.IsInsideOrOn(Scoop->BowlCenter)
        && ScoopBox.IsInsideOrOn(Scoop->ReleaseLip));
    TestTrue(TEXT("真实冰半径能够包住厘米模型且允许通过 Cup03"), Cube->RadiusCM >= 1.2f && Cube->RadiusCM < 4.1f);

    UWorld* Map = LoadObject<UWorld>(nullptr, MapPath);
    if (!TestNotNull(TEXT("只读加载保存地图，不运行原世界"), Map)
        || !TestNotNull(TEXT("只读遍历 PersistentLevel Actors"), Map->PersistentLevel.Get())) return false;
    const bool bMapBegunBefore = Map->HasBegunPlay();
    AIceStationActor* Station = nullptr;
    AGlassRecoveryActor* Recovery = nullptr;
    ATutorialDirector* Director = nullptr;
    AOrderHologramActor* Hologram = nullptr;
    ABottleActor* OriginalMilk = nullptr;
    int32 Stations = 0, Recoveries = 0, Directors = 0, Holograms = 0;
    for (AActor* Actor : Map->PersistentLevel->Actors)
    {
        if (!Actor) continue;
        if (AIceStationActor* A = Cast<AIceStationActor>(Actor)) { ++Stations; if (A->GetFName() == TEXT("IceStation_Practice")) Station = A; }
        if (AGlassRecoveryActor* A = Cast<AGlassRecoveryActor>(Actor))
        {
            const bool bTutorialManager = A->GetFName() == TEXT("GlassRecovery_Tutorial");
            if (bTutorialManager) { ++Recoveries; Recovery = A; }
            FRow Row; Row.Text(TEXT("stage"), TEXT("savedRecoveryManager"));
            Row.Text(TEXT("manager"), A->GetPathName()); Row.Text(TEXT("managerClass"), A->GetClass()->GetPathName());
            Row.Bool(TEXT("countedTutorialManager"), bTutorialManager);
            Row.Text(TEXT("prototypeCup"), GetPathNameSafe(A->PrototypeCup.Get()));
            Row.Text(TEXT("cupProfile"), GetPathNameSafe(A->CupProfile.Get()));
            Row.Number(TEXT("restoreTargetCount"), A->RestoreTargets.Num()); Rows.Add(Row.Json());
        }
        if (ATutorialDirector* A = Cast<ATutorialDirector>(Actor)) { ++Directors; if (A->GetFName() == TEXT("BP_TutorialDirecter_C_0")) Director = A; }
        if (AOrderHologramActor* A = Cast<AOrderHologramActor>(Actor)) { ++Holograms; if (A->GetFName() == TEXT("BP_OrderHologram_Tutorial")) Hologram = A; }
        TestFalse(TEXT("退场的原第一杯实例不再留在地图"), Actor->GetFName() == TEXT("BP_Cup01_C_1"));
        if (Actor->GetFName() == TEXT("BP_Bottle04_Milk_Tutorial")) OriginalMilk = Cast<ABottleActor>(Actor);
    }
    if (!TestNotNull(TEXT("保存 IceStation_Practice 实例"), Station)
        || !TestNotNull(TEXT("保留原玻璃恢复实例"), Recovery) || !TestNotNull(TEXT("保留原六步导演"), Director)
        || !TestNotNull(TEXT("保留原全息实例"), Hologram) || !TestNotNull(TEXT("保留原奶瓶"), OriginalMilk)) return false;
    ACupActor* PrimaryCup = Cast<ACupActor>(Recovery->PrototypeCup.Get());
    if (!TestNotNull(TEXT("从指定经理 PrototypeCup 读取第一杯"), PrimaryCup)) return false;
    TestEqual(TEXT("冰站唯一"), Stations, 1);
    TestEqual(TEXT("命名 GlassRecovery_Tutorial 唯一，独立菜单经理不计入"), Recoveries, 1);
    TestEqual(TEXT("旧导演唯一"), Directors, 1); TestEqual(TEXT("旧全息唯一"), Holograms, 1);
    if (!TestNotNull(TEXT("冰站真实桶引用"), Station->Bucket.Get()) || !TestNotNull(TEXT("冰站真实铲引用"), Station->Scoop.Get())
        || !TestNotNull(TEXT("冰站真实杯引用"), Station->Cup.Get())) return false;
    TestTrue(TEXT("冰站沿用原生 class"), Station->GetClass() == AIceStationActor::StaticClass());
    TestTrue(TEXT("场景引用新的保存 BP，而非 native 占位"), Station->Bucket->GetClass() == Assets.Bucket
        && Station->Scoop->GetClass() == Assets.Scoop && Station->Cup->GetClass() == Assets.Cup);
    TestEqual(TEXT("冰站杯 classpath 为既有 Cup03"), Station->Cup->GetClass()->GetPathName(), FString(CupPath));
    TestEqual(TEXT("保存新桶实例名"), Station->Bucket->GetName(), FString(TEXT("IceBucket_Practice")));
    TestEqual(TEXT("保存新铲实例名"), Station->Scoop->GetName(), FString(TEXT("IceScoop_Practice")));
    TestEqual(TEXT("保存新杯实例名"), Station->Cup->GetName(), FString(TEXT("IceCup_Practice")));
    TestTrue(TEXT("新实例均属于被读取的保存地图"), Station->Bucket->GetLevel() == Map->PersistentLevel
        && Station->Scoop->GetLevel() == Map->PersistentLevel && Station->Cup->GetLevel() == Map->PersistentLevel);
    TestTrue(TEXT("场景铲引用同一场景桶和新冰类"), Station->Scoop->Bucket.Get() == Station->Bucket.Get()
        && Station->Scoop->IceClass.Get() == Assets.Cube);
    TestTrue(TEXT("场景网格仍引用保存的真实模型"), Station->Bucket->Mesh->GetStaticMesh() == Bucket->Mesh->GetStaticMesh()
        && Station->Scoop->Mesh->GetStaticMesh() == Scoop->Mesh->GetStaticMesh());
    TestEqual(TEXT("保存杯内半径 4.1cm"), Station->CupInteriorRadiusCM, 4.1f);
    TestEqual(TEXT("保存杯内深度 8.8cm"), Station->CupInteriorDepthCM, 8.8f);
    TestTrue(TEXT("冰站练习杯独立于新恢复首杯"), Station->Cup.Get() != PrimaryCup && Recovery->PrototypeCup.Get() == PrimaryCup);
    TestTrue(TEXT("恢复器仍引用原奶瓶"), Recovery->PrototypeBottle.Get() == OriginalMilk);
    TestTrue(TEXT("第一杯属于原保存地图"), PrimaryCup->GetLevel() == Map->PersistentLevel.Get());
    TestEqual(TEXT("第一杯实例为批准的新 Highball"), PrimaryCup->GetName(), FString(TEXT("BP_MenuCup_Highball_C_1")));
    TestEqual(TEXT("第一杯 class 精确匹配 Highball"), PrimaryCup->GetClass()->GetPathName(), FString(PrimaryCupClassPath));
    TestEqual(TEXT("第一杯网格精确匹配 Highball"), GetPathNameSafe(PrimaryCup->ContainerMesh->GetStaticMesh().Get()), FString(PrimaryCupMeshPath));
    TestTrue(TEXT("第一杯根网格及缩放保持批准的 1.1"), PrimaryCup->GetRootComponent() == PrimaryCup->ContainerMesh.Get()
        && PrimaryCup->ContainerMesh->GetRelativeScale3D().Equals(FVector(1.1), .0001));
    TestEqual(TEXT("第一杯容量为 300mL"), PrimaryCup->MaxVolumeML, 300.f);
    TestEqual(TEXT("第一杯自身 profile 精确匹配 Highball"), GetPathNameSafe(PrimaryCup->GlassBreakProfile.Get()), FString(PrimaryCupProfilePath));
    TestEqual(TEXT("恢复器仍严格六个目标"), Recovery->RestoreTargets.Num(), 6);
    TestTrue(TEXT("六目标包含新首杯和原奶瓶"), Recovery->RestoreTargets.Contains(PrimaryCup) && Recovery->RestoreTargets.Contains(OriginalMilk));
    TestEqual(TEXT("原奶瓶类保留"), OriginalMilk->GetClass()->GetPathName(), FString(TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle04/BP_Bottle04_Milk.BP_Bottle04_Milk_C")));
    TestEqual(TEXT("经理首杯 profile 精确匹配 Highball"), GetPathNameSafe(Recovery->CupProfile.Get()), FString(PrimaryCupProfilePath));
    TestEqual(TEXT("恢复器瓶 profile 保留"), GetPathNameSafe(Recovery->BottleProfile.Get()), FString(TEXT("/Game/VRTemplate/Breakage/DA_MilkBottleBreak.DA_MilkBottleBreak")));
    TestEqual(TEXT("菜单插入后教学恰好八步"), Director->Steps.Num(), 8);
    const TCHAR* Names[] = {TEXT("TutorialStep_ShowMessage_0"), TEXT("TutorialStep_ShowMessage_1"), TEXT("TutorialStep_ShowMessage_2"),
        TEXT("TutorialStep_ShowMessage_3"), TEXT("TutorialStep_WaitForServe_0"), TEXT("TutorialStep_ConsumeServedDrink_0")};
    const TCHAR* Hints[] = {TEXT("你醒了？……刚刚说到我教你做一杯拿铁，你就睡着了。快，先看看你的台子。"),
        TEXT("台子上有咖啡液和牛奶，把瓶盖拿掉，往杯子里倒。"),
        TEXT("空杯子在你左手边。"),
        TEXT("够不着就别探身子了。左手按 X，或者右手按 A，隔空就能拿过来。"),
        TEXT("给我做一杯拿铁。咖啡和牛奶差不多一半一半，加起来一百八到两百毫升。做好了放到吧台前面，不对的话拿回去改。"),
        TEXT("时零准备尝一口，稍等一下。")};
    const int32 OriginalStepIndices[] = {0, 1, 2, 3, 6, 7};
    for (int32 I = 0; I < 6 && Director->Steps.IsValidIndex(OriginalStepIndices[I]); ++I)
    {
        UTutorialStep* Step = Director->Steps[OriginalStepIndices[I]].Get();
        if (!TestNotNull(TEXT("保存旧教学步骤实例"), Step)) return false;
        const FString Type = I < 4 ? TEXT("TutorialStep_ShowMessage") : (I == 4 ? TEXT("TutorialStep_WaitForServe") : TEXT("TutorialStep_ConsumeServedDrink"));
        TestEqual(TEXT("原六步顺序及名称保留"), Step->GetName(), FString(Names[I]));
        TestEqual(TEXT("原六步原生类型保留"), Step->GetClass()->GetPathName(), TEXT("/Script/CyberCafe.") + Type);
        TestTrue(TEXT("旧步骤仍归属原导演"), Step->GetOuter() == Director);
        TestEqual(TEXT("步骤提示精确匹配批准的冰拿铁基线"), Step->HintText.ToString(), FString(Hints[I]));
        const FString Completed = I < 4 ? TEXT("") : (I == 4
            ? TEXT("分量对，比例也对。这杯我收下了。") : TEXT("……嗯，奶和咖啡融得挺好。这杯是给人喝的了。"));
        TestEqual(TEXT("步骤完成文本精确匹配批准基线"), Step->CompletedText.ToString(), Completed);
        FRow Row; Row.Text(TEXT("stage"), TEXT("savedOriginalStep")); Row.Number(TEXT("index"), OriginalStepIndices[I]);
        Row.Text(TEXT("path"), Step->GetPathName()); Row.Text(TEXT("class"), Step->GetClass()->GetPathName());
        Row.Text(TEXT("hint"), Step->HintText.ToString()); Row.Text(TEXT("completed"), Step->CompletedText.ToString()); Rows.Add(Row.Json());
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
    UTutorialStep_WaitForServe* Wait = Director->Steps.IsValidIndex(6) ? Cast<UTutorialStep_WaitForServe>(Director->Steps[6].Get()) : nullptr;
    if (!TestNotNull(TEXT("保留原 Step[6] 上菜步骤"), Wait)) return false;
    TestTrue(TEXT("旧上菜步骤仍引用原全息"), Wait->OrderHologram.Get() == Hologram);
    TestEqual(TEXT("旧拿铁配方保留"), GetPathNameSafe(Wait->RequiredRecipe.Get()), FString(TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte")));
    TestEqual(TEXT("旧上菜点保留"), GetPathNameSafe(Wait->RequiredSpot.Get()), FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_ServingSpot_C_1")));
    if (!TestNotNull(TEXT("原拿铁配方有效"), Wait->RequiredRecipe.Get()) || !TestNotNull(TEXT("原服务位存在"), Wait->RequiredSpot.Get())
        || !TestNotNull(TEXT("原 NPC 引用存在"), Director->NPCRef.Get())) return false;
    TestEqual(TEXT("原 RecipeId Latte 保留"), Wait->RequiredRecipe->RecipeId, FName(TEXT("Latte")));
    TestEqual(TEXT("拿铁杯 class 精确匹配 Highball"), GetPathNameSafe(Wait->RequiredRecipe->AcceptedCupClass.Get()), FString(PrimaryCupClassPath));
    TestEqual(TEXT("旧加冰建议值保留"), Wait->RequiredRecipe->ExpectedIceCount, 3);
    TestEqual(TEXT("冰拿铁目标仍为 200mL"), Wait->RequiredRecipe->TargetVolumeML, 200.0);
    TestTrue(TEXT("原服务位仅 Z 更新为批准高度"), Wait->RequiredSpot->GetActorLocation().Equals(FVector(-21, 71, 114.68106781), .0001));
    TestEqual(TEXT("原 NPC 实例引用不变"), Director->NPCRef->GetPathName(), FString(TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_TutorialNPC_Manny_C_1")));
    TestTrue(TEXT("原 NPC 位置不变"), Director->NPCRef->GetActorLocation().Equals(FVector(-9, 48, 82), .001));
    TestEqual(TEXT("读取过程中没有在原地图调用 BeginPlay"), Map->HasBegunPlay(), bMapBegunBefore);
    FRow Row; Row.Text(TEXT("stage"), TEXT("savedStationReferences")); Row.Text(TEXT("map"), Map->GetPathName());
    Row.Text(TEXT("station"), Station->GetPathName()); Row.Text(TEXT("bucket"), Station->Bucket->GetPathName());
    Row.Text(TEXT("scoop"), Station->Scoop->GetPathName()); Row.Text(TEXT("cup"), Station->Cup->GetPathName());
    Row.Text(TEXT("cupClass"), Station->Cup->GetClass()->GetPathName()); Row.Text(TEXT("primaryCup"), PrimaryCup->GetPathName());
    Row.Text(TEXT("recovery"), Recovery->GetPathName()); Row.Text(TEXT("hologram"), Hologram->GetPathName());
    Row.Text(TEXT("primaryCupClass"), PrimaryCup->GetClass()->GetPathName());
    Row.Text(TEXT("primaryCupMesh"), GetPathNameSafe(PrimaryCup->ContainerMesh->GetStaticMesh().Get()));
    Row.Text(TEXT("primaryCupProfile"), GetPathNameSafe(Recovery->CupProfile.Get()));
    Row.Number(TEXT("primaryCupCapacityML"), PrimaryCup->MaxVolumeML);
    Row.Vector(TEXT("primaryCupRootScale"), PrimaryCup->ContainerMesh->GetRelativeScale3D());
    Row.Vector(TEXT("bowlCenterLocalCM"), Scoop->BowlCenter); Row.Vector(TEXT("releaseLipLocalCM"), Scoop->ReleaseLip);
    Row.Number(TEXT("stepCount"), Director->Steps.Num()); Rows.Add(Row.Json());
    return SaveSamples(*this, TEXT("SavedStationAndAssetProfiles"), Rows);
}

struct FRuntimeWorld
{
    UWorld* World;
    UGameInstance* GI;
    int32 Frames = 0;
    FRuntimeWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(true);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine); Context.OwningGameInstance = GI;
        World->SetGameInstance(GI); GI->Init();
    }
    ~FRuntimeWorld()
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void BeginPlay()
    {
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
    }
    void Tick()
    {
        ++GFrameCounter; World->Tick(LEVELTICK_All, StepSeconds); ++Frames;
    }
    ULiquidTransferSubsystem* Transfers() const { return World->GetSubsystem<ULiquidTransferSubsystem>(); }
};

void IsolateCup(ACupActor* Cup)
{
    Cup->ContainerMesh->SetSimulatePhysics(false); Cup->ContainerMesh->SetEnableGravity(false);
    Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Cup->bCanPour = false; Cup->bEnableSplash = false; Cup->LiquidFXTemplate = nullptr; Cup->SplashEffectTemplate = nullptr;
    for (UNiagaraComponent* FX : {Cup->LiquidFX.Get(), Cup->PourFX.Get()})
    {
        FX->SetAutoActivate(false); FX->SetAsset(nullptr); FX->DeactivateImmediate();
    }
    Cup->VolumeWidgetClass = nullptr; Cup->VolumeWidgetComp->SetWidgetClass(nullptr); Cup->bFaceCamera = false;
    Cup->GrabComp->PhysicsMode = EGrabPhysicsMode::Kinematic; Cup->GrabComp->bSimulateOnDrop = false;
    Cup->GrabComp->bCaptureHand = false; Cup->GrabComp->OnGrabHapticEffect = nullptr;
}

bool PrepareBody(FAutomationTestBase& Test, UStaticMeshComponent* Mesh)
{
    if (!Test.TestNotNull(TEXT("实际物理网格"), Mesh->GetStaticMesh().Get())) return false;
    UBodySetup* Body = Mesh->GetStaticMesh()->GetBodySetup();
    if (!Test.TestNotNull(TEXT("实际物理 BodySetup"), Body)) return false;
    Body->CreatePhysicsMeshes();
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Mesh->RecreatePhysicsState();
    return Test.TestTrue(TEXT("真实碰撞体已经创建并有效"), Mesh->IsRegistered()
        && Mesh->GetBodyInstance() && Mesh->GetBodyInstance()->IsValidBodyInstance());
}

void Capture(TArray<FString>& Rows, const TCHAR* Stage, const FRuntimeWorld& T,
    const AIceScoopActor* Scoop, const ACupActor* Cup, const UCupIceComponent* CupIce)
{
    TArray<FString> Cubes;
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
    {
        const AIceCubeActor* Ice = *It;
        if (!IsValid(Ice) || Ice->IsActorBeingDestroyed()) continue;
        FRow Row; Row.Number(TEXT("id"), Ice->GetUniqueID()); Row.Text(TEXT("class"), Ice->GetClass()->GetPathName());
        Row.Text(TEXT("mesh"), GetPathNameSafe(Ice->Mesh->GetStaticMesh().Get()));
        Row.Text(TEXT("state"), Ice->State == EIceState::InScoop ? TEXT("InScoop") : (Ice->State == EIceState::InCup ? TEXT("InCup") : TEXT("Free")));
        Row.Vector(TEXT("positionCM"), Ice->GetActorLocation()); Row.Vector(TEXT("physicsVelocityCms"), Ice->Mesh->GetPhysicsLinearVelocity());
        Row.Vector(TEXT("localVelocityCms"), Ice->LocalVelocity); Row.Vector(TEXT("worldScale"), Ice->Mesh->GetComponentScale());
        Row.Bool(TEXT("simulating"), Ice->Mesh->IsSimulatingPhysics()); Row.Bool(TEXT("gravity"), Ice->Mesh->IsGravityEnabled());
        Row.Bool(TEXT("validBody"), Ice->Mesh->GetBodyInstance() && Ice->Mesh->GetBodyInstance()->IsValidBodyInstance());
        Row.Text(TEXT("cupIce"), GetPathNameSafe(Ice->CupIce.Get())); Row.Text(TEXT("scoop"), GetPathNameSafe(Ice->Scoop.Get()));
        Cubes.Add(Row.Json());
    }
    const FLiquidBalance B = T.Transfers()->GetBalance();
    TArray<FString> Ingredients;
    for (const auto& Pair : Cup->GetComposition().Ingredients)
    {
        FRow Ingredient; Ingredient.Text(TEXT("id"), Pair.Key.ToString()); Ingredient.Number(TEXT("volumeML"), Pair.Value);
        Ingredients.Add(Ingredient.Json());
    }
    FRow Row; Row.Text(TEXT("stage"), Stage); Row.Number(TEXT("worldFrame"), T.Frames);
    Row.Number(TEXT("worldTimeSeconds"), T.World->GetTimeSeconds()); Row.Number(TEXT("dtSeconds"), StepSeconds);
    Row.Number(TEXT("gravityCms2"), T.World->GetGravityZ()); Row.Number(TEXT("loadedCount"), Scoop->GetLoadedCount());
    Row.Number(TEXT("cupIceCount"), CupIce->GetIceCount()); Row.Number(TEXT("liveIce"), T.World->GetSubsystem<UIceWorldSubsystem>()->GetLiveCount());
    Row.Vector(TEXT("scoopPositionCM"), Scoop->GetActorLocation()); Row.Vector(TEXT("scoopForward"), Scoop->GetActorForwardVector());
    Row.Vector(TEXT("lipWorldCM"), Scoop->Mesh->GetComponentTransform().TransformPosition(Scoop->ReleaseLip));
    Row.Vector(TEXT("cupMouthWorldCM"), Cup->PourEntryPoint->GetComponentLocation());
    Row.Number(TEXT("volumeML"), Cup->GetPreciseVolumeML()); Row.Number(TEXT("milkML"), Cup->GetIngredientVolumeML(MilkId));
    Row.Number(TEXT("capacityML"), Cup->MaxVolumeML); Row.Number(TEXT("fill"), Cup->FillAmount); Row.Number(TEXT("revision"), Cup->GetLiquidRevision());
    Row.Bool(TEXT("frozen"), Cup->IsLiquidFrozen()); Row.Bool(TEXT("broken"), Cup->IsBroken()); Row.Bool(TEXT("held"), Scoop->Grab->IsHeld());
    Row.Number(TEXT("initialML"), B.InitialML); Row.Number(TEXT("injectedML"), B.InjectedML); Row.Number(TEXT("removedML"), B.RemovedML);
    Row.Number(TEXT("containerML"), B.ContainerML); Row.Number(TEXT("inFlightML"), B.InFlightML); Row.Number(TEXT("spilledML"), B.SpilledML);
    Row.Number(TEXT("errorML"), B.ErrorML); Row.Number(TEXT("packetCount"), B.PacketCount);
    Row.Raw(TEXT("ingredients"), TEXT("[") + FString::Join(Ingredients, TEXT(",")) + TEXT("]"));
    Row.Raw(TEXT("cubes"), TEXT("[") + FString::Join(Cubes, TEXT(",")) + TEXT("]")); Rows.Add(Row.Json());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceActualScoopToCupWithPhysicsTest,
    "CyberCafe.Ice.ActualScoopToCupWithPhysics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIceActualScoopToCupWithPhysicsTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("真实世界需要 Engine"), GEngine)) return false;
    FAssets Assets;
    if (!Assets.Check(*this)) return false;
    FinishCompilation();
    for (UStaticMeshComponent* Mesh : {Assets.Cube->GetDefaultObject<AIceCubeActor>()->Mesh.Get(),
        Assets.Scoop->GetDefaultObject<AIceScoopActor>()->Mesh.Get(), Assets.Bucket->GetDefaultObject<AIceBucketActor>()->Mesh.Get(),
        Assets.Cup->GetDefaultObject<ACupActor>()->ContainerMesh.Get()})
    {
        if (!TestNotNull(TEXT("保存 BP 的真实物理网格"), Mesh->GetStaticMesh().Get())
            || !TestNotNull(TEXT("保存 BP 的实际 BodySetup"), Mesh->GetStaticMesh()->GetBodySetup())) return false;
        Mesh->GetStaticMesh()->GetBodySetup()->CreatePhysicsMeshes();
    }
    FRuntimeWorld T;
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIceBucketActor* Bucket = T.World->SpawnActor<AIceBucketActor>(Assets.Bucket, FVector(-30, 0, 0), FRotator::ZeroRotator, P);
    const FTransform CupTransform(FVector::ZeroVector);
    ACupActor* Cup = T.World->SpawnActorDeferred<ACupActor>(Assets.Cup, CupTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("生成实际保存桶 BP"), Bucket) || !TestNotNull(TEXT("生成已有 Cup03 BP"), Cup)) return false;
    IsolateCup(Cup); Cup->MaxVolumeML = 300.f; Cup->FillAmount = 0.f; Cup->InitialIngredientId = MilkId;
    Cup->FinishSpawning(CupTransform); IsolateCup(Cup);
    UCupIceComponent* CupIce = Cup->FindComponentByClass<UCupIceComponent>();
    if (!CupIce) { CupIce = NewObject<UCupIceComponent>(Cup); Cup->AddInstanceComponent(CupIce); }
    CupIce->InteriorRadiusCM = 4.1f; CupIce->InteriorDepthCM = 8.8f; CupIce->ClinkSound = nullptr;
    if (!CupIce->IsRegistered()) CupIce->RegisterComponent();
    const AIceScoopActor* ScoopCDO = Assets.Scoop->GetDefaultObject<AIceScoopActor>();
    const FVector LoadPosition = Bucket->Mesh->GetComponentTransform().TransformPosition(FVector(0, 0, 12)) - ScoopCDO->BowlCenter;
    AIceScoopActor* Scoop = T.World->SpawnActor<AIceScoopActor>(Assets.Scoop, LoadPosition, FRotator::ZeroRotator, P);
    if (!TestNotNull(TEXT("生成实际保存铲 BP"), Scoop)) return false;
    Scoop->Bucket = Bucket; Scoop->ScoopSound = nullptr; Scoop->Grab->OnGrabHapticEffect = nullptr;
    TestTrue(TEXT("铲使用保存冰 BP，不替换 native class"), Scoop->IceClass.Get() == Assets.Cube);
    T.BeginPlay(); IsolateCup(Cup);
    // 仅固定测试操作者的铲；独立自由冰始终使用真实物理，不手动移动、不代调接纳。
    Scoop->Mesh->SetSimulatePhysics(false); Scoop->Mesh->SetEnableGravity(false);
    Scoop->Grab->PhysicsMode = EGrabPhysicsMode::Kinematic; Scoop->Grab->bSimulateOnDrop = false; Scoop->Grab->bCaptureHand = false;
    if (!PrepareBody(*this, Cup->ContainerMesh.Get()) || !PrepareBody(*this, Scoop->Mesh.Get())) return false;
    TestTrue(TEXT("杯仍保留真实 QueryAndPhysics 碰撞"), Cup->ContainerMesh->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
    TestTrue(TEXT("真实 GI 与事件 context"), T.World->GetGameInstance() == T.GI && T.GI->GetSubsystem<UCyberCafeEventSubsystem>() != nullptr);
    if (!TestNotNull(TEXT("实际液体账本"), T.Transfers()) || !TestNotNull(TEXT("实际冰 subsystem"), T.World->GetSubsystem<UIceWorldSubsystem>())) return false;
    TestTrue(TEXT("Chaos 世界确实开启模拟及重力"), T.World->bShouldSimulatePhysics && T.World->GetPhysicsScene() && T.World->GetGravityZ() < 0);
    TestTrue(TEXT("杯与杯冰组件正常 BeginPlay"), Cup->HasActorBegunPlay() && CupIce->HasBegunPlay() && CupIce->IsComponentTickEnabled());
    TestEqual(TEXT("注入真实 Milk 200mL"), Cup->AddIngredientLiquid(200.f, MilkId, FLinearColor::White), 200.f);
    TestEqual(TEXT("权威 200mL"), Cup->GetPreciseVolumeML(), 200.0); TestEqual(TEXT("Milk 200mL"), Cup->GetIngredientVolumeML(MilkId), 200.0);
    TestTrue(TEXT("实际 200/300 Fill"), FMath::IsNearlyEqual(Cup->FillAmount, 2.f / 3.f, .000001f));
    const double VolumeBefore = Cup->GetPreciseVolumeML(); const float FillBefore = Cup->FillAmount, CapacityBefore = Cup->MaxVolumeML;
    const int32 RevisionBefore = Cup->GetLiquidRevision(); const FLiquidComposition CompositionBefore = Cup->GetComposition();
    const FLiquidBalance BalanceBefore = T.Transfers()->GetBalance();
    TArray<FString> Rows; Capture(Rows, TEXT("beforeLoad"), T, Scoop, Cup, CupIce);
    TestTrue(TEXT("真实碗中心在真实桶加载区"), Bucket->ContainsBowl(Scoop->Mesh->GetComponentTransform().TransformPosition(Scoop->BowlCenter)));
    T.Tick();
    if (Scoop->GetLoadedCount() == 0) TestTrue(TEXT("允许通过正常加载 API 补装，无隐藏手柄条件"), Scoop->TryLoadFromBucket());
    if (!TestEqual(TEXT("实际装入三块保存 BP 冰"), Scoop->GetLoadedCount(), 3)) { SaveSamples(*this, TEXT("ActualScoopToCupWithPhysics"), Rows); return false; }
    TArray<TWeakObjectPtr<AIceCubeActor>> Cubes;
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
    {
        TestTrue(TEXT("装冰均为保存 BP 独立 Actor"), It->GetClass() == Assets.Cube && It->State == EIceState::InScoop && It->Scoop.Get() == Scoop);
        if (!PrepareBody(*this, It->Mesh.Get())) return false;
        It->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        TestTrue(TEXT("实际生成冰尺寸为 2.4cm，没有继承 fallback .024 缩放"),
            (It->Mesh->GetStaticMesh()->GetBoundingBox().GetSize() * It->Mesh->GetComponentScale().GetAbs()).Equals(FVector(2.4), .15));
        Cubes.Add(TWeakObjectPtr<AIceCubeActor>(*It));
    }
    if (!TestEqual(TEXT("发现真实三块冰身份"), Cubes.Num(), 3)) return false;
    Capture(Rows, TEXT("loadedThree"), T, Scoop, Cup, CupIce);
    // 两次大于 50cm 的追踪跳变由 production 的 >5000cm/s 门限自然归零；不写私有 FrameVelocity。
    Scoop->SetActorLocation(FVector(-120, 0, 12), false, nullptr, ETeleportType::TeleportPhysics); T.Tick();
    const FQuat Tilt = FRotator(-70, 0, 0).Quaternion();
    const FVector LipWorld(-2, 0, 18);
    const FVector ReleasePosition = LipWorld - Tilt.RotateVector(Scoop->ReleaseLip * Scoop->Mesh->GetComponentScale());
    TestTrue(TEXT("移动前后的跳变超过真实安全采样门限"), FVector::Dist(ReleasePosition, Scoop->GetActorLocation()) / StepSeconds > 5000);
    Scoop->SetActorLocationAndRotation(ReleasePosition, Tilt, false, nullptr, ETeleportType::TeleportPhysics);
    TestTrue(TEXT("真实 ReleaseLip 逆变换落在指定杯上方"), Scoop->Mesh->GetComponentTransform().TransformPosition(Scoop->ReleaseLip).Equals(LipWorld, .001));
    TestTrue(TEXT("真实铲尖 Pitch-70 朝下"), FVector::DotProduct(Scoop->GetActorForwardVector(), FVector::DownVector) > .45);
    TestTrue(TEXT("释放点在保存杯入口上方"), Cup->PourEntryPoint->GetComponentTransform().InverseTransformPosition(LipWorld).Z > 0);
    Capture(Rows, TEXT("beforeActualTiltTick"), T, Scoop, Cup, CupIce);
    TSet<uint32> SimulatedFree, MovingFree, Accepted;
    TMap<uint32, FVector> FirstFreePosition;
    TMap<uint32, int32> ReleaseFrames;
    for (int32 Frame = 0; Frame < 200; ++Frame)
    {
        T.Tick(); // 唯一运动与业务调度：不调用 ReleaseOne、ReleaseToWorld、TryAccept 或手工组件 Tick。
        for (const TWeakObjectPtr<AIceCubeActor>& Weak : Cubes)
        {
            AIceCubeActor* Ice = Weak.Get();
            if (!IsValid(Ice) || Ice->IsActorBeingDestroyed()) continue;
            const uint32 Id = Ice->GetUniqueID();
            if (Ice->State == EIceState::Free)
            {
                const bool bPhysics = Ice->Mesh->IsSimulatingPhysics() && Ice->Mesh->IsGravityEnabled()
                    && Ice->Mesh->GetBodyInstance() && Ice->Mesh->GetBodyInstance()->IsValidBodyInstance();
                TestTrue(TEXT("每个自由冰实际启用 Chaos 有效刚体和重力"), bPhysics);
                if (bPhysics) SimulatedFree.Add(Id);
                if (!FirstFreePosition.Contains(Id))
                {
                    FirstFreePosition.Add(Id, Ice->GetActorLocation()); ReleaseFrames.Add(Id, T.Frames);
                    TestTrue(TEXT("跳变没有虚假投掷速度，真实释放使用铲前向90cm/s"),
                        Ice->LocalVelocity.Equals(Scoop->GetActorForwardVector() * 90.f, .01));
                }
                else if (Ice->GetActorLocation().Z < FirstFreePosition[Id].Z - .1) MovingFree.Add(Id);
            }
            if (Ice->State == EIceState::InCup) Accepted.Add(Id);
        }
        Capture(Rows, TEXT("actualChaosWorldTick"), T, Scoop, Cup, CupIce);
    }
    TestEqual(TEXT("三块均观察到真实物理 Free 阶段"), SimulatedFree.Num(), 3);
    TestEqual(TEXT("三块均观察到重力造成实际 Z 下降"), MovingFree.Num(), 3);
    TestEqual(TEXT("三块均由正常穿口检测进入杯中"), Accepted.Num(), 3);
    TestEqual(TEXT("200 个真实帧后杯内恰好三块"), CupIce->GetIceCount(), 3);
    TestEqual(TEXT("自然倾倒后铲装载归零"), Scoop->GetLoadedCount(), 0);
    TestEqual(TEXT("三块活冰，无额外补造"), T.World->GetSubsystem<UIceWorldSubsystem>()->GetLiveCount(), 3);
    TArray<int32> Frames; ReleaseFrames.GenerateValueArray(Frames); Frames.Sort();
    for (int32 I = 1; I < Frames.Num(); ++I) TestTrue(TEXT("实际释放至少间隔十五个 .01 世界帧"), Frames[I] - Frames[I - 1] >= 15);
    for (const TWeakObjectPtr<AIceCubeActor>& Weak : Cubes)
    {
        AIceCubeActor* Ice = Weak.Get();
        if (!TestNotNull(TEXT("原三块身份仍存在"), Ice)) continue;
        TestTrue(TEXT("原冰身份已 InCup 且只引用目标杯组件"), Ice->State == EIceState::InCup && Ice->CupIce.Get() == CupIce && Ice->Scoop == nullptr);
        TestTrue(TEXT("入杯后真实附着 Cup03"), Ice->GetAttachParentActor() == Cup);
    }
    TestFalse(TEXT("全过程不需要 HUD 或人手输入"), Scoop->Grab->IsHeld());
    TestFalse(TEXT("未冻结、未破碎"), Cup->IsLiquidFrozen() || Cup->IsBroken());
    TestEqual(TEXT("冰不改权威 mL"), Cup->GetPreciseVolumeML(), VolumeBefore); TestEqual(TEXT("冰不改 Fill"), Cup->FillAmount, FillBefore);
    TestEqual(TEXT("冰不改容量"), Cup->MaxVolumeML, CapacityBefore); TestEqual(TEXT("冰不改液体 revision"), Cup->GetLiquidRevision(), RevisionBefore);
    const FLiquidComposition AfterComposition = Cup->GetComposition();
    TestEqual(TEXT("冰不改成分数量"), AfterComposition.Ingredients.Num(), CompositionBefore.Ingredients.Num());
    TestEqual(TEXT("冰不改成分总量"), AfterComposition.Total(), CompositionBefore.Total());
    for (const auto& Pair : CompositionBefore.Ingredients) TestEqual(TEXT("冰不改成分 ") + Pair.Key.ToString(), AfterComposition.Get(Pair.Key), Pair.Value);
    const FLiquidBalance After = T.Transfers()->GetBalance();
    TestEqual(TEXT("冰不改 InitialML"), After.InitialML, BalanceBefore.InitialML); TestEqual(TEXT("冰不改 InjectedML"), After.InjectedML, BalanceBefore.InjectedML);
    TestEqual(TEXT("冰不改 RemovedML"), After.RemovedML, BalanceBefore.RemovedML); TestEqual(TEXT("冰不改 ContainerML"), After.ContainerML, BalanceBefore.ContainerML);
    TestEqual(TEXT("冰不改 InFlightML"), After.InFlightML, BalanceBefore.InFlightML); TestEqual(TEXT("冰不改 SpilledML"), After.SpilledML, BalanceBefore.SpilledML);
    TestEqual(TEXT("冰不改 PacketCount"), After.PacketCount, BalanceBefore.PacketCount);
    TestTrue(TEXT("真实账本有限且闭合"), FMath::IsFinite(After.ErrorML) && FMath::Abs(After.ErrorML) < 1.e-6);
    Capture(Rows, TEXT("afterActualPhysicsFinalLedger"), T, Scoop, Cup, CupIce);
    return SaveSamples(*this, TEXT("ActualScoopToCupWithPhysics"), Rows);
}
}
#endif // WITH_DEV_AUTOMATION_TESTS
