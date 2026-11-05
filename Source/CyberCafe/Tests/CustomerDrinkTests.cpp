// 第一客人原生业务验收；只使用真实已保存资产、World Tick 和公开 API，不替换动作样本或伪造验收。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/CustomerDrinkTestProbe.h"
#include "Tutorial/CustomerDrinkComponent.h"
#include "Tutorial/CustomerDrinkMotion.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialStep.h"
#include "Tutorial/Steps/TutorialStep_ConsumeServedDrink.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Liquid/CupActor.h"
#include "Liquid/BottleActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Serving/ServingSpotActor.h"
#include "Serving/DrinkRecipe.h"
#include "GrabComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
constexpr double DrinkTolerance = 1.e-6;
const FName DrinkCoffee(TEXT("Coffee"));
const FName DrinkMilk(TEXT("Milk"));
const TCHAR* DrinkMotionPath = TEXT("/Game/Characters/BarGirl/FirstCustomer/DA_BarGirlDrinkMotion.DA_BarGirlDrinkMotion");
const TCHAR* DrinkAnimationPath = TEXT("/Game/Characters/BarGirl/FirstCustomer/A_BarGirl_FirstCustomerDrink.A_BarGirl_FirstCustomerDrink");
const TCHAR* DrinkCupPath = TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup01.BP_Cup01_C");
TArray<FString> DrinkMeasurements;

void CheckDrinkNumber(FAutomationTestBase& Test, const FString& Label, double Actual, double Expected)
{
    Test.TestTrue(FString::Printf(TEXT("%s：实测 %.17g，预期 %.17g，误差 < 1e-6"), *Label, Actual, Expected),
        FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) < DrinkTolerance);
}

struct FCustomerDrinkWorld
{
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    ATutorialNPC* NPC = nullptr;
    ACupActor* Cup = nullptr;
    AServingSpotActor* Spot = nullptr;
    ACustomerDrinkTestProbe* Probe = nullptr;
    UCustomerDrinkMotion* Motion = nullptr;
    USkeletalMesh* BarGirl = nullptr;
    UAnimSequence* Idle = nullptr;
    UCyberCafeEventSubsystem* Events = nullptr;
    FTransform OriginalCupTransform;
    bool bBeganPlay = false;

    FCustomerDrinkWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
            ERHIFeatureLevel::SM5, &IV);
        if (World && GEngine)
        {
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            GI = NewObject<UGameInstance>(GEngine);
            Context.OwningGameInstance = GI;
            World->SetGameInstance(GI);
            GI->Init();
            Events = GI->GetSubsystem<UCyberCafeEventSubsystem>();
        }
    }

    ~FCustomerDrinkWorld()
    {
        if (!World) return;
        if (bBeganPlay) World->EndPlay(EEndPlayReason::Quit);
        if (GI) GI->Shutdown();
        if (GEngine) GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    UCustomerDrinkComponent* Drink() const { return NPC->CustomerDrink.Get(); }
    ULiquidTransferSubsystem* Transfers() const { return World->GetSubsystem<ULiquidTransferSubsystem>(); }

    bool Setup(FAutomationTestBase& Test, bool bSubmit = true, bool bSavedNPC = false)
    {
        if (!Test.TestNotNull(TEXT("真实游戏 World"), World) ||
            !Test.TestNotNull(TEXT("真实 GI 事件总线"), Events) ||
            !Test.TestNotNull(TEXT("真实液体世界账本"), Transfers())) return false;
        Motion = LoadObject<UCustomerDrinkMotion>(nullptr, DrinkMotionPath);
        BarGirl = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/BarGirl/SK_BarGirl.SK_BarGirl"));
        Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/BarGirl/A_BarGirl_Idle.A_BarGirl_Idle"));
        UClass* CupClass = LoadClass<ACupActor>(nullptr, DrinkCupPath);
        UDrinkRecipe* Latte = LoadObject<UDrinkRecipe>(nullptr, TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));
        if (!Test.TestNotNull(TEXT("必须先保存真实 DA_BarGirlDrinkMotion；禁止合成样本顶替"), Motion) ||
            !Test.TestNotNull(TEXT("真实 SK_BarGirl"), BarGirl) ||
            !Test.TestNotNull(TEXT("真实 BarGirl Idle"), Idle) ||
            !Test.TestNotNull(TEXT("真实已保存 Cup01 蓝图"), CupClass) ||
            !Test.TestNotNull(TEXT("真实已保存拿铁配方"), Latte)) return false;
        if (!Test.TestTrue(TEXT("已保存动作资产有效"), Motion->IsValidMotion()) ||
            !Test.TestNotNull(TEXT("真实喝饮动画"), Motion->Animation.Get())) return false;
        Test.TestEqual(TEXT("使用第一客人真实动画，不使用旧预览"), Motion->Animation->GetPathName(), FString(DrinkAnimationPath));
        Test.TestEqual(TEXT("动作采样数 211"), Motion->CupSamples.Num(), 211);
        Test.TestEqual(TEXT("动作采样率 30"), Motion->SampleRate, 30.f);
        CheckDrinkNumber(Test, TEXT("时长 7 秒"), Motion->Duration(), 7.0);
        Test.TestEqual(TEXT("开始喝 3.2 秒"), Motion->SipStart, 3.2f);
        Test.TestEqual(TEXT("结束喝 3.7 秒"), Motion->SipEnd, 3.7f);
        Test.TestEqual(TEXT("抓杯 1.55 秒"), Motion->GraspTime, 1.55f);
        Test.TestEqual(TEXT("放杯 5.35 秒"), Motion->ReleaseTime, 5.35f);
        Test.TestEqual(TEXT("喝掉 20 mL"), Motion->SipVolumeML, 20.f);
        Test.TestEqual(TEXT("起点只允许 5 cm 偏差"), Motion->MaxStartOffsetCM, 5.f);
        Test.TestTrue(TEXT("真实样本起点为 (-12,23,118) cm"),
            Motion->CupSamples[0].GetLocation().Equals(FVector(-12, 23, 118), .00001));
        Test.TestTrue(TEXT("BarGirl 与喝饮动画同 Skeleton"), BarGirl->GetSkeleton() == Motion->Animation->GetSkeleton());

        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (bSavedNPC)
        {
            auto* Map = LoadObject<UWorld>(nullptr,TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
            ATutorialNPC* SourceNPC=nullptr;
            if(Map&&Map->PersistentLevel)for(const auto& A:Map->PersistentLevel->Actors)
                if(auto* Director=Cast<ATutorialDirector>(A.Get()))if(Director->NPCRef)SourceNPC=Director->NPCRef.Get();
            if(!Test.TestNotNull(TEXT("从真实保存地图读取第一客人模板"),SourceNPC))return false;
            Params.Template=SourceNPC;
            NPC=World->SpawnActor<ATutorialNPC>(SourceNPC->GetClass(),FVector(0,0,82),FRotator::ZeroRotator,Params);
            Params.Template=nullptr;
        }
        else NPC = World->SpawnActor<ATutorialNPC>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
        Probe = World->SpawnActor<ACustomerDrinkTestProbe>();
        Spot = World->SpawnActor<AServingSpotActor>();
        if (!Test.TestNotNull(TEXT("原生 NPC"), NPC) || !Test.TestNotNull(TEXT("反射动态委托探针"), Probe) ||
            !Test.TestNotNull(TEXT("原生上菜点"), Spot) || !Test.TestNotNull(TEXT("原生默认子组件"), Drink())) return false;
        if(!bSavedNPC)
        {
            Test.TestFalse(TEXT("原生 CustomerDrink 默认不启用"), Drink()->bEnabled);
            const float HalfZ = NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            NPC->SetActorLocation(FVector(0, 0, HalfZ));
            NPC->GetMesh()->SetSkeletalMesh(BarGirl);
            NPC->GetMesh()->SetRelativeTransform(FTransform(FQuat::Identity,FVector(0,0,-HalfZ),FVector::OneVector));
            Drink()->bEnabled=true;Drink()->Motion=Motion;Drink()->IdleAnimation=Idle;
        }
        else
        {
            Test.TestTrue(TEXT("保存地图的第一客人实例已配置启用"),Drink()->bEnabled);
            Test.TestTrue(TEXT("保存NPC的真实BarGirl网格无夹具替换"),NPC->GetMesh()->GetSkeletalMeshAsset()==BarGirl);
            Test.TestTrue(TEXT("保存实例动作绑定无夹具修补"),Drink()->Motion==Motion&&Drink()->IdleAnimation==Idle);
            Test.TestTrue(TEXT("运行时组件属于新NPC，而非编辑器模板"),Drink()->GetOwner()==NPC);
        }
        NPC->GetCharacterMovement()->DisableMovement();
        Drink()->SipSound = nullptr;
        Drink()->OnDrinkEnded.AddDynamic(Probe, &ACustomerDrinkTestProbe::HandleEnded);
        Probe->Component = Drink();
        Probe->NPC = NPC;
        Probe->Spot = Spot;
        Events->OnDrinkServed.AddDynamic(Probe, &ACustomerDrinkTestProbe::HandleServed);
        Spot->SetActorTransform(Motion->Sample(0) * NPC->GetMesh()->GetComponentTransform());
        Spot->bOneShot = true;
        Spot->bLockCupAfterServing = true;
        Spot->SetActiveRecipe(Latte);
        Cup = World->SpawnActor<ACupActor>(CupClass, Spot->GetActorLocation(), Spot->GetActorRotation(), Params);
        if (!Test.TestNotNull(TEXT("真实 Cup01 实例"), Cup)) return false;
        ConfigureContainer(Cup);
        Cup->VolumeWidgetClass = nullptr;
        Cup->VolumeWidgetComp->SetWidgetClass(nullptr);
        Cup->bFaceCamera = false;
        Cup->MaxVolumeML = 200.f;
        Cup->FillAmount = 0.f;
        Cup->InitialIngredientId = DrinkCoffee;
        Cup->InitializeLiquidState();
        Test.TestEqual(TEXT("显式注入 Coffee 100"), Cup->AddIngredientLiquid(100.f, DrinkCoffee, FLinearColor::Red), 100.f);
        Test.TestEqual(TEXT("显式注入 Milk 100"), Cup->AddIngredientLiquid(100.f, DrinkMilk, FLinearColor::White), 100.f);
        Probe->Cup = Cup;
        World->InitializeActorsForPlay(FURL());
        // 测试时允许末帧微小校正；仍由真实 World 自动调度组件，不手动推进私有业务。
        World->GetWorldSettings()->MinUndilatedFrameTime = 0.f;
        World->UpdateWorldComponents(false, false);
        World->GetWorldSettings()->NotifyBeginPlay();
        bBeganPlay = true;
        NPC->GetCharacterMovement()->DisableMovement();
        // GrabComponent::BeginPlay 会恢复 PhysicsActor 碰撞；夹具在真正 BeginPlay 后再关闭。
        Cup->ContainerMesh->SetSimulatePhysics(false);
        Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        TickFrame(.01f);
        Test.TestTrue(TEXT("Mesh 正式注册且 BeginPlay 已执行"), NPC->GetMesh()->IsRegistered() && NPC->HasActorBegunPlay());
        if(!bSavedNPC)Test.TestTrue(TEXT("原生测试 Mesh 世界原点为零"),NPC->GetMesh()->GetComponentLocation().Equals(FVector::ZeroVector,.00001));
        else Test.TestTrue(TEXT("保存模板实际起点与当前服务点对齐，不假定世界原点为零"),(Motion->Sample(0)*NPC->GetMesh()->GetComponentTransform()).GetLocation().Equals(Spot->GetActorLocation(),.00001));
        Test.TestTrue(TEXT("Mesh 世界缩放为 1"), NPC->GetMesh()->GetComponentScale().Equals(FVector::OneVector, .00001));
        Test.TestTrue(TEXT("Cup01 自己的静态 Mesh 根组件，世界缩放为 1"),
            Cup->GetRootComponent() == Cup->ContainerMesh && Cup->GetActorScale3D().Equals(FVector::OneVector, .00001));
        Test.TestTrue(TEXT("真实 Cup01 的 Mesh 是资产支持的 Mesh"), Cup->ContainerMesh->GetStaticMesh() == Motion->SupportedCupMesh);
        Test.TestTrue(TEXT("Niagara 和 Widget 无运行资产依赖"),
            !Cup->LiquidFX->GetAsset() && !Cup->PourFX->GetAsset() && !Cup->VolumeWidgetComp->GetWidgetClass());
        CheckDrinkNumber(Test, TEXT("显式注入账本"), Transfers()->GetBalance().InjectedML, 200.0);
        CheckDrinkNumber(Test, TEXT("无初始填充账本"), Transfers()->GetBalance().InitialML, 0.0);
        OriginalCupTransform = Cup->GetActorTransform();
        return !bSubmit || Accept(Test);
    }

    static void ConfigureContainer(ALiquidContainerActor* Container)
    {
        Container->ContainerMesh->SetSimulatePhysics(false);
        Container->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Container->SetActorScale3D(FVector::OneVector);
        Container->LiquidFXTemplate = nullptr;
        Container->SplashEffectTemplate = nullptr;
        Container->LiquidFX->SetAutoActivate(false);
        Container->PourFX->SetAutoActivate(false);
        Container->LiquidFX->DeactivateImmediate();
        Container->PourFX->DeactivateImmediate();
        Container->LiquidFX->SetAsset(nullptr);
        Container->PourFX->SetAsset(nullptr);
        Container->bEnableSplash = false;
        Container->bCanPour = false;
    }

    bool Accept(FAutomationTestBase& Test)
    {
        if (!Test.TestTrue(TEXT("TrySubmitCup 真实配方验收成功"), Spot->TrySubmitCup(Cup))) return false;
        OriginalCupTransform = Cup->GetActorTransform();
        Test.TestTrue(TEXT("验收原杯身份"), Spot->GetServedCup() == Cup);
        Test.TestTrue(TEXT("冻结且保存 Accepted 结果"), Cup->IsLiquidFrozen() && Spot->GetLastValidation().bAccepted);
        Test.TestEqual(TEXT("订单与验收 serial 一致"), Spot->GetOrderSerial(), Spot->GetAcceptedOrderSerial());
        Test.TestEqual(TEXT("唯一一次真实 OnDrinkServed"), Probe->ServedCount, 1);
        return true;
    }

    void TickFrame(float Dt) { ++GFrameCounter; World->Tick(LEVELTICK_All, Dt); }
    void TickFor(double Seconds)
    {
        for (double Left = Seconds; Left > 1.e-9; Left -= .01)
            TickFrame(static_cast<float>(FMath::Min(.01, Left)));
    }
    // 最后一帧使用公开 elapsed 校正到目标，避免 float 累加把 3.45 秒变成另一个测量时刻。
    void AdvanceTo(float Target)
    {
        for (int32 I = 0; I < 1100 && Drink()->IsDrinking() && Drink()->GetElapsedSeconds() < Target; ++I)
            TickFrame(FMath::Min(.01f, Target - Drink()->GetElapsedSeconds()));
    }
};

void CheckDrinkLedger(FAutomationTestBase& Test, FCustomerDrinkWorld& T, double Removed, double Remaining)
{
    const FLiquidBalance B = T.Transfers()->GetBalance();
    CheckDrinkNumber(Test, TEXT("InitialML"), B.InitialML, 0);
    CheckDrinkNumber(Test, TEXT("InjectedML"), B.InjectedML, 200);
    CheckDrinkNumber(Test, TEXT("RemovedML"), B.RemovedML, Removed);
    CheckDrinkNumber(Test, TEXT("ContainerML"), B.ContainerML, Remaining);
    CheckDrinkNumber(Test, TEXT("SpilledML"), B.SpilledML, 0);
    CheckDrinkNumber(Test, TEXT("InFlightML"), B.InFlightML, 0);
    CheckDrinkNumber(Test, TEXT("账本误差"), B.ErrorML, 0);
}

void CheckReturnedDrink(FAutomationTestBase& Test, FCustomerDrinkWorld& T, double Consumed)
{
    if (!Test.TestTrue(TEXT("原杯未销毁"), IsValid(T.Cup) && !T.Cup->IsActorBeingDestroyed())) return;
    Test.TestFalse(TEXT("原杯继续可见"), T.Cup->IsHidden());
    Test.TestTrue(TEXT("回到原上菜点完整 Transform"), T.Cup->GetActorTransform().Equals(T.OriginalCupTransform, .00001));
    Test.TestTrue(TEXT("重附原 Spot Root"), T.Cup->GetRootComponent()->GetAttachParent() == T.Spot->SceneRoot);
    Test.TestTrue(TEXT("原杯仍冻结"), T.Cup->IsLiquidFrozen());
    Test.TestFalse(TEXT("没有恢复物理"), T.Cup->ContainerMesh->IsSimulatingPhysics());
    Test.TestEqual(TEXT("没有恢复碰撞"), T.Cup->ContainerMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    Test.TestEqual(TEXT("Grab 保持 None"), T.Cup->GrabComp->GrabType, EGrabType::None);
    Test.TestFalse(TEXT("Grab 组件仍禁用"), T.Cup->GrabComp->IsActive());
    Test.TestFalse(TEXT("远距 Grab 仍禁用"), T.Cup->GrabComp->bAllowRemoteGrab);
    CheckDrinkNumber(Test, TEXT("原杯权威余量"), T.Cup->GetPreciseVolumeML(), 200 - Consumed);
    CheckDrinkNumber(Test, TEXT("Coffee 同比例减少"), T.Cup->GetIngredientVolumeML(DrinkCoffee), 100 - Consumed / 2);
    CheckDrinkNumber(Test, TEXT("Milk 同比例减少"), T.Cup->GetIngredientVolumeML(DrinkMilk), 100 - Consumed / 2);
    CheckDrinkNumber(Test, TEXT("成分总量等于余量"), T.Cup->GetComposition().Total(), T.Cup->GetPreciseVolumeML());
    CheckDrinkLedger(Test, T, Consumed, 200 - Consumed);
}

void SaveDrinkMeasurement(FAutomationTestBase& Test, const TCHAR* Case, FCustomerDrinkWorld& T)
{
    const FLiquidBalance B = T.Transfers()->GetBalance();
    const bool bCup = IsValid(T.Cup) && !T.Cup->IsActorBeingDestroyed();
    const bool bNPC = IsValid(T.NPC) && !T.NPC->IsActorBeingDestroyed();
    const UCustomerDrinkComponent* C = bNPC ? T.Drink() : nullptr;
    DrinkMeasurements.Add(FString::Printf(TEXT("{\"case\":\"%s\",\"cupValid\":%s,\"sameServedCup\":%s,\"visible\":%s,\"frozen\":%s,\"returned\":%s,\"remainingML\":%.17g,\"coffeeML\":%.17g,\"milkML\":%.17g,\"elapsed\":%.17g,\"consumedML\":%.17g,\"active\":%s,\"endedCount\":%d,\"completed\":%s,\"callbackConsumedML\":%.17g,\"servedCount\":%d,\"initialML\":%.17g,\"injectedML\":%.17g,\"removedML\":%.17g,\"containerML\":%.17g,\"spilledML\":%.17g,\"inFlightML\":%.17g,\"errorML\":%.17g}"),
        Case, bCup ? TEXT("true") : TEXT("false"), IsValid(T.Spot) && T.Spot->GetServedCup() == T.Cup ? TEXT("true") : TEXT("false"),
        bCup && !T.Cup->IsHidden() ? TEXT("true") : TEXT("false"), bCup && T.Cup->IsLiquidFrozen() ? TEXT("true") : TEXT("false"),
        bCup && T.Cup->GetActorTransform().Equals(T.OriginalCupTransform, .00001) ? TEXT("true") : TEXT("false"),
        bCup ? T.Cup->GetPreciseVolumeML() : -1.0, bCup ? T.Cup->GetIngredientVolumeML(DrinkCoffee) : -1.0,
        bCup ? T.Cup->GetIngredientVolumeML(DrinkMilk) : -1.0, C ? C->GetElapsedSeconds() : -1.0,
        C ? C->GetConsumedML() : -1.0, C && C->IsDrinking() ? TEXT("true") : TEXT("false"),
        T.Probe->EndedCount, T.Probe->bEndedCompleted ? TEXT("true") : TEXT("false"), T.Probe->EndedConsumedML, T.Probe->ServedCount,
        B.InitialML, B.InjectedML, B.RemovedML, B.ContainerML, B.SpilledML, B.InFlightML, B.ErrorML));
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("FirstCustomer");
    if (!Test.TestTrue(TEXT("创建运行实测目录"), IFileManager::Get().MakeDirectory(*Dir, true))) return;
    const FString Json = TEXT("{\"measurementSource\":\"native-public-getters-real-world-tick\",\"notAPassCertificate\":true,\"cases\":[\n")
        + FString::Join(DrinkMeasurements, TEXT(",\n")) + TEXT("\n]}\n");
    Test.TestTrue(TEXT("保存实测 behaviour.json，不写预填通过结果"), FFileHelper::SaveStringToFile(Json,
        *(Dir / TEXT("behaviour.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

void CheckDrinkRejected(FAutomationTestBase& Test, FCustomerDrinkWorld& T, const TCHAR* Label,
    ALiquidContainerActor* Candidate, AServingSpotActor* CandidateSpot)
{
    const FLiquidBalance Before = T.Transfers()->GetBalance();
    const double Volume = Candidate ? Candidate->GetPreciseVolumeML() : 0;
    const double Coffee = Candidate ? Candidate->GetIngredientVolumeML(DrinkCoffee) : 0;
    const double Milk = Candidate ? Candidate->GetIngredientVolumeML(DrinkMilk) : 0;
    const int32 Revision = Candidate ? Candidate->GetLiquidRevision() : 0;
    const FTransform TM = Candidate ? Candidate->GetActorTransform() : FTransform::Identity;
    const bool Frozen = Candidate && Candidate->IsLiquidFrozen();
    const bool Hidden = Candidate && Candidate->IsHidden();
    Test.TestFalse(Label, T.Drink()->BeginDrink(Candidate, CandidateSpot));
    T.TickFor(.02);
    Test.TestFalse(TEXT("拒绝不激活演出"), T.Drink()->IsDrinking());
    Test.TestNull(TEXT("拒绝不持有 ActiveCup"), T.Drink()->GetDrinkingCup());
    Test.TestEqual(TEXT("拒绝不触发结束回调"), T.Probe->EndedCount, 0);
    if (Candidate)
    {
        Test.TestTrue(TEXT("拒绝不销毁候选"), IsValid(Candidate) && !Candidate->IsActorBeingDestroyed());
        CheckDrinkNumber(Test, TEXT("拒绝不改体积"), Candidate->GetPreciseVolumeML(), Volume);
        CheckDrinkNumber(Test, TEXT("拒绝不改 Coffee"), Candidate->GetIngredientVolumeML(DrinkCoffee), Coffee);
        CheckDrinkNumber(Test, TEXT("拒绝不改 Milk"), Candidate->GetIngredientVolumeML(DrinkMilk), Milk);
        Test.TestEqual(TEXT("拒绝不改 revision"), Candidate->GetLiquidRevision(), Revision);
        Test.TestEqual(TEXT("拒绝不改冻结"), Candidate->IsLiquidFrozen(), Frozen);
        Test.TestEqual(TEXT("拒绝不改可见性"), Candidate->IsHidden(), Hidden);
        Test.TestTrue(TEXT("拒绝不移动候选"), Candidate->GetActorTransform().Equals(TM, .00001));
    }
    const FLiquidBalance After = T.Transfers()->GetBalance();
    CheckDrinkNumber(Test, TEXT("拒绝不记 Removed"), After.RemovedML, Before.RemovedML);
    CheckDrinkNumber(Test, TEXT("拒绝不改 Injected"), After.InjectedML, Before.InjectedML);
    CheckDrinkNumber(Test, TEXT("拒绝不改容器总量"), After.ContainerML, Before.ContainerML);
    CheckDrinkNumber(Test, TEXT("拒绝不制造在途包"), After.InFlightML, Before.InFlightML);
    CheckDrinkNumber(Test, TEXT("拒绝不洒液"), After.SpilledML, Before.SpilledML);
    CheckDrinkNumber(Test, TEXT("拒绝仍守恒"), After.ErrorML, 0);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerDrinkRejectTest, "CyberCafe.Customer.RejectsUnacceptedWrongWorldUnsupportedAndUnreachable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerDrinkRejectTest::RunTest(const FString&)
{
    FCustomerDrinkWorld T;
    if (!T.Setup(*this, false)) return false;
    CheckDrinkRejected(*this, T, TEXT("未验收原杯拒绝"), T.Cup, T.Spot);
    T.Cup->FreezeLiquidForServing();
    CheckDrinkRejected(*this, T, TEXT("只冻结未验收也拒绝"), T.Cup, T.Spot);
    CheckDrinkRejected(*this, T, TEXT("空杯引用拒绝"), nullptr, T.Spot);
    CheckDrinkRejected(*this, T, TEXT("空 Spot 拒绝"), T.Cup, nullptr);

    FCustomerDrinkWorld Legacy;
    if (!Legacy.Setup(*this, false)) return false;
    Legacy.Spot->SetActiveRecipe(nullptr);
    if (!TestTrue(TEXT("旧无配方路径确实上菜"), Legacy.Spot->TrySubmitCup(Legacy.Cup))) return false;
    Legacy.Cup->FreezeLiquidForServing();
    TestTrue(TEXT("旧路径已 served 且冻结"), Legacy.Spot->HasBeenServed() && Legacy.Cup->IsLiquidFrozen());
    TestFalse(TEXT("旧 served 不是配方 Accepted"), Legacy.Spot->GetLastValidation().bAccepted);
    CheckDrinkRejected(*this, Legacy, TEXT("served/frozen 但没有配方验收仍拒绝"), Legacy.Cup, Legacy.Spot);

    FCustomerDrinkWorld Rejected;
    if (!Rejected.Setup(*this, false)) return false;
    Rejected.Cup->ConsumeLiquid(100.f);
    Rejected.Cup->AddIngredientLiquid(100.f, DrinkCoffee, FLinearColor::Red);
    TestFalse(TEXT("错误配比真实验收失败"), Rejected.Spot->TrySubmitCup(Rejected.Cup));
    Rejected.Cup->FreezeLiquidForServing();
    CheckDrinkRejected(*this, Rejected, TEXT("错误配比杯不能靠随后冻结绕过验收"), Rejected.Cup, Rejected.Spot);

    FCustomerDrinkWorld A;
    if (!A.Setup(*this)) return false;
    CheckDrinkRejected(*this, A, TEXT("跨世界 Cup 和 Spot 拒绝"), T.Cup, T.Spot);
    CheckDrinkRejected(*this, A, TEXT("单独跨世界 Cup 拒绝"), T.Cup, A.Spot);
    CheckDrinkRejected(*this, A, TEXT("单独跨世界 Spot 拒绝"), A.Cup, T.Spot);
    A.Drink()->bEnabled = false;
    CheckDrinkRejected(*this, A, TEXT("默认不启用路径拒绝"), A.Cup, A.Spot);
    A.Drink()->bEnabled = true;
    A.Drink()->Motion = nullptr;
    CheckDrinkRejected(*this, A, TEXT("未配置 Motion 拒绝"), A.Cup, A.Spot);
    A.Drink()->Motion = DuplicateObject<UCustomerDrinkMotion>(A.Motion, A.World);
    A.Drink()->Motion->CupSamples.Reset();
    CheckDrinkRejected(*this, A, TEXT("无有效采样的配置拒绝"), A.Cup, A.Spot);
    A.Drink()->Motion = A.Motion;
    A.Cup->ContainerMesh->SetStaticMesh(NewObject<UStaticMesh>(A.World));
    CheckDrinkRejected(*this, A, TEXT("非支持静态 Cup Mesh 拒绝"), A.Cup, A.Spot);
    A.Cup->ContainerMesh->SetStaticMesh(A.Motion->SupportedCupMesh);
    A.NPC->GetMesh()->SetAnimation(nullptr);
    A.NPC->GetMesh()->SetSkeletalMesh(nullptr);
    CheckDrinkRejected(*this, A, TEXT("未配置 NPC Mesh 拒绝"), A.Cup, A.Spot);
    USkeletalMesh* OtherSkeletonMesh = NewObject<USkeletalMesh>(A.World);
    TestTrue(TEXT("不同 Skeleton 前提"), OtherSkeletonMesh->GetSkeleton() != A.Motion->Animation->GetSkeleton());
    A.NPC->GetMesh()->SetSkeletalMesh(OtherSkeletonMesh);
    CheckDrinkRejected(*this, A, TEXT("NPC Skeleton 不匹配拒绝"), A.Cup, A.Spot);
    A.NPC->GetMesh()->SetSkeletalMesh(A.BarGirl);
    A.NPC->GetMesh()->PlayAnimation(A.Idle, true);
    A.NPC->GetMesh()->SetRelativeScale3D(FVector(1.1));
    CheckDrinkRejected(*this, A, TEXT("非单位 Mesh 世界缩放拒绝"), A.Cup, A.Spot);
    A.NPC->GetMesh()->SetRelativeScale3D(FVector::OneVector);
    A.Cup->SetActorScale3D(FVector(1.1));
    CheckDrinkRejected(*this, A, TEXT("非单位 Cup 世界缩放拒绝"), A.Cup, A.Spot);
    A.Cup->SetActorScale3D(FVector::OneVector);
    A.Cup->SetActorLocation(A.OriginalCupTransform.GetLocation() + FVector(5.01, 0, 0));
    CheckDrinkRejected(*this, A, TEXT("起点偏差大于 5 cm 拒绝"), A.Cup, A.Spot);
    A.Cup->SetActorTransform(A.OriginalCupTransform);
    ACupActor* OtherCup = A.World->SpawnActor<ACupActor>();
    if (!TestNotNull(TEXT("不同身份但类型正确的第二杯"), OtherCup)) return false;
    FCustomerDrinkWorld::ConfigureContainer(OtherCup);
    OtherCup->ContainerMesh->SetStaticMesh(A.Motion->SupportedCupMesh);
    OtherCup->SetActorTransform(A.OriginalCupTransform);
    OtherCup->AddIngredientLiquid(100.f, DrinkCoffee, FLinearColor::Red);
    OtherCup->AddIngredientLiquid(100.f, DrinkMilk, FLinearColor::White);
    OtherCup->FreezeLiquidForServing();
    CheckDrinkRejected(*this, A, TEXT("冻结且同 Mesh 同位置不能替换被验收的原杯身份"), OtherCup, A.Spot);
    ABottleActor* Bottle = A.World->SpawnActor<ABottleActor>();
    if (!TestNotNull(TEXT("错误容器类"), Bottle)) return false;
    FCustomerDrinkWorld::ConfigureContainer(Bottle);
    Bottle->FreezeLiquidForServing();
    CheckDrinkRejected(*this, A, TEXT("瓶子不能代替 ACup"), Bottle, A.Spot);
    SaveDrinkMeasurement(*this, TEXT("reject"), A);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerDrinkFullTest, "CyberCafe.Customer.RealAcceptedCupFullDrinkUsesOriginalIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerDrinkFullTest::RunTest(const FString&)
{
    FCustomerDrinkWorld T;
    if (!T.Setup(*this) || !TestTrue(TEXT("合格原杯开始"), T.Drink()->BeginDrink(T.Cup, T.Spot))) return false;
    TestFalse(TEXT("角色由同一时间轴采样，不额外自动播放产生杯手错帧"),T.NPC->GetMesh()->GetSingleNodeInstance()->IsPlaying());
    TestTrue(TEXT("ActiveCup 就是提交原杯"), T.Drink()->GetDrinkingCup() == T.Cup);
    TestFalse(TEXT("同时开始第二个实例拒绝"), T.Drink()->BeginDrink(T.Cup, T.Spot));
    ATutorialNPC* OtherNPC = T.World->SpawnActor<ATutorialNPC>();
    if (!TestNotNull(TEXT("竞争同一杯的第二原生 NPC"), OtherNPC)) return false;
    OtherNPC->GetCharacterMovement()->DisableMovement();
    OtherNPC->SetActorTransform(T.NPC->GetActorTransform());
    OtherNPC->GetMesh()->SetSkeletalMesh(T.BarGirl);
    OtherNPC->GetMesh()->SetRelativeTransform(T.NPC->GetMesh()->GetRelativeTransform());
    OtherNPC->CustomerDrink->bEnabled = true;
    OtherNPC->CustomerDrink->Motion = T.Motion;
    OtherNPC->CustomerDrink->IdleAnimation = T.Idle;
    TestFalse(TEXT("另一个组件不能抢占正在饮用的原杯"), OtherNPC->CustomerDrink->BeginDrink(T.Cup, T.Spot));
    TestTrue(TEXT("拒绝竞争后原组件保持原杯身份"), T.Drink()->GetDrinkingCup() == T.Cup && T.Drink()->IsDrinking());
    T.AdvanceTo(3.f);
    TestTrue(TEXT("抓杯后真实原 Actor 被移动"), !T.Cup->GetActorTransform().Equals(T.OriginalCupTransform, .01));
    TestNull(TEXT("抓杯时从原 Spot 脱离，不创建替身"), T.Cup->GetRootComponent()->GetAttachParent());
    CheckDrinkNumber(*this, TEXT("喝前仍 200"), T.Cup->GetPreciseVolumeML(), 200);
    T.AdvanceTo(7.f);
    TestEqual(TEXT("完整动作时长才结束"), T.Drink()->GetElapsedSeconds(), 7.f);
    TestFalse(TEXT("演出完成不再 active"), T.Drink()->IsDrinking());
    TestNull(TEXT("完成清空 ActiveCup"), T.Drink()->GetDrinkingCup());
    CheckDrinkNumber(*this, TEXT("累计只喝 20"), T.Drink()->GetConsumedML(), 20);
    CheckReturnedDrink(*this, T, 20);
    TestEqual(TEXT("完整演出只有一次结束"), T.Probe->EndedCount, 1);
    TestTrue(TEXT("结束报告完成"), T.Probe->bEndedCompleted);
    TestTrue(TEXT("回调还是原杯"), T.Probe->EndedCup.Get() == T.Cup);
    CheckDrinkNumber(*this, TEXT("回调实际消费量"), T.Probe->EndedConsumedML, 20);
    TestEqual(TEXT("不重复广播上菜"), T.Probe->ServedCount, 1);
    TestEqual(TEXT("恢复 Idle 模式"), T.NPC->GetMesh()->GetAnimationMode(), EAnimationMode::AnimationSingleNode);
    if (TestNotNull(TEXT("恢复原生单节点实例"), T.NPC->GetMesh()->GetSingleNodeInstance()))
        TestTrue(TEXT("恢复原 Idle 资产"), T.NPC->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() == T.Idle);
    TestEqual(TEXT("冻结原杯公共 Consume 仍拒绝"), T.Cup->ConsumeLiquid(20), 0.f);
    TestEqual(TEXT("冻结原杯 Add 仍拒绝"), T.Cup->AddIngredientLiquid(20, DrinkMilk, FLinearColor::White), 0.f);
    TestEqual(TEXT("冻结原杯 emitter 仍拒绝"), T.Transfers()->EmitVolume(T.Cup, 20,
        T.Cup->GetActorLocation(), FVector(0, 0, -50), FVector::ZeroVector), 0.0);
    TestFalse(TEXT("冻结原杯不能破碎"), T.Cup->BreakContainerForImpact());
    TestFalse(TEXT("同杯终生不能再次 BeginDrink"), T.Drink()->BeginDrink(T.Cup, T.Spot));
    T.Drink()->CancelDrink();
    T.TickFor(10);
    CheckReturnedDrink(*this, T, 20);
    TestEqual(TEXT("完成后再 Tick 或 Cancel 不重复结束"), T.Probe->EndedCount, 1);
    SaveDrinkMeasurement(*this, TEXT("full"), T);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerDrinkCancelTest, "CyberCafe.Customer.BeforeMidAndLateSipCancelReturnsOriginalCup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerDrinkCancelTest::RunTest(const FString&)
{
    for (float Time : {0.f, 3.f, 3.45f, 6.2f})
    {
        FCustomerDrinkWorld T;
        if (!T.Setup(*this) || !TestTrue(TEXT("取消场景开始"), T.Drink()->BeginDrink(T.Cup, T.Spot))) return false;
        T.AdvanceTo(Time);
        TestEqual(TEXT("精确到目标时间，不能提前结束"), T.Drink()->GetElapsedSeconds(), Time);
        const double Expected = Time < 3.2f ? 0.0 : Time < 3.7f ? 10.0 : 20.0;
        CheckDrinkNumber(*this, TEXT("取消前精确前缀消耗"), T.Drink()->GetConsumedML(), Expected);
        T.Drink()->CancelDrink();
        TestFalse(TEXT("取消立即停演"), T.Drink()->IsDrinking());
        CheckReturnedDrink(*this, T, Expected);
        TestEqual(TEXT("取消只回调一次"), T.Probe->EndedCount, 1);
        TestFalse(TEXT("取消不报告完成"), T.Probe->bEndedCompleted);
        CheckDrinkNumber(*this, TEXT("取消回调消耗已提交"), T.Probe->EndedConsumedML, Expected);
        TestFalse(TEXT("已用杯即使取消也不可重播"), T.Drink()->BeginDrink(T.Cup, T.Spot));
        T.Drink()->CancelDrink();
        T.TickFor(10);
        CheckReturnedDrink(*this, T, Expected);
        TestEqual(TEXT("取消后计数不重置或继续"), T.Drink()->GetConsumedML(), Expected);
        TestEqual(TEXT("无旧 Tick 再次回调"), T.Probe->EndedCount, 1);
        SaveDrinkMeasurement(*this, *FString::Printf(TEXT("cancel-%.2f"), Time), T);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerDrinkInvalidationTest, "CyberCafe.Customer.OrderSerialMovementAndActorLifecycleAbortWithoutExtraSip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerDrinkInvalidationTest::RunTest(const FString&)
{
    for (int32 Change : {0, 1, 2})
    {
        FCustomerDrinkWorld T;
        if (!T.Setup(*this) || !TestTrue(TEXT("失效场景开始"), T.Drink()->BeginDrink(T.Cup, T.Spot))) return false;
        T.AdvanceTo(3.45f);
        CheckDrinkNumber(*this, TEXT("失效前只喝 10"), T.Drink()->GetConsumedML(), 10);
        const uint32 Serial = T.Spot->GetOrderSerial();
        if (Change == 0) T.Spot->SetActiveRecipe(T.Spot->ActiveRecipe);
        if (Change == 1) T.Spot->SetSubmissionEnabled(false);
        if (Change == 2) T.NPC->AddActorWorldOffset(FVector(50, 0, 0));
        if (Change != 2) TestTrue(TEXT("公开订单 API 确实推进 serial"), T.Spot->GetOrderSerial() != Serial);
        T.TickFrame(.01f);
        TestFalse(TEXT("失效下一帧取消，不能继续喝"), T.Drink()->IsDrinking());
        TestEqual(TEXT("失效帧不推进 elapsed"), T.Drink()->GetElapsedSeconds(), 3.45f);
        CheckReturnedDrink(*this, T, 10);
        TestEqual(TEXT("失效只回调一次"), T.Probe->EndedCount, 1);
        TestFalse(TEXT("失效不报告完整演出"), T.Probe->bEndedCompleted);
        CheckDrinkNumber(*this, TEXT("失效回调只报告旧前缀"), T.Probe->EndedConsumedML, 10);
        T.TickFor(10);
        CheckReturnedDrink(*this, T, 10);
        SaveDrinkMeasurement(*this, *FString::Printf(TEXT("invalidate-%d"), Change), T);
    }
    for (bool bAfterFinish : {false, true})
    for (bool bDestroyNPC : {false, true})
    {
        FCustomerDrinkWorld T;
        if (!T.Setup(*this) || !TestTrue(TEXT("销毁场景开始"), T.Drink()->BeginDrink(T.Cup, T.Spot))) return false;
        T.AdvanceTo(bAfterFinish ? 7.f : 3.45f);
        const double Expected = bAfterFinish ? 20.0 : 10.0;
        const TWeakObjectPtr<UCustomerDrinkComponent> Component(T.Drink());
        const TWeakObjectPtr<ACupActor> OriginalCup(T.Cup);
        if (bDestroyNPC)
        {
            TestTrue(TEXT("销毁 NPC 执行真实 EndPlay"), T.NPC->Destroy());
            CheckReturnedDrink(*this, T, Expected);
        }
        else TestTrue(TEXT("销毁杯执行真实 EndPlay/Destroyed"), T.Cup->Destroy());
        T.TickFor(.02);
        TestEqual(TEXT("销毁期间或完成后都只能回调一次"), T.Probe->EndedCount, 1);
        TestEqual(TEXT("销毁回调保留完成语义"), T.Probe->bEndedCompleted, bAfterFinish);
        CheckDrinkNumber(*this, TEXT("销毁不把 20 记第二次"), T.Probe->EndedConsumedML, Expected);
        if (bDestroyNPC)
        {
            CheckDrinkLedger(*this, T, Expected, 200 - Expected);
            TestTrue(TEXT("NPC 销毁不带走杯"), OriginalCup.IsValid());
            TestTrue(TEXT("随后销毁余杯"), T.Cup->Destroy());
        }
        TestFalse(TEXT("杯弱引用失效"), OriginalCup.IsValid());
        CheckDrinkLedger(*this, T, 200, 0);
        if (Component.IsValid()) Component->CancelDrink();
        T.TickFor(10);
        CheckDrinkLedger(*this, T, 200, 0);
        TestEqual(TEXT("销毁后的 Tick 不复活回调"), T.Probe->EndedCount, 1);
        SaveDrinkMeasurement(*this, *FString::Printf(TEXT("destroy-npc-%d-after-%d"), bDestroyNPC, bAfterFinish), T);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerDrinkStepTest, "CyberCafe.Customer.ConsumeStepWaitsForAnimationAndSkipCancels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerDrinkStepTest::RunTest(const FString&)
{
    for (float After : {0.f, 1.5f})
    {
        FCustomerDrinkWorld T;
        if (!T.Setup(*this)) return false;
        ATutorialDirector* D = T.World->SpawnActorDeferred<ATutorialDirector>(ATutorialDirector::StaticClass(), FTransform::Identity);
        if (!TestNotNull(TEXT("原生无 HUD Director"), D)) return false;
        D->bAutoStart = false;
        D->NarrativeHUDWidgetClass = nullptr;
        D->FinishedText = FText::GetEmpty();
        D->bDialogueVoice = false;
        D->NPCRef = T.NPC;
        D->LastServedCup = T.Cup;
        D->FinishSpawning(FTransform::Identity);
        UTutorialStep_ConsumeServedDrink* Step = NewObject<UTutorialStep_ConsumeServedDrink>(D);
        D->Steps.Add(Step);
        Step->PreDrinkDelay = 0;
        Step->AfterDrinkDelay = After;
        Step->bDestroyCupInsteadOfHide = true;
        Step->OnStepCompleted.AddDynamic(T.Probe, &ACustomerDrinkTestProbe::HandleStepCompleted);
        Step->EnterStep(D);
        TestTrue(TEXT("Enter 走启用的 CustomerDrink"), T.Drink()->IsDrinking());
        T.AdvanceTo(1.f);
        TestFalse(TEXT("旧 Pre/After 计时器不能在一秒完成"), Step->IsCompleted());
        TestFalse(TEXT("旧 Pre 不能隐藏原杯"), T.Cup->IsHidden());
        TestTrue(TEXT("旧 Destroy 配置不能销毁原杯"), IsValid(T.Cup) && !T.Cup->IsActorBeingDestroyed());
        T.AdvanceTo(7.f);
        if (After > 0)
        {
            TestFalse(TEXT("动作结束还必须等 AfterDrink 1.5 秒"), Step->IsCompleted());
            T.TickFor(1.48);
            TestFalse(TEXT("AfterDrink 计时不能提前完成"), Step->IsCompleted());
            T.TickFor(.04);
        }
        else T.TickFor(.02);
        TestTrue(TEXT("真实结束和 AfterDrink 才 MarkCompleted"), Step->IsCompleted());
        TestEqual(TEXT("只完成一次"), T.Probe->StepCompletedCount, 1);
        CheckReturnedDrink(*this, T, 20);
        TestTrue(TEXT("Director 引用仍是原杯"), D->LastServedCup == T.Cup);
        TestEqual(TEXT("Step 不重复上菜事件"), T.Probe->ServedCount, 1);
        Step->ExitStep();
        Step->EnterStep(D);
        TestFalse(TEXT("再次 Enter 不能重播同杯"), T.Drink()->IsDrinking());
        T.TickFor(After + .05);
        CheckReturnedDrink(*this, T, 20);
        Step->ExitStep();
        SaveDrinkMeasurement(*this, *FString::Printf(TEXT("step-after-%.1f"), After), T);
    }
    for (bool bDirectorSkip : {false, true})
    {
        FCustomerDrinkWorld T;
        if (!T.Setup(*this)) return false;
        ATutorialDirector* D = T.World->SpawnActorDeferred<ATutorialDirector>(ATutorialDirector::StaticClass(), FTransform::Identity);
        if (!TestNotNull(TEXT("取消用原生 Director"), D)) return false;
        D->bAutoStart = false;
        D->NarrativeHUDWidgetClass = nullptr;
        D->FinishedText = FText::GetEmpty();
        D->bDialogueVoice = false;
        D->NPCRef = T.NPC;
        D->LastServedCup = T.Cup;
        D->FinishSpawning(FTransform::Identity);
        UTutorialStep_ConsumeServedDrink* Step = NewObject<UTutorialStep_ConsumeServedDrink>(D);
        Step->PreDrinkDelay = 0;
        Step->AfterDrinkDelay = 0;
        D->Steps.Add(Step);
        Step->OnStepCompleted.AddDynamic(T.Probe, &ACustomerDrinkTestProbe::HandleStepCompleted);
        if (bDirectorSkip) { D->StartTutorial(); T.TickFrame(.01f); }
        else Step->EnterStep(D);
        if (!TestTrue(TEXT("Exit/Skip 前动画实际开始"), T.Drink()->IsDrinking())) return false;
        T.AdvanceTo(3.45f);
        if (bDirectorSkip) D->SkipTutorial();
        else Step->ExitStep();
        TestFalse(TEXT("Exit/Skip 立即取消组件"), T.Drink()->IsDrinking());
        TestFalse(TEXT("Exit/Skip 不把当前 Step 标记完成"), Step->IsCompleted());
        TestFalse(TEXT("Exit 移除自己的动态订阅"), T.Drink()->OnDrinkEnded.Contains(Step,
            FName(TEXT("HandleCustomerDrinkEnded"))));
        T.TickFor(10);
        TestFalse(TEXT("Exit 后没有旧 timer 完成 Step"), Step->IsCompleted());
        TestEqual(TEXT("Exit 后没有完成回调"), T.Probe->StepCompletedCount, 0);
        CheckReturnedDrink(*this, T, 10);
        SaveDrinkMeasurement(*this, bDirectorSkip ? TEXT("director-skip") : TEXT("step-exit"), T);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCustomerDrinkReentrantTest, "CyberCafe.Customer.ReentrantOnLiquidChangedCommitsBeforeCancelOrDestruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCustomerDrinkReentrantTest::RunTest(const FString&)
{
    for (ECustomerDrinkProbeAction Action : {ECustomerDrinkProbeAction::Cancel, ECustomerDrinkProbeAction::DestroyCup,
        ECustomerDrinkProbeAction::DestroyNPC, ECustomerDrinkProbeAction::ChangeOrder})
    {
        FCustomerDrinkWorld T;
        if (!T.Setup(*this) || !TestTrue(TEXT("重入场景开始"), T.Drink()->BeginDrink(T.Cup, T.Spot))) return false;
        T.AdvanceTo(3.2f);
        CheckDrinkNumber(*this, TEXT("SipStart 前不消耗"), T.Drink()->GetConsumedML(), 0);
        T.Probe->Action = Action;
        T.Probe->bAttemptRestartOnEnded = true;
        T.Cup->OnLiquidChanged.AddDynamic(T.Probe, &ACustomerDrinkTestProbe::HandleLiquidChanged);
        // 单个真实世界帧覆盖半口；回调必须看见 10 mL 已提交，再取消/销毁/切订单。
        T.TickFrame(.25f);
        TestEqual(TEXT("只收到一次液体通知"), T.Probe->LiquidChangedCount, 1);
        TestTrue(TEXT("通知之前消费计数、成分和账本已一致提交"), T.Probe->bCommittedBeforeLiquidCallback);
        CheckDrinkNumber(*this, TEXT("回调观察到组件已喝 10"), T.Probe->LiquidConsumedML, 10);
        CheckDrinkNumber(*this, TEXT("回调观察到余量 190"), T.Probe->LiquidRemainingML, 190);
        CheckDrinkNumber(*this, TEXT("回调观察到 Removed 10"), T.Probe->LiquidRemovedML, 10);
        TestEqual(TEXT("重入只触发一次结束"), T.Probe->EndedCount, 1);
        TestFalse(TEXT("重入不能报告完整动作"), T.Probe->bEndedCompleted);
        CheckDrinkNumber(*this, TEXT("重入结束回调保留已提交 10"), T.Probe->EndedConsumedML, 10);
        TestFalse(TEXT("结束回调不能重入重播同杯"), T.Probe->bRestartAccepted);
        TestTrue(TEXT("结束时已无 ActiveCup"), T.Probe->bInactiveAtEnded);
        if (Action != ECustomerDrinkProbeAction::DestroyNPC)
            CheckDrinkNumber(*this, TEXT("结束回调重入不能重置组件消费计数"), T.Drink()->GetConsumedML(), 10);
        if (Action == ECustomerDrinkProbeAction::DestroyCup) CheckDrinkLedger(*this, T, 200, 0);
        else CheckReturnedDrink(*this, T, 10);
        T.TickFor(10);
        TestEqual(TEXT("回调后无重复通知"), T.Probe->LiquidChangedCount, 1);
        TestEqual(TEXT("回调后无重复结束"), T.Probe->EndedCount, 1);
        if (Action != ECustomerDrinkProbeAction::DestroyNPC)
            CheckDrinkNumber(*this, TEXT("额外十秒不能重置或继续组件消费计数"), T.Drink()->GetConsumedML(), 10);
        if (Action == ECustomerDrinkProbeAction::DestroyCup) CheckDrinkLedger(*this, T, 200, 0);
        else CheckReturnedDrink(*this, T, 10);
        SaveDrinkMeasurement(*this, *FString::Printf(TEXT("reentrant-%d"), static_cast<int32>(Action)), T);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavedFirstCustomerFlowTest,"CyberCafe.Customer.SavedMapNPCInstanceAndRealConsumeStep",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSavedFirstCustomerFlowTest::RunTest(const FString&)
{
    FCustomerDrinkWorld T;if(!T.Setup(*this,true,true))return false;
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* D=T.World->SpawnActorDeferred<ATutorialDirector>(ATutorialDirector::StaticClass(),FTransform::Identity,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    D->bAutoStart=false;D->bDialogueVoice=false;D->NPCRef=T.NPC;D->LastServedCup=T.Cup;D->FinishSpawning(FTransform::Identity);
    auto* Step=NewObject<UTutorialStep_ConsumeServedDrink>(D);Step->AfterDrinkDelay=0;Step->EnterStep(D);
    TestTrue(TEXT("保存NPC配置能启动真实Consume步骤"),T.Drink()->IsDrinking());
    T.AdvanceTo(3.45f);CheckDrinkNumber(*this,TEXT("实际抿饮一半10mL"),T.Drink()->GetConsumedML(),10);
    T.AdvanceTo(7.f);T.TickFor(.05);TestTrue(TEXT("步骤从真实动画结束通知完成"),Step->IsCompleted());
    CheckReturnedDrink(*this,T,20);TestEqual(TEXT("只记一次交付和一次结束"),T.Probe->ServedCount,1);TestEqual(TEXT("结束回调一次"),T.Probe->EndedCount,1);
    SaveDrinkMeasurement(*this,TEXT("savedMapNPCConsumeStepFinal"),T);Step->ExitStep();return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
