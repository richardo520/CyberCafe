#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Customers/RegularCustomerMemory.h"
#include "Customers/RegularCustomerEncounter.h"
#include "Customers/CustomerDrinkComponent.h"
#include "Customers/CustomerOrderComponent.h"
#include "Customers/RegularCustomerReactions.h"
#include "Customers/RegularCustomerAttention.h"
#include "Core/Events/CyberCafeEventSubsystem.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/VRPawn.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "BoneControllers/AnimNode_LookAt.h"
#include "UObject/UnrealType.h"
#include "EngineUtils.h"
#include "Serving/ServingSpotActor.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialDirector.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/CupActor.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace RegularTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
FString NewSlot() { return TEXT("RegularTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits); }
const FAnimNode_LookAt* HeadNode(UAnimInstance* Instance)
{
    if (Instance) for (TFieldIterator<FStructProperty> Property(Instance->GetClass()); Property; ++Property)
        if (Property->Struct == FAnimNode_LookAt::StaticStruct()) return Property->ContainerPtrToValuePtr<FAnimNode_LookAt>(Instance);
    return nullptr;
}

struct FEncounterWorld
{
    UWorld* World = nullptr;
    UGameInstance* GI = nullptr;
    ARegularCustomerEncounter* Encounter = nullptr;
    ACupActor* Cup = nullptr;
    FString Slot = NewSlot();
    bool bBegan = false;
    ~FEncounterWorld()
    {
        if (World)
        {
            if (bBegan) World->EndPlay(EEndPlayReason::Quit);
            if (GI) GI->Shutdown();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        UGameplayStatics::DeleteGameInSlot(Slot, 0);
    }
    URegularCustomerMemorySubsystem* Memory() const { return GI ? GI->GetSubsystem<URegularCustomerMemorySubsystem>() : nullptr; }
    void Headless()
    {
        for (const auto& A : World->PersistentLevel->Actors)
        {
            if (!IsValid(A.Get())) continue;
            TInlineComponentArray<UActorComponent*> Components(A.Get());
            for (UActorComponent* C : Components)
            {
                if (auto* FX = Cast<UNiagaraComponent>(C)) { FX->SetAutoActivate(false); FX->DeactivateImmediate(); FX->SetAsset(nullptr); }
                if (auto* Audio = Cast<UAudioComponent>(C)) { Audio->SetAutoActivate(false); Audio->Stop(); Audio->SetSound(nullptr); }
                if (auto* W = Cast<UWidgetComponent>(C)) W->SetComponentTickEnabled(false);
                if (!Cast<ATutorialNPC>(A.Get())) if (auto* P = Cast<UPrimitiveComponent>(C)) P->SetSimulatePhysics(false);
            }
            if (auto* L = Cast<ALiquidContainerActor>(A.Get()))
            { L->LiquidFXTemplate = nullptr; L->SplashEffectTemplate = nullptr; L->bEnableSplash = false; L->bCanPour = false; }
        }
    }
    bool Setup(FAutomationTestBase& Test)
    {
        UWorld* Source = LoadObject<UWorld>(nullptr, TEXT("/Game/CyberCafe/Maps/L_RegularCustomer.L_RegularCustomer"));
        if (!Test.TestNotNull(TEXT("Saved standalone encounter map"), Source)) return false;
        UPackage* Package = CreatePackage(*(TEXT("/Temp/RegularCustomer") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        World = Cast<UWorld>(StaticDuplicateObject(Source, Package));
        if (!Test.TestTrue(TEXT("Full saved world copied independently"), World && World != Source && World->PersistentLevel != Source->PersistentLevel)) return false;
        World->WorldType = EWorldType::Game;
        World->AddToRoot();
        auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        GI = NewObject<UGameInstance>(GEngine);
        Context.OwningGameInstance = GI; World->SetGameInstance(GI); GI->OnWorldChanged(nullptr, World); GI->Init();
        if (!Memory()->LoadProfile(Slot)) return false;
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).ShouldSimulatePhysics(false).CreateNavigation(false).CreateAISystem(false);
        World->InitWorld(IV);
        for (const auto& A : World->PersistentLevel->Actors)
        {
            if (auto* E = Cast<ARegularCustomerEncounter>(A.Get())) Encounter = E;
            if (auto* R = Cast<AGlassRecoveryActor>(A.Get())) Cup = Cast<ACupActor>(R->PrototypeCup.Get());
            if (auto* D = Cast<ATutorialDirector>(A.Get()))
                if (!Test.TestFalse(TEXT("Saved tutorial does not auto-start"), D->bAutoStart)) return false;
        }
        if (!Test.TestTrue(TEXT("Saved encounter references copied NPC, spot and recipe"), Encounter && Cup &&
            Encounter->Customer && Encounter->Customer->GetWorld() == World && Encounter->ServingSpot && Encounter->ServingSpot->GetWorld() == World && Encounter->Recipe)) return false;
        Headless();
        Encounter->Customer->CustomerDrink->SipSound = nullptr;
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->MinUndilatedFrameTime = 0;
        World->UpdateWorldComponents(false, false);
        World->GetWorldSettings()->NotifyBeginPlay(); bBegan = true;
        Encounter->Customer->GetCharacterMovement()->DisableMovement();
        Headless();
        Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        return Test.TestTrue(TEXT("Saved encounter automatically greets"), Encounter->GetStage() == ERegularCustomerStage::Greeting);
    }
    void Tick() { ++GFrameCounter; World->Tick(LEVELTICK_All, .02f); }
    bool Serve(FAutomationTestBase& Test)
    {
        if (!Test.TestEqual(TEXT("Real coffee added"), Cup->AddIngredientLiquid(100, TEXT("Coffee"), FLinearColor::Red), 100.f) ||
            !Test.TestEqual(TEXT("Real milk added"), Cup->AddIngredientLiquid(100, TEXT("Milk"), FLinearColor::White), 100.f) ||
            !Test.TestTrue(TEXT("Actual ice crosses cup opening"), TutorialMenuTestHelpers::AddIce(Cup, 1))) return false;
        const FTransform From = Cup->GetActorTransform();
        FTransform To = Encounter->ServingSpot->GetActorTransform(); To.SetScale3D(From.GetScale3D());
        for (int32 I = 1; I <= 16; ++I)
        { FTransform T; T.Blend(From, To, I / 16.f); Cup->SetActorTransform(T, false, nullptr, ETeleportType::TeleportPhysics); Tick(); }
        Cup->ContainerMesh->UpdateOverlaps(); Encounter->ServingSpot->DetectBox->UpdateOverlaps();
        for (int32 I = 0; I < 50 && !Encounter->ServingSpot->HasBeenServed(); ++I) Tick();
        return Test.TestTrue(TEXT("Real stable placement commits accepted cup"), Encounter->ServingSpot->GetServedCup() == Cup &&
            Encounter->GetStage() == ERegularCustomerStage::Tasting);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularMemoryRoundTrip, "CyberCafe.RegularCustomer.MemoryRoundTrip", RegularTests::Flags)
bool FRegularMemoryRoundTrip::RunTest(const FString&)
{
    const FString Slot = RegularTests::NewSlot();
    ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<URegularCustomerMemorySubsystem> M(NewObject<URegularCustomerMemorySubsystem>(GI.Get()));
    TestTrue(TEXT("New profile"), M->LoadProfile(Slot));
    const FGuid Visit = FGuid::NewGuid();
    TestTrue(TEXT("Actual save succeeds"), M->RememberCompleted(TEXT("NightShift"), Visit, TEXT("Latte"), TEXT("冰拿铁"), TEXT("City"), .48, 3));
    TestTrue(TEXT("Repeated callback is idempotent"), M->RememberCompleted(TEXT("NightShift"), Visit, TEXT("Latte"), TEXT("冰拿铁"), TEXT("City"), .48, 3));
    TestEqual(TEXT("One completed visit"), M->Recall(TEXT("NightShift")).CompletedVisits, 1);
    TStrongObjectPtr<URegularCustomerMemorySubsystem> Reloaded(NewObject<URegularCustomerMemorySubsystem>(GI.Get()));
    TestTrue(TEXT("Read from disk through a new service"), Reloaded->LoadProfile(Slot));
    const FRegularCustomerMemory R = Reloaded->Recall(TEXT("NightShift"));
    TestEqual(TEXT("Topic persisted"), R.LastTopic, FName(TEXT("City")));
    TestEqual(TEXT("Composition persisted"), R.LastCoffeeFraction, .48);
    TestEqual(TEXT("Actual ice persisted"), R.LastIceCount, 3);
    TestEqual(TEXT("Other customer isolated"), Reloaded->Recall(TEXT("Other")).CompletedVisits, 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularMemoryUnsupportedSave, "CyberCafe.RegularCustomer.UnsupportedSaveProtected", RegularTests::Flags)
bool FRegularMemoryUnsupportedSave::RunTest(const FString&)
{
    const FString Slot = RegularTests::NewSlot();
    ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
    TStrongObjectPtr<URegularCustomerSaveGame> Future(NewObject<URegularCustomerSaveGame>());
    Future->Version = 99;
    TestTrue(TEXT("Future-version fixture saved"), UGameplayStatics::SaveGameToSlot(Future.Get(), Slot, 0));
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<URegularCustomerMemorySubsystem> M(NewObject<URegularCustomerMemorySubsystem>(GI.Get()));
    TestFalse(TEXT("Future format not treated as empty writable save"), M->LoadProfile(Slot));
    TestFalse(TEXT("Session can remember but cannot overwrite file"), M->RememberCompleted(TEXT("NightShift"), FGuid::NewGuid(), TEXT("Latte"), TEXT("冰拿铁"), NAME_None, .5, 1));
    TestEqual(TEXT("Session remains playable"), M->Recall(TEXT("NightShift")).CompletedVisits, 1);
    auto* Loaded = Cast<URegularCustomerSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestTrue(TEXT("Original future save untouched"), Loaded && Loaded->Version == 99 && Loaded->Customers.IsEmpty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularSavedEncounter, "CyberCafe.RegularCustomer.SavedMapServeTasteAndReturn", RegularTests::Flags)
bool FRegularSavedEncounter::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F;
    if (!F.Setup(*this)) return false;
    TestTrue(TEXT("Saved encounter has live facing tick enabled"), F.Encounter->PrimaryActorTick.bCanEverTick && F.Encounter->IsActorTickEnabled());
    TestTrue(TEXT("Talk choice"), F.Encounter->Choose(0));
    TestTrue(TEXT("Night-shift topic starts real order"), F.Encounter->Choose(0));
    TestFalse(TEXT("No duplicate choice after order starts"), F.Encounter->Choose(0));
    TestEqual(TEXT("No memory from ordering alone"), F.Memory()->Recall(F.Encounter->CustomerId).CompletedVisits, 0);
    if (!F.Serve(*this)) return false;
    TestEqual(TEXT("No memory merely from accepted placement"), F.Memory()->Recall(F.Encounter->CustomerId).CompletedVisits, 0);
    for (int32 I = 0; I < 1200 && F.Encounter->GetStage() == ERegularCustomerStage::Tasting; ++I) F.Tick();
    TestTrue(TEXT("Actual animation and consumption finish encounter"), F.Encounter->GetStage() == ERegularCustomerStage::Completed);
    TestTrue(TEXT("Real cup consumed 20 ml"), FMath::Abs(F.Cup->GetPreciseVolumeML() - 180) < .001);
    TestEqual(TEXT("Feedback uses pre-sip 200 ml snapshot"), F.Encounter->GetAcceptedSnapshot().TotalML, 200.);
    TestEqual(TEXT("One completion persisted"), F.Memory()->Recall(F.Encounter->CustomerId).CompletedVisits, 1);
    TestTrue(TEXT("Reload real disk save"), F.Memory()->LoadProfile(F.Slot));
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* ReturnVisit = F.World->SpawnActorDeferred<ARegularCustomerEncounter>(ARegularCustomerEncounter::StaticClass(), FTransform::Identity);
    ReturnVisit->bAutoStart = false; ReturnVisit->Customer = F.Encounter->Customer;
    ReturnVisit->ServingSpot = F.World->SpawnActor<AServingSpotActor>(Params);
    ReturnVisit->Recipe = F.Encounter->Recipe;
    ReturnVisit->FinishSpawning(FTransform::Identity);
    TestTrue(TEXT("Second encounter starts"), ReturnVisit->StartEncounter());
    TestTrue(TEXT("Return dialogue remembers actual drink and topic"), ReturnVisit->GetLine().ToString().Contains(TEXT("冰拿铁")) && ReturnVisit->GetLine().ToString().Contains(TEXT("夜班")));
    TestTrue(TEXT("Returning talk branch"), ReturnVisit->Choose(0));
    TestTrue(TEXT("Returning choice follows earlier passenger"), ReturnVisit->GetChoices()[0].ToString().Contains(TEXT("乘客")));
    TestTrue(TEXT("Second chapter starts order"), ReturnVisit->Choose(0));
    TestTrue(TEXT("Second visit tells a new chapter"), ReturnVisit->GetLine().ToString().Contains(TEXT("明信片")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularCancelledEncounter, "CyberCafe.RegularCustomer.CancelledVisitDoesNotPersist", RegularTests::Flags)
bool FRegularCancelledEncounter::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F;
    if (!F.Setup(*this)) return false;
    TestTrue(TEXT("Quiet branch starts order"), F.Encounter->Choose(1));
    if (!F.Serve(*this)) return false;
    for (int32 I = 0; I < 100; ++I) F.Tick();
    F.Encounter->CancelEncounter();
    for (int32 I = 0; I < 700; ++I) F.Tick();
    TestTrue(TEXT("Terminal cancelled encounter"), F.Encounter->GetStage() == ERegularCustomerStage::Interrupted);
    TestEqual(TEXT("No completed memory"), F.Memory()->Recall(F.Encounter->CustomerId).CompletedVisits, 0);
    TestFalse(TEXT("No successful save created"), UGameplayStatics::DoesSaveGameExist(F.Slot, 0));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularQuietVisitMemory, "CyberCafe.RegularCustomer.QuietVisitPreservesStory", RegularTests::Flags)
bool FRegularQuietVisitMemory::RunTest(const FString&)
{
    const FString Slot = RegularTests::NewSlot();
    ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<URegularCustomerMemorySubsystem> M(NewObject<URegularCustomerMemorySubsystem>(GI.Get()));
    M->LoadProfile(Slot);
    TestTrue(TEXT("First conversation saved"), M->RememberCompleted(TEXT("Regular"), FGuid::NewGuid(), TEXT("Latte"), TEXT("冰拿铁"), TEXT("NightShift"), .5, 1, true));
    TestEqual(TEXT("First topic chapter completed"), M->Recall(TEXT("Regular")).GetTopicVisits(TEXT("NightShift")), 1);
    TestTrue(TEXT("Grinding fact persisted"), M->Recall(TEXT("Regular")).bLastObservedGrinding);
    M->RememberCompleted(TEXT("Regular"), FGuid::NewGuid(), TEXT("Latte"), TEXT("冰拿铁"), NAME_None, .5, 1);
    M->LoadProfile(Slot);
    TestEqual(TEXT("Quiet visit does not erase topic"), M->Recall(TEXT("Regular")).LastTopic, FName(TEXT("NightShift")));
    TestEqual(TEXT("Quiet visit does not advance story"), M->Recall(TEXT("Regular")).GetTopicVisits(TEXT("NightShift")), 1);
    const FGuid Visit = FGuid::NewGuid();
    M->RememberCompleted(TEXT("Regular"), Visit, TEXT("Latte"), TEXT("冰拿铁"), TEXT("NightShift"), .5, 1);
    M->RememberCompleted(TEXT("Regular"), Visit, TEXT("Latte"), TEXT("冰拿铁"), TEXT("NightShift"), .5, 1);
    TestEqual(TEXT("Only one new chapter per completed visit"), M->Recall(TEXT("Regular")).GetTopicVisits(TEXT("NightShift")), 2);
    TestEqual(TEXT("Exactly three completed visits"), M->Recall(TEXT("Regular")).CompletedVisits, 3);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularReactionPacing, "CyberCafe.RegularCustomer.ReactionPacingAndReading", RegularTests::Flags)
bool FRegularReactionPacing::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F;
    if (!F.Setup(*this)) return false;
    auto* Events = F.GI->GetSubsystem<UCyberCafeEventSubsystem>();
    Events->OnAnyObjectGrabbed.Broadcast(F.Cup, EControllerHand::Left);
    for (int32 I = 0; I < 750; ++I) F.Tick();
    TestTrue(TEXT("Actions do not replace unanswered choice"), F.Encounter->GetStage() == ERegularCustomerStage::Greeting && F.Encounter->Reactions->GetSpokenCount() == 0);
    F.Encounter->Choose(1);
    for (int32 I = 0; I < 750; ++I) F.Tick();
    Events->OnAnyObjectGrabbed.Broadcast(F.Cup, EControllerHand::Left);
    F.Tick();
    TestEqual(TEXT("Real cup event produces one reaction"), F.Encounter->Reactions->GetSpokenCount(), 1);
    TestTrue(TEXT("Cup-specific line"), F.Encounter->GetLine().ToString().Contains(TEXT("杯子")));
    for (int32 I = 0; I < 50; ++I) Events->OnLiquidPoured.Broadcast(nullptr, F.Cup, 1);
    for (int32 I = 0; I < 400; ++I) F.Tick();
    TestEqual(TEXT("Pouring does not interrupt within cooldown"), F.Encounter->Reactions->GetSpokenCount(), 1);
    auto* PC = F.World->SpawnActor<APlayerController>();
    auto* Pawn = F.World->SpawnActor<AVRPawn>();
    PC->Possess(Pawn);
    const FVector ScreenNormal = F.Encounter->OrderDisplay->Panel->GetForwardVector();
    // Supply a stable view pose in the no-HMD fixture; the controller's normal
    // camera update must not replace it with its default control rotation.
    Pawn->Camera->bLockToHmd = false;
    Pawn->Camera->bUsePawnControlRotation = false;
    PC->SetControlRotation((-ScreenNormal).Rotation());
    Pawn->SetActorRotation((-ScreenNormal).Rotation());
    Pawn->Camera->SetWorldLocation(F.Encounter->OrderDisplay->Panel->GetComponentLocation() + ScreenNormal * 80);
    Pawn->Camera->SetWorldRotation((-ScreenNormal).Rotation());
    PC->SetViewTarget(Pawn);
    PC->PlayerCameraManager->bUseClientSideCameraUpdates = false;
    PC->PlayerCameraManager->UpdateCamera(.02f);
    TestTrue(TEXT("Menu-facing camera cache matches supplied eye position"),
        PC->PlayerCameraManager->GetCameraLocation().Equals(Pawn->Camera->GetComponentLocation(), .001));
    // Headless fixtures suppress widget draw ticks. Build the real menu's
    // current geometry once, as the existing pointer-input fixtures do.
    F.Encounter->OrderDisplay->Panel->TickComponent(.02f, LEVELTICK_All, nullptr);
    FMenuPointerHit ReadingHit;
    TestTrue(TEXT("Camera actually intersects the rendered menu plane"), F.Encounter->OrderDisplay->TraceMenuPointer(
        Pawn->Camera->GetComponentLocation(), Pawn->Camera->GetForwardVector(), 250, ReadingHit) && ReadingHit.bPanel);
    for (int32 I = 0; I < 210; ++I) F.Tick();
    FMenuPointerHit CurrentReadingHit;
    const bool StillOnMenu = F.Encounter->OrderDisplay->TraceMenuPointer(Pawn->Camera->GetComponentLocation(), Pawn->Camera->GetForwardVector(), 250, CurrentReadingHit) && CurrentReadingHit.bPanel;
    AddInfo(FString::Printf(TEXT("Reading camera=%s forward=%s menu=%s normal=%s hit=%d open=%d"),
        *Pawn->Camera->GetComponentLocation().ToString(), *Pawn->Camera->GetForwardVector().ToString(),
        *F.Encounter->OrderDisplay->Panel->GetComponentLocation().ToString(), *F.Encounter->OrderDisplay->Panel->GetForwardVector().ToString(), StillOnMenu,
        F.Encounter->OrderDisplay->IsPanelOpen()));
    TestTrue(TEXT("Head pose still intersects menu after real world ticks"), StillOnMenu);
    TestEqual(TEXT("Actually looking at open menu defers ready reaction without a button event"), F.Encounter->Reactions->GetSpokenCount(), 1);
    Pawn->Camera->SetWorldRotation(FRotator(90, 0, 0));
    PC->SetControlRotation(FRotator(90, 0, 0));
    for (int32 I = 0; I < 70; ++I) F.Tick();
    TestEqual(TEXT("Reaction resumes after reading window"), F.Encounter->Reactions->GetSpokenCount(), 2);
    for (int32 I = 0; I < 30; ++I) Events->OnLiquidPoured.Broadcast(nullptr, F.Cup, 1);
    for (int32 I = 0; I < 700; ++I) F.Tick();
    TestEqual(TEXT("Repeated drops do not produce repeated lines"), F.Encounter->Reactions->GetSpokenCount(), 2);
    F.Encounter->Reactions->QueueRejection(TEXT("Expired"), TEXT("过期的反馈"));
    for (int32 Pass = 0; Pass < 5; ++Pass)
    {
        F.Encounter->OrderDisplay->OnMenuInteractionNative.Broadcast(F.Encounter->OrderDisplay);
        for (int32 I = 0; I < 200; ++I) F.Tick();
    }
    for (int32 I = 0; I < 350; ++I) F.Tick();
    TestEqual(TEXT("Stale reaction discarded after long reading"), F.Encounter->Reactions->GetSpokenCount(), 2);
    F.Encounter->Reactions->QueueRejection(TEXT("Expired"), TEXT("新一次出品的反馈"));
    F.Tick();
    TestEqual(TEXT("New submission can retry an expired feedback reason"), F.Encounter->Reactions->GetSpokenCount(), 3);
    F.Encounter->Reactions->QueueRejection(TEXT("WrongCup"), TEXT("测试拒收反馈"));
    F.Encounter->CancelEncounter();
    const FText CancelledLine = F.Encounter->GetLine();
    for (int32 I = 0; I < 900; ++I) F.Tick();
    TestTrue(TEXT("Cancel discards queued reactions"), F.Encounter->GetLine().EqualTo(CancelledLine));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularGrindingObservation, "CyberCafe.RegularCustomer.ActualGrindingAndHeadLayer", RegularTests::Flags)
bool FRegularGrindingObservation::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F;
    if (!F.Setup(*this)) return false;
    F.Encounter->Choose(1);
    ACoffeeGrinderActor* Grinder = nullptr;
    for (TActorIterator<ACoffeeGrinderActor> It(F.World); It; ++It) { Grinder = *It; break; }
    if (!TestNotNull(TEXT("Saved workbench grinder"), Grinder)) return false;
    for (int32 I = 0; I < 750; ++I) F.Tick();
    Grinder->AdvanceGrinding(180 * Grinder->EffectiveDirectionSign, .02f);
    TestFalse(TEXT("Empty handle motion is not grinding"), F.Encounter->Reactions->DidObserveGrinding());
    TestTrue(TEXT("Real beans added"), Grinder->TryAddBeans(2) > 0);
    const double Before = Grinder->TotalGroundOutputGrams;
    Grinder->AdvanceGrinding(180 * Grinder->EffectiveDirectionSign, .02f);
    TestTrue(TEXT("Actual production increases even when drawer immediately receives grounds"), Grinder->TotalGroundOutputGrams > Before);
    F.Tick();
    TestTrue(TEXT("Actual grinding observed"), F.Encounter->Reactions->DidObserveGrinding());
    TestTrue(TEXT("Grinding-specific response"), F.Encounter->GetLine().ToString().Contains(TEXT("磨豆")));
    auto* Mesh = F.Encounter->Customer->GetMesh();
    auto* Layer = Cast<URegularCustomerAttentionAnimation>(Mesh->GetPostProcessInstance());
    if (!TestNotNull(TEXT("Saved head-only postprocess layer running"), Layer)) return false;
    auto* PC = F.World->SpawnActor<APlayerController>();
    auto* Pawn = F.World->SpawnActor<AVRPawn>();
    PC->Possess(Pawn);
    const FTransform MeshBefore = Mesh->GetComponentTransform();
    const FVector Head = Mesh->GetSocketLocation(TEXT("head"));
    Pawn->Camera->SetWorldLocation(Head + F.Encounter->Customer->GetActorForwardVector() * 100 + FVector(0, 35, 20));
    for (int32 I = 0; I < 250; ++I) F.Tick();
    TestTrue(TEXT("Gaze receives actual player camera"), Layer->AttentionAlpha > .1);
    Mesh->GetSingleNodeInstance()->SetPlaying(false);
    const FTransform HeadWith = Mesh->GetSocketTransform(TEXT("head"));
    const FTransform HandWith = Mesh->GetSocketTransform(TEXT("hand_r"));
    Mesh->SetDisablePostProcessBlueprint(true);
    Mesh->RefreshBoneTransforms();
    const FTransform BaseHead = Mesh->GetSocketTransform(TEXT("head"));
    const double HeadAngle = HeadWith.GetRotation().AngularDistance(BaseHead.GetRotation());
    TestTrue(TEXT("Head pose actually changed by gaze"), HeadAngle > .01);
    TestTrue(TEXT("Head turn stays within authored 28-degree limit"), HeadAngle <= FMath::DegreesToRadians(28.5));
    const FAnimNode_LookAt* LookNode = RegularTests::HeadNode(Layer);
    if (TestNotNull(TEXT("Actual compiled skeletal control node"), LookNode))
    {
        const FVector Desired = (Layer->AttentionTarget - BaseHead.GetLocation()).GetSafeNormal();
        TestTrue(TEXT("Head's calibrated forward axis moves toward the actual target"),
            FVector::DotProduct(HeadWith.TransformVectorNoScale(LookNode->LookAt_Axis.Axis), Desired) >
            FVector::DotProduct(BaseHead.TransformVectorNoScale(LookNode->LookAt_Axis.Axis), Desired));
    }
    AddInfo(FString::Printf(TEXT("Actual head delta %.3f degrees; right hand displacement %.6f cm"),
        FMath::RadiansToDegrees(HeadAngle), FVector::Distance(HandWith.GetLocation(), Mesh->GetSocketLocation(TEXT("hand_r")))));
    TestTrue(TEXT("Head layer leaves hand trajectory unchanged"), HandWith.Equals(Mesh->GetSocketTransform(TEXT("hand_r")), .001));
    TestTrue(TEXT("Character mesh world calibration unchanged"), MeshBefore.Equals(Mesh->GetComponentTransform(), .001));
    Mesh->SetDisablePostProcessBlueprint(false);
    Mesh->GetSingleNodeInstance()->SetPlaying(true);
    if (!F.Serve(*this)) return false;
    bool bSawDrink = false, bHeadDisabledDuringDrink = true, bCheckedSipPose = false;
    for (int32 I = 0; I < 1200 && F.Encounter->GetStage() == ERegularCustomerStage::Tasting; ++I)
    {
        F.Tick();
        Layer = Cast<URegularCustomerAttentionAnimation>(Mesh->GetPostProcessInstance());
        if (F.Encounter->Customer->CustomerDrink->IsDrinking())
        {
            bSawDrink = true;
            const FAnimNode_LookAt* RuntimeNode = RegularTests::HeadNode(Layer);
            bHeadDisabledDuringDrink &= Layer && Layer->AttentionAlpha == 0 && RuntimeNode && RuntimeNode->Alpha == 0;
            if (!bCheckedSipPose && F.Encounter->Customer->CustomerDrink->GetElapsedSeconds() > 2.5)
            {
                Mesh->RefreshBoneTransforms();
                const FTransform WithLayer = Mesh->GetSocketTransform(TEXT("head"));
                Mesh->SetDisablePostProcessBlueprint(true); Mesh->RefreshBoneTransforms();
                TestTrue(TEXT("Sipping head pose is exactly the authored animation"), WithLayer.Equals(Mesh->GetSocketTransform(TEXT("head")), .001));
                Mesh->SetDisablePostProcessBlueprint(false); Mesh->RefreshBoneTransforms();
                bCheckedSipPose = true;
            }
        }
    }
    TestTrue(TEXT("Head overlay yields throughout actual sipping"), bSawDrink && bHeadDisabledDuringDrink);
    TestTrue(TEXT("Actual sipping pose comparison performed"), bCheckedSipPose);
    TestTrue(TEXT("Real serving and sipping still complete"), F.Encounter->GetStage() == ERegularCustomerStage::Completed);
    TestTrue(TEXT("Observed grinding persists only after completion"), F.Memory()->Recall(F.Encounter->CustomerId).bLastObservedGrinding);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularTwoSavedVisits, "CyberCafe.RegularCustomer.TwoSavedVisitsProgressStory", RegularTests::Flags)
bool FRegularTwoSavedVisits::RunTest(const FString&)
{
    RegularTests::FEncounterWorld First;
    if (!First.Setup(*this)) return false;
    First.Encounter->Choose(0); First.Encounter->Choose(0);
    if (!First.Serve(*this)) return false;
    for (int32 I = 0; I < 1200 && First.Encounter->GetStage() == ERegularCustomerStage::Tasting; ++I) First.Tick();
    if (!TestTrue(TEXT("First real visit completes"), First.Encounter->GetStage() == ERegularCustomerStage::Completed)) return false;
    RegularTests::FEncounterWorld Second;
    Second.Slot = First.Slot; // New world and GameInstance read the actual file, not a copied in-memory record.
    if (!Second.Setup(*this)) return false;
    TestTrue(TEXT("Second real visit recalls first topic"), Second.Encounter->GetLine().ToString().Contains(TEXT("夜班")));
    Second.Encounter->Choose(0); Second.Encounter->Choose(0);
    TestTrue(TEXT("Second conversation continues rather than repeats"), Second.Encounter->GetLine().ToString().Contains(TEXT("明信片")));
    if (!Second.Serve(*this)) return false;
    for (int32 I = 0; I < 1200 && Second.Encounter->GetStage() == ERegularCustomerStage::Tasting; ++I) Second.Tick();
    TestTrue(TEXT("Second real visit completes"), Second.Encounter->GetStage() == ERegularCustomerStage::Completed);
    TestTrue(TEXT("Read persisted second result"), Second.Memory()->LoadProfile(Second.Slot));
    const FRegularCustomerMemory Record = Second.Memory()->Recall(Second.Encounter->CustomerId);
    TestEqual(TEXT("Two real completions counted"), Record.CompletedVisits, 2);
    TestEqual(TEXT("Two finished conversations counted"), Record.GetTopicVisits(TEXT("NightShift")), 2);
    return !HasAnyErrors();
}
#endif
