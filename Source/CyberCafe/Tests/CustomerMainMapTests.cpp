// 完整保存地图回归：不重建 NPC/Spot、不直接进入 Consume、不补写 Mesh Tick。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Tests/CustomerDrinkTestProbe.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/CustomerDrinkComponent.h"
#include "Tutorial/CustomerDrinkMotion.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/Steps/TutorialStep_Grab.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Tutorial/Steps/TutorialStep_WaitForRecipeMenu.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "UI/Narrative/NarrativeHUDActor.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "Serving/ServingSpotActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace CustomerMainMap
{
constexpr float FrameSeconds = .02f;
constexpr double VolumeTolerance = 1.e-4;

FString JsonString(FString Value)
{
    Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return TEXT("\"") + Value + TEXT("\"");
}
const TCHAR* JsonBool(bool Value) { return Value ? TEXT("true") : TEXT("false"); }

struct FRun
{
    FAutomationTestBase& Test;
    FString CaseName;
    FString OutputDir;
    FString Reason = TEXT("尚未初始化");
    TArray<FString> Stages;
    UWorld* Source = nullptr;
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    ATutorialDirector* Director = nullptr;
    ATutorialNPC* NPC = nullptr;
    ACupActor* Cup = nullptr;
    AServingSpotActor* Spot = nullptr;
    UTutorialStep_WaitForServe* Wait = nullptr;
    UTutorialStep_ConsumeServedDrink* Consume = nullptr;
    UCyberCafeEventSubsystem* Events = nullptr;
    UNarrativeSubsystem* Narrative = nullptr;
    ACustomerDrinkTestProbe* Probe = nullptr;
    FTransform SavedNPCTM = FTransform::Identity;
    FTransform SavedSpotTM = FTransform::Identity;
    FTransform AcceptedCupTM = FTransform::Identity;
    TArray<FName> Hands;
    TArray<FVector> InitialHands;
    double MaxHandDistanceCM = 0;
    double MaxClockErrorSeconds = 0;
    int32 ClockSamples = 0;
    int32 InitialActorCount = 0;
    bool bContext = false;
    bool bGIInitialized = false;
    bool bBeganPlay = false;
    bool bSourceWasDirty = false;
    bool bSawDrinking = false;
    bool bClockValid = true;
    bool bCupAnimationMatches = true;
    bool bIceAdded = false;

    FRun(FAutomationTestBase& InTest, const TCHAR* InCase) : Test(InTest), CaseName(InCase)
    {
        OutputDir = FPlatformMisc::GetEnvironmentVariable(TEXT("DRINK_FIX_RUN_DIR"));
    }

    ~FRun()
    {
        Record(TEXT("清理前"));
        Save();
        if (World && World != Source && World->PersistentLevel != (Source ? Source->PersistentLevel : nullptr))
        {
            if (bBeganPlay) World->EndPlay(EEndPlayReason::Quit);
            if (bGIInitialized) GI->Shutdown();
            if (bContext && GEngine) GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }

    UCustomerDrinkComponent* Drink() const
    {
        return IsValid(NPC) && IsValid(NPC->CustomerDrink) ? NPC->CustomerDrink.Get() : nullptr;
    }
    USkeletalMeshComponent* Mesh() const { return IsValid(NPC) && IsValid(NPC->GetMesh()) ? NPC->GetMesh() : nullptr; }

    bool RuntimeValid()
    {
        return Need(IsValid(World) && IsValid(Director) && IsValid(NPC) && IsValid(Cup) && IsValid(Spot) &&
            IsValid(Wait) && IsValid(Consume) && IsValid(Events) && IsValid(Narrative) && IsValid(Probe) &&
            Drink() && Mesh() && IsValid(Drink()->Motion) && IsValid(Drink()->Motion->Animation) &&
            IsValid(Cup->ContainerMesh) && IsValid(Cup->GetRootComponent()) && IsValid(Spot->DetectBox),
            TEXT("完整地图运行中关键 Actor/组件/Motion 失效；停止并记录，不解引用空指针"));
    }

    bool Fail(const FString& InReason)
    {
        Reason = InReason;
        Test.AddError(InReason);
        Record(TEXT("失败"));
        return false;
    }
    bool Need(bool Condition, const TCHAR* InReason) { return Condition || Fail(InReason); }

    void Save()
    {
        if (OutputDir.IsEmpty()) return;
        const FString Json = TEXT("{\"measurementSource\":\"saved-entire-map-real-world-tick\",\"notAPassCertificate\":true,\"case\":")
            + JsonString(CaseName) + FString::Printf(TEXT(",\"liquidFixture\":\"empty saved Highball + public Coffee100 Milk100 + %d real ice; not VR pouring\",\"stages\":[\n"), FixtureIceCount())
            + FString::Join(Stages, TEXT(",\n")) + TEXT("\n]}\n");
        if (!IFileManager::Get().MakeDirectory(*OutputDir, true) ||
            !FFileHelper::SaveStringToFile(Json, *(OutputDir / (CaseName + TEXT(".json"))),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("无法保存 DRINK_FIX_RUN_DIR/MainMap*.json"));
    }

    int32 ActiveIndex() const
    {
        if (!IsValid(Director)) return INDEX_NONE;
        for (int32 I = 0; I < Director->Steps.Num(); ++I)
            if (IsValid(Director->Steps[I]) && Director->Steps[I]->GetDirector() == Director &&
                !Director->Steps[I]->IsCompleted()) return I;
        return INDEX_NONE;
    }

    void Record(const TCHAR* Stage)
    {
        UCustomerDrinkComponent* C = Drink();
        USkeletalMeshComponent* M = Mesh();
        UCustomerDrinkMotion* Motion = C && IsValid(C->Motion) ? C->Motion.Get() : nullptr;
        UAnimSingleNodeInstance* Single = M ? M->GetSingleNodeInstance() : nullptr;
        const bool bCup = IsValid(Cup) && !Cup->IsActorBeingDestroyed();
        const bool bSpot = IsValid(Spot);
        const FVector Expected = Motion && M ? (Motion->Sample(0) * M->GetComponentTransform()).GetLocation() : FVector::ZeroVector;
        const double StartDistance = bSpot && Motion && M ? FVector::Distance(Spot->GetActorLocation(), Expected) : -1;
        const double CupDistance = bCup && Motion && M ? FVector::Distance(Cup->GetActorLocation(), Expected) : -1;
        const double Elapsed = C ? C->GetElapsedSeconds() : -1;
        const double Position = Single ? Single->GetCurrentTime() : -1;
        const FString CupTM = bCup ? Cup->GetActorTransform().ToString() : TEXT("null");
        const FString MeshTM = M ? M->GetComponentTransform().ToString() : TEXT("null");
        const FString HeadTM = M && M->DoesSocketExist(TEXT("head")) ? M->GetSocketTransform(TEXT("head"), RTS_World).ToString() : TEXT("无 head socket");
        const FString HandTM = M && !Hands.IsEmpty() ? M->GetSocketTransform(Hands[0], RTS_World).ToString() : TEXT("无真实手骨骼");
        const FDrinkValidationResult Validation = bSpot ? Spot->GetLastValidation() : FDrinkValidationResult();
        Stages.Add(FString::Printf(TEXT("{\"stage\":%s,\"reason\":%s,\"world\":%s,\"step\":%d,\"actorCountAtDuplication\":%d,\"elapsed\":%.9g,\"animationPosition\":%.9g,\"clockErrorSeconds\":%.9g,\"maxClockErrorSeconds\":%.9g,\"clockSamples\":%d,\"drinking\":%s,\"consumedML\":%.17g,\"remainingML\":%.17g,\"servedCount\":%d,\"endedCount\":%d,\"sameCupIdentity\":%s,\"frozen\":%s,\"visible\":%s,\"returned\":%s,\"accepted\":%s,\"validationReason\":%d,\"expectedStartWorldCM\":%s,\"spotVsMotionStartDistanceCM\":%.9g,\"cupVsMotionStartDistanceCM\":%.9g,\"maxHandDisplacementCM\":%.9g,\"cupTM\":%s,\"meshTM\":%s,\"headWorldTM\":%s,\"handWorldTM\":%s,\"meshTickEnabled\":%s,\"animBpaused\":%s,\"noSkeletonUpdate\":%s,\"narrativeTyping\":%s,\"presentationSerial\":%u}"),
            *JsonString(Stage), *JsonString(Reason), *JsonString(GetPathNameSafe(World)), ActiveIndex(), InitialActorCount,
            Elapsed, Position, C && C->IsDrinking() && Single ? FMath::Abs(Elapsed - Position) : -1.,
            MaxClockErrorSeconds, ClockSamples, JsonBool(C && C->IsDrinking()), C ? C->GetConsumedML() : -1.,
            bCup ? Cup->GetPreciseVolumeML() : -1., IsValid(Probe) ? Probe->ServedCount : 0, IsValid(Probe) ? Probe->EndedCount : 0,
            JsonBool(bCup && bSpot && IsValid(Director) && Spot->GetServedCup() == Cup && Director->LastServedCup == Cup),
            JsonBool(bCup && Cup->IsLiquidFrozen()), JsonBool(bCup && !Cup->IsHidden()),
            JsonBool(bCup && Cup->GetActorTransform().Equals(AcceptedCupTM, .05)), JsonBool(Validation.bAccepted), static_cast<int32>(Validation.Reason),
            *JsonString(Expected.ToString()), StartDistance, CupDistance, MaxHandDistanceCM,
            *JsonString(CupTM), *JsonString(MeshTM), *JsonString(HeadTM), *JsonString(HandTM),
            JsonBool(M && M->IsComponentTickEnabled()), JsonBool(M && M->bPauseAnims), JsonBool(M && M->bNoSkeletonUpdate),
            JsonBool(Narrative && Narrative->IsTyping()), Narrative ? Narrative->GetPresentationSerial() : 0u));
        Stages.Last() = Stages.Last().LeftChop(1) + FString::Printf(
            TEXT(",\"customerEnabled\":%s,\"motionAsset\":%s,\"animationAsset\":%s,\"skeletalMeshAsset\":%s,\"cupMeshAsset\":%s,\"supportedCupMesh\":%s,\"meshRegistered\":%s,\"meshVisible\":%s,\"cupHeld\":%s,\"cupPouring\":%s,\"submissionEnabled\":%s,\"orderSerial\":%u,\"acceptedOrderSerial\":%u,\"handBones\":%s}"),
            JsonBool(C && C->bEnabled), *JsonString(GetPathNameSafe(Motion)),
            *JsonString(Motion ? GetPathNameSafe(Motion->Animation.Get()) : TEXT("null")),
            *JsonString(M ? GetPathNameSafe(M->GetSkeletalMeshAsset()) : TEXT("null")),
            *JsonString(bCup && Cup->ContainerMesh ? GetPathNameSafe(Cup->ContainerMesh->GetStaticMesh()) : TEXT("null")),
            *JsonString(Motion ? GetPathNameSafe(Motion->SupportedCupMesh.Get()) : TEXT("null")),
            JsonBool(M && M->IsRegistered()), JsonBool(M && M->IsVisible()), JsonBool(bCup && Cup->IsHeld()),
            JsonBool(bCup && Cup->bIsPouring), JsonBool(bSpot && Spot->bAcceptSubmissions),
            bSpot ? Spot->GetOrderSerial() : 0u, bSpot ? Spot->GetAcceptedOrderSerial() : 0u,
            *JsonString(FString::JoinBy(Hands, TEXT(","), [](const FName& Name) { return Name.ToString(); })));
        auto* Screen = Wait ? Wait->OrderHologram.Get() : nullptr;
        Stages.Last() = Stages.Last().LeftChop(1) + FString::Printf(
            TEXT(",\"recipeAutomaticallyRevealed\":%s,\"panelOpen\":%s,\"panelPlayerOpened\":%s,\"actualIceCount\":%d,\"fixtureIceCount\":%d}"),
            JsonBool(IsValid(Screen) && Screen->HasRevealedFirstOrderRecipe()), JsonBool(IsValid(Screen) && Screen->IsPanelOpen()),
            JsonBool(IsValid(Screen) && Screen->IsPlayerOpened()), bCup && Cup->CupIce ? Cup->CupIce->GetIceCount() : -1, FixtureIceCount());
        Save();
    }

    // 只去除无头测试的表现/物理依赖；不移除 Actor，不修改 NPC 组件或骨骼 Tick。
    void DisableHeadlessDependencies()
    {
        if (!World || !World->PersistentLevel) return;
        for (const auto& Entry : World->PersistentLevel->Actors)
        {
            AActor* A = Entry.Get();
            if (!IsValid(A)) continue;
            TInlineComponentArray<UActorComponent*> Components;
            A->GetComponents(Components);
            for (UActorComponent* Component : Components)
            {
                if (UNiagaraComponent* FX = Cast<UNiagaraComponent>(Component))
                {
                    FX->SetAutoActivate(false);
                    FX->DeactivateImmediate();
                    FX->SetAsset(nullptr);
                }
                if (UAudioComponent* Audio = Cast<UAudioComponent>(Component))
                {
                    Audio->SetAutoActivate(false);
                    Audio->Stop();
                    Audio->SetSound(nullptr);
                }
                // 保留 Widget 实例和 timer，只禁止世界空间绘制 Tick。
                if (UWidgetComponent* Widget = Cast<UWidgetComponent>(Component)) Widget->SetComponentTickEnabled(false);
                if (!Cast<ATutorialNPC>(A))
                    if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component)) Primitive->SetSimulatePhysics(false);
            }
            if (ALiquidContainerActor* Container = Cast<ALiquidContainerActor>(A))
            {
                Container->LiquidFXTemplate = nullptr;
                Container->SplashEffectTemplate = nullptr;
                Container->bEnableSplash = false;
                // 注入代替 VR 实倒；防满杯 fixture 因自动倾倒改变配方。
                Container->bCanPour = false;
            }
        }
    }

    bool Setup()
    {
        if (!Need(!OutputDir.IsEmpty(), TEXT("必须提供 DRINK_FIX_RUN_DIR，禁止无诊断记录运行"))) return false;
        Record(TEXT("加载前"));
        if (!Need(GEngine != nullptr, TEXT("GEngine 不可用"))) return false;
        Source = LoadObject<UWorld>(nullptr, TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
        if (!Need(IsValid(Source) && Source->PersistentLevel, TEXT("无法读取真实保存 L_Cafe"))) return false;
        bSourceWasDirty = Source->GetOutermost()->IsDirty();
        if (!Need(Source->GetStreamingLevels().IsEmpty(), TEXT("L_Cafe 含 streaming levels；本夹具未加载子关卡，停止而非伪称完整地图"))) return false;
        const FString PackageName = TEXT("/Temp/CustomerMainMap") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UPackage* Package = CreatePackage(*PackageName);
        World = Cast<UWorld>(StaticDuplicateObject(Source, Package));
        Reason = TEXT("复制保存地图，不重建源 Editor World");
        Record(TEXT("复制后"));
        if (!Need(World && World != Source && World->PersistentLevel && World->PersistentLevel != Source->PersistentLevel,
            TEXT("全地图复制失败或仍引用源 Level"))) return false;
        // InitWorld 为 UE5.6 public API；不能对已初始化副本盲目清标志再初始化。
        if (!Need(!World->IsInitialized(), TEXT("复制 World 已初始化；停止，需核对 PIE 复制策略，不重建源地图"))) return false;
        World->WorldType = EWorldType::Game;
        World->AddToRoot();
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        bContext = true;
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI;
        World->SetGameInstance(GI);
        // UE5.6 public 回调补齐 GI 自己的 WorldContext，无须 GameViewport 或 dummy world。
        GI->OnWorldChanged(nullptr, World);
        if (!Need(GI->GetWorld() == World, TEXT("GI public OnWorldChanged 未关联新 Game Context"))) return false;
        GI->Init();
        bGIInitialized = true;
        DisableHeadlessDependencies();
        UWorld::InitializationValues IV;
        IV.CreatePhysicsScene(true).ShouldSimulatePhysics(false).AllowAudioPlayback(false)
            .CreateNavigation(false).CreateAISystem(false);
        Reason = TEXT("InitWorld 前");
        Record(TEXT("初始化前"));
        World->InitWorld(IV);
        if (!Need(World->IsInitialized(), TEXT("副本 InitWorld 未成功"))) return false;
        Events = GI->GetSubsystem<UCyberCafeEventSubsystem>();
        Narrative = World->GetSubsystem<UNarrativeSubsystem>();
        if (!Need(Events && Narrative, TEXT("副本缺真实事件或 Narrative 子系统"))) return false;

        TMap<FName, AActor*> ByName;
        for (const auto& A : World->PersistentLevel->Actors) if (IsValid(A.Get()))
        {
            ++InitialActorCount;
            ByName.Add(A->GetFName(), A.Get());
            if (ATutorialDirector* D = Cast<ATutorialDirector>(A.Get()))
            {
                if (Director) return Fail(TEXT("保存地图有多个 Director，不能任意选择"));
                Director = D;
            }
        }
        for (const auto& A : Source->PersistentLevel->Actors) if (IsValid(A.Get()))
            if (!Need(ByName.Contains(A->GetFName()) && ByName[A->GetFName()] != A.Get(), TEXT("源地图 Actor 未完整复制"))) return false;
        if (!Need(IsValid(Director) && IsValid(Director->NPCRef), TEXT("保存 Director/NPCRef 不完整"))) return false;
        NPC = Director->NPCRef.Get();
        if (!Need(NPC->GetWorld() == World && Drink() && Mesh(), TEXT("NPC 或组件引用未重映射至副本"))) return false;
        SavedNPCTM = NPC->GetActorTransform();
        for (UTutorialStep* Step : Director->Steps)
        {
            if (UTutorialStep_WaitForServe* S = Cast<UTutorialStep_WaitForServe>(Step))
            {
                if (Wait) return Fail(TEXT("多个 WaitForServe，需明确客人订单而非猜测"));
                Wait = S;
            }
            if (UTutorialStep_ConsumeServedDrink* S = Cast<UTutorialStep_ConsumeServedDrink>(Step)) Consume = S;
            if (UTutorialStep_Grab* S = Cast<UTutorialStep_Grab>(Step))
                if (ACupActor* Candidate = Cast<ACupActor>(S->RequiredActor.Get()))
                {
                    if (Cup && Cup != Candidate) return Fail(TEXT("保存 Grab 步骤指定多个教学杯，不能任意选择"));
                    Cup = Candidate;
                }
        }
        AGlassRecoveryActor* Recovery = Cast<AGlassRecoveryActor>(ByName.FindRef(TEXT("GlassRecovery_Tutorial")));
        if (!Need(IsValid(Recovery) && Recovery->GetWorld() == World && IsValid(Recovery->PrototypeCup),
            TEXT("原恢复经理或 PrototypeCup 未重映射至副本"))) return false;
        ACupActor* PrototypeCup = Cast<ACupActor>(Recovery->PrototypeCup.Get());
        if (!Cup) Cup = PrototypeCup;
        if (!Need(Wait && Consume && IsValid(Cup) && Cup == PrototypeCup && IsValid(Wait->RequiredSpot) && IsValid(Wait->RequiredRecipe),
            TEXT("保存步骤/原恢复经理唯一教学杯/Spot/配方引用缺失或不一致"))) return false;
        if (!Need(Recovery->bRestoreAllSupplies && Recovery->RestoreTargets.Num() == 15 && Recovery->RestoreToolTargets.Num() == 3 && IsValid(Recovery->RestoreScoop.Get()), TEXT("新版统一恢复必须覆盖15瓶杯、3工具与冰铲"))) return false;
        for (const auto& Tool : Recovery->RestoreToolTargets)
            if (!Need(IsValid(Tool.Get()) && Tool->GetWorld() == World, TEXT("新版工具引用必须重映射到真实完整地图副本"))) return false;
        int32 CupTargetCount = 0;
        for (const auto& Target : Recovery->RestoreTargets)
        {
            if (!Need(IsValid(Target.Get()) && Target->GetWorld() == World, TEXT("保存恢复目标失效或仍指向源地图"))) return false;
            if (Target.Get() == Cup) ++CupTargetCount;
        }
        if (!Need(CupTargetCount == 1, TEXT("保存恢复目标未唯一引用新的教学杯"))) return false;
        Spot = Wait->RequiredSpot.Get();
        SavedSpotTM = Spot->GetActorTransform();
        if (!Need(Cup->GetWorld() == World && Spot->GetWorld() == World && Wait->GetOuter() == Director && Consume->GetOuter() == Director,
            TEXT("保存杯/Spot/Instanced Steps 仍指向源地图"))) return false;
        UClass* HighballClass = LoadClass<ACupActor>(nullptr,
            TEXT("/Game/VRTemplate/MenuIngredients/Cups/BP_MenuCup_Highball.BP_MenuCup_Highball_C"));
        if (!Need(IsValid(HighballClass) && Cup->GetClass() == HighballClass, TEXT("恢复经理教学杯不是保存 Highball 类"))) return false;
        if (!Need(Drink()->bEnabled && IsValid(Drink()->Motion) && Drink()->Motion->IsValidMotion() && IsValid(Drink()->Motion->Animation),
            TEXT("保存 NPC 未启用或真实喝饮 Motion 无效；禁止夹具修补"))) return false;
        if (!Need(Mesh()->GetSkeletalMeshAsset() && Mesh()->GetSkeletalMeshAsset()->GetSkeleton() == Drink()->Motion->Animation->GetSkeleton(),
            TEXT("保存 Mesh/Animation Skeleton 不匹配"))) return false;
        if (!Need(Director->Steps.Num() == 8 && Cast<UTutorialStep_ShowMessage>(Director->Steps[0]) &&
            Cast<UTutorialStep_ShowMessage>(Director->Steps[1]) && Cast<UTutorialStep_ShowMessage>(Director->Steps[2]) &&
            Cast<UTutorialStep_ShowMessage>(Director->Steps[3]), TEXT("保存教学前四步不是已核对的四条 ShowMessage，停止并报告"))) return false;
        if (!Need(IsValid(Director->Steps[4].Get()) &&
            Director->Steps[4]->GetClass()->GetPathName() == TEXT("/Script/CyberCafe.TutorialStep_ShowMessage") &&
            Director->Steps[4]->GetOuter() == Director, TEXT("保存 Step[4] 菜单提示不是原生 ShowMessage 或未内联归属副本导演"))) return false;
        auto* FirstOrderLine = Cast<UTutorialStep_ShowMessage>(Director->Steps[1]);
        if (!Need(FirstOrderLine && FirstOrderLine->FirstOrderMenu == Wait->OrderHologram &&
            FirstOrderLine->FirstOrderRecipe == Wait->RequiredRecipe, TEXT("保存首次点单句未配置自动配方展示"))) return false;
        if (!Need(IsValid(Director->Steps[5].Get()) &&
            Director->Steps[5]->GetClass()->GetPathName() == TEXT("/Script/CyberCafe.TutorialStep_WaitForRecipeMenu") &&
            Director->Steps[5]->GetOuter() == Director, TEXT("保存 Step[5] 菜单实操不是原生 WaitForRecipeMenu 或未内联归属副本导演"))) return false;

        Probe = World->SpawnActor<ACustomerDrinkTestProbe>();
        if (!Need(IsValid(Probe), TEXT("无法创建现有真实委托探针"))) return false;
        Probe->NPC = NPC;
        Probe->Component = Drink();
        Probe->Cup = Cup;
        Probe->Spot = Spot;
        Events->OnDrinkServed.AddDynamic(Probe, &ACustomerDrinkTestProbe::HandleServed);
        Drink()->OnDrinkEnded.AddDynamic(Probe, &ACustomerDrinkTestProbe::HandleEnded);
        Consume->OnStepCompleted.AddDynamic(Probe, &ACustomerDrinkTestProbe::HandleStepCompleted);
        Reason = TEXT("InitializeActorsForPlay 前，LevelBP 与其他 Actor 全部保留");
        Record(TEXT("Actor 初始化前"));
        World->InitializeActorsForPlay(FURL());
        World->UpdateWorldComponents(false, false);
        // Loaded scene-component world transforms are undefined until registered.
        // Snapshot the saved placements here, before BeginPlay, not their earlier
        // unregistered identity transforms. This still catches runtime NPC drift.
        SavedNPCTM = NPC->GetActorTransform();
        SavedSpotTM = Spot->GetActorTransform();
        if (!Need(Cup->GetActorScale3D().Equals(FVector(1.1), .00001), TEXT("保存 Highball 根缩放不是批准的 1.1；不由夹具修补"))) return false;
        World->GetWorldSettings()->MinUndilatedFrameTime = 0.f;
        Reason = TEXT("NotifyBeginPlay 前");
        Record(TEXT("BeginPlay 前"));
        World->GetWorldSettings()->NotifyBeginPlay();
        bBeganPlay = true;
        DisableHeadlessDependencies();
        if (!RuntimeValid()) return false;
        Cup->ContainerMesh->SetSimulatePhysics(false);
        Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        if (!Tick()) return false;
        if (!Director->bAutoStart) Director->StartTutorial();
        Reason = TEXT("副本完整 BeginPlay，现有 Director 正常启动");
        Record(TEXT("教学启动"));
        return true;
    }

    void ObserveMotion()
    {
        UCustomerDrinkComponent* C = Drink();
        USkeletalMeshComponent* M = Mesh();
        if (!C || !C->IsDrinking() || !M || !IsValid(C->Motion) || !IsValid(C->Motion->Animation)) return;
        bSawDrinking = true;
        UAnimSingleNodeInstance* Single = M->GetSingleNodeInstance();
        ++ClockSamples;
        if (!Single || Single->GetCurrentAsset() != C->Motion->Animation.Get()) bClockValid = false;
        else
        {
            const double Error = FMath::Abs(C->GetElapsedSeconds() - Single->GetCurrentTime());
            MaxClockErrorSeconds = FMath::Max(MaxClockErrorSeconds, Error);
            bClockValid &= FMath::IsFinite(Error) && Error < .02;
        }
        if (Hands.IsEmpty())
        {
            for (int32 I = 0; I < M->GetNumBones(); ++I)
            {
                const FName Bone = M->GetBoneName(I);
                const FString Name = Bone.ToString().ToLower();
                if (Name == TEXT("hand_r") || Name == TEXT("hand_l") || Name.EndsWith(TEXT("righthand")) ||
                    Name.EndsWith(TEXT("lefthand")) || Name == TEXT("hand.r") || Name == TEXT("hand.l"))
                {
                    Hands.Add(Bone);
                    InitialHands.Add(M->GetSocketTransform(Bone, RTS_World).GetLocation() - M->GetComponentLocation());
                }
            }
        }
        for (int32 I = 0; I < Hands.Num(); ++I)
        {
            const FVector Actual = M->GetSocketTransform(Hands[I], RTS_World).GetLocation() - M->GetComponentLocation();
            MaxHandDistanceCM = FMath::Max(MaxHandDistanceCM, static_cast<double>(FVector::Distance(Actual, InitialHands[I])));
        }
        const float Elapsed = C->GetElapsedSeconds();
        if (IsValid(Cup) && Elapsed >= 2.5f && Elapsed < 4.f)
        {
            FTransform Expected = C->Motion->Sample(Elapsed) * M->GetComponentTransform();
            Expected.SetScale3D(AcceptedCupTM.GetScale3D());
            bCupAnimationMatches &= Cup->GetActorTransform().Equals(Expected, .05);
        }
    }

    bool Tick()
    {
        if (!RuntimeValid()) return false;
        ++GFrameCounter;
        World->Tick(LEVELTICK_All, FrameSeconds);
        if (!RuntimeValid()) return false;
        if (bIceAdded)
        {
            UCupIceComponent* Ice = Cup->FindComponentByClass<UCupIceComponent>();
            if (!Need(IsValid(Ice) && Ice->GetIceCount() == FixtureIceCount(), TEXT("真实 Tick 路径未保留本用例的杯内冰"))) return false;
            if (!Need(Cup->GetActorScale3D().Equals(FVector(1.1), .00001), TEXT("真实 Tick 路径破坏了 Highball 根缩放 1.1"))) return false;
        }
        ObserveMotion();
        return true;
    }

    bool ReachOrder()
    {
        int32 PreviousIndex = INDEX_NONE;
        float SinceInput = 0;
        for (int32 Frame = 0; Frame < 6000; ++Frame)
        {
            if (!RuntimeValid()) return false;
            const int32 Index = ActiveIndex();
            if (Index != PreviousIndex)
            {
                PreviousIndex = Index;
                SinceInput = 0;
                Reason = FString::Printf(TEXT("Director 自动进入保存步骤 %d"), Index);
                Record(TEXT("步骤变化"));
                if (Index == 1)
                {
                    auto* Screen = Wait->OrderHologram.Get();
                    if (!Need(IsValid(Screen) && Screen->HasRevealedFirstOrderRecipe() && Screen->IsPanelOpen() &&
                        !Screen->IsRecipeMenuMode() && !Screen->IsPlayerOpened() && !Screen->IsPending() &&
                        Screen->GetPresentedRecipe() == Wait->RequiredRecipe.Get() && !Screen->GetCurrentRecipe() &&
                        Screen->GetDisplayWidget() && Screen->GetDisplayWidget()->GetOrderText().ToString().Contains(TEXT("冰拿铁")),
                        TEXT("真实首句点单未在任何手输入前自动显示对应当前订单"))) return false;
                    Reason = TEXT("首次点单句自动展开当前订单冰拿铁；未按电源、未创建服务验收订单");
                    Record(TEXT("首次点单自动展开"));
                }
            }
            if (Wait->GetDirector() == Director && !Wait->IsCompleted())
            {
                Reason = TEXT("现有 Director 自动到达 WaitForServe");
                Record(TEXT("订单等待"));
                return Need(Spot->bAcceptSubmissions && Spot->ActiveRecipe == Wait->RequiredRecipe, TEXT("真实 Wait 未开启保存订单/配方"));
            }
            if (Director->Steps.IsValidIndex(Index))
            {
                UTutorialStep* Step = Director->Steps[Index];
                if (UTutorialStep_Grab* Grab = Cast<UTutorialStep_Grab>(Step))
                {
                    if (!Need(IsValid(Grab->RequiredActor) && Grab->RequiredActor->GetWorld() == World, TEXT("Grab 缺真实地图 RequiredActor"))) return false;
                    Events->OnAnyObjectGrabbed.Broadcast(Grab->RequiredActor.Get(), EControllerHand::Right);
                }
                if (auto* MenuStep = Cast<UTutorialStep_WaitForRecipeMenu>(Step))
                {
                    if (!MenuStep->IsCompleted() && MenuStep->GetDirector() == Director)
                    {
                        if (!Need(!MenuStep->WasUnavailable() && MenuStep->OrderHologram &&
                            MenuStep->OrderHologram->GetWorld() == World, TEXT("保存菜单步骤未指向副本真实屏幕"))) return false;
                        AOrderHologramActor* Screen = MenuStep->OrderHologram.Get();
                        if (!Need(IsValid(Screen->ButtonZone) && IsValid(Screen->Panel) && MenuStep->RequiredPinyinIndex == FName(TEXT("BNT")),
                            TEXT("保存菜单缺真实开屏按钮/面板或教学索引不是 BNT"))) return false;
                        if (!Need(Screen->HasRevealedFirstOrderRecipe() && Screen->IsPanelOpen(),
                            TEXT("首次订单自动展开必须一直可见，不能靠测试补按电源"))) return false;
                        if (Screen->Panel->GetRelativeScale3D().Z < Screen->Panel->GetRelativeScale3D().Y * .999f)
                            for (int32 I = 0; I < 20; ++I) if (!Tick()) return false;
                        if (!Need(Screen->IsPanelOpen() && Screen->Panel->GetRelativeScale3D().Z >= Screen->Panel->GetRelativeScale3D().Y * .999f,
                            TEXT("真实 World Tick 后菜单展开动画仍未完成"))) return false;
                        if (!Need(!Screen->IsRecipeMenuMode() && TutorialMenuTestHelpers::TouchAction(Screen, 2) && Screen->IsRecipeMenuMode(),
                            TEXT("真实当前订单配方入口未用手采样打开冰拿铁配方"))) return false;
                        if (!Need(MenuStep->IsCompleted(), TEXT("真实菜单操作未完成教学门槛"))) return false;
                        Reason = TEXT("首次自动显示当前订单后真实手触查看这杯的配方，无需返回，未跳过教学");
                        Record(TEXT("菜单教学完成"));
                    }
                }
                if (UTutorialStep_ShowMessage* Message = Cast<UTutorialStep_ShowMessage>(Step))
                    if (Message->bAllowSkipByInput && SinceInput >= .2f)
                    {
                        Events->OnPlayerSkipRequested.Broadcast();
                        SinceInput = 0;
                    }
            }
            if (!Tick()) return false;
            SinceInput += FrameSeconds;
        }
        return Fail(FString::Printf(TEXT("完整 Director 未到 WaitForServe：当前步骤 %d，typing=%d；未绕过反馈调度"), ActiveIndex(), Narrative->IsTyping()));
    }

    int32 FixtureIceCount() const { return CaseName == TEXT("MainMapRealSavedDirectorServeToSip") ? 1 : 3; }
    bool FillAndPlace()
    {
        if (!RuntimeValid()) return false;
        if (!Need(IsValid(Cup) && !Cup->IsBroken() && !Cup->IsLiquidFrozen() && !Cup->IsHeld(), TEXT("保存 Highball 已破碎/冻结/被持有，禁止强制改 Grab 状态"))) return false;
        if (!Need(FMath::Abs(Cup->GetPreciseVolumeML()) < VolumeTolerance, TEXT("保存 Highball 非空，不清空或修改初始液量"))) return false;
        if (!Need(Cup->AddIngredientLiquid(100.f, TEXT("Coffee"), FLinearColor::Red) == 100.f,
            TEXT("公共 fixture 未接收 Coffee100，不扩容或改 recipe")) || !RuntimeValid()) return false;
        if (!Need(Cup->AddIngredientLiquid(100.f, TEXT("Milk"), FLinearColor::White) == 100.f,
            TEXT("公共 fixture 未接收 Milk100，不扩容或改 recipe")) || !RuntimeValid()) return false;
        if (!Need(TutorialMenuTestHelpers::AddIce(Cup, FixtureIceCount()), TEXT("同 World 真实冰未穿杯口验收")) || !RuntimeValid()) return false;
        bIceAdded = true;
        UCupIceComponent* Ice = Cup->FindComponentByClass<UCupIceComponent>();
        if (!Need(IsValid(Ice) && Ice->GetIceCount() == FixtureIceCount(), TEXT("上菜夹具真实冰量与本用例不符"))) return false;
        // Public placement fixture is deliberately slower than glass break speed.
        // A one-frame 42cm teleport incorrectly looks like a >2000cm/s impact in
        // the full scene (the old isolated fixture had no table to hit).
        const FTransform From = Cup->GetActorTransform();
        FTransform To = Spot->GetActorTransform();
        To.SetScale3D(From.GetScale3D());
        for (int32 I = 1; I <= 16; ++I)
        {
            FTransform T;
            T.Blend(From, To, static_cast<float>(I) / 16.f);
            Cup->SetActorTransform(T, false, nullptr, ETeleportType::TeleportPhysics);
            if (!Tick()) return false;
        }
        Cup->ContainerMesh->UpdateOverlaps();
        Spot->DetectBox->UpdateOverlaps();
        Reason = FString::Printf(TEXT("玩家放置真实保存 Highball；200mL 加%d块冰，根缩放仍 1.1，NPC/Spot 未移动"), FixtureIceCount());
        Record(TEXT("放杯"));
        return Need(FMath::Abs(Cup->GetPreciseVolumeML() - 200.) < VolumeTolerance, TEXT("上菜前未保有 200mL"));
    }

    bool Submit(bool bReplaceFeedback)
    {
        if (!RuntimeValid()) return false;
        UNarrativeWidget* ReplacementWidget = nullptr;
        if (bReplaceFeedback)
        {
            ANarrativeHUDActor* HUD = Narrative->GetHUDActor();
            if (!Need(IsValid(HUD), TEXT("反馈覆盖用真实 HUD 未被 Narrative 发现"))) return false;
            UWidgetComponent* WidgetComponent = HUD->FindComponentByClass<UWidgetComponent>();
            if (!Need(WidgetComponent != nullptr, TEXT("真实 HUD 缺 WidgetComponent"))) return false;
            // 共享完整地图，只对 UI 做公开绑定；不是原生 Spot 或两步伪地图。
            Narrative->SetHUDWidgetClass(nullptr);
            ReplacementWidget = NewObject<UNarrativeWidget>(World);
            ReplacementWidget->bEnableVoice = false;
            WidgetComponent->SetWidget(ReplacementWidget);
            if (!Need(HUD->GetNarrativeWidget() == ReplacementWidget, TEXT("真实 HUD SetWidget 绑定失败"))) return false;
            if (!Need(!Wait->CompletedText.IsEmpty() && Director->bDialogueTypewriter, TEXT("保存 Wait 无逐字 CompletedText，覆盖回归前提不成立"))) return false;
        }
        if (!FillAndPlace()) return false;
        if (bReplaceFeedback)
        {
            if (!Need(Spot->TrySubmitCup(Cup), TEXT("真实保存 BP Spot TrySubmitCup 拒收"))) return false;
        }
        else
        {
            if (!Need(Spot->DetectBox->IsOverlappingActor(Cup), TEXT("真实杯在保存 DetectBox 内未产生 overlap；不添加替代碰撞体"))) return false;
            // 默认 0.4 秒稳定窗也由真实 World Tick 驱动，不直接调用 Spot.Tick。
            const int32 Limit = FMath::CeilToInt(FMath::Max(.4f, Spot->StabilitySeconds) / FrameSeconds) + 25;
            for (int32 I = 0; I < Limit && !Spot->HasBeenServed(); ++I) if (!Tick()) return false;
        }
        if (!RuntimeValid()) return false;
        AcceptedCupTM = Cup->GetActorTransform();
        Reason = TEXT("真实 Spot 验收事件已发生，Director LastServedCup 只由事件写入");
        Record(TEXT("上菜验收"));
        if (!Need(Spot->HasBeenServed() && Spot->GetLastValidation().bAccepted && Spot->GetServedCup() == Cup &&
            Director->LastServedCup == Cup && Wait->IsCompleted() && Probe->ServedCount == 1 &&
            Probe->ServedCup.Get() == Cup && Probe->ServedSpot.Get() == Spot, TEXT("真实验收/事件/Wait/原杯身份未一致完成"))) return false;
        if (!Need(Cup->IsLiquidFrozen() && !Cup->IsHidden() && FMath::Abs(Cup->GetPreciseVolumeML() - 200.) < VolumeTolerance,
            TEXT("验收杯未冻结可见或验收时不再有200mL"))) return false;
        if (bReplaceFeedback)
        {
            const uint32 OldSerial = Narrative->GetPresentationSerial();
            if (!Need(IsValid(Narrative->GetHUDActor()) && Narrative->GetHUDActor()->GetNarrativeWidget() == ReplacementWidget &&
                Wait->CompletedText.ToString().StartsWith(ReplacementWidget->GetVisibleText()) &&
                (Narrative->IsTyping() || ReplacementWidget->GetVisibleText() == Wait->CompletedText.ToString()),
                TEXT("真实 CompletedText 未使用已绑定反馈 Widget"))) return false;
            Narrative->ShowText(FText::FromString(TEXT("其他提示")));
            if (!Need(Narrative->GetPresentationSerial() > OldSerial && ReplacementWidget->GetVisibleText() == TEXT("其他提示"),
                TEXT("ShowText 未真实覆盖 CompletedText"))) return false;
            Reason = TEXT("成功后立即 ShowText 覆盖旧反馈，不补发旧 serial 完成事件");
            Record(TEXT("反馈覆盖"));
        }
        return true;
    }

    bool FinishDrink()
    {
        if (!RuntimeValid()) return false;
        for (int32 I = 0; I < 1500 && !bSawDrinking; ++I) if (!Tick()) return false;
        Reason = bSawDrinking ? TEXT("Director 自动进入 Consume 并启动真实动画") : TEXT("Director 上菜后没有自动启动喝饮");
        Record(TEXT("Consume 进入"));
        if (!Need(bSawDrinking && Consume->GetDirector() == Director && Drink()->IsDrinking(), TEXT("上菜后未自动进入 Consume/bDrinking；不直接 EnterStep/BeginDrink"))) return false;
        if (!Need(Drink()->GetDrinkingCup() == Cup, TEXT("Consume 正在喝的不是验收原杯"))) return false;
        bool bSipRecorded = false;
        for (int32 I = 0; I < 1500 && Drink()->IsDrinking(); ++I)
        {
            if (!Tick()) return false;
            if (!bSipRecorded && Drink()->GetElapsedSeconds() >= Drink()->Motion->SipEnd)
            {
                bSipRecorded = true;
                Reason = TEXT("真实 World Tick 已经过完整 sip 窗口");
                Record(TEXT("抿饮后"));
            }
        }
        for (int32 I = 0; I < 1500 && !Consume->IsCompleted(); ++I) if (!Tick()) return false;
        Reason = TEXT("喝饮动作及真实 Consume 完成等待结束");
        Record(TEXT("动作结束"));
        if (!Need(IsValid(Cup) && IsValid(NPC) && IsValid(Spot) && IsValid(Probe), TEXT("演出期间原杯/NPC/Spot/探针失效"))) return false;
        bool bOK = true;
        bOK &= Test.TestFalse(TEXT("真实动作结束 bDrinking=false"), Drink()->IsDrinking());
        bOK &= Test.TestTrue(TEXT("Consume 由结束事件自动完成"), Consume->IsCompleted());
        bOK &= Test.TestEqual(TEXT("只广播一次完整喝饮结束"), Probe->EndedCount, 1);
        bOK &= Test.TestTrue(TEXT("结束回调完整且仍是原杯"), Probe->bEndedCompleted && Probe->EndedCup.Get() == Cup);
        bOK &= Test.TestTrue(TEXT("200mL 自动喝掉20，剩180"), FMath::Abs(Drink()->GetConsumedML() - 20.) < VolumeTolerance &&
            FMath::Abs(Cup->GetPreciseVolumeML() - 180.) < VolumeTolerance && FMath::Abs(Probe->EndedConsumedML - 20.) < VolumeTolerance);
        bOK &= Test.TestTrue(TEXT("冰拿铁喝饮后保留实际提交的实体冰"), Cup->CupIce && Cup->CupIce->GetIceCount() == FixtureIceCount());
        bOK &= Test.TestTrue(TEXT("新杯根缩放仍为原1.1"), Cup->GetActorScale3D().Equals(FVector(1.1), .001));
        bOK &= Test.TestTrue(TEXT("两种成分各剩90mL"), FMath::Abs(Cup->GetIngredientVolumeML(TEXT("Coffee")) - 90.) < VolumeTolerance &&
            FMath::Abs(Cup->GetIngredientVolumeML(TEXT("Milk")) - 90.) < VolumeTolerance);
        bOK &= Test.TestTrue(TEXT("原杯身份始终不替换、冻结、可见并返回服务位"), Spot->GetServedCup() == Cup && Director->LastServedCup == Cup &&
            Cup->IsLiquidFrozen() && !Cup->IsHidden() && Cup->GetActorTransform().Equals(AcceptedCupTM, .05) &&
            Cup->GetRootComponent()->GetAttachParent() == Spot->SceneRoot);
        bOK &= Test.TestTrue(TEXT("真实骨骼 hand world socket 相对起点移动超过5cm，未手动刷新骨骼"), !Hands.IsEmpty() && MaxHandDistanceCM > 5.);
        bOK &= Test.TestTrue(TEXT("自动 elapsed 与真实 Motion.Animation position 每帧误差小于0.02秒"), bClockValid && ClockSamples > 10 && MaxClockErrorSeconds < .02);
        bOK &= Test.TestTrue(TEXT("同时间轴真实杯姿态匹配 Motion 样本"), bCupAnimationMatches && bSipRecorded);
        bOK &= Test.TestTrue(TEXT("测试未移动 NPC/Spot，生产动作也未使其偏移"), NPC->GetActorTransform().Equals(SavedNPCTM, .05) && Spot->GetActorTransform().Equals(SavedSpotTM, .05));
        bOK &= Test.TestEqual(TEXT("源地图只读，不保存或改 dirty 标志"), Source->GetOutermost()->IsDirty(), bSourceWasDirty);
        Reason = bOK ? TEXT("公开状态和真实骨骼测量均满足断言") : TEXT("最终断言失败；保留真实 Tick/骨骼/反馈诊断，不补写状态");
        Record(TEXT("断言结果"));
        return bOK;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerMainMapRealSavedDirectorServeToSipTest,
    "CyberCafe.Customer.MainMap.RealSavedDirectorServeToSip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerMainMapRealSavedDirectorServeToSipTest::RunTest(const FString&)
{
    CustomerMainMap::FRun Run(*this, TEXT("MainMapRealSavedDirectorServeToSip"));
    return Run.Setup() && Run.ReachOrder() && Run.Submit(false) && Run.FinishDrink();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerMainMapFeedbackReplacementDoesNotSuppressSipTest,
    "CyberCafe.Customer.MainMap.FeedbackReplacementDoesNotSuppressSip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerMainMapFeedbackReplacementDoesNotSuppressSipTest::RunTest(const FString&)
{
    CustomerMainMap::FRun Run(*this, TEXT("MainMapFeedbackReplacementDoesNotSuppressSip"));
    return Run.Setup() && Run.ReachOrder() && Run.Submit(true) && Run.FinishDrink();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerMainMapSkipAutomaticTransitionTest,
    "CyberCafe.Customer.MainMap.SkipCancelsPendingAutomaticTasting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerMainMapSkipAutomaticTransitionTest::RunTest(const FString&)
{
    CustomerMainMap::FRun Run(*this, TEXT("MainMapSkipCancelsPendingAutomaticTasting"));
    if (!Run.Setup() || !Run.ReachOrder() || !Run.Submit(true)) return false;
    Run.Director->SkipTutorial();
    for (int32 I = 0; I < 600; ++I) if (!Run.Tick()) return false;
    bool OK = true;
    OK &= TestFalse(TEXT("跳过教学取消上菜后的自动品尝定时器"), Run.Drink()->IsDrinking());
    OK &= TestEqual(TEXT("跳过后不能再启动喝饮结束回调"), Run.Probe->EndedCount, 0);
    OK &= TestEqual(TEXT("原交付事件仍只发生一次"), Run.Probe->ServedCount, 1);
    OK &= TestTrue(TEXT("杯子保留200mL且没有额外消费"),
        FMath::Abs(Run.Cup->GetPreciseVolumeML() - 200.) < CustomerMainMap::VolumeTolerance && Run.Drink()->GetConsumedML() == 0.);
    OK &= TestTrue(TEXT("原杯仍冻结可见并在原服务点"), Run.Cup->IsLiquidFrozen() && !Run.Cup->IsHidden() &&
        Run.Spot->GetServedCup() == Run.Cup && Run.Cup->GetActorTransform().Equals(Run.AcceptedCupTM, .05));
    Run.Reason = OK ? TEXT("跳过取消待启动演出，原杯无消费且保留") : TEXT("跳过回归断言失败");
    Run.Record(TEXT("自动过渡跳过后12秒"));
    return OK;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
