#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Customers/RegularCustomerMemory.h"
#include "Customers/RegularCustomerEncounter.h"
#include "Customers/CustomerDrinkComponent.h"
#include "Customers/CustomerOrderComponent.h"
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
#endif
