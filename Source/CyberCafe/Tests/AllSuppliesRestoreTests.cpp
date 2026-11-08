// @trace RS-01 RS-02 RS-03 RS-04 CF-02 CF-03 CF-04
// writtenpendingrun：保存地图只读；独立 Game World 公共 API 测试，不代表原地图、音频设备或 HMD 验收。
#include "Tests/AllSuppliesCallbackProbe.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Engine/World.h"

// UHT 生成的调用入口在非自动化构建中仍需链接到这个真实定义。
void UAllSuppliesCallbackProbe::OnLiquidChange(float Fill, FLinearColor Color)
{
    ++Notifications;
    if (Fired) return;
    auto* Source = Kettle.Get();
    if (!IsValid(Source) || Source->IsActorBeingDestroyed()) return;
    if (Action == 1 || Action == 2)
    {
        if (Fill != 0 || Source->GetPreciseVolumeML() != 0) return;
        if (Action == 1)
        {
            auto* Target = Cup.Get();
            if (!IsValid(Target) || Target->IsActorBeingDestroyed()) return;
            Fired = true;
            GuardSeen = Source->bWorkbenchRestoreEmissionBlocked;
            Target->FreezeLiquidForServing();
        }
        else
        {
            auto* Owner = Grinder.Get();
            auto* Drawer = IsValid(Owner) && !Owner->IsActorBeingDestroyed() ? Owner->DrawerRef.Get() : nullptr;
            if (!IsValid(Drawer) || Drawer->IsActorBeingDestroyed()) return;
            Fired = true;
            GuardSeen = Source->bWorkbenchRestoreEmissionBlocked;
            Drawer->Destroy();
        }
    }
    else if (Action == 3 && Fill > 0 && Source->GetPreciseVolumeML() > 0)
    {
        Fired = true;
        GuardSeen = Source->bWorkbenchRestoreEmissionBlocked;
        Source->SetActorRotation(FRotator(-65, Source->GetActorRotation().Yaw, 0));
        AttemptVolumeML = Source->GetPreciseVolumeML();
        AttemptTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Source->GetActorUpVector().Z, -1.0, 1.0)));
        AttemptSpoutDown = FVector::DotProduct(Source->GetKettleSpoutWorldTransform().GetUnitAxis(EAxis::X), FVector::DownVector);
        const float Threshold = FMath::Clamp(FMath::IsFinite(Source->PourTiltDegrees) ? Source->PourTiltDegrees : 40.f, 10.f, 80.f);
        AttemptRateMLPerSecond = FMath::Max(0.f, FMath::IsFinite(Source->MaxFlowMLPerSecond) ? Source->MaxFlowMLPerSecond : 0.f)
            * FMath::Clamp((AttemptTiltDegrees - Threshold) / 40.f, 0.f, 1.f);
        auto* Transfer = Source->GetWorld()->GetSubsystem<ULiquidTransferSubsystem>();
        AttemptPacketsBefore = Transfer->GetBalance().PacketCount;
        AttemptEmitML = Source->PourWater(.02f);
        AttemptPacketsAfter = Transfer->GetBalance().PacketCount;
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ActorEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
#include "Internationalization/Regex.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "NiagaraComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "GrabComponent.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/BottleActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"

namespace AllSuppliesRestoreTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
constexpr double Tolerance = 1.e-4;
const TCHAR* BeforePath = TEXT("C:/Users/cloudcheng/WorkBuddy/2026-11-01-16-35-18/restore_all_facing/current_before.json");
const TCHAR* MapPath = TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe");
const FName Water(TEXT("Water")), Milk(TEXT("Milk")), Coffee(TEXT("Coffee"));

bool Near(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected)
{
    return Test.TestTrue(FString::Printf(TEXT("%s：实际 %.12g，预期 %.12g"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance);
}

struct FBeforeActor
{
    FString Class, Label;
    FTransform Pose;
};

// 只读取已给定导出器的 actors/name/config 模式；不是通用 JSON 解析器，不增加 Json 模块依赖。
struct FBeforeLayout
{
    FString Text;
    TMap<FString, FBeforeActor> Actors;
    bool Load(FAutomationTestBase& Test)
    {
        if (!Test.TestTrue(TEXT("读取指定最新布局，缺失即失败而非使用旧坐标兜底"),
            FFileHelper::LoadFileToString(Text, BeforePath))) return false;
        const int32 End = Text.Find(TEXT("\"interactives\""));
        if (!Test.TestTrue(TEXT("布局含 actors 和 interactives 分界"), End > 0)) return false;
        const FString ActorText = Text.Left(End);
        const FRegexPattern Pattern(TEXT("\"([^\"]+)\"\\s*:\\s*\\{\\s*\"class\"\\s*:\\s*\"([^\"]+)\"\\s*,\\s*\"label\"\\s*:\\s*\"([^\"]*)\"\\s*,\\s*\"pose\"\\s*:\\s*\\[([^\\]]+)\\]"));
        FRegexMatcher Match(Pattern, ActorText);
        while (Match.FindNext())
        {
            TArray<FString> Values;
            Match.GetCaptureGroup(4).ParseIntoArray(Values, TEXT(","), false);
            if (!Test.TestEqual(TEXT("每个布局 pose 恰好九个数"), Values.Num(), 9)) return false;
            double V[9];
            for (int32 I = 0; I < 9; ++I)
                if (!Test.TestTrue(TEXT("布局数值可解析且有限"),
                    FDefaultValueHelper::ParseDouble(Values[I].TrimStartAndEnd(), V[I]) && FMath::IsFinite(V[I]))) return false;
            const FString Name = Match.GetCaptureGroup(1);
            if (!Test.TestFalse(TEXT("基线 actor 名称不重复"), Actors.Contains(Name))) return false;
            Actors.Add(Name, {Match.GetCaptureGroup(2), Match.GetCaptureGroup(3),
                FTransform(FRotator(V[3], V[4], V[5]), FVector(V[0], V[1], V[2]), FVector(V[6], V[7], V[8]))});
        }
        return Test.TestEqual(TEXT("Before 布局真实记录 312 个 actor"), Actors.Num(), 312);
    }
    FString Config(const FString& Name) const
    {
        const int32 Start = Text.Find(TEXT("\"name\": \"") + Name + TEXT("\""));
        if (Start == INDEX_NONE) return FString();
        const int32 ConfigStart = Text.Find(TEXT("\"config\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
        if (ConfigStart == INDEX_NONE) return FString();
        const int32 End = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ConfigStart);
        return End == INDEX_NONE ? FString() : Text.Mid(ConfigStart, End - ConfigStart);
    }
    FString Value(const FString& ConfigText, const TCHAR* Key) const
    {
        const FRegexPattern Pattern(FString::Printf(TEXT("\"%s\"\\s*:\\s*\"([^\"]*)\""), Key));
        FRegexMatcher Match(Pattern, ConfigText);
        return Match.FindNext() ? Match.GetCaptureGroup(1) : FString();
    }
};

bool Alive(const AActor* A) { return IsValid(A) && !A->IsActorBeingDestroyed(); }

bool IsRuntimeChild(const AActor* A)
{
    return Cast<ABottleCapActor>(A) || Cast<AGrinderHandleActor>(A)
        || Cast<AGrinderDrawerActor>(A) || Cast<AIceCubeActor>(A);
}

TSet<AActor*> GrabRoots(UWorld* World, bool SavedOnly)
{
    TSet<AActor*> Result;
    auto Add = [&Result](AActor* A)
    {
        if (!Alive(A) || IsRuntimeChild(A)) return;
        TArray<UGrabComponent*> Grabs;
        A->GetComponents(Grabs);
        for (auto* Grab : Grabs)
            if (Grab && Grab->GetOwner() == A) { Result.Add(A); break; }
    };
    if (SavedOnly) { for (AActor* A : World->PersistentLevel->Actors) Add(A); }
    else { for (TActorIterator<AActor> It(World); It; ++It) Add(*It); }
    return Result;
}

struct FSavedSupplies
{
    FBeforeLayout Before;
    UWorld* Map = nullptr;
    AGlassRecoveryActor* Manager = nullptr;
    TArray<ALiquidContainerActor*> Vessels;
    TArray<AActor*> Tools;
    AIceScoopActor* Scoop = nullptr;
    TMap<FString, AActor*> Actors;
    bool Load(FAutomationTestBase& Test)
    {
        if (!Before.Load(Test)) return false;
        Map = LoadObject<UWorld>(nullptr, MapPath);
        if (!Test.TestNotNull(TEXT("只读加载保存 L_Cafe"), Map)
            || !Test.TestNotNull(TEXT("保存 PersistentLevel 有效"), Map->PersistentLevel.Get())) return false;
        if (!Test.TestTrue(TEXT("保存地图不得已经开始游戏"), !Map->HasBegunPlay())) return false;
        if (!Map->bIsWorldInitialized)
        {
            UWorld::InitializationValues IV;
            IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(false);
            Map->InitWorld(IV);
        }
        Map->UpdateWorldComponents(true, false); // 只注册，绝不 NotifyBeginPlay 或保存资产。
        int32 Managers = 0;
        for (AActor* A : Map->PersistentLevel->Actors)
        {
            if (!Alive(A) || !A->IsEditable() || !A->IsListedInSceneOutliner() || A->IsTemplate() || A->HasAnyFlags(RF_Transient) || Cast<AWorldSettings>(A) || FActorEditorUtils::IsABuilderBrush(A)) continue;
            Actors.Add(A->GetName(), A);
            if (auto* M = Cast<AGlassRecoveryActor>(A)) { ++Managers; Manager = M; }
        }
        if (!Test.TestEqual(TEXT("全地图仅一个 GlassRecoveryActor，不按 label 筛掉额外经理"), Managers, 1)
            || !Test.TestNotNull(TEXT("保存恢复经理"), Manager)) return false;
        Test.TestEqual(TEXT("保存替换清单必须为十五项"), Manager->RestoreTargets.Num(), 15);
        Test.TestEqual(TEXT("保存显式工具清单必须为三项"), Manager->RestoreToolTargets.Num(), 3);
        Test.TestTrue(TEXT("保存地图显式 opt-in 全用品"), Manager->bRestoreAllSupplies);
        if (!Test.TestNotNull(TEXT("保存首杯原型有效"), Manager->PrototypeCup.Get())
            || !Test.TestNotNull(TEXT("保存首瓶原型有效"), Manager->PrototypeBottle.Get())) return false;
        Vessels.AddUnique(Manager->PrototypeCup.Get()); Vessels.AddUnique(Manager->PrototypeBottle.Get());
        TSet<ALiquidContainerActor*> ReplacementSet;
        for (const auto& A : Manager->RestoreTargets)
        {
            if (!Test.TestNotNull(TEXT("十五项不允许空引用"), A.Get())) return false;
            Test.TestFalse(TEXT("保存替换清单不重复"), ReplacementSet.Contains(A.Get()));
            ReplacementSet.Add(A.Get()); Vessels.AddUnique(A.Get());
        }
        Test.TestTrue(TEXT("十五项显式包含首杯首瓶"), ReplacementSet.Contains(Manager->PrototypeCup.Get())
            && ReplacementSet.Contains(Manager->PrototypeBottle.Get()));
        for (const auto& A : Manager->RestoreToolTargets)
        {
            if (!Test.TestNotNull(TEXT("三个工具不允许空引用"), A.Get())) return false;
            Tools.AddUnique(A.Get());
        }
        Scoop = Manager->RestoreScoop.Get();
        return Test.TestEqual(TEXT("原型与替换清单 union 仍是十五槽"), Vessels.Num(), 15)
            && Test.TestEqual(TEXT("工具无重复"), Tools.Num(), 3)
            && Test.TestNotNull(TEXT("冰铲单独列出"), Scoop);
    }
};

void StopSimulation(AActor* A)
{
    // 仅隔离模拟，不删网格、不关闭碰撞、不扩大入口、不修改抓取模式或业务库存。
    TArray<UStaticMeshComponent*> Meshes;
    A->GetComponents(Meshes);
    for (auto* Mesh : Meshes) Mesh->SetSimulatePhysics(false);
    A->SetActorTickEnabled(false);
}

void QuietPresentation(AActor* A)
{
    TArray<UNiagaraComponent*> FXs;
    A->GetComponents(FXs);
    for (auto* FX : FXs) { FX->SetAutoActivate(false); FX->SetComponentTickEnabled(false); FX->DeactivateImmediate(); }
    TArray<UGrabComponent*> Grabs;
    A->GetComponents(Grabs);
    for (auto* Grab : Grabs) { Grab->bCaptureHand = false; Grab->bHideHandWhileHeld = false; Grab->OnGrabHapticEffect = nullptr; }
    if (auto* Cup = Cast<ACupActor>(A))
    {
        Cup->VolumeWidgetClass = nullptr;
        Cup->VolumeWidgetComp->SetWidgetClass(nullptr);
        Cup->bFaceCamera = false;
    }
    if (auto* Liquid = Cast<ALiquidContainerActor>(A))
    {
        Liquid->LiquidFXTemplate = nullptr;
        Liquid->SplashEffectTemplate = nullptr;
        Liquid->bEnableSplash = false;
    }
}

struct FVesselSnapshot
{
    ALiquidContainerActor* Identity = nullptr;
    FTransform Pose;
    UClass* Class = nullptr;
    UStaticMesh* Mesh = nullptr;
    TArray<UMaterialInterface*> Materials;
    double Volume = 0;
    float Capacity = 0, Fill = 0;
    FName Ingredient;
    FLinearColor Color;
    bool StartCapped = false;
};

UMaterialInterface* MaterialParent(UMaterialInterface* Material)
{
    while (auto* MID = Cast<UMaterialInstanceDynamic>(Material)) Material = MID->Parent;
    return Material;
}

struct FAllWorld
{
    FAutomationTestBase& Test;
    FSavedSupplies Saved;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    AGlassRecoveryActor* Manager = nullptr;
    AIceScoopActor* Scoop = nullptr;
    AHandPourKettleActor* Kettle = nullptr;
    AHandDripperActor* Dripper = nullptr;
    ACoffeeGrinderActor* Grinder = nullptr;
    TArray<FVesselSnapshot> Slots;
    TMap<AActor*, AActor*> Spawned;
    TArray<FTransform> ToolPoses;
    FTransform ScoopPose;
    ~FAllWorld()
    {
        if (!World) return;
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }
    explicit FAllWorld(FAutomationTestBase& InTest) : Test(InTest) {}
    ULiquidTransferSubsystem* Transfer() const { return World->GetSubsystem<ULiquidTransferSubsystem>(); }

    AActor* SpawnSaved(AActor* Original)
    {
        const auto* Baseline = Saved.Before.Actors.Find(Original->GetName());
        if (!Test.TestNotNull(TEXT("每个根来自 Before，而非旧脚本硬编码位置"), Baseline)) return nullptr;
        UClass* Class = LoadClass<AActor>(nullptr, *Baseline->Class);
        if (!Test.TestNotNull(TEXT("从最新布局加载真实保存 class，无原生替代兜底"), Class)) return nullptr;
        AActor* A = World->SpawnActorDeferred<AActor>(Class, Baseline->Pose, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!Test.TestNotNull(TEXT("Deferred 生成保存类"), A)) return nullptr;
        auto CopyVessel = [&]()
        {
            auto* Source = Cast<ALiquidContainerActor>(Original);
            auto* Target = Cast<ALiquidContainerActor>(A);
            if (!Source || !Target) return;
            Target->MaxVolumeML = Source->MaxVolumeML;
            Target->FillAmount = Source->FillAmount;
            Target->InitialIngredientId = Source->InitialIngredientId;
            Target->bCanPour = Source->bCanPour;
            Target->bAcceptLiquidFromOthers = Source->bAcceptLiquidFromOthers;
            Target->LiquidColor = Source->LiquidColor;
            Target->GlassBreakProfile = Source->GlassBreakProfile;
            Target->PourEntryRadius = Source->PourEntryRadius;
            Target->PourEntryPoint->SetAbsolute(Source->PourEntryPoint->IsUsingAbsoluteLocation(),
                Source->PourEntryPoint->IsUsingAbsoluteRotation(), Source->PourEntryPoint->IsUsingAbsoluteScale());
            Target->PourEntryPoint->SetRelativeTransform(Source->PourEntryPoint->GetRelativeTransform());
            Target->PourTargetPoint->SetRelativeTransform(Source->PourTargetPoint->GetRelativeTransform());
            Target->ContainerMesh->SetStaticMesh(Source->ContainerMesh->GetStaticMesh());
            for (int32 I = 0; I < Source->ContainerMesh->GetNumMaterials(); ++I)
                Target->ContainerMesh->SetMaterial(I, MaterialParent(Source->ContainerMesh->GetMaterial(I)));
            if (auto* Bottle = Cast<ABottleActor>(Target))
            {
                auto* OldBottle = CastChecked<ABottleActor>(Source);
                Bottle->CapClass = OldBottle->CapClass; Bottle->CapSocketName = OldBottle->CapSocketName;
                Bottle->bStartCapped = OldBottle->bStartCapped;
            }
        };
        CopyVessel(); StopSimulation(A); QuietPresentation(A);
        A->FinishSpawning(Baseline->Pose);
        CopyVessel(); StopSimulation(A); QuietPresentation(A);
        A->SetActorTransform(Baseline->Pose, false, nullptr, ETeleportType::TeleportPhysics);
        if (auto* Cup = Cast<ACupActor>(A))
        {
            if (!Cup->FindComponentByClass<UCupIceComponent>())
            {
                auto* Ice = NewObject<UCupIceComponent>(Cup);
                Cup->AddInstanceComponent(Ice); Ice->RegisterComponent();
            }
        }
        Spawned.Add(Original, A);
        return A;
    }

    bool Init()
    {
        if (!Test.TestNotNull(TEXT("Engine 可用"), GEngine) || !Saved.Load(Test)) return false;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        if (!Test.TestNotNull(TEXT("独立 Game World"), World)) return false;
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine); Context.OwningGameInstance = GI;
        World->SetGameInstance(GI); GI->Init();
        const FTransform ManagerPose = Saved.Before.Actors.FindChecked(Saved.Manager->GetName()).Pose;
        // 经理先入 Level，属性在 FinishSpawning/NotifyBeginPlay 前完整配置，先捕获再初始化液体。
        Manager = World->SpawnActorDeferred<AGlassRecoveryActor>(AGlassRecoveryActor::StaticClass(), ManagerPose,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Test.TestNotNull(TEXT("原生 deferred 经理"), Manager)) return false;
        Manager->bRestoreAllSupplies = true; Manager->bUnifiedRestore = Saved.Manager->bUnifiedRestore;
        Manager->HoldSeconds = Saved.Manager->HoldSeconds;
        for (auto* Original : Saved.Vessels)
        {
            auto* A = Cast<ALiquidContainerActor>(SpawnSaved(Original));
            if (!A) return false;
            Manager->RestoreTargets.Add(A);
            FVesselSnapshot Slot;
            Slot.Identity = A; Slot.Pose = A->GetActorTransform(); Slot.Class = A->GetClass();
            Slot.Mesh = A->ContainerMesh->GetStaticMesh(); Slot.Capacity = A->MaxVolumeML;
            Slot.Fill = A->FillAmount; Slot.Ingredient = A->InitialIngredientId; Slot.Color = A->LiquidColor;
            if (auto* Bottle = Cast<ABottleActor>(A)) Slot.StartCapped = Bottle->bStartCapped && Bottle->CapClass;
            for (int32 I = 0; I < A->ContainerMesh->GetNumMaterials(); ++I)
                Slot.Materials.Add(MaterialParent(A->ContainerMesh->GetMaterial(I)));
            Slots.Add(Slot);
        }
        Manager->BindPrototype(CastChecked<ALiquidContainerActor>(Spawned.FindChecked(Saved.Manager->PrototypeCup.Get())),
            CastChecked<ALiquidContainerActor>(Spawned.FindChecked(Saved.Manager->PrototypeBottle.Get())),
            Saved.Manager->CupProfile.Get(), Saved.Manager->BottleProfile.Get());
        for (auto* Original : Saved.Tools)
        {
            AActor* A = SpawnSaved(Original); if (!A) return false;
            Manager->RestoreToolTargets.Add(A); ToolPoses.Add(A->GetActorTransform());
            if (auto* K = Cast<AHandPourKettleActor>(A)) Kettle = K;
            if (auto* D = Cast<AHandDripperActor>(A)) Dripper = D;
            if (auto* G = Cast<ACoffeeGrinderActor>(A)) Grinder = G;
        }
        Scoop = Cast<AIceScoopActor>(SpawnSaved(Saved.Scoop));
        if (!Test.TestNotNull(TEXT("真实保存冰铲"), Scoop) || !Test.TestNotNull(TEXT("真实壶"), Kettle)
            || !Test.TestNotNull(TEXT("真实滤杯"), Dripper) || !Test.TestNotNull(TEXT("真实研磨器"), Grinder)) return false;
        ScoopPose = Scoop->GetActorTransform(); Scoop->Bucket = nullptr; Manager->RestoreScoop = Scoop;
        Manager->FinishSpawning(ManagerPose);
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
        if (!Test.TestNotNull(TEXT("真实液体账本"), Transfer())
            || !Test.TestNotNull(TEXT("主体 BeginPlay 真实生成柄"), Grinder->HandleRef.Get())
            || !Test.TestNotNull(TEXT("主体 BeginPlay 真实生成抽屉"), Grinder->DrawerRef.Get())) return false;
        for (const auto& Pair : Spawned) StopSimulation(Pair.Value);
        StopSimulation(Grinder->HandleRef.Get()); StopSimulation(Grinder->DrawerRef.Get());
        QuietPresentation(Grinder->HandleRef.Get()); QuietPresentation(Grinder->DrawerRef.Get());
        for (auto& Slot : Slots)
        {
            Slot.Volume = Slot.Identity->GetPreciseVolumeML();
            const double Expected = Slot.Fill * static_cast<double>(Slot.Capacity);
            Near(Test, TEXT("真实 BeginPlay 初始化全部槽，不写权威 mL"), Slot.Volume, Expected);
            const FString Config = Saved.Before.Config(Saved.Vessels[&Slot - Slots.GetData()]->GetName());
            const FString Ingredient = Saved.Before.Value(Config, TEXT("initial_ingredient_id"));
            if (!Ingredient.IsEmpty()) Test.TestEqual(TEXT("已有 JSON 原料配置必须匹配，Coffee/Milk 不猜颜色"), Slot.Ingredient, FName(*Ingredient));
            for (const auto& Key : {FString(TEXT("fill_amount")), FString(TEXT("max_volume_ml"))})
            {
                const FString Value = Saved.Before.Value(Config, *Key);
                if (Value.IsEmpty()) continue; // JSON 没导出的根属性来自真实保存 map，不猜默认名称。
                double Parsed = 0;
                if (!Test.TestTrue(TEXT("已有 JSON 容量/填充属性可解析"), FDefaultValueHelper::ParseDouble(Value, Parsed))) return false;
                Near(Test, TEXT("夹具属性符合 Before JSON"), Key == TEXT("fill_amount") ? Slot.Fill : Slot.Capacity, Parsed);
            }
            auto* Bottle = Cast<ABottleActor>(Slot.Identity);
            if (Bottle && Bottle->CapActor) StopSimulation(Bottle->CapActor.Get());
        }
        Test.TestEqual(TEXT("经理实际捕获十五槽"), Manager->GetRestoreSlotCount(), 15);
        Test.TestEqual(TEXT("经理实际捕获三工具"), Manager->GetRestoreToolCount(), 3);
        Test.TestEqual(TEXT("实际 Game World 可抓根是十九，不把盖柄抽屉冰计为根"), GrabRoots(World, false).Num(), 19);
        return !Test.HasAnyErrors();
    }

    UMotionControllerComponent* Controller(const FVector& Position)
    {
        auto* Pawn = World->SpawnActor<APawn>();
        auto* MC = NewObject<UMotionControllerComponent>(Pawn);
        Pawn->SetRootComponent(MC); Pawn->AddInstanceComponent(MC);
        MC->SetTrackingSource(EControllerHand::Left); MC->RegisterComponent();
        Pawn->SetActorLocation(Position);
        return MC;
    }
    void OffsetRoots()
    {
        for (AActor* A : GrabRoots(World, false))
        {
            FTransform Pose = A->GetActorTransform();
            Pose.AddToTranslation(FVector(0, 20, 0));
            Pose.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(35.0)) * Pose.GetRotation());
            A->SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
        }
    }
    void Settle(double Seconds)
    {
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Seconds / .05));
        const float Dt = static_cast<float>(Seconds / Steps);
        for (int32 I = 0; I < Steps; ++I)
        {
            Transfer()->Simulate(Dt);
            for (const auto& Target : Manager->RestoreTargets)
                if (auto* A = Target.Get(); Alive(A)) A->ALiquidContainerActor::Tick(Dt);
            Kettle->ALiquidContainerActor::Tick(Dt); Dripper->ALiquidContainerActor::Tick(Dt);
        }
    }
};

void Balanced(FAutomationTestBase& Test, const FAllWorld& T)
{
    const double Error = T.Transfer()->GetBalance().ErrorML;
    Test.TestTrue(TEXT("真实液体账本 ErrorML 小于 1e-6 mL"), FMath::IsFinite(Error) && FMath::Abs(Error) < 1.e-6);
}

int32 Caps(UWorld* World)
{
    int32 Count = 0;
    for (TActorIterator<ABottleCapActor> It(World); It; ++It) if (Alive(*It)) ++Count;
    return Count;
}

int32 OwnedParts(UWorld* World, ACoffeeGrinderActor* Grinder)
{
    int32 Count = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (Alive(*It))
        {
            if (auto* H = Cast<AGrinderHandleActor>(*It)) Count += H->OwnerGrinder.Get() == Grinder;
            if (auto* D = Cast<AGrinderDrawerActor>(*It)) Count += D->OwnerGrinder.Get() == Grinder;
        }
    return Count;
}

bool PutIce(FAutomationTestBase& Test, FAllWorld& T, ACupActor* Cup, int32 Count)
{
    auto* IceComp = Cup->FindComponentByClass<UCupIceComponent>();
    if (!Test.TestNotNull(TEXT("真实杯冰组件"), IceComp)) return false;
    const FTransform Mouth = Cup->PourEntryPoint->GetComponentTransform();
    for (int32 I = 0; I < Count; ++I)
    {
        auto* Ice = T.World->SpawnActor<AIceCubeActor>(T.Scoop->IceClass, Mouth.TransformPosition(FVector(0, 0, 4)), FRotator::ZeroRotator);
        if (!Test.TestNotNull(TEXT("真实冰类实例"), Ice)) return false;
        StopSimulation(Ice);
        if (!Test.TestTrue(TEXT("真实入口跨越接受冰，不直接改 State/Count"), IceComp->TryAccept(Ice,
            Mouth.TransformPosition(FVector(0, 0, 4)), Mouth.TransformPosition(FVector(0, 0, -3))))) return false;
    }
    return Test.TestEqual(TEXT("真实杯内冰数量"), IceComp->GetIceCount(), Count);
}

bool LoadScoop(FAutomationTestBase& Test, FAllWorld& T)
{
    auto* Bucket = T.World->SpawnActor<AIceBucketActor>();
    if (!Test.TestNotNull(TEXT("真实冰桶夹具"), Bucket)) return false;
    Bucket->Mesh->SetMobility(EComponentMobility::Movable);
    Bucket->SetActorLocation(T.Scoop->Mesh->GetComponentTransform().TransformPosition(T.Scoop->BowlCenter) - FVector(0, 0, 12));
    T.Scoop->Bucket = Bucket;
    if (!Test.TestTrue(TEXT("真实 ContainsBowl 和装载 API，不绕过倾角/碰撞"), T.Scoop->TryLoadFromBucket())) return false;
    Test.TestEqual(TEXT("真实冰铲装载三块"), T.Scoop->GetLoadedCount(), 3);
    // 桶不是恢复根，移开以免夹具人为占据工具的初始位置。
    Bucket->SetActorLocation(FVector(1500, 1500, 0));
    return true;
}

void SameBalance(FAutomationTestBase& Test, const FLiquidBalance& B, const FLiquidBalance& A)
{
    Test.TestTrue(TEXT("预检账本逐字段完全相等，不用浮点容差掩盖修改"), A.InitialML == B.InitialML
        && A.InjectedML == B.InjectedML && A.RemovedML == B.RemovedML && A.ContainerML == B.ContainerML
        && A.InFlightML == B.InFlightML && A.SpilledML == B.SpilledML && A.ErrorML == B.ErrorML && A.PacketCount == B.PacketCount);
    Test.TestEqual(TEXT("预检不改 Initial"), A.InitialML, B.InitialML);
    Test.TestEqual(TEXT("预检不改 Injected"), A.InjectedML, B.InjectedML);
    Test.TestEqual(TEXT("预检不改 Removed"), A.RemovedML, B.RemovedML);
    Test.TestEqual(TEXT("预检不改 Container"), A.ContainerML, B.ContainerML);
    Test.TestEqual(TEXT("预检不改 Flight"), A.InFlightML, B.InFlightML);
    Test.TestEqual(TEXT("预检不改 Spilled"), A.SpilledML, B.SpilledML);
    Test.TestEqual(TEXT("预检不改 Error"), A.ErrorML, B.ErrorML);
    Test.TestEqual(TEXT("预检不改包数"), A.PacketCount, B.PacketCount);
}

struct FUnchanged
{
    struct FActorState
    {
        AActor* A;
        FTransform Pose;
        USceneComponent* Root;
        AActor* Owner;
        double Volume = 0;
        int32 Revision = 0, Ice = 0;
        bool Held = false, Pulled = false, CanPour = false, Accept = false, EmissionBlocked = false;
        UMotionControllerComponent* Controller = nullptr;
    };
    TArray<FActorState> Actors;
    FLiquidBalance Balance;
    int32 Generation, CapCount, Loaded, PartCount;
    double Beans, Grounds, DrawerGrounds, DripperGrounds, Budget, Consumed, Output, Loss;
    double DiscardBeans, DiscardGrounds, DiscardDrawer, DiscardFilter, Produced, Dripped;
    float HandleAngle;
    AGrinderDrawerActor::EDrawerState DrawerState;
    explicit FUnchanged(const FAllWorld& T)
        : Balance(T.Transfer()->GetBalance()), Generation(T.Manager->GetRestoreGeneration()),
          CapCount(Caps(T.World)), Loaded(T.Scoop->GetLoadedCount()), PartCount(OwnedParts(T.World, T.Grinder)),
          Beans(T.Grinder->GetCurrentBeanGrams()), Grounds(T.Grinder->GetCurrentGroundGrams()),
          DrawerGrounds(T.Grinder->DrawerRef->GetGroundsGrams()), DripperGrounds(T.Dripper->GetGroundsGrams()),
          Budget(T.Dripper->GetCoffeeBudgetML()), Consumed(T.Grinder->GetTotalBeanConsumedGrams()),
          Output(T.Grinder->GetTotalGroundOutputGrams()), Loss(T.Grinder->GetGrindLossGrams()),
          DiscardBeans(T.Grinder->DiscardedBeanGrams), DiscardGrounds(T.Grinder->DiscardedGroundGrams),
          DiscardDrawer(T.Grinder->DrawerRef->DiscardedGroundGrams), DiscardFilter(T.Dripper->DiscardedGroundsGrams),
          Produced(T.Dripper->CoffeeProducedML), Dripped(T.Dripper->DrippedML),
          HandleAngle(T.Grinder->HandleRef->CurrentAngleDeg), DrawerState(T.Grinder->DrawerRef->State)
    {
        for (TActorIterator<AActor> It(T.World); It; ++It)
        {
            AActor* A = *It; if (!Alive(A)) continue;
            FActorState S{A, A->GetActorTransform(), A->GetRootComponent(), A->GetOwner()};
            if (auto* L = Cast<ALiquidContainerActor>(A))
            {
                S.Volume = L->GetPreciseVolumeML(); S.Revision = L->GetLiquidRevision();
                S.CanPour = L->bCanPour; S.Accept = L->bAcceptLiquidFromOthers;
                S.EmissionBlocked = L->bWorkbenchRestoreEmissionBlocked;
                if (auto* Ice = L->FindComponentByClass<UCupIceComponent>()) S.Ice = Ice->GetIceCount();
            }
            if (auto* Grab = A->FindComponentByClass<UGrabComponent>())
            {
                S.Held = Grab->IsHeld(); S.Pulled = Grab->IsPulled(); S.Controller = Grab->GetHoldingController();
            }
            Actors.Add(S);
        }
    }
    void Check(FAutomationTestBase& Test, const FAllWorld& T) const
    {
        SameBalance(Test, Balance, T.Transfer()->GetBalance());
        int32 Live = 0;
        for (TActorIterator<AActor> It(T.World); It; ++It) if (Alive(*It)) ++Live;
        Test.TestEqual(TEXT("失败预检未生成或删除任何 actor"), Live, Actors.Num());
        for (const auto& S : Actors)
        {
            if (!Test.TestTrue(TEXT("拒绝后原身份仍存活"), Alive(S.A))) continue;
            Test.TestTrue(TEXT("拒绝不移动根/子件"), S.A->GetActorTransform().Equals(S.Pose, .0001));
            Test.TestTrue(TEXT("拒绝不改 root/owner"), S.A->GetRootComponent() == S.Root && S.A->GetOwner() == S.Owner);
            if (auto* L = Cast<ALiquidContainerActor>(S.A))
            {
                Test.TestEqual(TEXT("拒绝不改 mL"), L->GetPreciseVolumeML(), S.Volume);
                Test.TestEqual(TEXT("拒绝不改 revision"), L->GetLiquidRevision(), S.Revision);
                Test.TestTrue(TEXT("拒绝不提前停源/接收"), L->bCanPour == S.CanPour && L->bAcceptLiquidFromOthers == S.Accept);
                Test.TestEqual(TEXT("拒绝不遗留或覆盖发量门"), L->bWorkbenchRestoreEmissionBlocked, S.EmissionBlocked);
                if (auto* Ice = L->FindComponentByClass<UCupIceComponent>()) Test.TestEqual(TEXT("拒绝不删杯冰"), Ice->GetIceCount(), S.Ice);
            }
            if (auto* Grab = S.A->FindComponentByClass<UGrabComponent>())
                Test.TestTrue(TEXT("拒绝不强制 Drop/StopPull"), Grab->IsHeld() == S.Held && Grab->IsPulled() == S.Pulled
                    && Grab->GetHoldingController() == S.Controller);
        }
        Test.TestEqual(TEXT("拒绝不推进代数"), T.Manager->GetRestoreGeneration(), Generation);
        Test.TestEqual(TEXT("拒绝不预建盖"), Caps(T.World), CapCount);
        Test.TestEqual(TEXT("拒绝不丢铲内冰"), T.Scoop->GetLoadedCount(), Loaded);
        Test.TestEqual(TEXT("拒绝不新增子件"), OwnedParts(T.World, T.Grinder), PartCount);
        Near(Test, TEXT("拒绝保留豆"), T.Grinder->GetCurrentBeanGrams(), Beans);
        Near(Test, TEXT("拒绝保留主体粉"), T.Grinder->GetCurrentGroundGrams(), Grounds);
        Near(Test, TEXT("拒绝保留抽屉粉"), T.Grinder->DrawerRef->GetGroundsGrams(), DrawerGrounds);
        Near(Test, TEXT("拒绝保留滤杯粉"), T.Dripper->GetGroundsGrams(), DripperGrounds);
        Near(Test, TEXT("拒绝保留预算"), T.Dripper->GetCoffeeBudgetML(), Budget);
        Near(Test, TEXT("拒绝保留研磨消耗历史"), T.Grinder->GetTotalBeanConsumedGrams(), Consumed);
        Near(Test, TEXT("拒绝保留研磨产出历史"), T.Grinder->GetTotalGroundOutputGrams(), Output);
        Near(Test, TEXT("拒绝保留研磨损耗历史"), T.Grinder->GetGrindLossGrams(), Loss);
        Near(Test, TEXT("拒绝保留丢弃豆"), T.Grinder->DiscardedBeanGrams, DiscardBeans);
        Near(Test, TEXT("拒绝保留丢弃主体粉"), T.Grinder->DiscardedGroundGrams, DiscardGrounds);
        Near(Test, TEXT("拒绝保留丢弃抽屉粉"), T.Grinder->DrawerRef->DiscardedGroundGrams, DiscardDrawer);
        Near(Test, TEXT("拒绝保留丢弃滤杯粉"), T.Dripper->DiscardedGroundsGrams, DiscardFilter);
        Near(Test, TEXT("拒绝保留咖啡产量"), T.Dripper->CoffeeProducedML, Produced);
        Near(Test, TEXT("拒绝保留滴量"), T.Dripper->DrippedML, Dripped);
        Near(Test, TEXT("拒绝保留柄角"), T.Grinder->HandleRef->CurrentAngleDeg, HandleAngle);
        Test.TestTrue(TEXT("拒绝保留抽屉状态"), T.Grinder->DrawerRef->State == DrawerState);
        for (int32 I = 0; I < T.Slots.Num(); ++I)
            Test.TestTrue(TEXT("拒绝保留全部十五槽引用"), T.Manager->GetCurrent(I) == T.Slots[I].Identity);
    }
};

bool CheckRestored(FAutomationTestBase& Test, FAllWorld& T, const TArray<ALiquidContainerActor*>& Old,
    ALiquidContainerActor* Protected = nullptr)
{
    TSet<AActor*> Current;
    int32 Fresh = 0, ExpectedCaps = 0;
    for (int32 I = 0; I < T.Slots.Num(); ++I)
    {
        const auto& S = T.Slots[I];
        auto* A = T.Manager->GetCurrent(I);
        if (!Test.TestNotNull(TEXT("每个槽恢复后有效"), A)) return false;
        Current.Add(A);
        Test.TestTrue(TEXT("当前槽与公开 RestoreTargets 同步迁移"), T.Manager->RestoreTargets[I].Get() == A);
        if (A == Protected)
        {
            Test.TestTrue(TEXT("冻结槽没有替换"), A == Old[I]);
            continue;
        }
        ++Fresh;
        Test.TestTrue(TEXT("瓶杯使用新身份和保存 class"), A != Old[I] && A->GetClass() == S.Class && !Alive(Old[I]));
        Test.TestTrue(TEXT("全部槽恢复 Before 捕获位姿而非旧坐标"), A->GetActorTransform().Equals(S.Pose, .0001));
        Test.TestTrue(TEXT("原 root 网格不被换成测试几何"), A->GetRootComponent() == A->ContainerMesh.Get()
            && A->ContainerMesh->GetStaticMesh() == S.Mesh);
        Near(Test, TEXT("容量恢复保存属性"), A->MaxVolumeML, S.Capacity);
        Test.TestEqual(TEXT("原料身份不由颜色猜测"), A->InitialIngredientId, S.Ingredient);
        Test.TestTrue(TEXT("颜色仍为捕获初始配置"), A->LiquidColor.Equals(S.Color, .0001));
        const double Volume = Cast<ACupActor>(A) ? 0 : S.Volume;
        Near(Test, TEXT("杯空、瓶按捕获初始量补满且不超容量"), A->GetPreciseVolumeML(), Volume);
        Near(Test, TEXT("实际原料量"), A->GetIngredientVolumeML(S.Ingredient), Volume);
        Near(Test, TEXT("实际 Fill 与库存一致"), A->FillAmount, Volume / S.Capacity);
        Test.TestTrue(TEXT("新槽为 replacement、非冻结破碎并激活抓取"), A->bInitialVolumeIsReplacement
            && !A->IsLiquidFrozen() && !A->IsBroken() && A->GrabComp->IsActive());
        Test.TestFalse(TEXT("成功退出不遗留新槽发量门"), A->bWorkbenchRestoreEmissionBlocked);
        for (int32 M = 0; M < S.Materials.Num(); ++M)
            Test.TestTrue(TEXT("材质保持捕获资产，不继承运行时 MID"), MaterialParent(A->ContainerMesh->GetMaterial(M)) == S.Materials[M]);
        if (auto* Cup = Cast<ACupActor>(A))
        {
            auto* Ice = Cup->FindComponentByClass<UCupIceComponent>();
            if (!Test.TestNotNull(TEXT("新杯恢复真实冰支持"), Ice)) return false;
            Test.TestEqual(TEXT("新杯冰清零"), Ice->GetIceCount(), 0);
        }
        if (auto* B = Cast<ABottleActor>(A))
        {
            ExpectedCaps += S.StartCapped;
            Test.TestEqual(TEXT("瓶封口恢复初始值"), B->bIsCapped, S.StartCapped);
            if (S.StartCapped)
            {
                if (!Test.TestNotNull(TEXT("真正生成新盖而非假封口"), B->CapActor.Get())) return false;
                Test.TestTrue(TEXT("盖仍归新瓶并附着"), B->CapActor->OwnerBottle.Get() == B && B->CapActor->bIsAttached);
            }
        }
        // 先采样恢复结果，再停止无桌面夹具模拟，不修补业务结果。
        StopSimulation(A);
        QuietPresentation(A);
    }
    Test.TestEqual(TEXT("新鲜槽恰好十五或冻结保护后的十四"), Fresh, Protected ? 14 : 15);
    Test.TestEqual(TEXT("当前十五身份无重复"), Current.Num(), 15);
    Test.TestEqual(TEXT("盖数量与初始封口数精确一致"), Caps(T.World), ExpectedCaps);
    Test.TestEqual(TEXT("恢复后十九根总数恒定，包括冻结根，不计动态子件"), GrabRoots(T.World, false).Num(), 19);
    Test.TestEqual(TEXT("研磨器只有一柄一抽屉"), OwnedParts(T.World, T.Grinder), 2);
    for (int32 I = 0; I < T.ToolPoses.Num(); ++I)
    {
        AActor* Tool = T.Manager->GetCapturedTool(I);
        Test.TestTrue(TEXT("工具始终原地身份，独立初始引用而非只比较当前列表"),
            Tool == T.Spawned.FindChecked(T.Saved.Tools[I]) && Tool == T.Manager->RestoreToolTargets[I].Get());
        if (!Test.TestNotNull(TEXT("工具存活"), Tool)) return false;
        Test.TestTrue(TEXT("工具回到捕获完整 TM"), Tool->GetActorTransform().Equals(T.ToolPoses[I], .0001));
    }
    Test.TestTrue(TEXT("冰铲同一身份并回到原 TM"), T.Manager->RestoreScoop.Get() == T.Scoop
        && T.Scoop->GetActorTransform().Equals(T.ScoopPose, .0001));
    Test.TestEqual(TEXT("冰铲装载归零"), T.Scoop->GetLoadedCount(), 0);
    StopSimulation(T.Scoop);
    Balanced(Test, T);
    return !Test.HasAnyErrors();
}

TArray<ALiquidContainerActor*> CurrentSlots(const FAllWorld& T)
{
    TArray<ALiquidContainerActor*> A;
    for (int32 I = 0; I < T.Slots.Num(); ++I) A.Add(T.Manager->GetCurrent(I));
    return A;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesSavedManifest, "CyberCafe.AllSupplies.SavedMapCompleteManifest", Flags)
bool FAllSuppliesSavedManifest::RunTest(const FString&)
{
    FSavedSupplies S;
    if (!S.Load(*this)) return false;
    TestEqual(TEXT("保存地图仍是 Before 的 312 actor，没有删除"), S.Actors.Num(), S.Before.Actors.Num());
    for (const auto& Pair : S.Before.Actors)
    {
        AActor* const* Found = S.Actors.Find(Pair.Key);
        if (!TestTrue(Pair.Key + TEXT(" 未被脚本删除"), Found && Alive(*Found))) continue;
        TestEqual(Pair.Key + TEXT(" 保存 class 不变"), (*Found)->GetClass()->GetPathName(), Pair.Value.Class);
        TestTrue(Pair.Key + TEXT(" 用户完整布局不回套旧坐标"), (*Found)->GetActorTransform().Equals(Pair.Value.Pose, .001));
    }
    const auto* ManagerBefore = S.Before.Actors.Find(S.Manager->GetName());
    if (!TestNotNull(TEXT("经理来自 Before"), ManagerBefore)) return false;
    TestTrue(TEXT("用户经理位置仍是 (28,136,92)"), S.Manager->GetActorLocation().Equals(FVector(28, 136, 92), .001));
#if WITH_EDITOR
    TestEqual(TEXT("统一按钮只更新用途标签不变位置"), S.Manager->GetActorLabel(), FString(TEXT("Restore supplies - all interactable supplies - hold 1s")));
#endif
    TestFalse(TEXT("原生默认关闭全用品 opt-in，不改变旧双键契约"), GetDefault<AGlassRecoveryActor>()->bRestoreAllSupplies);
    Near(*this, TEXT("统一恢复必须持续一秒"), S.Manager->HoldSeconds, 1);
    TSet<AActor*> Manifest;
    for (auto* A : S.Vessels)
    {
        TestTrue(TEXT("十五替换根确为瓶杯，不把壶滤杯放入 replacement"),
            (Cast<ABottleActor>(A) || Cast<ACupActor>(A)) && !Cast<AHandPourKettleActor>(A));
        Manifest.Add(A);
    }
    int32 Kettles = 0, Drippers = 0, Grinders = 0;
    for (auto* A : S.Tools)
    {
        Kettles += Cast<AHandPourKettleActor>(A) != nullptr;
        Drippers += Cast<AHandDripperActor>(A) != nullptr;
        Grinders += Cast<ACoffeeGrinderActor>(A) != nullptr;
        TestFalse(TEXT("工具与瓶杯清单严格不交叉"), Manifest.Contains(A));
        Manifest.Add(A);
    }
    TestEqual(TEXT("显式壶一个"), Kettles, 1); TestEqual(TEXT("显式滤杯一个"), Drippers, 1); TestEqual(TEXT("显式研磨器一个"), Grinders, 1);
    TestEqual(TEXT("指定原冰铲"), S.Scoop->GetName(), FString(TEXT("IceScoop_Practice")));
    Manifest.Add(S.Scoop);
    TestEqual(TEXT("完整清单 union 恰好十九根"), Manifest.Num(), 19);
    const TSet<AActor*> AllMap = GrabRoots(S.Map, true);
    TestEqual(TEXT("地图 UGrabComponent owner 去除运行时子件后恰好十九根"), AllMap.Num(), 19);
    for (auto* A : AllMap) TestTrue(TEXT("每个真实可抓根都在 union，不能只凭数量声称全覆盖"), Manifest.Contains(A));
    for (auto* A : Manifest) TestTrue(TEXT("每个清单根都是真实 map UGrab owner"), AllMap.Contains(A));
    for (const auto& Pair : S.Actors)
        TestFalse(TEXT("保存布局无 beanJar/beanScoop，不能从其他地图偷偷补根"),
            Pair.Value->GetClass()->GetName().Contains(TEXT("BeanJar")) || Pair.Value->GetClass()->GetName().Contains(TEXT("CoffeeScoop")));
    auto* Screen = Cast<AOrderHologramActor>(S.Actors.FindRef(TEXT("BP_OrderHologram_Tutorial")));
    if (!TestNotNull(TEXT("原菜单仍在地图"), Screen)) return false;
    TestTrue(TEXT("新菜单朝向在保存实例 opt-in"), Screen->bFaceHeadset);
    TestFalse(TEXT("菜单原生默认仍 opt-out"), GetDefault<AOrderHologramActor>()->bFaceHeadset);
    TestNotNull(TEXT("菜单保留原保存配方数据"), Screen->RecipeMenu.Get());
    TestFalse(TEXT("map-only 从未 BeginPlay"), S.Map->HasBegunPlay());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesRestoreRoots, "CyberCafe.AllSupplies.RealConfiguredRestoreAllRoots", Flags)
bool FAllSuppliesRestoreRoots::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    const TArray<ALiquidContainerActor*> Old = CurrentSlots(T);
    auto* KettleIdentity = T.Kettle; auto* DripperIdentity = T.Dripper; auto* GrinderIdentity = T.Grinder;
    auto* Handle = T.Grinder->HandleRef.Get(); auto* Drawer = T.Grinder->DrawerRef.Get(); auto* ScoopGrab = T.Scoop->Grab.Get();
    const auto KettleMode = T.Kettle->GrabComp->PhysicsMode, GrinderMode = T.Grinder->GrabComp->PhysicsMode,
        DripperMode = T.Dripper->GrabComp->PhysicsMode;
    const auto KettleGrabType = T.Kettle->GrabComp->GrabType, DripperGrabType = T.Dripper->GrabComp->GrabType,
        GrinderGrabType = T.Grinder->GrabComp->GrabType;
    const bool KettleSim = T.Kettle->ContainerMesh->IsSimulatingPhysics(), GrinderSim = T.Grinder->BodyMesh->IsSimulatingPhysics(),
        DripperSim = T.Dripper->ContainerMesh->IsSimulatingPhysics();
    auto* KettleRoot = T.Kettle->GetRootComponent(); auto* DripperRoot = T.Dripper->GetRootComponent(); auto* GrinderRoot = T.Grinder->GetRootComponent();
    auto* KettleGrab = T.Kettle->GrabComp.Get(); auto* DripperGrab = T.Dripper->GrabComp.Get(); auto* GrinderGrab = T.Grinder->GrabComp.Get();
    const double KettleInitial = T.Kettle->GetPreciseVolumeML();
    Near(*this, TEXT("真壶初始 Water500"), KettleInitial, 500);
    const double RemovedBeforeConsume = T.Transfer()->GetBalance().RemovedML;
    Near(*this, TEXT("公开壶 ConsumeLiquid200"), T.Kettle->ConsumeLiquid(200), 200);
    Near(*this, TEXT("壶消耗真正记 Removed，不直接改 Fill"), T.Transfer()->GetBalance().RemovedML - RemovedBeforeConsume, 200);
    Near(*this, TEXT("真实滤杯入口加入五克"), T.Dripper->AddGrounds(5, T.Dripper->PourEntryPoint->GetComponentLocation()), 5);
    Near(*this, TEXT("真实滤杯注水五十毫升"), T.Dripper->AddIngredientLiquid(50, Water, T.Kettle->WaterStreamColor), 50);
    T.Dripper->AdvanceBrew(T.Dripper->BloomSeconds + .1f);
    TestTrue(TEXT("真实冲煮已产咖啡和液包"), T.Dripper->CoffeeProducedML > 0 && T.Transfer()->GetBalance().PacketCount > 0);
    T.Settle(T.Transfer()->GetUnsettledFlightHorizon() + .1);
    Near(*this, TEXT("真实研磨器投六克"), T.Grinder->TryAddBeans(6), 6);
    Near(*this, TEXT("AdvanceGrinding360/.3 真消耗二点五克"), T.Grinder->AdvanceGrinding(360, .3f), 2.5);
    Near(*this, TEXT("生产回调真搬粉入抽屉"), Drawer->GetGroundsGrams(), 2.5 * T.Grinder->GrindEfficiency);
    const float R = Handle->HandleArmRadius;
    TestTrue(TEXT("柄采样立基准"), Handle->ApplyHandSample(FVector(R, 0, 0), .05f));
    TestTrue(TEXT("柄采样真实旋转三十度"), Handle->ApplyHandSample(FVector(R * FMath::Cos(PI / 6), R * FMath::Sin(PI / 6), 0), .05f));
    TestTrue(TEXT("重置前真实柄角非零"), Handle->CurrentAngleDeg > 0);
    auto* MC = T.Controller(Drawer->GetActorLocation());
    if (!TestTrue(TEXT("真实抓抽屉"), Drawer->GrabComp->TryGrab(MC))) return false;
    Drawer->DetachFromGrinder(MC); Drawer->SetActorLocation(Drawer->GetActorLocation() + FVector(0, 100, 0));
    if (!TestTrue(TEXT("真实 Drop 抽屉"), Drawer->GrabComp->TryRelease())) return false;
    StopSimulation(Drawer);
    T.OffsetRoots();
    for (auto* A : Old)
    {
        if (auto* Cup = Cast<ACupActor>(A))
        {
            Near(*this, TEXT("每只旧杯真实加入一百二十毫升"), Cup->AddIngredientLiquid(120, Milk, FLinearColor::White), 120);
            if (!PutIce(*this, T, Cup, 3)) return false;
        }
        else Near(*this, TEXT("每只旧瓶真实消耗二十毫升"), A->ConsumeLiquid(20), 20);
    }
    if (!LoadScoop(*this, T)) return false;
    const auto Before = T.Transfer()->GetBalance();
    double Removed = T.Kettle->GetPreciseVolumeML() + T.Dripper->GetPreciseVolumeML(), Injected = KettleInitial;
    for (int32 I = 0; I < Old.Num(); ++I) { Removed += Old[I]->GetPreciseVolumeML(); if (!Cast<ACupActor>(Old[I])) Injected += T.Slots[I].Volume; }
    const double Beans = T.Grinder->GetCurrentBeanGrams(), Ground = T.Grinder->GetCurrentGroundGrams(), DrawerGround = Drawer->GetGroundsGrams();
    const double Consumed = T.Grinder->GetTotalBeanConsumedGrams(), Output = T.Grinder->GetTotalGroundOutputGrams(), Loss = T.Grinder->GetGrindLossGrams();
    const double Produced = T.Dripper->CoffeeProducedML, Dripped = T.Dripper->DrippedML;
    const double DiscardBeans = T.Grinder->DiscardedBeanGrams, DiscardGround = T.Grinder->DiscardedGroundGrams;
    const double DiscardDrawer = Drawer->DiscardedGroundGrams, DiscardFilter = T.Dripper->DiscardedGroundsGrams;
    const int32 CapsBefore = Caps(T.World);
    if (!TestTrue(TEXT("真实配置十九根整批恢复成功"), T.Manager->RestoreWorkbench())) return false;
    if (!CheckRestored(*this, T, Old)) return false;
    TestTrue(TEXT("三个工具 identity 原封保留"), T.Kettle == KettleIdentity && T.Dripper == DripperIdentity && T.Grinder == GrinderIdentity);
    TestTrue(TEXT("柄、抽屉、铲抓取 identity 原封保留"), T.Grinder->HandleRef.Get() == Handle && T.Grinder->DrawerRef.Get() == Drawer && T.Scoop->Grab.Get() == ScoopGrab);
    TestTrue(TEXT("工具 root 与抓取模式不变"), T.Kettle->GetRootComponent() == T.Kettle->ContainerMesh.Get()
        && T.Grinder->GetRootComponent() == T.Grinder->BodyMesh.Get() && T.Kettle->GrabComp->PhysicsMode == KettleMode
        && T.Grinder->GrabComp->PhysicsMode == GrinderMode && T.Dripper->GrabComp->PhysicsMode == DripperMode
        && T.Kettle->GrabComp->GrabType == KettleGrabType && T.Dripper->GrabComp->GrabType == DripperGrabType
        && T.Grinder->GrabComp->GrabType == GrinderGrabType);
    TestTrue(TEXT("三工具的 Root/Grab 组件 identity 不变"), T.Kettle->GetRootComponent() == KettleRoot
        && T.Dripper->GetRootComponent() == DripperRoot && T.Grinder->GetRootComponent() == GrinderRoot
        && T.Kettle->GrabComp.Get() == KettleGrab && T.Dripper->GrabComp.Get() == DripperGrab && T.Grinder->GrabComp.Get() == GrinderGrab);
    TestTrue(TEXT("三工具模拟模式保留"), T.Kettle->ContainerMesh->IsSimulatingPhysics() == KettleSim
        && T.Grinder->BodyMesh->IsSimulatingPhysics() == GrinderSim && T.Dripper->ContainerMesh->IsSimulatingPhysics() == DripperSim);
    Near(*this, TEXT("壶补满原初始量"), T.Kettle->GetPreciseVolumeML(), KettleInitial);
    Near(*this, TEXT("滤杯水粉预算均清零"), T.Dripper->GetPreciseVolumeML() + T.Dripper->GetGroundsGrams() + T.Dripper->GetCoffeeBudgetML(), 0);
    Near(*this, TEXT("滤杯真实丢弃五克，非改生产历史"), T.Dripper->DiscardedGroundsGrams - DiscardFilter, 5);
    Near(*this, TEXT("滤杯咖啡历史保留"), T.Dripper->CoffeeProducedML, Produced); Near(*this, TEXT("滤杯滴量历史保留"), T.Dripper->DrippedML, Dripped);
    Near(*this, TEXT("研磨器及抽屉库存清空"), T.Grinder->GetCurrentBeanGrams() + T.Grinder->GetCurrentGroundGrams() + Drawer->GetGroundsGrams(), 0);
    Near(*this, TEXT("主体丢弃豆独立计账"), T.Grinder->DiscardedBeanGrams - DiscardBeans, Beans);
    Near(*this, TEXT("主体丢弃粉独立计账"), T.Grinder->DiscardedGroundGrams - DiscardGround, Ground);
    Near(*this, TEXT("抽屉丢弃粉独立计账"), Drawer->DiscardedGroundGrams - DiscardDrawer, DrawerGround);
    Near(*this, TEXT("研磨消耗历史不归零"), T.Grinder->GetTotalBeanConsumedGrams(), Consumed);
    Near(*this, TEXT("研磨产出历史不归零"), T.Grinder->GetTotalGroundOutputGrams(), Output);
    Near(*this, TEXT("研磨损耗历史不归零"), T.Grinder->GetGrindLossGrams(), Loss);
    Near(*this, TEXT("研磨质量守恒"), Consumed, Output + Loss);
    Near(*this, TEXT("柄角归零"), Handle->CurrentAngleDeg, 0);
    TestTrue(TEXT("抽屉合上且 OwnerRefs 正确"), Drawer->State == AGrinderDrawerActor::EDrawerState::Attached
        && Drawer->OwnerGrinder.Get() == T.Grinder && Handle->OwnerGrinder.Get() == T.Grinder
        && Drawer->GetRootComponent()->GetAttachParent() == T.Grinder->DrawerMountPoint.Get()
        && Handle->GetRootComponent()->GetAttachParent() == T.Grinder->HandleMountPoint.Get());
    Near(*this, TEXT("恢复旧库存 Removed 增量合理且精确"), T.Transfer()->GetBalance().RemovedML - Before.RemovedML, Removed);
    Near(*this, TEXT("仅瓶与壶新供给记 Injected"), T.Transfer()->GetBalance().InjectedML - Before.InjectedML, Injected);
    Near(*this, TEXT("Initial 历史不改"), T.Transfer()->GetBalance().InitialML, Before.InitialML);
    TestEqual(TEXT("没有盖数尖峰"), Caps(T.World), CapsBefore);
    const auto First = CurrentSlots(T); const auto FirstBalance = T.Transfer()->GetBalance();
    T.Manager->AdvanceRestoreHold(0, false);
    TestFalse(TEXT("第二次按半秒不足"), T.Manager->AdvanceRestoreHold(.5f, true));
    if (!TestTrue(TEXT("松开后第二次整秒恢复"), T.Manager->AdvanceRestoreHold(.5f, true))) return false;
    if (!CheckRestored(*this, T, First)) return false;
    TestEqual(TEXT("第二次只有第二代"), T.Manager->GetRestoreGeneration(), 2);
    Near(*this, TEXT("重复恢复库存不累积"), T.Transfer()->GetBalance().ContainerML, FirstBalance.ContainerML);
    TestEqual(TEXT("重复恢复盖数不增加"), Caps(T.World), CapsBefore);
    Near(*this, TEXT("空工具重复恢复不再丢豆"), T.Grinder->DiscardedBeanGrams, DiscardBeans + Beans);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesHold, "CyberCafe.AllSupplies.OneSecondHoldSingleVisibleKey", Flags)
bool FAllSuppliesHold::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    UStaticMeshComponent* Key0 = nullptr; UStaticMeshComponent* Key1 = nullptr;
    UTextRenderComponent* Label0 = nullptr;
    TArray<UStaticMeshComponent*> Keys; T.Manager->GetComponents(Keys);
    for (auto* K : Keys) { if (K->GetFName() == TEXT("Key0")) Key0 = K; if (K->GetFName() == TEXT("Key1")) Key1 = K; }
    TArray<UTextRenderComponent*> Labels; T.Manager->GetComponents(Labels);
    for (auto* L : Labels) if (L->GetFName() == TEXT("Label0")) Label0 = L;
    if (!TestNotNull(TEXT("原生 Key0"), Key0) || !TestNotNull(TEXT("原生 Key1"), Key1) || !TestNotNull(TEXT("原生 Label0"), Label0)) return false;
    TestTrue(TEXT("只显示第一实体键"), Key0->IsVisible() && !Key1->IsVisible());
    TestTrue(TEXT("第一 Button 激活而第二停用"), T.Manager->CupButton->IsActive() && !T.Manager->BottleButton->IsActive());
    TestEqual(TEXT("统一键实际标签"), Label0->Text.ToString(), FString(TEXT("RESTORE SUPPLIES")));
    const FTransform ButtonTM = T.Manager->CupButton->GetComponentTransform();
    const FVector E = T.Manager->CupButton->GetUnscaledBoxExtent();
    const FVector MeasuredContact = ButtonTM.TransformPosition(FVector(E.X * .4, E.Y * .4, 0));
    const FBox Box(-E, E);
    TestTrue(TEXT("采用真实 ButtonTM 的局部几何接触点"), Box.IsInsideOrOn(ButtonTM.InverseTransformPosition(MeasuredContact)));
    TestFalse(TEXT("实际几何外点不接触"), Box.IsInsideOrOn(ButtonTM.InverseTransformPosition(ButtonTM.TransformPosition(FVector(E.X + 1, 0, 0)))));
    const auto Before = T.Transfer()->GetBalance();
    TestFalse(TEXT("显式累计半秒不触发"), T.Manager->AdvanceRestoreHold(.5f, true));
    Near(*this, TEXT("半秒进度"), T.Manager->GetRestoreProgress(), .5);
    SameBalance(*this, Before, T.Transfer()->GetBalance());
    if (!TestTrue(TEXT("再半秒只触发一代"), T.Manager->AdvanceRestoreHold(.5f, true))) return false;
    const auto First = CurrentSlots(T); const auto Balance = T.Transfer()->GetBalance(); const int32 CapCount = Caps(T.World);
    for (int32 I = 0; I < 4; ++I) TestFalse(TEXT("继续两秒锁存，不重复创建"), T.Manager->AdvanceRestoreHold(.5f, true));
    TestEqual(TEXT("持续按代数仍为一"), T.Manager->GetRestoreGeneration(), 1);
    SameBalance(*this, Balance, T.Transfer()->GetBalance());
    TestTrue(TEXT("持续按保留首次十五身份"), CurrentSlots(T) == First);
    TestEqual(TEXT("持续按盖数不变"), Caps(T.World), CapCount);
    T.Manager->AdvanceRestoreHold(0, false);
    Near(*this, TEXT("松开清零进度"), T.Manager->GetRestoreProgress(), 0);
    TestFalse(TEXT("松开后半秒仍不足"), T.Manager->AdvanceRestoreHold(.5f, true));
    TestTrue(TEXT("松开后完整一秒可触发第二代"), T.Manager->AdvanceRestoreHold(.5f, true));
    TestEqual(TEXT("代数恰好二"), T.Manager->GetRestoreGeneration(), 2);
    TestEqual(TEXT("长按恢复十九根恒定"), GrabRoots(T.World, false).Num(), 19);
    Balanced(*this, T);
    AddInfo(TEXT("只验证 AdvanceRestoreHold 明确时间和实体键几何；未调用要求真实 MC.IsTracked 的 Tick 轮询，不声称硬件输入验收。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesBusy, "CyberCafe.AllSupplies.HeldBottleCapScoopAndGrinderChildrenPreflight", Flags)
bool FAllSuppliesBusy::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    auto* Bottle = Cast<ABottleActor>(T.Manager->PrototypeBottle.Get());
    if (!TestNotNull(TEXT("真实初始瓶盖"), Bottle ? Bottle->CapActor.Get() : nullptr)) return false;
    auto* Cap = Bottle->CapActor.Get(); QuietPresentation(Cap);
    Near(*this, TEXT("忙碌前真实加滤杯粉"), T.Dripper->AddGrounds(5, T.Dripper->PourEntryPoint->GetComponentLocation()), 5);
    Near(*this, TEXT("忙碌前真实加豆"), T.Grinder->TryAddBeans(6), 6);
    T.Grinder->AdvanceGrinding(360, .3f);
    if (!LoadScoop(*this, T)) return false;
    const TArray<UGrabComponent*> Busy = {Bottle->GrabComp.Get(), Cap->GrabComp.Get(), T.Scoop->Grab.Get(),
        T.Kettle->GrabComp.Get(), T.Dripper->GrabComp.Get(), T.Grinder->GrabComp.Get(),
        T.Grinder->HandleRef->GrabComp.Get(), T.Grinder->DrawerRef->GrabComp.Get()};
    for (auto* Grab : Busy)
    {
        auto* MC = T.Controller(Grab->GetComponentLocation());
        if (!TestTrue(TEXT("真实 TryGrab 成功，不反射写私有 Held"), Grab->TryGrab(MC))) return false;
        if (Grab == Cap->GrabComp.Get()) Cap->DetachFromBottle(MC);
        if (Grab == T.Grinder->DrawerRef->GrabComp.Get()) T.Grinder->DrawerRef->DetachFromGrinder(MC);
        TestTrue(TEXT("忙碌状态来自公开 getter"), Grab->IsHeld());
        const FUnchanged Before(T);
        TestFalse(TEXT("任一根/相关子件 Held 整批预检拒绝"), T.Manager->RestoreWorkbench());
        Before.Check(*this, T);
        TestEqual(TEXT("拒绝明确要求放下用品"), T.Manager->RestoreStatus.ToString(), FString(TEXT("PUT DOWN SUPPLIES")));
        if (!TestTrue(TEXT("真实 Drop，不强制模拟状态"), Grab->TryRelease())) return false;
        StopSimulation(Grab->GetOwner());
        if (Grab == Cap->GrabComp.Get()) Cap->ReattachToBottle();
        if (Grab == T.Grinder->DrawerRef->GrabComp.Get()) T.Grinder->DrawerRef->ReattachToGrinder();
    }
    // 只对真实允许远程拉取的瓶/铲/主体使用 TryPull；不把不可远程抓的柄盖强改为可拉。
    for (auto* Grab : {Bottle->GrabComp.Get(), T.Scoop->Grab.Get(), T.Grinder->GrabComp.Get()})
    {
        if (!TestTrue(TEXT("真实 TryPull"), Grab->TryPull(EControllerHand::Left))) return false;
        const FUnchanged Before(T);
        TestFalse(TEXT("Pulled 整批拒绝"), T.Manager->RestoreWorkbench()); Before.Check(*this, T);
        Grab->StopPull();
    }
    if (!TestTrue(TEXT("失败锁存前再次真拿瓶"), Bottle->GrabComp->TryGrab(T.Controller(Bottle->GetActorLocation())))) return false;
    const FUnchanged BeforeHold(T);
    TestFalse(TEXT("半秒忙碌不足阈值"), T.Manager->AdvanceRestoreHold(.5f, true));
    TestFalse(TEXT("到一秒因忙碌失败"), T.Manager->AdvanceRestoreHold(.5f, true)); BeforeHold.Check(*this, T);
    if (!TestTrue(TEXT("失败锁存后真实放下瓶"), Bottle->GrabComp->TryRelease())) return false;
    StopSimulation(Bottle);
    const FUnchanged AfterDrop(T);
    for (int32 I = 0; I < 4; ++I) TestFalse(TEXT("放下但不松键，失败仍锁存"), T.Manager->AdvanceRestoreHold(.5f, true));
    AfterDrop.Check(*this, T);
    T.Manager->AdvanceRestoreHold(0, false);
    TestTrue(TEXT("松键之后全部闲置才允许十九根恢复"), T.Manager->RestoreWorkbench());
    TestEqual(TEXT("仅成功推进一代"), T.Manager->GetRestoreGeneration(), 1);
    Balanced(*this, T);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesFrozen, "CyberCafe.AllSupplies.FrozenCupProtectionWithinTargets", Flags)
bool FAllSuppliesFrozen::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    const auto Old = CurrentSlots(T);
    auto* Cup = Cast<ACupActor>(T.Manager->PrototypeCup.Get());
    if (!TestNotNull(TEXT("清单内真实首杯"), Cup)) return false;
    auto* SavedScreen = Cast<AOrderHologramActor>(T.Saved.Actors.FindRef(TEXT("BP_OrderHologram_Tutorial")));
    if (!TestNotNull(TEXT("保存菜单"), SavedScreen) || !TestNotNull(TEXT("保存真实菜单 DA"), SavedScreen->RecipeMenu.Get())) return false;
    const FDrinkMenuEntry* Latte = nullptr;
    for (const auto& Entry : SavedScreen->RecipeMenu->Entries)
        if (Entry.Recipe && Entry.Recipe->RecipeId == TEXT("Latte")) { Latte = &Entry; break; }
    if (!TestNotNull(TEXT("实际菜单中的 Latte 配方，不伪造完成订单"), Latte)) return false;
    const FTransform MenuPose = T.Saved.Before.Actors.FindChecked(SavedScreen->GetName()).Pose;
    auto* Screen = T.World->SpawnActorDeferred<AOrderHologramActor>(AOrderHologramActor::StaticClass(), MenuPose,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("原生真实订单菜单夹具"), Screen)) return false;
    Screen->RecipeMenu = SavedScreen->RecipeMenu; Screen->bFaceHeadset = true;
    Screen->PanelCenter = SavedScreen->PanelCenter; Screen->PanelWidthCM = SavedScreen->PanelWidthCM;
    Screen->FinishSpawning(MenuPose);
    auto* Spot = T.World->SpawnActor<AServingSpotActor>();
    if (!TestNotNull(TEXT("真实服务点"), Spot)) return false;
    Spot->SetActorLocation(FVector(1200, 1000, 100)); // 明确非恢复目标位置，避免冻结杯占其他根位置。
    Spot->SetActiveRecipe(Latte->Recipe.Get());
    if (!TestTrue(TEXT("真实 BeginOrder 建立当前订单"), Screen->BeginOrder(Latte->Recipe.Get(), Spot, nullptr, FText::FromString(TEXT("测试顾客"))))) return false;
    Cup->SetActorLocation(Spot->GetActorLocation());
    for (const auto& Ingredient : Latte->Ingredients)
        Near(*this, TEXT("按真实菜单配方加入实际液体"), Cup->AddIngredientLiquid(static_cast<float>(Ingredient.VolumeML), Ingredient.IngredientId, FLinearColor::White), Ingredient.VolumeML);
    if (!PutIce(*this, T, Cup, 3)) return false;
    if (!TestTrue(TEXT("真实 Evaluate/TrySubmitCup 成功，非 MarkCompleted 或历史字段写入"), Spot->TrySubmitCup(Cup))) return false;
    TestTrue(TEXT("已交付杯真的冻结"), Cup->IsLiquidFrozen());
    const FTransform FrozenPose = Cup->GetActorTransform();
    const double FrozenVolume = Cup->GetPreciseVolumeML();
    const auto FrozenComposition = Cup->GetComposition(); const int32 Revision = Cup->GetLiquidRevision();
    auto* IceComp = Cup->FindComponentByClass<UCupIceComponent>();
    const int32 IceRevision = IceComp->GetPreparationRevision();
    const auto OrderSerial = Spot->GetOrderSerial(), AcceptedSerial = Spot->GetAcceptedOrderSerial();
    const auto Validation = Spot->GetLastValidation();
    const auto CurrentRecipe = Screen->GetCurrentRecipe(); const auto CurrentSerial = Screen->GetCurrentOrderSerial();
    const auto Records = Screen->GetDeliveryRecords();
    TestTrue(TEXT("真实事件形成已交付状态及历史"), Screen->IsDelivered() && Records.Num() > 0);
    // 真实浏览与朝向只改表现；恢复不得把选中菜单项变成订单或清历史。
    if (!TestTrue(TEXT("真实菜单浏览开启"), Screen->ToggleRecipeMenu())) return false;
    if (!TestTrue(TEXT("真实菜单浏览下一项"), Screen->BrowseRecipeMenu(1))) return false;
    if (!TestTrue(TEXT("公开朝向 API 使用明确相机位置，不冒充 HMD"), Screen->UpdatePanelFacing(MenuPose.GetLocation() + FVector(-50, -80, 80), 0))) return false;
    const int32 MenuIndex = Screen->GetSelectedMenuIndex(); const FTransform PanelPose = Screen->Panel->GetComponentTransform();
    const auto Balance = T.Transfer()->GetBalance();
    double ExpectedRemoved = T.Kettle->GetPreciseVolumeML() + T.Dripper->GetPreciseVolumeML();
    for (auto* A : Old) if (A != Cup) ExpectedRemoved += A->GetPreciseVolumeML();
    if (!TestTrue(TEXT("目标内冻结杯不阻断其他十八根恢复"), T.Manager->RestoreWorkbench())) return false;
    Near(*this, TEXT("冻结液体不计入 Removed"), T.Transfer()->GetBalance().RemovedML - Balance.RemovedML, ExpectedRemoved);
    if (!CheckRestored(*this, T, Old, Cup)) return false;
    TestTrue(TEXT("冻结身份保持，状态文案明确 protected"), Alive(Cup) && Cup->IsLiquidFrozen()
        && T.Manager->RestoreStatus.ToString() == TEXT("RESTORED - PROTECTED"));
    TestTrue(TEXT("冻结杯不移回旧供应位置或解除服务附着"), Cup->GetActorTransform().Equals(FrozenPose, .0001) && Cup->IsAttachedTo(Spot));
    Near(*this, TEXT("冻结液体未变"), Cup->GetPreciseVolumeML(), FrozenVolume);
    TestEqual(TEXT("冻结液 revision 未变"), Cup->GetLiquidRevision(), Revision);
    for (const auto& Ingredient : FrozenComposition.Ingredients)
        Near(*this, TEXT("冻结实际成分逐项未改"), Cup->GetIngredientVolumeML(Ingredient.Key), Ingredient.Value);
    TestEqual(TEXT("冻结杯仍有三块原冰"), IceComp->GetIceCount(), 3);
    TestEqual(TEXT("冻结冰 revision 未变"), IceComp->GetPreparationRevision(), IceRevision);
    TestTrue(TEXT("同一服务订单与 ServedCup 保持"), Spot->GetServedCup() == Cup && Spot->HasBeenServed()
        && Spot->ActiveRecipe.Get() == Latte->Recipe.Get());
    TestEqual(TEXT("OrderSerial 不 reset"), Spot->GetOrderSerial(), OrderSerial);
    TestEqual(TEXT("AcceptedSerial 不 reset"), Spot->GetAcceptedOrderSerial(), AcceptedSerial);
    TestEqual(TEXT("验收 revision 不 reset"), Spot->GetLastValidation().LiquidRevision, Validation.LiquidRevision);
    TestTrue(TEXT("CurrentRecipe/Current订单策略不随 reset"), Screen->GetCurrentRecipe() == CurrentRecipe && Screen->GetCurrentOrderSerial() == CurrentSerial);
    TestEqual(TEXT("浏览选中项保持"), Screen->GetSelectedMenuIndex(), MenuIndex);
    TestTrue(TEXT("朝向 opt-in 和 Panel TM 保持，root 不变"), Screen->bFaceHeadset && Screen->Panel->GetComponentTransform().Equals(PanelPose, .0001)
        && Screen->GetActorTransform().Equals(MenuPose, .0001));
    TestEqual(TEXT("真实历史数不清零或新增假记录"), Screen->GetDeliveryRecords().Num(), Records.Num());
    for (int32 I = 0; I < Records.Num(); ++I)
    {
        const auto& After = Screen->GetDeliveryRecords()[I];
        TestTrue(TEXT("真实交付记录完整保留"), After.RecipeId == Records[I].RecipeId && After.CustomerId == Records[I].CustomerId
            && After.DrinkName.EqualTo(Records[I].DrinkName) && After.VolumeML == Records[I].VolumeML);
    }
    Near(*this, TEXT("冻结恢复不改 Initial 历史"), T.Transfer()->GetBalance().InitialML, Balance.InitialML);
    // 核心旧 Bug：预建阶段不能给冻结槽再生成一只未发布杯。
    int32 SameClass = 0;
    for (TActorIterator<ALiquidContainerActor> It(T.World); It; ++It) if (Alive(*It) && It->GetClass() == Cup->GetClass()) ++SameClass;
    int32 ExpectedClass = 0; for (const auto& Slot : T.Slots) ExpectedClass += Slot.Class == Cup->GetClass();
    TestEqual(TEXT("冻结槽 class 数恒定，未发布预建 duplicate 也会被捕获"), SameClass, ExpectedClass);
    const auto First = CurrentSlots(T);
    T.Manager->AdvanceRestoreHold(0, false);
    TestFalse(TEXT("保护状态下再次半秒"), T.Manager->AdvanceRestoreHold(.5f, true));
    if (!TestTrue(TEXT("保护状态下再次整秒"), T.Manager->AdvanceRestoreHold(.5f, true))) return false;
    if (!CheckRestored(*this, T, First, Cup)) return false;
    TestEqual(TEXT("重复恢复历史仍无新增"), Screen->GetDeliveryRecords().Num(), Records.Num());
    Near(*this, TEXT("重复保护不改冻结液体"), Cup->GetPreciseVolumeML(), FrozenVolume);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesFlight, "CyberCafe.AllSupplies.PreparationFlightAndAudioStop", Flags)
bool FAllSuppliesFlight::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    const auto Old = CurrentSlots(T);
    auto* Cup = Cast<ACupActor>(T.Manager->PrototypeCup.Get()); if (!Cup) return false;
    const FVector Mouth = Cup->PourEntryPoint->GetComponentLocation();
    // 不修改毫升、flow 或 pouring 状态；真实倾斜壶从实际嘴口发包。
    T.Kettle->SetActorRotation(FRotator(-80, 55, 0));
    const FTransform ActualSpout = T.Kettle->GetKettleSpoutWorldTransform();
    TestTrue(TEXT("实际壶嘴朝下，满足生产倾斜门限"), ActualSpout.GetUnitAxis(EAxis::X).Z < -.3);
    const double KettleBefore = T.Kettle->GetPreciseVolumeML();
    const double Emitted = T.Kettle->PourWater(.1f);
    if (!TestTrue(TEXT("真实 PourWater(.1) 发包而非修改测试计数器"), Emitted > 0)) return false;
    Near(*this, TEXT("真实壶扣量等于发包量"), KettleBefore - T.Kettle->GetPreciseVolumeML(), Emitted);
    TestTrue(TEXT("真实出流状态"), T.Kettle->bIsPouring && T.Kettle->CurrentFlowMLPerSecond > 0);
    TestTrue(TEXT("真实在途包已产生"), T.Transfer()->GetBalance().PacketCount > 0);
    // 无关已结束源留下延迟包，覆盖 manager 接收冷却；不 Destroy 游戏的工具根。
    auto* Tail = T.World->SpawnActorDeferred<ABottleActor>(ABottleActor::StaticClass(), FTransform(FVector(1500, 1500, 100)),
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("独立瞬态尾包源"), Tail)) return false;
    Tail->ContainerMesh->SetStaticMesh(T.Kettle->ContainerMesh->GetStaticMesh());
    Tail->FillAmount = .1f; Tail->MaxVolumeML = 100; Tail->InitialIngredientId = Water;
    Tail->CapClass = nullptr; Tail->bStartCapped = false; Tail->bCanPour = false;
    StopSimulation(Tail); QuietPresentation(Tail); Tail->FinishSpawning(Tail->GetActorTransform()); StopSimulation(Tail);
    Near(*this, TEXT("真实无关延迟八毫升"), T.Transfer()->EmitVolume(Tail, 8, Mouth + FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector, 1), 8);
    if (!TestTrue(TEXT("仅瞬态尾包源正常 EndPlay，已发包独立存在"), Tail->Destroy())) return false;
    const double Horizon = T.Transfer()->GetUnsettledFlightHorizon();
    const auto Before = T.Transfer()->GetBalance();
    const int32 AudioStarts = T.Kettle->AudioStartCount;
    auto* Loop = T.Kettle->PourLoopSound.Get();
    if (!TestTrue(TEXT("十九根整批准备和恢复"), T.Manager->RestoreWorkbench())) return false;
    if (!CheckRestored(*this, T, Old)) return false;
    TestFalse(TEXT("恢复实际清除壶 pouring"), T.Kettle->bIsPouring);
    Near(*this, TEXT("恢复实际清除壶 flow"), T.Kettle->CurrentFlowMLPerSecond, 0);
    Near(*this, TEXT("恢复实际清除诊断音频流量"), T.Kettle->PourAudioActualFlow, 0);
    TestFalse(TEXT("恢复不保留音频请求"), T.Kettle->IsPourAudioRequested());
    if (auto* Audio = T.Kettle->GetPourAudioComponent()) TestFalse(TEXT("存在的真实音频组件停播"), Audio->IsPlaying());
    TestEqual(TEXT("恢复不重新发起音频"), T.Kettle->AudioStartCount, AudioStarts);
    TestTrue(TEXT("原声音引用保留，不用 BGM/音频 mock"), T.Kettle->PourLoopSound.Get() == Loop);
    TestEqual(TEXT("旧 WaterJet 实例清空"), T.Kettle->WaterJet->GetInstanceCount(), 0);
    Near(*this, TEXT("相关壶在途包只洒一次"), T.Transfer()->GetBalance().SpilledML - Before.SpilledML, Emitted);
    Near(*this, TEXT("无关旧包仍保留独立八毫升"), T.Transfer()->GetBalance().InFlightML, 8);
    TestTrue(TEXT("新杯接收冷却覆盖剩余 horizon"), Cup != T.Manager->PrototypeCup.Get() && T.Manager->PrototypeCup->RestoreReceiveCooldown >= Horizon - .01);
    TestTrue(TEXT("原地工具同样有接收冷却"), T.Kettle->RestoreReceiveCooldown >= Horizon - .01 && T.Dripper->RestoreReceiveCooldown >= Horizon - .01);
    const auto Spilled = T.Transfer()->GetBalance().SpilledML;
    T.Settle(Horizon + .15);
    auto* NewCup = CastChecked<ACupActor>(T.Manager->PrototypeCup.Get());
    Near(*this, TEXT("旧包不能污染新杯"), NewCup->GetPreciseVolumeML(), 0);
    Near(*this, TEXT("无关尾包结算一次八毫升"), T.Transfer()->GetBalance().SpilledML - Spilled, 8);
    Near(*this, TEXT("新杯冷却自然归零"), NewCup->RestoreReceiveCooldown, 0);
    Near(*this, TEXT("原地工具冷却自然归零"), T.Kettle->RestoreReceiveCooldown + T.Dripper->RestoreReceiveCooldown, 0);
    const auto SettledSpilled = T.Transfer()->GetBalance().SpilledML;
    T.Transfer()->Simulate(Horizon + .15);
    Near(*this, TEXT("重复结算不会重复洒包"), T.Transfer()->GetBalance().SpilledML, SettledSpilled);
    const FVector NewMouth = NewCup->PourEntryPoint->GetComponentLocation();
    Near(*this, TEXT("冷却后真实新水包五毫升"), T.Transfer()->EmitVolume(T.Kettle, 5,
        NewMouth + FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector), 5);
    T.Transfer()->Simulate(.3);
    Near(*this, TEXT("冷却到期后真实接收 Water5，不是永久禁止"), NewCup->GetIngredientVolumeML(Water), 5);
    Balanced(*this, T);
    AddInfo(TEXT("本组禁止设备音频，验证真实发包/停流及音频无重启；不声称曾有可听尾音或设备声压测量。"));
    return !HasAnyErrors();
}

// 三组只验证真实发量门和公开恢复的退出状态，不冒充恢复期间的动态回调测试。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesDirectEmissionGate, "CyberCafe.AllSupplies.DirectEmissionGateNoLedgerMutation", Flags)
bool FAllSuppliesDirectEmissionGate::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    auto* Bottle = T.Manager->PrototypeBottle.Get();
    if (!TestNotNull(TEXT("清单内真实瓶源"), Bottle)) return false;
    TestFalse(TEXT("发量门原生默认关闭"), GetDefault<ALiquidContainerActor>()->bWorkbenchRestoreEmissionBlocked);
    for (ALiquidContainerActor* Source : {static_cast<ALiquidContainerActor*>(T.Kettle), Bottle})
    {
        const auto Before = T.Transfer()->GetBalance();
        const double Volume = Source->GetPreciseVolumeML();
        const int32 Revision = Source->GetLiquidRevision();
        const bool CanPour = Source->bCanPour, Accept = Source->bAcceptLiquidFromOthers;
        const FVector Mouth = Source->PourEntryPoint->GetComponentLocation();
        {
            TGuardValue<bool> Gate(Source->bWorkbenchRestoreEmissionBlocked, true);
            TestEqual(TEXT("直接 EmitVolume 被 native 门拒绝，返回精确零"),
                T.Transfer()->EmitVolume(Source, 5, Mouth, FVector(0, 0, -50), FVector::ZeroVector), 0.0);
            SameBalance(*this, Before, T.Transfer()->GetBalance());
            TestEqual(TEXT("拒绝不扣真实库存"), Source->GetPreciseVolumeML(), Volume);
            TestEqual(TEXT("拒绝不改变 liquid revision"), Source->GetLiquidRevision(), Revision);
            TestTrue(TEXT("发量门不改倒液和接收权限"), Source->bCanPour == CanPour && Source->bAcceptLiquidFromOthers == Accept);
        }
        TestFalse(TEXT("测试临时门恢复原值"), Source->bWorkbenchRestoreEmissionBlocked);
    }
    {
        TGuardValue<bool> Gate(T.Kettle->bWorkbenchRestoreEmissionBlocked, true);
        const double Before = Bottle->GetPreciseVolumeML();
        Near(*this, TEXT("只阻被 guard 的源，不是全局禁止发量"), T.Transfer()->EmitVolume(Bottle, 5,
            Bottle->PourEntryPoint->GetComponentLocation(), FVector(0, 0, -50), FVector::ZeroVector), 5);
        Near(*this, TEXT("无关门状态的瓶仍真实扣五毫升"), Before - Bottle->GetPreciseVolumeML(), 5);
    }
    Balanced(*this, T);
    AddInfo(TEXT("本组自行设置临时 flag，仅覆盖 EmitVolume 门和源局部性；未注入 RestoreWorkbench 回调。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesKettleZeroEmissionGate, "CyberCafe.AllSupplies.KettleGateZeroMLNoAudioRestart", Flags)
bool FAllSuppliesKettleZeroEmissionGate::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    T.Kettle->SetActorRotation(FRotator(-80, 55, 0));
    const FTransform Spout = T.Kettle->GetKettleSpoutWorldTransform();
    if (!TestTrue(TEXT("真实壶嘴满足独立 PourWater 出流几何"), Spout.GetUnitAxis(EAxis::X).Z < -.3)) return false;
    TestFalse(TEXT("真壶原有 bCanPour=false，不依赖它阻独立发量"), T.Kettle->bCanPour);
    const auto Before = T.Transfer()->GetBalance();
    const int32 Revision = T.Kettle->GetLiquidRevision(), Starts = T.Kettle->AudioStartCount;
    auto* Sound = T.Kettle->PourLoopSound.Get();
    {
        TGuardValue<bool> Gate(T.Kettle->bWorkbenchRestoreEmissionBlocked, true);
        TestEqual(TEXT("倾斜壶独立 PourWater 也被实际 EmitVolume 门阻断"), T.Kettle->PourWater(.1f), 0.f);
        SameBalance(*this, Before, T.Transfer()->GetBalance());
        TestEqual(TEXT("零毫升不发液体通知 revision"), T.Kettle->GetLiquidRevision(), Revision);
        Near(*this, TEXT("实际壶流量为零"), T.Kettle->CurrentFlowMLPerSecond, 0);
        Near(*this, TEXT("诊断音频真实流量为零"), T.Kettle->PourAudioActualFlow, 0);
        TestFalse(TEXT("零毫升不保留出流状态"), T.Kettle->bIsPouring);
        TestFalse(TEXT("零毫升不发起音频请求"), T.Kettle->IsPourAudioRequested());
        TestEqual(TEXT("零毫升不重新启动声音"), T.Kettle->AudioStartCount, Starts);
        TestTrue(TEXT("原声音引用不变"), T.Kettle->PourLoopSound.Get() == Sound);
        if (auto* Audio = T.Kettle->GetPourAudioComponent()) TestFalse(TEXT("真实组件未播放"), Audio->IsPlaying());
    }
    TestFalse(TEXT("临时门解除"), T.Kettle->bWorkbenchRestoreEmissionBlocked);
    const double Volume = T.Kettle->GetPreciseVolumeML();
    const double Emitted = T.Kettle->PourWater(.1f);
    TestTrue(TEXT("解除门后同一真实壶几何可再次发量"), Emitted > 0);
    Near(*this, TEXT("解除门后库存扣量与实际发量一致"), Volume - T.Kettle->GetPreciseVolumeML(), Emitted);
    Balanced(*this, T);
    AddInfo(TEXT("本组仅临时设置 native flag，真实壶和音频状态无 mock；禁用设备播放，不代表可听音效验收或恢复回调覆盖。"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesRestoreEmissionFlagExit, "CyberCafe.AllSupplies.RestoreEmissionFlagsPreservePriorState", Flags)
bool FAllSuppliesRestoreEmissionFlagExit::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    TArray<TStrongObjectPtr<ALiquidContainerActor>> Old;
    for (auto* A : CurrentSlots(T)) Old.Emplace(A);
    auto* Bottle = T.Manager->PrototypeBottle.Get();
    {
        TGuardValue<bool> PriorKettleGate(T.Kettle->bWorkbenchRestoreEmissionBlocked, true);
        auto* MC = T.Controller(Bottle->GetActorLocation());
        if (!TestTrue(TEXT("真实 Held 触发预检拒绝"), Bottle->GrabComp->TryGrab(MC))) return false;
        const FUnchanged Before(T);
        TestFalse(TEXT("预检失败不覆盖调用前 native 门"), T.Manager->RestoreWorkbench());
        Before.Check(*this, T);
        if (!TestTrue(TEXT("真实 Drop 后允许恢复"), Bottle->GrabComp->TryRelease())) return false;
        StopSimulation(Bottle);
        if (!TestTrue(TEXT("已有门状态不影响十九根真实恢复"), T.Manager->RestoreWorkbench())) return false;
        TestTrue(TEXT("成功退出恢复原地壶原来 true 的门"), T.Kettle->bWorkbenchRestoreEmissionBlocked);
        TestFalse(TEXT("原地滤杯不遗留新门"), T.Dripper->bWorkbenchRestoreEmissionBlocked);
        for (const auto& Guard : Old)
        {
            TestFalse(TEXT("旧槽已 Destroy，不保留未发布活根"), Alive(Guard.Get()));
            TestFalse(TEXT("强引用旧身份的 native 门也恢复原值"), Guard->bWorkbenchRestoreEmissionBlocked);
        }
        for (auto* A : CurrentSlots(T))
        {
            if (!TestTrue(TEXT("全部新槽仍有效"), Alive(A))) return false;
            TestFalse(TEXT("所有完成 FinishSpawning 的新槽已解除发量门"), A->bWorkbenchRestoreEmissionBlocked);
        }
        TestEqual(TEXT("恢复后仍十九根"), GrabRoots(T.World, false).Num(), 19);
        TestEqual(TEXT("只成功发布一次"), T.Manager->GetRestoreGeneration(), 1);
    }
    TestFalse(TEXT("调用者原有临时门自行退出"), T.Kettle->bWorkbenchRestoreEmissionBlocked);
    Balanced(*this, T);
    AddInfo(TEXT("本组覆盖成功退出及预检失败；未覆盖 Prepare 后回调冻结杯、销毁子件或补液回调篡改库存的失败清理。"));
    return !HasAnyErrors();
}

AGlassRecoveryActor* InvalidCapture(FAllWorld& T, AActor* InvalidTool)
{
    const FTransform Pose(FVector(1800, 1800, 100));
    auto* M = T.World->SpawnActorDeferred<AGlassRecoveryActor>(AGlassRecoveryActor::StaticClass(), Pose,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!M) return nullptr;
    M->bRestoreAllSupplies = true;
    M->BindPrototype(T.Manager->PrototypeCup.Get(), T.Manager->PrototypeBottle.Get(), nullptr, nullptr);
    M->RestoreTargets = T.Manager->RestoreTargets; M->RestoreScoop = T.Scoop;
    M->RestoreToolTargets.Add(InvalidTool); M->FinishSpawning(Pose);
    return M;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesInvalid, "CyberCafe.AllSupplies.MissingUnsupportedAndOccupiedToolsReject", Flags)
bool FAllSuppliesInvalid::RunTest(const FString&)
{
    {
        FAllWorld T(*this); if (!T.Init()) return false;
        auto* Unsupported = T.World->SpawnActor<AActor>();
        auto* M = InvalidCapture(T, Unsupported);
        if (!TestNotNull(TEXT("真实捕获不支持的 Native AActor"), M)) return false;
        const FUnchanged Before(T);
        TestFalse(TEXT("不支持工具必须明确拒绝，不 silent skip"), M->RestoreWorkbench()); Before.Check(*this, T);
        TestEqual(TEXT("真实错误是 UNSUPPORTED TOOL"), M->RestoreStatus.ToString(), FString(TEXT("CHECK SUPPLIES - UNSUPPORTED TOOL")));
        TestEqual(TEXT("不支持工具经理代数仍零"), M->GetRestoreGeneration(), 0);
    }
    {
        FAllWorld T(*this); if (!T.Init()) return false;
        auto* Invalid = T.World->SpawnActor<AHandDripperActor>();
        if (!TestNotNull(TEXT("实际默认 Native 滤杯"), Invalid)) return false;
        TestNull(TEXT("默认 native 工具实际无资产网格，不给测试塞 mesh 修绿"), Invalid->ContainerMesh->GetStaticMesh().Get());
        auto* M = InvalidCapture(T, Invalid); if (!M) return false;
        const FUnchanged Before(T);
        TestFalse(TEXT("无网格工具捕获必须拒绝"), M->RestoreWorkbench()); Before.Check(*this, T);
        TestEqual(TEXT("无网格 setup 错误明确"), M->RestoreStatus.ToString(), FString(TEXT("CHECK TOOL SETUP")));
        TestEqual(TEXT("无网格不能假成功推进一代"), M->GetRestoreGeneration(), 0);
    }
    {
        FAllWorld T(*this); if (!T.Init()) return false;
        // 明确负例：游戏不会 Destroy 这些工具，但预检必须拒绝失去捕获身份。
        if (!TestTrue(TEXT("负例显式销毁捕获壶"), T.Kettle->Destroy())) return false;
        TestTrue(TEXT("CurrentDestroyed 真实状态"), !Alive(T.Kettle));
        const FUnchanged Before(T);
        TestFalse(TEXT("捕获工具 Destroy 后整批拒绝，不能假成功恢复其他根"), T.Manager->RestoreWorkbench()); Before.Check(*this, T);
        TestTrue(TEXT("缺失工具状态明确 missing"), T.Manager->RestoreStatus.ToString().Contains(TEXT("MISSING")));
        TestEqual(TEXT("缺失工具代数仍零"), T.Manager->GetRestoreGeneration(), 0);
        for (int32 I = 0; I < T.Manager->GetRestoreToolCount(); ++I)
            if (T.Manager->RestoreToolTargets[I].Get() == T.Kettle) TestNull(TEXT("失效工具公开 getter 返回空"), T.Manager->GetCapturedTool(I));
    }
    {
        FAllWorld T(*this); if (!T.Init()) return false;
        auto* Original = T.Saved.Manager->PrototypeCup.Get();
        const FTransform Far(FVector(1500, 1500, 100));
        auto* Blocking = T.World->SpawnActorDeferred<ACupActor>(Original->GetClass(), Far, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
        if (!TestNotNull(TEXT("真实保存杯 class 占位者"), Blocking)) return false;
        Blocking->FillAmount = 0; StopSimulation(Blocking); QuietPresentation(Blocking);
        Blocking->FinishSpawning(Far); StopSimulation(Blocking);
        Near(*this, TEXT("占位冻结杯有实际二十毫升"), Blocking->AddIngredientLiquid(20, Milk, FLinearColor::White), 20);
        Blocking->FreezeLiquidForServing(); // 仅冻结守卫，不宣称订单已完成。
        for (int32 I = 0; I < T.Manager->GetRestoreToolCount(); ++I)
        {
            auto* Tool = T.Manager->GetCapturedTool(I);
            auto* Mesh = Cast<UStaticMeshComponent>(Tool->GetRootComponent());
            if (!TestNotNull(TEXT("真实工具 root mesh"), Mesh) || !TestNotNull(TEXT("真实工具资产"), Mesh->GetStaticMesh().Get())) return false;
            const FTransform InitialMesh = Mesh->GetComponentTransform().GetRelativeTransform(Tool->GetActorTransform()) * T.ToolPoses[I];
            const FVector Center = InitialMesh.TransformPosition(Mesh->GetStaticMesh()->GetBoundingBox().GetCenter());
            Blocking->SetActorLocation(Center);
            const FUnchanged Before(T);
            TestFalse(TEXT("冻结非目标杯占壶/滤杯/研磨器中心必须拒绝"), T.Manager->RestoreWorkbench()); Before.Check(*this, T);
            TestEqual(TEXT("真实占位状态"), T.Manager->RestoreStatus.ToString(), FString(TEXT("SUPPLY POSITION OCCUPIED")));
        }
        Blocking->SetActorLocation(Far.GetLocation());
        const auto FrozenPose = Blocking->GetActorTransform();
        TestTrue(TEXT("移开占位后真实十九根可恢复"), T.Manager->RestoreWorkbench());
        TestTrue(TEXT("非目标冻结杯不删不移"), Alive(Blocking) && Blocking->GetActorTransform().Equals(FrozenPose) && Blocking->IsLiquidFrozen());
        Near(*this, TEXT("非目标冻结液体保留"), Blocking->GetPreciseVolumeML(), 20);
        Balanced(*this, T);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesFunctional, "CyberCafe.AllSupplies.GrinderChildrenAfterResetStillFunctional", Flags)
bool FAllSuppliesFunctional::RunTest(const FString&)
{
    FAllWorld T(*this); if (!T.Init()) return false;
    auto* Drawer = T.Grinder->DrawerRef.Get(); auto* Handle = T.Grinder->HandleRef.Get();
    Near(*this, TEXT("默认效率真实为一"), T.Grinder->GrindEfficiency, 1);
    Near(*this, TEXT("默认每度比率不随 reset 修改"), T.Grinder->GramsPerDegree, 15.0 / (6 * 360));
    Near(*this, TEXT("旧批次真实加一克"), T.Dripper->AddGrounds(1, T.Dripper->PourEntryPoint->GetComponentLocation()), 1);
    Near(*this, TEXT("旧批次真实注水三十二毫升"), T.Dripper->AddIngredientLiquid(32, Water, FLinearColor::White), 32);
    T.Dripper->AdvanceBrew(T.Dripper->BloomSeconds);
    for (int32 I = 0; I < 40; ++I) { T.Dripper->AdvanceBrew(.1f); T.Transfer()->Simulate(.1); }
    T.Settle(T.Transfer()->GetUnsettledFlightHorizon() + .1);
    Near(*this, TEXT("旧批次真实产量十六毫升每克"), T.Dripper->CoffeeProducedML, 16);
    const double OldProduced = T.Dripper->CoffeeProducedML, OldDripped = T.Dripper->DrippedML;
    Near(*this, TEXT("旧研磨批次投六克"), T.Grinder->TryAddBeans(6), 6);
    T.Grinder->AdvanceGrinding(360, .3f);
    const double OldConsumed = T.Grinder->GetTotalBeanConsumedGrams(), OldOutput = T.Grinder->GetTotalGroundOutputGrams(), OldLoss = T.Grinder->GetGrindLossGrams();
    if (!TestTrue(TEXT("真实整批 reset"), T.Manager->RestoreWorkbench())) return false;
    for (auto* A : CurrentSlots(T)) { StopSimulation(A); QuietPresentation(A); }
    TestTrue(TEXT("子件仍是原 identity，不重复生成"), T.Grinder->DrawerRef.Get() == Drawer && T.Grinder->HandleRef.Get() == Handle);
    TestEqual(TEXT("仅一柄一抽屉"), OwnedParts(T.World, T.Grinder), 2);
    Near(*this, TEXT("reset 保留旧研磨输出历史"), T.Grinder->GetTotalGroundOutputGrams(), OldOutput);
    Near(*this, TEXT("reset 保留旧咖啡产量历史"), T.Dripper->CoffeeProducedML, OldProduced);
    Near(*this, TEXT("reset 保留旧滴量历史"), T.Dripper->DrippedML, OldDripped);
    const float GramsPerDegree = T.Grinder->GramsPerDegree, Efficiency = T.Grinder->GrindEfficiency;
    Near(*this, TEXT("reset 后再次投六克真实接收"), T.Grinder->TryAddBeans(6), 6);
    Near(*this, TEXT("reset 后 360/.3 仍真实消耗二点五克"), T.Grinder->AdvanceGrinding(360, .3f), 2.5);
    Near(*this, TEXT("清空后的抽屉实际接粉二点五，无重复绑定翻倍"), Drawer->GetGroundsGrams(), 2.5);
    Near(*this, TEXT("真实回调搬完主体留存粉"), T.Grinder->GetCurrentGroundGrams(), 0);
    Near(*this, TEXT("研磨历史输出只加新二点五"), T.Grinder->GetTotalGroundOutputGrams() - OldOutput, 2.5);
    Near(*this, TEXT("研磨历史消耗只加新二点五"), T.Grinder->GetTotalBeanConsumedGrams() - OldConsumed, 2.5);
    Near(*this, TEXT("原效率损耗比例不改"), T.Grinder->GetGrindLossGrams(), OldLoss);
    TestTrue(TEXT("参数不变"), T.Grinder->GramsPerDegree == GramsPerDegree && T.Grinder->GrindEfficiency == Efficiency);
    Near(*this, TEXT("新旧累计质量守恒"), T.Grinder->GetTotalBeanConsumedGrams(), T.Grinder->GetTotalGroundOutputGrams() + T.Grinder->GetGrindLossGrams());
    const auto BrewBalance = T.Transfer()->GetBalance();
    Near(*this, TEXT("重置后的真入口重新加五克"), T.Dripper->AddGrounds(5, T.Dripper->PourEntryPoint->GetComponentLocation()), 5);
    Near(*this, TEXT("新批五克预算恰好八十，不扣历史产量"), T.Dripper->GetCoffeeBudgetML(), 80);
    Near(*this, TEXT("新批真实加水五十"), T.Dripper->AddIngredientLiquid(50, Water, FLinearColor::White), 50);
    const auto BeforeBloom = T.Transfer()->GetBalance();
    T.Dripper->AdvanceBrew(T.Dripper->BloomSeconds * .5f);
    Near(*this, TEXT("合法半闷蒸时间尚无新产量"), T.Dripper->CoffeeProducedML, OldProduced);
    Near(*this, TEXT("闷蒸没有擅自出水"), T.Dripper->GetPreciseVolumeML(), 50);
    TestEqual(TEXT("闷蒸没有伪造液包"), T.Transfer()->GetBalance().PacketCount, BeforeBloom.PacketCount);
    T.Dripper->AdvanceBrew(T.Dripper->BloomSeconds * .5f);
    for (int32 I = 0; I < 60; ++I) { T.Dripper->AdvanceBrew(.1f); T.Transfer()->Simulate(.1); }
    Near(*this, TEXT("reset 后第一桶五十全转咖啡"), T.Dripper->CoffeeProducedML - OldProduced, 50);
    Near(*this, TEXT("五克预算剩三十"), T.Dripper->GetCoffeeBudgetML(), 30);
    Near(*this, TEXT("同批再注五十水"), T.Dripper->AddIngredientLiquid(50, Water, FLinearColor::White), 50);
    for (int32 I = 0; I < 60; ++I) { T.Dripper->AdvanceBrew(.1f); T.Transfer()->Simulate(.1); }
    T.Settle(T.Transfer()->GetUnsettledFlightHorizon() + .1);
    Near(*this, TEXT("两桶新批最终八十咖啡，回归旧历史扣两次 Bug"), T.Dripper->CoffeeProducedML - OldProduced, 80);
    Near(*this, TEXT("整批真实滴一百，超预算二十仍为 Water"), T.Dripper->DrippedML - OldDripped, 100);
    Near(*this, TEXT("新批预算耗尽"), T.Dripper->GetCoffeeBudgetML(), 0);
    Near(*this, TEXT("真实滤杯滴空"), T.Dripper->GetPreciseVolumeML(), 0);
    Near(*this, TEXT("两桶只记实际注水一百，转换咖啡不再次注入八十"), T.Transfer()->GetBalance().InjectedML - BrewBalance.InjectedML, 100);
    Near(*this, TEXT("当前批次不改 Initial 历史"), T.Transfer()->GetBalance().InitialML, BrewBalance.InitialML);
    Balanced(*this, T);
    const double NewProduced = T.Dripper->CoffeeProducedML, NewOutput = T.Grinder->GetTotalGroundOutputGrams();
    if (!TestTrue(TEXT("第二次恢复后子件仍可用"), T.Manager->RestoreWorkbench())) return false;
    Near(*this, TEXT("二次恢复不清新生产历史"), T.Dripper->CoffeeProducedML, NewProduced);
    Near(*this, TEXT("二次恢复不清研磨历史"), T.Grinder->GetTotalGroundOutputGrams(), NewOutput);
    Near(*this, TEXT("二次恢复后再投六克"), T.Grinder->TryAddBeans(6), 6);
    T.Grinder->AdvanceGrinding(360, .3f);
    Near(*this, TEXT("二次恢复后的抽屉仍仅新增二点五"), Drawer->GetGroundsGrams(), 2.5);
    TestEqual(TEXT("两次后子件不积累"), OwnedParts(T.World, T.Grinder), 2);
    TestEqual(TEXT("两次后仍十九根"), GrabRoots(T.World, false).Num(), 19);
    Balanced(*this, T);
    return !HasAnyErrors();
}

// 仅服务下面三例；保留前十一例的夹具调用和测试主体。
bool PrepareRestorableScene(FAutomationTestBase& Test, FAllWorld& T)
{
    if (!T.Init()) return false;
    Test.TestEqual(TEXT("回调夹具从真实保存配置加载十九个可抓根"), GrabRoots(T.World, false).Num(), 19);
    for (int32 I = 0; I < T.ToolPoses.Num(); ++I)
        Test.TestTrue(TEXT("回调夹具三工具使用原捕获指针"),
            T.Manager->GetCapturedTool(I) == T.Spawned.FindChecked(T.Saved.Tools[I]));
    for (auto* A : CurrentSlots(T))
    {
        Test.TestFalse(TEXT("回调订阅前旧槽没有冻结"), A->IsLiquidFrozen());
        Test.TestFalse(TEXT("回调订阅前旧槽发量门关闭"), A->bWorkbenchRestoreEmissionBlocked);
    }
    Test.TestFalse(TEXT("回调订阅前真实壶发量门关闭"), T.Kettle->bWorkbenchRestoreEmissionBlocked);
    Test.TestFalse(TEXT("回调订阅前真实滤杯发量门关闭"), T.Dripper->bWorkbenchRestoreEmissionBlocked);
    Near(Test, TEXT("真实壶初始 Water500"), T.Kettle->GetIngredientVolumeML(Water), 500);
    Near(Test, TEXT("订阅前公开 ConsumeLiquid200"), T.Kettle->ConsumeLiquid(200), 200);
    Near(Test, TEXT("旧壶真实剩三百，恢复清空一定产生原 delegate 通知"), T.Kettle->GetPreciseVolumeML(), 300);
    T.OffsetRoots();
    return !Test.HasAnyErrors();
}

int32 LiquidRootsIncludingDeferred(UWorld* World)
{
    int32 Count = 0;
    // Level 数组也包含尚未 FinishSpawning 的预建 actor，不能只数已注册抓取组件。
    for (AActor* A : World->PersistentLevel->Actors)
        if (Alive(A) && Cast<ALiquidContainerActor>(A)) ++Count;
    return Count;
}

void CheckCallbackEmissionFlags(FAutomationTestBase& Test, const FAllWorld& T,
    const TArray<TStrongObjectPtr<ALiquidContainerActor>>& OldGuards)
{
    for (const auto& Old : OldGuards)
        Test.TestFalse(TEXT("所有强引用旧槽退出后发量门恢复 false，Destroy 不掩盖遗留标记"), Old->bWorkbenchRestoreEmissionBlocked);
    Test.TestFalse(TEXT("原地壶退出后发量门恢复 false"), T.Kettle->bWorkbenchRestoreEmissionBlocked);
    Test.TestFalse(TEXT("原地滤杯退出后发量门恢复 false"), T.Dripper->bWorkbenchRestoreEmissionBlocked);
    for (AActor* A : T.World->PersistentLevel->Actors)
        if (Alive(A))
            if (auto* Liquid = Cast<ALiquidContainerActor>(A))
            {
                Test.TestFalse(TEXT("所有存活液体根退出后均无发量门"), Liquid->bWorkbenchRestoreEmissionBlocked);
                Test.TestTrue(TEXT("退出后没有存活但未 FinishSpawning 的预建液体根"), Liquid->HasActorBegunPlay());
            }
    Balanced(Test, T);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesFrozenDuringReset,
    "CyberCafe.AllSupplies.FrozenDuringResetAbortsWithoutDeletingIce", Flags)
bool FAllSuppliesFrozenDuringReset::RunTest(const FString&)
{
    FAllWorld T(*this); if (!PrepareRestorableScene(*this, T)) return false;
    const auto Old = CurrentSlots(T);
    TArray<TStrongObjectPtr<ALiquidContainerActor>> OldGuards;
    for (auto* A : Old) OldGuards.Emplace(A);
    auto* Cup = Cast<ACupActor>(T.Manager->PrototypeCup.Get());
    if (!TestNotNull(TEXT("真实清单内首杯"), Cup)) return false;
    TestFalse(TEXT("订阅前真实杯未冻结，不伪造完成订单"), Cup->IsLiquidFrozen());
    Near(*this, TEXT("真实杯公开加入 Water200"), Cup->AddIngredientLiquid(200, Water, T.Kettle->WaterStreamColor), 200);
    if (!PutIce(*this, T, Cup, 1)) return false;
    auto* IceComp = Cup->FindComponentByClass<UCupIceComponent>();
    TStrongObjectPtr<AIceCubeActor> OriginalIce;
    for (TActorIterator<AIceCubeActor> It(T.World); It; ++It)
        if (Alive(*It) && It->State == EIceState::InCup && It->CupIce.Get() == IceComp) OriginalIce.Reset(*It);
    if (!TestNotNull(TEXT("保存公开入口接受的真实一块冰身份"), OriginalIce.Get())) return false;
    const int32 IceRevision = IceComp->GetPreparationRevision();
    const int32 LiquidCount = LiquidRootsIncludingDeferred(T.World);
    const int32 RootCount = GrabRoots(T.World, false).Num();
    TStrongObjectPtr<UAllSuppliesCallbackProbe> Probe(NewObject<UAllSuppliesCallbackProbe>());
    Probe->Cup = Cup; Probe->Kettle = T.Kettle; Probe->Action = 1;
    T.Kettle->OnLiquidChanged.AddDynamic(Probe.Get(), &UAllSuppliesCallbackProbe::OnLiquidChange);
    // 允许安全拒绝的 STATE CHANGED Warning；必须另行精确验证状态和返回值。
    AddExpectedError(TEXT("STATE CHANGED"), EAutomationExpectedErrorFlags::Contains, 0);
    const bool Restored = T.Manager->RestoreWorkbench();
    T.Kettle->OnLiquidChanged.RemoveDynamic(Probe.Get(), &UAllSuppliesCallbackProbe::OnLiquidChange);
    TestFalse(TEXT("Prepare 后真实清空通知冻结杯，整批必须失败"), Restored);
    TestTrue(TEXT("实际 delegate 触发一次动作"), Probe->Fired);
    TestEqual(TEXT("只收到恢复清空旧壶的一次通知"), Probe->Notifications, 1);
    TestTrue(TEXT("清空通知期间经理发量门已经生效"), Probe->GuardSeen);
    Near(*this, TEXT("旧壶自然已被清空，不声称强事务回滚"), T.Kettle->GetPreciseVolumeML(), 0);
    TestEqual(TEXT("明确报告恢复中状态变化而非成功"), T.Manager->RestoreStatus.ToString(), FString(TEXT("SUPPLY RESET FAILED - STATE CHANGED")));
    TestEqual(TEXT("失败不推进代数"), T.Manager->GetRestoreGeneration(), 0);
    TestTrue(TEXT("回调冻结的原杯身份仍存活且仍冻结"), Alive(Cup) && Cup->IsLiquidFrozen());
    Near(*this, TEXT("冻结杯原二百毫升未删"), Cup->GetPreciseVolumeML(), 200);
    Near(*this, TEXT("冻结杯真实 Water200 未改"), Cup->GetIngredientVolumeML(Water), 200);
    TestEqual(TEXT("冻结杯保留一块冰"), IceComp->GetIceCount(), 1);
    TestEqual(TEXT("冻结杯冰准备版本未变"), IceComp->GetPreparationRevision(), IceRevision);
    TestTrue(TEXT("同一块真实冰仍在同一个杯冰组件"), Alive(OriginalIce.Get())
        && OriginalIce->State == EIceState::InCup && OriginalIce->CupIce.Get() == IceComp);
    for (int32 I = 0; I < Old.Num(); ++I)
        TestTrue(TEXT("失败没有迁移任何经理槽或公开目标指针"), T.Manager->GetCurrent(I) == Old[I]
            && T.Manager->RestoreTargets[I].Get() == Old[I] && Alive(OldGuards[I].Get()));
    TestEqual(TEXT("含 Deferred 的液体根数前后相等，无未发布预建残留"), LiquidRootsIncludingDeferred(T.World), LiquidCount);
    TestEqual(TEXT("十九个原可抓根前后相等"), GrabRoots(T.World, false).Num(), RootCount);
    CheckCallbackEmissionFlags(*this, T, OldGuards);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesToolChildKilledDuringReset,
    "CyberCafe.AllSupplies.ToolChildKilledDuringResetFailsWithoutReplacement", Flags)
bool FAllSuppliesToolChildKilledDuringReset::RunTest(const FString&)
{
    FAllWorld T(*this); if (!PrepareRestorableScene(*this, T)) return false;
    const auto Old = CurrentSlots(T);
    TArray<TStrongObjectPtr<ALiquidContainerActor>> OldGuards;
    for (auto* A : Old) OldGuards.Emplace(A);
    TStrongObjectPtr<AGrinderDrawerActor> Drawer(T.Grinder->DrawerRef.Get());
    TStrongObjectPtr<AGrinderHandleActor> Handle(T.Grinder->HandleRef.Get());
    TWeakObjectPtr<AGrinderDrawerActor> DrawerWeak(Drawer.Get());
    if (!TestTrue(TEXT("预检前捕获真实存活抽屉，不能用预先缺失的子件代替回调销毁"), Alive(Drawer.Get()) && DrawerWeak.IsValid())) return false;
    TestEqual(TEXT("预检前真实主体拥有一柄一抽屉"), OwnedParts(T.World, T.Grinder), 2);
    const int32 LiquidCount = LiquidRootsIncludingDeferred(T.World);
    const int32 RootCount = GrabRoots(T.World, false).Num();
    TStrongObjectPtr<UAllSuppliesCallbackProbe> Probe(NewObject<UAllSuppliesCallbackProbe>());
    Probe->Kettle = T.Kettle; Probe->Grinder = T.Grinder; Probe->Action = 2;
    T.Kettle->OnLiquidChanged.AddDynamic(Probe.Get(), &UAllSuppliesCallbackProbe::OnLiquidChange);
    AddExpectedError(TEXT("STATE CHANGED"), EAutomationExpectedErrorFlags::Contains, 0);
    const bool Restored = T.Manager->RestoreWorkbench();
    T.Kettle->OnLiquidChanged.RemoveDynamic(Probe.Get(), &UAllSuppliesCallbackProbe::OnLiquidChange);
    TestFalse(TEXT("恢复清空真实通知销毁抽屉必须整批失败，不补子件转成功"), Restored);
    TestTrue(TEXT("真实通知确实执行销毁动作"), Probe->Fired);
    TestEqual(TEXT("仅清空旧壶的一次实际通知"), Probe->Notifications, 1);
    TestTrue(TEXT("销毁抽屉回调也处于经理发量门内"), Probe->GuardSeen);
    TestTrue(TEXT("旧抽屉弱指针失效或已进入 Destroy"), !DrawerWeak.IsValid() || DrawerWeak->IsActorBeingDestroyed());
    TestFalse(TEXT("强持有也不能阻止旧抽屉 Destroy"), Alive(Drawer.Get()));
    TestTrue(TEXT("原柄身份和归属仍有效"), Alive(Handle.Get()) && T.Grinder->HandleRef.Get() == Handle.Get()
        && Handle->OwnerGrinder.Get() == T.Grinder);
    TestEqual(TEXT("仅剩原柄，不允许 CloneChild 或替补抽屉"), OwnedParts(T.World, T.Grinder), 1);
    int32 Drawers = 0;
    for (TActorIterator<AGrinderDrawerActor> It(T.World); It; ++It)
        if (Alive(*It) && It->OwnerGrinder.Get() == T.Grinder) ++Drawers;
    TestEqual(TEXT("主体没有任何新存活抽屉"), Drawers, 0);
    TestEqual(TEXT("明确拒绝已捕获研磨器子件的状态变化"), T.Manager->RestoreStatus.ToString(), FString(TEXT("GRINDER RESET FAILED - STATE CHANGED")));
    TestEqual(TEXT("回调失败代数仍零"), T.Manager->GetRestoreGeneration(), 0);
    Near(*this, TEXT("允许已经 Prepare 并清空壶，不伪称完全回滚"), T.Kettle->GetPreciseVolumeML(), 0);
    for (int32 I = 0; I < Old.Num(); ++I)
        TestTrue(TEXT("失败保留所有原槽和公开目标引用"), T.Manager->GetCurrent(I) == Old[I]
            && T.Manager->RestoreTargets[I].Get() == Old[I] && Alive(OldGuards[I].Get()));
    TestEqual(TEXT("失败清理全部 Deferred 液体根，数量不增加"), LiquidRootsIncludingDeferred(T.World), LiquidCount);
    TestEqual(TEXT("子件销毁不替换十九个原用品根"), GrabRoots(T.World, false).Num(), RootCount);
    CheckCallbackEmissionFlags(*this, T, OldGuards);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAllSuppliesNoReentrantKettleEmitDuringRefill,
    "CyberCafe.AllSupplies.NoReentrantKettleEmitDuringRefill", Flags)
bool FAllSuppliesNoReentrantKettleEmitDuringRefill::RunTest(const FString&)
{
    FAllWorld T(*this); if (!PrepareRestorableScene(*this, T)) return false;
    const auto Old = CurrentSlots(T);
    TArray<TStrongObjectPtr<ALiquidContainerActor>> OldGuards;
    for (auto* A : Old) OldGuards.Emplace(A);
    FTransform KettleOrigin;
    bool FoundKettle = false;
    for (int32 I = 0; I < T.ToolPoses.Num(); ++I)
        if (T.Manager->GetCapturedTool(I) == T.Kettle)
        {
            KettleOrigin = T.Manager->GetToolInitialTransform(I); FoundKettle = true;
            TestTrue(TEXT("公开工具原 TM 与真实夹具捕获值一致"), KettleOrigin.Equals(T.ToolPoses[I], .0001));
        }
    if (!TestTrue(TEXT("通过真实指针找到捕获壶，而非猜工具索引"), FoundKettle)) return false;
    Near(*this, TEXT("真实 PourWater 速率参数 MaxFlow28"), T.Kettle->MaxFlowMLPerSecond, 28);
    Near(*this, TEXT("真实 PourWater 倾角门限40"), T.Kettle->PourTiltDegrees, 40);
    TestFalse(TEXT("真实壶独立发量不受基类 bCanPour=false 伪阻断"), T.Kettle->bCanPour);
    TestFalse(TEXT("真实壶没有瓶盖阻断"), T.Kettle->bIsCapped);
    if (!TestNotNull(TEXT("真实壶 PourFX 组件可用，不用空组件制造零发量"), T.Kettle->PourFX.Get())) return false;
    T.Kettle->SetActorRotation(FRotator(-65, T.Kettle->GetActorRotation().Yaw, 0));
    TestTrue(TEXT("未加门的65度真实壶嘴朝下满足生产几何"),
        FVector::DotProduct(T.Kettle->GetKettleSpoutWorldTransform().GetUnitAxis(EAxis::X), FVector::DownVector) > .3);
    const double ControlBefore = T.Kettle->GetPreciseVolumeML();
    const double ControlEmission = T.Kettle->PourWater(.02f);
    if (!TestTrue(TEXT("订阅前同65度和.02秒真实正发量控制组"), ControlEmission > 0)) return false;
    Near(*this, TEXT("真实速率17.5乘.02秒发出.35毫升"), ControlEmission, .35);
    Near(*this, TEXT("控制组从实际库存扣除实际发量"), ControlBefore - T.Kettle->GetPreciseVolumeML(), ControlEmission);
    auto* Cup = Cast<ACupActor>(T.Manager->PrototypeCup.Get());
    if (!TestNotNull(TEXT("真实旧杯作为延迟包目标入口"), Cup)) return false;
    // 订阅前将尾包瞄准新杯将恢复的真实入口，避免因旧杯偏移而自然错过目标。
    for (const auto& Slot : T.Slots)
        if (Slot.Identity == Cup) Cup->SetActorTransform(Slot.Pose, false, nullptr, ETeleportType::TeleportPhysics);
    const FVector Mouth = Cup->PourEntryPoint->GetComponentLocation();
    auto* Tail = T.World->SpawnActorDeferred<ABottleActor>(ABottleActor::StaticClass(), FTransform(FVector(1500, 1500, 100)),
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("真实独立旧尾包源"), Tail)) return false;
    Tail->ContainerMesh->SetStaticMesh(T.Kettle->ContainerMesh->GetStaticMesh());
    Tail->FillAmount = .1f; Tail->MaxVolumeML = 100; Tail->InitialIngredientId = Water;
    Tail->CapClass = nullptr; Tail->bStartCapped = false; Tail->bCanPour = false;
    StopSimulation(Tail); QuietPresentation(Tail); Tail->FinishSpawning(Tail->GetActorTransform()); StopSimulation(Tail);
    Near(*this, TEXT("真实八毫升尾包保留一秒出生延迟"), T.Transfer()->EmitVolume(Tail, 8,
        Mouth + FVector(0, 0, 10), FVector(0, 0, -50), FVector::ZeroVector, 1), 8);
    if (!TestTrue(TEXT("尾包源真实 Destroy，已发延迟包仍独立在途"), Tail->Destroy())) return false;
    const double Horizon = T.Transfer()->GetUnsettledFlightHorizon();
    TestTrue(TEXT("实际 horizon 包含一秒 BirthDelay 与三秒飞行，不用固定短冷却"), Horizon >= 4 - Tolerance);
    const auto Before = T.Transfer()->GetBalance();
    const int32 AudioStarts = T.Kettle->AudioStartCount;
    TStrongObjectPtr<UAllSuppliesCallbackProbe> Probe(NewObject<UAllSuppliesCallbackProbe>());
    Probe->Kettle = T.Kettle; Probe->Action = 3;
    T.Kettle->OnLiquidChanged.AddDynamic(Probe.Get(), &UAllSuppliesCallbackProbe::OnLiquidChange);
    const bool Restored = T.Manager->RestoreWorkbench();
    T.Kettle->OnLiquidChanged.RemoveDynamic(Probe.Get(), &UAllSuppliesCallbackProbe::OnLiquidChange);
    TestTrue(TEXT("真实正 Fill 补液通知才执行一次重入 PourWater"), Probe->Fired);
    TestEqual(TEXT("实际清空和补液各一次通知，零发量没有递归通知"), Probe->Notifications, 2);
    TestTrue(TEXT("补液通知期间经理发量门仍为 true"), Probe->GuardSeen);
    Near(*this, TEXT("尝试时真实库存已补满五百"), Probe->AttemptVolumeML, 500);
    Near(*this, TEXT("回调内真的将壶倾斜65度"), Probe->AttemptTiltDegrees, 65);
    TestTrue(TEXT("回调内实际壶嘴朝下，不是默认 yaw/pitch 零速率伪通过"), Probe->AttemptSpoutDown > .3);
    Near(*this, TEXT("回调内实际公开参数对应正速率17.5"), Probe->AttemptRateMLPerSecond, 17.5);
    TestEqual(TEXT("经理门使真实 PourWater(.02) 返回精确零"), Probe->AttemptEmitML, 0.0);
    TestEqual(TEXT("回调没有新增任何真实液包"), Probe->AttemptPacketsAfter - Probe->AttemptPacketsBefore, 0);
    TestEqual(TEXT("回调所见只有独立旧尾包"), Probe->AttemptPacketsBefore, 1);
    if (!TestTrue(TEXT("门禁阻止重入发量但允许正常整批恢复成功"), Restored))
    {
        CheckCallbackEmissionFlags(*this, T, OldGuards);
        return false;
    }
    TestEqual(TEXT("成功只推进一代"), T.Manager->GetRestoreGeneration(), 1);
    TestEqual(TEXT("成功状态不伪造失败转成功"), T.Manager->RestoreStatus.ToString(), FString(TEXT("RESTORED")));
    // 经理必须在所有补液/构造回调后真正恢复 TM；测试绝不自行修补这个结果。
    TestTrue(TEXT("所有回调后真实壶最终完整 TM 等于捕获原 TM"), T.Kettle->GetActorTransform().Equals(KettleOrigin, .0001));
    CheckRestored(*this, T, Old);
    Near(*this, TEXT("重入不扣恢复后的 Water500"), T.Kettle->GetIngredientVolumeML(Water), 500);
    TestEqual(TEXT("恢复后没有回调新包，只有一个无关旧包"), T.Transfer()->GetBalance().PacketCount, Probe->AttemptPacketsAfter);
    Near(*this, TEXT("相关壶旧控制包只被取消洒出一次"), T.Transfer()->GetBalance().SpilledML - Before.SpilledML, ControlEmission);
    Near(*this, TEXT("独立尾包八毫升仍在途"), T.Transfer()->GetBalance().InFlightML, 8);
    TestFalse(TEXT("重入零发量没有残留 pouring 状态"), T.Kettle->bIsPouring);
    Near(*this, TEXT("重入零发量没有残留流速"), T.Kettle->CurrentFlowMLPerSecond, 0);
    TestFalse(TEXT("重入零发量没有重新请求音频"), T.Kettle->IsPourAudioRequested());
    TestEqual(TEXT("补液回调没有重启声音"), T.Kettle->AudioStartCount, AudioStarts);
    const double FinalHorizon = T.Transfer()->GetUnsettledFlightHorizon();
    Near(*this, TEXT("恢复后实际 horizon 仍覆盖独立旧延迟包"), FinalHorizon, Horizon);
    for (auto* A : CurrentSlots(T))
        TestTrue(TEXT("所有新槽最终接收冷却覆盖实际旧包 horizon"), A->RestoreReceiveCooldown >= FinalHorizon - .01);
    TestTrue(TEXT("原地壶和滤杯最终冷却也覆盖实际 horizon"), T.Kettle->RestoreReceiveCooldown >= FinalHorizon - .01
        && T.Dripper->RestoreReceiveCooldown >= FinalHorizon - .01);
    CheckCallbackEmissionFlags(*this, T, OldGuards);
    const double Spilled = T.Transfer()->GetBalance().SpilledML;
    T.Settle(FinalHorizon + .15);
    for (auto* A : CurrentSlots(T))
        if (auto* NewCup = Cast<ACupActor>(A))
        {
            Near(*this, TEXT("真实旧延迟包结算后每个新杯仍空"), NewCup->GetPreciseVolumeML(), 0);
            Near(*this, TEXT("新杯接收冷却自然结束，不永久锁死"), NewCup->RestoreReceiveCooldown, 0);
        }
    Near(*this, TEXT("独立八毫升旧包恰好洒出一次"), T.Transfer()->GetBalance().SpilledML - Spilled, 8);
    TestEqual(TEXT("最终无任何旧包或回调新包"), T.Transfer()->GetBalance().PacketCount, 0);
    Near(*this, TEXT("结算结束真实壶仍 Water500"), T.Kettle->GetIngredientVolumeML(Water), 500);
    TestTrue(TEXT("结算结束壶仍为捕获原 TM，而非测试事后修正"), T.Kettle->GetActorTransform().Equals(KettleOrigin, .0001));
    const double SettledSpilled = T.Transfer()->GetBalance().SpilledML;
    T.Transfer()->Simulate(FinalHorizon + .15);
    Near(*this, TEXT("重复结算不再次洒出旧包"), T.Transfer()->GetBalance().SpilledML, SettledSpilled);
    CheckCallbackEmissionFlags(*this, T, OldGuards);
    return !HasAnyErrors();
}
}
#endif // WITH_DEV_AUTOMATION_TESTS
