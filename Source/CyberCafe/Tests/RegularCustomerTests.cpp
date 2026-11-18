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
#include "Serving/DrinkMenuData.h"
#include "Tutorial/TutorialNPC.h"
#include "Tutorial/TutorialDirector.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/CupActor.h"
#include "Tests/TutorialMenuTestHelpers.h"
#include "Tests/RegularDialogueTestHelpers.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/RegularCustomerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
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
    bool Serve(FAutomationTestBase& Test, UDrinkRecipe* ActiveRecipe = nullptr)
    {
        if (ActiveRecipe)
        {
            auto* Menu = LoadObject<UDrinkMenuData>(nullptr, TEXT("/Game/CyberCafe/Gameplay/Serving/Menu/DA_RecipeMenu.DA_RecipeMenu"));
            const FDrinkMenuEntry* Entry = Menu ? Menu->Entries.FindByPredicate([ActiveRecipe](const FDrinkMenuEntry& E) { return E.Recipe == ActiveRecipe; }) : nullptr;
            if (!Test.TestNotNull(TEXT("Followup order exists in actual recipe menu"), Entry)) return false;
            for (const auto& Ingredient : Entry->Ingredients)
                if (!Test.TestEqual(TEXT("Actual recipe ingredient added"), Cup->AddIngredientLiquid(Ingredient.VolumeML, Ingredient.IngredientId, FLinearColor::White), float(Ingredient.VolumeML))) return false;
            if (ActiveRecipe->ExpectedIceCount > 0 && !Test.TestTrue(TEXT("Real ice for followup"), TutorialMenuTestHelpers::AddIce(Cup, 1))) return false;
        }
        else
        {
        if (!Test.TestEqual(TEXT("Real coffee added"), Cup->AddIngredientLiquid(100, TEXT("Coffee"), FLinearColor::Red), 100.f) ||
            !Test.TestEqual(TEXT("Real milk added"), Cup->AddIngredientLiquid(100, TEXT("Milk"), FLinearColor::White), 100.f) ||
            !Test.TestTrue(TEXT("Actual ice crosses cup opening"), TutorialMenuTestHelpers::AddIce(Cup, 1))) return false;
        }
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularCloudPresentationOwnership, "CyberCafe.RegularCustomer.CloudPresentationOwnership", RegularTests::Flags)
bool FRegularCloudPresentationOwnership::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F;
    if (!F.Setup(*this)) return false;
    auto Wire = MakeShared<RegularDialogueTests::FTransport>();
    double Time = 0;
    F.Encounter->DialogueClient->UseTestTransport(Wire, [&Time] { return Time; });
    F.Encounter->Choose(0);
    TestTrue(TEXT("Topic line awaiting async response"), F.Encounter->DialogueClient->IsPending());
    const FString OldLine = RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[0]);
    F.Encounter->Choose(1);
    const FString NewLine = RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[1]);
    Wire->Reply(0, 200, RegularDialogueTests::Response(OldLine));
    TestTrue(TEXT("Earlier page cannot overwrite selected order story"), F.Encounter->GetLine().ToString() != OldLine);
    Wire->Reply(1, 200, RegularDialogueTests::Response(NewLine));
    TestEqual(TEXT("Current response accepted"), F.Encounter->GetLine().ToString(), NewLine);
    TestTrue(TEXT("Cloud text cannot change order lifecycle"), F.Encounter->GetStage() == ERegularCustomerStage::Preparing && F.Encounter->Customer->CustomerOrder->IsOrderActive());
    TestEqual(TEXT("Cloud response does not write memories"), F.Memory()->Recall(F.Encounter->CustomerId).CompletedVisits, 0);
    for (int32 I = 0; I < 800; ++I) F.Tick();
    F.GI->GetSubsystem<UCyberCafeEventSubsystem>()->OnAnyObjectGrabbed.Broadcast(F.Cup, EControllerHand::Left);
    F.Tick();
    if (!TestEqual(TEXT("One ambient request"), Wire->Bodies.Num(), 3)) return false;
    const FString AmbientLine = RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[2]);
    F.Encounter->OrderDisplay->OnMenuInteractionNative.Broadcast(F.Encounter->OrderDisplay);
    Wire->Reply(2, 200, RegularDialogueTests::Response(AmbientLine));
    F.Encounter->Tick(.02f);
    TestEqual(TEXT("Reply arriving during menu reading waits"), F.Encounter->GetLine().ToString(), NewLine);
    for (int32 I = 0; I < 300; ++I) F.Tick();
    TestEqual(TEXT("Reply resumes after reading finishes"), F.Encounter->GetLine().ToString(), AmbientLine);
    for (int32 I = 0; I < 800; ++I) F.Tick();
    F.GI->GetSubsystem<UCyberCafeEventSubsystem>()->OnLiquidPoured.Broadcast(nullptr, F.Cup, 1);
    F.Tick();
    if (!TestEqual(TEXT("Another real cue requested"), Wire->Bodies.Num(), 4)) return false;
    const FString Stale = RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[3]);
    F.Encounter->CancelEncounter();
    const FString Cancelled = F.Encounter->GetLine().ToString();
    Wire->Reply(3, 200, RegularDialogueTests::Response(Stale));
    TestEqual(TEXT("Cancelled encounter cannot receive stale AI line"), F.Encounter->GetLine().ToString(), Cancelled);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularPresentationFeedback, "CyberCafe.RegularCustomer.PanelAspectAndDialogueNotice", RegularTests::Flags)
bool FRegularPresentationFeedback::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F; if (!F.Setup(*this)) return false;
    auto* E = F.Encounter;
    E->SetActorScale3D(FVector(1.25, 1.6875, .897436));
    const FTransform AuthoredPose = E->GetActorTransform();
    const FVector PanelAnchor = E->Panel->GetComponentLocation();
    F.Tick();
    const double ExpectedScale = double(E->PanelScale) * 1.6875;
    TestTrue(TEXT("Nonuniform actor scaling preserves readable uniform panel pixels"),
        E->Panel->GetComponentScale().Equals(FVector(ExpectedScale), .00001));
    TestTrue(TEXT("User actor placement and scale preserved"), E->GetActorTransform().Equals(AuthoredPose, .00001));
    TestTrue(TEXT("Panel remains at authored anchor"), E->Panel->GetComponentLocation().Equals(PanelAnchor, .00001));
    E->UpdatePanelFacing(PanelAnchor + FVector(80, 40, 30));
    const FIntRect Rect = URegularCustomerWidget::ChoiceRect(0);
    const FVector ChoicePoint = E->Panel->GetComponentTransform().TransformPosition(FVector(0,
        480 - (Rect.Min.X + Rect.Max.X) * .5, 280 - (Rect.Min.Y + Rect.Max.Y) * .5));
    const FVector Normal = E->Panel->GetForwardVector();
    FMenuPointerHit Hit;
    TestTrue(TEXT("Ray still hits visible scaled choice after billboard rotation"),
        E->TracePointer(ChoicePoint + Normal * 80, -Normal, 250, Hit) && Hit.Action == 0);
    const auto* Defaults = GetDefault<ARegularCustomerEncounter>();
    if (TestNotNull(TEXT("Notice has a real sound asset"), Defaults->DialogueNoticeAudio->Sound.Get()))
        TestTrue(TEXT("Notice grain is short and bounded"), Defaults->DialogueNoticeAudio->Sound->GetDuration() < .12f);
    TestNotNull(TEXT("Notice has spatial attenuation"), Defaults->DialogueNoticeAudio->AttenuationSettings.Get());
    TestTrue(TEXT("Notice comes from customer mesh, not floating panel"), E->DialogueNoticeAudio->GetAttachParent() == E->Customer->GetMesh());
    auto Wire = MakeShared<RegularDialogueTests::FTransport>();
    E->DialogueClient->UseTestTransport(Wire, [] { return 0.; });
    auto* Widget = Cast<URegularCustomerWidget>(E->Panel->GetUserWidgetObject());
    if (!TestNotNull(TEXT("Actual regular customer typewriter widget"), Widget)) return false;
    if (!TestTrue(TEXT("Choice starts a new pending cloud line"), E->Choose(0))) return false;
    TestFalse(TEXT("API waiting ellipsis is not typed or voiced"), Widget->IsTyping());
    const FString Reply = RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[0]);
    Wire->Reply(0, 200, RegularDialogueTests::Response(Reply));
    TestTrue(TEXT("Committed line starts empty and types with voice enabled"), Widget->IsTyping() && Widget->GetVisibleText().IsEmpty() && Widget->bEnableVoice);
    for (int32 I = 0; I < 25; ++I) F.Tick();
    TestTrue(TEXT("Only part of the line is revealed after half a second"), Widget->GetVisibleText().Len() > 0 && Widget->GetVisibleText().Len() < Reply.Len());
    const TSharedRef<SWidget> Slate = Widget->TakeWidget(); // Keep the UI alive like the real widget component does.
    const FString Visible = Widget->GetVisibleText();
    TArray<UWidget*> DrawnWidgets; Widget->WidgetTree->GetAllWidgets(DrawnWidgets);
    TestTrue(TEXT("Real UMG body shows the revealed prefix, not the full line"), DrawnWidgets.ContainsByPredicate([&Visible](UWidget* W)
        { auto* T = Cast<UTextBlock>(W); return T && T->GetText().ToString() == Visible; }));
    E->RefreshPanel();
    TestTrue(TEXT("Refreshing options or status does not restart or complete typing"), Widget->IsTyping() && Widget->GetVisibleText() == Visible);
    for (int32 I = 0; I < 600; ++I) F.Tick();
    TestTrue(TEXT("Typewriter finishes naturally without looping"), !Widget->IsTyping() && Widget->GetVisibleText() == Reply);
    TestTrue(TEXT("Next page requests another line"), E->Choose(0));
    E->bEnableDialogueNotice = false;
    Wire->Reply(1, 200, RegularDialogueTests::Response(RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[1])));
    TestTrue(TEXT("Voice can be disabled while text still types"), Widget->IsTyping() && !Widget->bEnableVoice);
    E->EndPlay(EEndPlayReason::Quit);
    Wire->Reply(0, 200, RegularDialogueTests::Response(Reply));
    TestFalse(TEXT("Ended encounter cannot restart typewriter or voice"), Widget->IsTyping());
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularContinuousConversation, "CyberCafe.RegularCustomer.ContinuousChatAndThreeDrinks", RegularTests::Flags)
bool FRegularContinuousConversation::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F; if (!F.Setup(*this)) return false;
    auto* E = F.Encounter;
    const FGuid SessionVisit = E->Visit;
    UWorld* OriginalWorld = F.World;
    auto* OriginalCustomer = E->Customer.Get();
    E->Choose(0); E->Choose(0);
    if (!F.Serve(*this)) return false;
    for (int32 I = 0; I < 1200 && E->GetStage() == ERegularCustomerStage::Tasting; ++I) F.Tick();
    if (!TestTrue(TEXT("First real drink finishes"), E->GetStage() == ERegularCustomerStage::Completed)) return false;
    TestEqual(TEXT("First drink persisted"), F.Memory()->Recall(E->CustomerId).CompletedDrinks, 1);
    const auto ChatUntilOffer = [this, E]()
    {
        for (int32 I = 0; I < 6 && E->GetStage() != ERegularCustomerStage::ReorderOffer; ++I)
            if (!TestTrue(TEXT("Keep chatting without level reload"), E->Choose(0))) return false;
        return TestTrue(TEXT("Customer proposes a drink after several exchanges"), E->GetStage() == ERegularCustomerStage::ReorderOffer);
    };
    if (!ChatUntilOffer()) return false;
    const uint32 CompletedSerial = E->ServingSpot->GetOrderSerial();
    TestTrue(TEXT("Proposal alone does not change served slot or start an order"), E->ServingSpot->GetServedCup() == F.Cup && !E->Customer->CustomerOrder->IsOrderActive());
    TestTrue(TEXT("Can decline a new drink and keep chatting"), E->Choose(1) && E->GetStage() == ERegularCustomerStage::Chatting);
    TestEqual(TEXT("Declining preserves old cup and order serial"), E->ServingSpot->GetOrderSerial(), CompletedSerial);
    if (!ChatUntilOffer()) return false;
    for (int32 Drink = 2; Drink <= 3; ++Drink)
    {
        UDrinkRecipe* Proposed = E->NextRecipe;
        auto* OldCup = F.Cup;
        const FVector SupportedScale = OldCup->GetActorScale3D();
        const FGuid OldOrder = E->DrinkOrderId;
        if (!TestTrue(TEXT("Player accepts proposed drink"), E->Choose(0) && E->GetStage() == ERegularCustomerStage::Preparing)) return false;
        E->HandleFinished(nullptr, true, 20);
        TestTrue(TEXT("A stale null-cup completion cannot cancel the next unserved order"), E->GetStage() == ERegularCustomerStage::Preparing && E->Customer->CustomerOrder->IsOrderActive());
        TestTrue(TEXT("Accepted recipe matches proposal and actual serving authority"), E->Recipe == Proposed && E->ServingSpot->ActiveRecipe == Proposed);
        TestTrue(TEXT("Same visit, new order identity"), E->Visit == SessionVisit && E->DrinkOrderId != OldOrder);
        TestTrue(TEXT("Old cup retired only after acceptance"), OldCup->IsActorBeingDestroyed());
        ACupActor* Fresh = nullptr;
        for (TActorIterator<AGlassRecoveryActor> It(F.World); It && !Fresh; ++It) Fresh = Cast<ACupActor>(It->GetCurrent(0));
        if (!TestTrue(TEXT("Station supplies a distinct clean usable cup"), Fresh && Fresh != OldCup && !Fresh->IsLiquidFrozen() && Fresh->GetPreciseVolumeML() == 0)) return false;
        TestTrue(TEXT("Fresh cup preserves the calibrated animation scale"), Fresh->GetActorScale3D().Equals(SupportedScale, .00001));
        for (TActorIterator<AGlassRecoveryActor> It(F.World); It; ++It)
            TestTrue(TEXT("Supply references no longer point to retired cup"), It->PrototypeCup != OldCup);
        for (TActorIterator<AIceStationActor> It(F.World); It; ++It)
            TestTrue(TEXT("Physical ice station no longer targets retired cup"), It->Cup != OldCup);
        F.Cup = Fresh; F.Headless();
        F.Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        if (!F.Serve(*this, Proposed)) return false;
        for (int32 I = 0; I < 1200 && E->GetStage() == ERegularCustomerStage::Tasting; ++I) F.Tick();
        if (!TestTrue(TEXT("Followup actually animates and consumes drink"), E->GetStage() == ERegularCustomerStage::Completed && F.Cup->GetPreciseVolumeML() < Proposed->TargetVolumeML)) return false;
        const auto M = F.Memory()->Recall(E->CustomerId);
        TestEqual(TEXT("Several drinks still count as one visit"), M.CompletedVisits, 1);
        TestEqual(TEXT("Each completed drink counts exactly once"), M.CompletedDrinks, Drink);
        TestEqual(TEXT("Latest real recipe remembered"), M.LastRecipeId, Proposed->RecipeId);
        TestTrue(TEXT("No milk-ratio claim for a multi-ingredient recipe"), !E->GetLine().ToString().Contains(TEXT("牛奶的比例稍微多一点")));
        TestTrue(TEXT("World and NPC were never replaced"), E->GetWorld() == OriginalWorld && E->Customer == OriginalCustomer);
        if (!ChatUntilOffer()) return false;
    }
    const int32 Completed = F.Memory()->Recall(E->CustomerId).CompletedDrinks;
    TestTrue(TEXT("Another order may begin"), E->Choose(0));
    E->CancelEncounter();
    TestEqual(TEXT("Cancelling next order does not count another drink"), F.Memory()->Recall(E->CustomerId).CompletedDrinks, Completed);
    TestTrue(TEXT("Memory reloads from disk"), F.Memory()->LoadProfile(F.Slot));
    TestEqual(TEXT("All completed drinks persisted"), F.Memory()->Recall(E->CustomerId).CompletedDrinks, 3);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularLegacyMultipleDrinks, "CyberCafe.RegularCustomer.LegacySaveAndMultipleOrders", RegularTests::Flags)
bool FRegularLegacyMultipleDrinks::RunTest(const FString&)
{
    const FString Slot = RegularTests::NewSlot();
    ON_SCOPE_EXIT { UGameplayStatics::DeleteGameInSlot(Slot, 0); };
    auto* Old = NewObject<URegularCustomerSaveGame>();
    auto& Before = Old->Customers.Add(TEXT("Regular"));
    Before.CompletedVisits = 2; Before.LastVisitId = FGuid::NewGuid(); Before.LastTopic = TEXT("City");
    TestTrue(TEXT("Legacy-shaped save written"), UGameplayStatics::SaveGameToSlot(Old, Slot, 0));
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    auto* M = NewObject<URegularCustomerMemorySubsystem>(GI.Get());
    if (!TestTrue(TEXT("Existing v1 save remains readable"), M->LoadProfile(Slot))) return false;
    const FGuid Visit = FGuid::NewGuid(), First = FGuid::NewGuid(), Second = FGuid::NewGuid();
    M->RememberCompleted(TEXT("Regular"), Visit, TEXT("Latte"), TEXT("冰拿铁"), NAME_None, .5, 1, false, First);
    M->RememberCompleted(TEXT("Regular"), Visit, TEXT("OrangeCoffee"), TEXT("橙暮黑咖"), NAME_None, .3, 1, false, Second);
    M->RememberCompleted(TEXT("Regular"), Visit, TEXT("OrangeCoffee"), TEXT("橙暮黑咖"), NAME_None, .3, 1, false, Second);
    TestEqual(TEXT("Legacy drinks plus two new drinks"), M->Recall(TEXT("Regular")).CompletedDrinks, 4);
    TestEqual(TEXT("Only one additional visit"), M->Recall(TEXT("Regular")).CompletedVisits, 3);
    TestEqual(TEXT("Quiet followup does not erase previous topic"), M->Recall(TEXT("Regular")).LastTopic, FName(TEXT("City")));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularLinkProvenance, "CyberCafe.RegularCustomer.LinkProvenanceAndDetailsPointer", RegularTests::Flags)
bool FRegularLinkProvenance::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F; if (!F.Setup(*this)) return false;
    auto* E = F.Encounter;
    auto* W = Cast<URegularCustomerWidget>(E->Panel->GetUserWidgetObject());
    if (!TestNotNull(TEXT("Actual status widget"), W)) return false;
    const TSharedRef<SWidget> Slate = W->TakeWidget();
    TestTrue(TEXT("Offline authored greeting is gray local mode"), E->GetLinkView().Source == ERegularDialogueSource::Local && !E->GetLinkView().bWaiting);
    auto Wire = MakeShared<RegularDialogueTests::FTransport>();
    double Time = 0;
    E->DialogueClient->UseTestTransport(Wire, [&Time] { return Time; });
    E->Choose(0);
    TestTrue(TEXT("Pending topic is waiting, not AI text"), E->GetLinkView().bWaiting && E->GetLinkView().bPlaceholder && E->GetLinkView().Source != ERegularDialogueSource::AI);
    TestTrue(TEXT("Blue pending badge"), W->GetLinkLabel().Contains(TEXT("正在生成")) && URegularCustomerWidget::LinkColor(E->GetLinkView()).B > .9f);
    Time = .75;
    Wire->Reply(0, 200, RegularDialogueTests::Response(RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[0])));
    TestTrue(TEXT("Applied validated reply is cyan AI text"), E->GetLinkView().Source == ERegularDialogueSource::AI && !E->GetLinkView().bWaiting && W->GetLinkLabel().Contains(TEXT("AI 台词")));
    E->Choose(0); Time = 1.25;
    Wire->Reply(1, 429, TEXT("provider private body"));
    TestTrue(TEXT("API failure marks the actual fallback line orange"), E->GetLinkView().Source == ERegularDialogueSource::Fallback && E->GetLinkView().CurrentReason == TEXT("http_429") && W->GetLinkLabel().Contains(TEXT("已回退")));
    E->Present(TEXT("谢谢，先让我尝一口。"));
    TestTrue(TEXT("A deterministic line is local despite a previous API error"), E->GetLinkView().Source == ERegularDialogueSource::Local && E->GetLinkView().CurrentReason == TEXT("authored_line"));
    const FString LocalLine = E->GetLine().ToString();
    Time = 63;
    E->Present(TEXT("看着杯子慢慢调好，也让我放松下来。"), TEXT("Cup"));
    E->OrderDisplay->OnMenuInteractionNative.Broadcast(E->OrderDisplay);
    Wire->Reply(2, 200, RegularDialogueTests::Response(RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[2])));
    TestTrue(TEXT("Queued success does not relabel the still-visible local line"), E->GetLine().ToString() == LocalLine && E->GetLinkView().Source == ERegularDialogueSource::Local && E->GetLinkView().bReadyForReading);
    TestTrue(TEXT("Queued label states reply ready, not still generating"), W->GetLinkLabel().Contains(TEXT("回复就绪")));
    Time += 9; E->Tick(.02f);
    TestTrue(TEXT("Expired queued reply leaves current provenance intact"), E->GetLine().ToString() == LocalLine && E->GetLinkView().Source == ERegularDialogueSource::Local && !E->GetLinkView().bWaiting);
    E->Present(TEXT("夜班结束了，我想再坐一会儿。"), TEXT("Conversation"));
    E->Present(TEXT("我们慢慢聊，不用着急。"));
    Wire->Reply(3, 200, RegularDialogueTests::Response(RegularDialogueTests::AuthoredFromRequest(Wire->Bodies[3])));
    TestTrue(TEXT("Late reply cannot switch a replacement local line to AI"), E->GetLinkView().Source == ERegularDialogueSource::Local && E->GetLine().ToString() == TEXT("我们慢慢聊，不用着急。"));
    const FString Visible = W->GetVisibleText();
    const auto Stage = E->GetStage();
    const FTransform Placement = E->GetActorTransform();
    E->UpdatePanelFacing(E->Panel->GetComponentLocation() + FVector(80, 40, 30));
    const FVector TopLeft = E->Panel->GetComponentTransform().TransformPosition(FVector(0, 480, 280));
    const FIntRect Button = URegularCustomerWidget::LinkToggleRect();
    const FVector ButtonPoint = E->Panel->GetComponentTransform().TransformPosition(FVector(0,
        480 - (Button.Min.X + Button.Max.X) * .5, 280 - (Button.Min.Y + Button.Max.Y) * .5));
    const FVector Normal = E->Panel->GetForwardVector();
    FMenuPointerHit Hit;
    TestTrue(TEXT("Real VR ray finds the details toggle"), E->TracePointer(ButtonPoint + Normal * 80, -Normal, 250, Hit) && Hit.Action == URegularCustomerWidget::LinkToggleAction);
    ++GFrameCounter;
    TestTrue(TEXT("Trigger expands diagnostic drawer"), E->ActivatePointer(EControllerHand::Left, Hit) && E->AreLinkDetailsExpanded());
    TestTrue(TEXT("Toggle preserves dialogue state, typing and placement"), E->GetStage() == Stage && W->IsTyping() && W->GetVisibleText() == Visible && E->GetActorTransform().Equals(Placement, .00001));
    TestTrue(TEXT("Expanded drawer keeps the main dialogue anchored"), E->Panel->GetDrawSize().Y == 800 && E->Panel->GetComponentTransform().TransformPosition(FVector(0, 480, 280)).Equals(TopLeft, .00001));
    const FVector DrawerPoint = E->Panel->GetComponentTransform().TransformPosition(FVector(0, 380, -420));
    FMenuPointerHit DrawerHit;
    TestTrue(TEXT("Expanded panel ray bounds include the drawer"), E->TracePointer(DrawerPoint + Normal * 80, -Normal, 250, DrawerHit) && DrawerHit.bPanel);
    const FString Details = W->GetLinkDetails();
    TestTrue(TEXT("Details contain configured model and live counters"), Details.Contains(TEXT("模型：")) && Details.Contains(TEXT("最近60秒")) && Details.Contains(TEXT("会话请求")));
    TestFalse(TEXT("Details never contain credential or raw response text"), Details.Contains(TEXT("unit-test-key")) || Details.Contains(TEXT("provider private body")));
    ++GFrameCounter;
    TestTrue(TEXT("Other hand can collapse the same drawer"), E->ActivatePointer(EControllerHand::Right, Hit) && !E->AreLinkDetailsExpanded() && E->Panel->GetDrawSize().Y == 560);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularAlternateCupReorder, "CyberCafe.RegularCustomer.ReorderFromAlternateCounterCup", RegularTests::Flags)
bool FRegularAlternateCupReorder::RunTest(const FString&)
{
    RegularTests::FEncounterWorld F; if (!F.Setup(*this)) return false;
    auto* Backup = F.Cup;
    const FTransform BackupPose = Backup->GetActorTransform();
    const double BackupML = Backup->GetPreciseVolumeML();
    ACupActor* Alternate = nullptr;
    for (TActorIterator<ACupActor> It(F.World); It; ++It)
        if (It->GetName() == TEXT("BP_MenuCup_Highball_C_0") && *It != Backup) Alternate = *It;
    if (!TestNotNull(TEXT("Player's alternate counter highball from the saved map"), Alternate)) return false;
    const FTransform ReturnPose = Alternate->GetActorTransform();
    F.Cup = Alternate;
    F.Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    F.Encounter->Choose(1);
    if (!F.Serve(*this)) return false;
    for (int32 I = 0; I < 1200 && F.Encounter->GetStage() == ERegularCustomerStage::Tasting; ++I) F.Tick();
    if (!TestTrue(TEXT("Alternate cup actually tasted successfully"), F.Encounter->GetStage() == ERegularCustomerStage::Completed)) return false;
    for (int32 Drink = 2; Drink <= 3; ++Drink)
    {
        for (int32 I = 0; I < 5 && F.Encounter->GetStage() != ERegularCustomerStage::ReorderOffer; ++I) F.Encounter->Choose(0);
        TSet<ACupActor*> Before;
        for (TActorIterator<ACupActor> It(F.World); It; ++It) Before.Add(*It);
        const auto* Old = F.Cup;
        F.Encounter->Choose(0);
        if (!TestTrue(TEXT("Approving next drink from an alternate cup starts preparing, not an endless cup wait"),
            F.Encounter->GetStage() == ERegularCustomerStage::Preparing))
        { AddInfo(F.Encounter->GetLine().ToString()); return false; }
        ACupActor* Fresh = nullptr;
        for (TActorIterator<ACupActor> It(F.World); It; ++It) if (!Before.Contains(*It) && It->GetClass() == Backup->GetClass()) Fresh = *It;
        if (!TestNotNull(TEXT("A new clean copy of the actual supplied cup exists"), Fresh)) return false;
        TestTrue(TEXT("Refill returns to that cup's original counter position and scale"), Fresh->GetActorTransform().Equals(ReturnPose, .01));
        TestTrue(TEXT("Only the completed cup retired; unused prototype preserved"), Old->IsActorBeingDestroyed() && IsValid(Backup) && Backup->GetActorTransform().Equals(BackupPose, .01) && Backup->GetPreciseVolumeML() == BackupML);
        F.Cup = Fresh; F.Headless(); F.Cup->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        if (!F.Serve(*this, F.Encounter->Recipe)) return false;
        for (int32 I = 0; I < 1200 && F.Encounter->GetStage() == ERegularCustomerStage::Tasting; ++I) F.Tick();
        if (!TestTrue(TEXT("Alternate supply chain supports real followup tasting"), F.Encounter->GetStage() == ERegularCustomerStage::Completed)) return false;
        TestEqual(TEXT("Correct total after alternate-cup refill"), F.Memory()->Recall(F.Encounter->CustomerId).CompletedDrinks, Drink);
        TestEqual(TEXT("Still the same visit"), F.Memory()->Recall(F.Encounter->CustomerId).CompletedVisits, 1);
    }
    return !HasAnyErrors();
}
#endif
