// @trace RC-01 RC-02 RC-03 RC-04 RC-06
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "Liquid/BottleActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/ServingSpotActor.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/Steps/TutorialStep_WaitForServe.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecipeSavedAssetsTest,"CyberCafe.Recipe.SavedMapIdentityPacketAndTeaching",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRecipeSavedAssetsTest::RunTest(const FString&)
{
    UWorld* Map=LoadObject<UWorld>(nullptr,TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe"));
    if(!TestNotNull(TEXT("Saved main map"),Map))return false;
    ABottleActor* MapCoffee=FindObject<ABottleActor>(nullptr,TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_Bottle01_C_1"));
    ABottleActor* MapMilk=FindObject<ABottleActor>(nullptr,TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_Bottle04_Milk_Tutorial"));
    ATutorialDirector* MapDirector=FindObject<ATutorialDirector>(nullptr,TEXT("/Game/VRTemplate/Maps/L_Cafe.L_Cafe:PersistentLevel.BP_TutorialDirecter_C_0"));
    if(!TestNotNull(TEXT("Saved coffee identity"),MapCoffee)||!TestNotNull(TEXT("Saved milk bottle"),MapMilk)||!TestNotNull(TEXT("Original director retained"),MapDirector))return false;
    if(!TestEqual(TEXT("Six original steps"),MapDirector->Steps.Num(),6))return false;
    UTutorialStep_WaitForServe* MapStep=Cast<UTutorialStep_WaitForServe>(MapDirector->Steps[4]);
    if(!TestNotNull(TEXT("Order step"),MapStep)||!TestNotNull(TEXT("Saved recipe reference"),MapStep->RequiredRecipe.Get()))return false;
    TestEqual(TEXT("Coffee is explicit, not material inference"),MapCoffee->InitialIngredientId,FName(TEXT("Coffee")));
    TestEqual(TEXT("Milk is explicit"),MapMilk->InitialIngredientId,FName(TEXT("Milk")));
    TestFalse(TEXT("No premature serving before order step"),MapStep->RequiredSpot->bAcceptSubmissions);
    UWorld::InitializationValues IV;
    IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::SM5,&IV);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    UGameInstance* GI=NewObject<UGameInstance>();W->SetGameInstance(GI);GI->Init();
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABottleActor* Coffee=W->SpawnActor<ABottleActor>(MapCoffee->GetClass(),FVector(100,0,0),FRotator::ZeroRotator,Params);
    ABottleActor* Milk=W->SpawnActor<ABottleActor>(MapMilk->GetClass(),FVector(200,0,0),FRotator::ZeroRotator,Params);
    Coffee->InitialIngredientId=MapCoffee->InitialIngredientId;Coffee->bStartCapped=false;
    Milk->InitialIngredientId=MapMilk->InitialIngredientId;Milk->bStartCapped=false;
    Coffee->bCanPour=false;Milk->bCanPour=false;
    UClass* CupClass=LoadClass<ACupActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup03.BP_Cup03_C"));
    ACupActor* Cup=W->SpawnActor<ACupActor>(CupClass,FVector::ZeroVector,FRotator::ZeroRotator,Params);Cup->bCanPour=false;
    for(ALiquidContainerActor* A:{static_cast<ALiquidContainerActor*>(Coffee),static_cast<ALiquidContainerActor*>(Milk),static_cast<ALiquidContainerActor*>(Cup)}) A->ContainerMesh->SetSimulatePhysics(false);
    AServingSpotActor* Spot=W->SpawnActor<AServingSpotActor>();Spot->bAcceptSubmissions=false;
    ATutorialDirector* Director=W->SpawnActor<ATutorialDirector>();Director->bAutoStart=false;
    W->InitializeActorsForPlay(FURL());W->GetWorldSettings()->NotifyBeginPlay();
    UTutorialStep_WaitForServe* Step=NewObject<UTutorialStep_WaitForServe>(Director);Step->RequiredSpot=Spot;Step->RequiredRecipe=MapStep->RequiredRecipe;Step->EnterStep(Director);
    ULiquidTransferSubsystem* S=W->GetSubsystem<ULiquidTransferSubsystem>();
    const FVector Mouth=Cup->PourEntryPoint->GetComponentLocation();
    Spot->SetActorLocation(Cup->GetActorLocation());
    const double CoffeeBefore=Coffee->GetPreciseVolumeML(),MilkBefore=Milk->GetPreciseVolumeML();
    S->EmitVolume(Coffee,100,Mouth+FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);S->Simulate(.3);
    TestTrue(TEXT("Coffee packet carries explicit 100 mL"),FMath::Abs(Cup->GetIngredientVolumeML(TEXT("Coffee"))-100)<.00001);
    TestFalse(TEXT("Only coffee does not complete serving"),Spot->TrySubmitCup(Cup));
    TestFalse(TEXT("Only coffee does not complete teaching"),Step->IsCompleted());
    TestNull(TEXT("Rejected cup not sent to consume step"),Director->LastServedCup.Get());
    S->EmitVolume(Milk,100,Mouth+FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);S->Simulate(.3);
    TestTrue(TEXT("Milk packet carries explicit 100 mL"),FMath::Abs(Cup->GetIngredientVolumeML(TEXT("Milk"))-100)<.00001);
    TestTrue(TEXT("Real saved recipe accepts mixed cup"),Spot->TrySubmitCup(Cup));
    TestTrue(TEXT("Success completes teaching"),Step->IsCompleted());
    TestEqual(TEXT("Success shares correct cup"),Director->LastServedCup.Get(),static_cast<AActor*>(Cup));
    TestTrue(TEXT("All native volume entries remain balanced"),FMath::Abs(S->GetBalance().ErrorML)<.00001);
    TestTrue(TEXT("Correct cup frozen after accepted"),Cup->IsLiquidFrozen());
    const FString JSON=FString::Printf(TEXT("{\"coffeeDebitedML\":%.8f,\"milkDebitedML\":%.8f,\"coffeeInCupML\":%.8f,\"milkInCupML\":%.8f,\"cupML\":%.8f,\"accepted\":%s,\"teachingCompleted\":%s,\"errorML\":%.12f,\"headless\":true}"),CoffeeBefore-Coffee->GetPreciseVolumeML(),MilkBefore-Milk->GetPreciseVolumeML(),Cup->GetIngredientVolumeML(TEXT("Coffee")),Cup->GetIngredientVolumeML(TEXT("Milk")),Cup->GetPreciseVolumeML(),Spot->GetLastValidation().bAccepted?TEXT("true"):TEXT("false"),Step->IsCompleted()?TEXT("true"):TEXT("false"),S->GetBalance().ErrorML);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("RecipeAcceptance");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("saved_world.json")));
    Step->ExitStep();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);
    return true;
}
#endif
