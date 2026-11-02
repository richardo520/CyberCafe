// @trace LQ-01 LQ-02 LQ-03 LQ-04 LQ-05 LQ-06
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/LiquidConservationTestProbe.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/LiquidTransferMath.h"
#include "Liquid/CupActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "NiagaraSystem.h"
#include "Liquid/BottleActor.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
struct FTestLiquidWorld
{
    UWorld* World;
    FTestLiquidWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::SM5,&IV);
    }
    ~FTestLiquidWorld(){ World->DestroyWorld(false); }
    ACupActor* Cup(double ML,double Capacity,FVector Position=FVector::ZeroVector)
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ACupActor* A=World->SpawnActor<ACupActor>(Position,FRotator::ZeroRotator,Params);
        A->ContainerMesh->SetSimulatePhysics(false); A->ContainerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        A->LiquidFX->DeactivateImmediate(); A->PourFX->DeactivateImmediate();
        A->bCanPour=false; A->MaxVolumeML=static_cast<float>(Capacity); A->FillAmount=0;
        A->PourEntryPoint->SetRelativeLocation(FVector::ZeroVector); A->PourEntryRadius=3.f;
        A->InitializeLiquidState();
        A->AddLiquid(static_cast<float>(ML),FLinearColor::White);
        return A;
    }
    ULiquidTransferSubsystem* Transfers(){return World->GetSubsystem<ULiquidTransferSubsystem>();}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidVolumeTest,"CyberCafe.Liquid.AuthoritativeVolume",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidVolumeTest::RunTest(const FString&)
{
    FTestLiquidWorld T;
    ACupActor* A=T.Cup(10,100);
    TestEqual(TEXT("Actual consume"),A->ConsumeLiquid(9.7f),9.7f);
    TestTrue(TEXT("Sub half mL tail is retained"),FMath::Abs(A->GetPreciseVolumeML()-.3)<.00001);
    const double Before=A->GetPreciseVolumeML();
    const float Tail=A->ConsumeLiquid(1);
    TestTrue(TEXT("Returned tail equals actual debit"),FMath::Abs(Tail-Before)<.000001);
    TestEqual(TEXT("Empty is exactly zero"),A->GetPreciseVolumeML(),0.0);
    TestEqual(TEXT("Negative consume rejected"),A->ConsumeLiquid(-1),0.f);
    TestEqual(TEXT("Negative add rejected"),A->AddLiquid(-1,FLinearColor::Red),0.f);
    TestEqual(TEXT("Add is capacity capped"),A->AddLiquid(120,FLinearColor::Red),100.f);
    TestEqual(TEXT("Full rejects add"),A->AddLiquid(1,FLinearColor::White),0.f);
    A->FillAmount=.123f;
    TestEqual(TEXT("Presentation fill cannot rewrite authoritative volume"),A->GetPreciseVolumeML(),100.0);
    A->RefreshLiquidFX();
    TestEqual(TEXT("Fill refresh derives from mL"),A->FillAmount,1.f);
    TestTrue(TEXT("External API ledger closes"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<.000001);
    A->SetCapacityML(50);
    TestEqual(TEXT("Capacity reduction spills surplus"),A->GetPreciseVolumeML(),50.0);
    TestTrue(TEXT("Capacity reduction remains balanced"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<.000001);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidMouthMathTest,"CyberCafe.Liquid.MouthCrossing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidMouthMathTest::RunTest(const FString&)
{
    double Alpha=0;
    TestTrue(TEXT("Inward segment crosses mouth"),CyberCafeLiquid::CrossMouth(FVector(0,0,5),FVector(0,0,-5),FTransform::Identity,FTransform::Identity,3,Alpha));
    TestTrue(TEXT("Crossing time"),FMath::Abs(Alpha-.5)<1.e-8);
    TestFalse(TEXT("Below-cup segment never fills"),CyberCafeLiquid::CrossMouth(FVector(0,0,-5),FVector(0,0,-15),FTransform::Identity,FTransform::Identity,3,Alpha));
    TestFalse(TEXT("Outside lip rejects"),CyberCafeLiquid::CrossMouth(FVector(4,0,5),FVector(4,0,-5),FTransform::Identity,FTransform::Identity,3,Alpha));
    TestFalse(TEXT("Outward rejects"),CyberCafeLiquid::CrossMouth(FVector(0,0,-5),FVector(0,0,5),FTransform::Identity,FTransform::Identity,3,Alpha));
    TestTrue(TEXT("Translated cup"),CyberCafeLiquid::CrossMouth(FVector(10,0,5),FVector(10,0,-5),FTransform(FVector(10,0,0)),FTransform(FVector(10,0,0)),3,Alpha));
    TestFalse(TEXT("Cup moved clear"),CyberCafeLiquid::CrossMouth(FVector(0,0,5),FVector(0,0,-5),FTransform::Identity,FTransform(FVector(20,0,0)),3,Alpha));
    TestTrue(TEXT("Scaled mouth uses local radius"),CyberCafeLiquid::CrossMouth(FVector(5,0,5),FVector(5,0,-5),FTransform(FQuat::Identity,FVector::ZeroVector,FVector(2)),FTransform(FQuat::Identity,FVector::ZeroVector,FVector(2)),3,Alpha));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidPacketTest,"CyberCafe.Liquid.PacketSettlement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidPacketTest::RunTest(const FString&)
{
    FTestLiquidWorld T;
    ACupActor* Source=T.Cup(100,100,FVector(100,0,0));
    ACupActor* Target=T.Cup(94,100);
    ULiquidTransferSubsystem* S=T.Transfers();
    TestEqual(TEXT("Ten mL emitted"),S->EmitVolume(Source,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector),10.0);
    TestEqual(TEXT("Debit committed"),Source->GetPreciseVolumeML(),90.0);
    TestEqual(TEXT("In flight before mouth"),S->GetBalance().InFlightML,10.0);
    S->Simulate(.1);
    TestEqual(TEXT("Not yet arrived"),Target->GetPreciseVolumeML(),94.0);
    S->Simulate(.2);
    TestEqual(TEXT("Only remaining six accepted"),Target->GetPreciseVolumeML(),100.0);
    TestTrue(TEXT("Four overflow"),FMath::Abs(S->GetBalance().SpilledML-4)<1.e-8);
    TestEqual(TEXT("Settled once"),S->GetBalance().InFlightML,0.0);
    S->Simulate(.5);
    TestEqual(TEXT("No duplicate fill"),Target->GetPreciseVolumeML(),100.0);
    TestTrue(TEXT("Ledger closed"),FMath::Abs(S->GetBalance().ErrorML)<1.e-8);
    T.Cup(0,100,FVector(0,0,-20));
    S->EmitVolume(Source,5,FVector(0,0,-5),FVector(0,0,-50),FVector::ZeroVector);
    S->Simulate(.5);
    TestTrue(TEXT("First real downstream entry is usable"),FMath::Abs(S->GetBalance().ErrorML)<1.e-8);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidFrameRateTest,"CyberCafe.Liquid.FrameRateAndVisualIndependence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidFrameRateTest::RunTest(const FString&)
{
    TArray<FString> Rows;
    for(int FPS: {30,60,90,120})
    {
        FTestLiquidWorld T;
        ACupActor* Source=T.Cup(120,200,FVector(100,0,0)); Source->PourRatePerSecond=60; Source->PourInitialSpeedCms=0; Source->bCanPour=true;
        ACupActor* Target=T.Cup(0,200); Target->PourEntryRadius=8;
        for(int i=0;i<FPS;++i)
        {
            T.Transfers()->EmitPour(Source,1.0/FPS,FTransform(FRotator(0,0,90),FVector(0,0,20)));
            T.Transfers()->Simulate(1.0/FPS);
        }
        T.Transfers()->Simulate(1);
        const FLiquidBalance B=T.Transfers()->GetBalance();
        TestTrue(FString::Printf(TEXT("%d FPS emits exactly 60 mL"),FPS),FMath::Abs(Source->GetPreciseVolumeML()-60)<1.e-7);
        TestTrue(FString::Printf(TEXT("%d FPS balance"),FPS),FMath::Abs(B.ErrorML)<1.e-7);
        TestTrue(TEXT("No Niagara is required"),!Source->PourFX->GetAsset());
        TestTrue(TEXT("Sixty mL received independent of frame rate"),FMath::Abs(Target->GetPreciseVolumeML()-60)<1.e-7);
        Rows.Add(FString::Printf(TEXT("{\"fps\":%d,\"sourceML\":%.10f,\"targetML\":%.10f,\"spilledML\":%.10f,\"errorML\":%.12f}"),FPS,Source->GetPreciseVolumeML(),Target->GetPreciseVolumeML(),B.SpilledML,B.ErrorML));
    }
    const FString JSON=TEXT("{\"frameRates\":[")+FString::Join(Rows,TEXT(","))+TEXT("]}");
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("LiquidConservation");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("measurements.json")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidLifecycleTest,"CyberCafe.Liquid.ObstructionMovementAndLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidLifecycleTest::RunTest(const FString&)
{
    FTestLiquidWorld T;
    ULiquidTransferSubsystem* S=T.Transfers();
    ACupActor* Source=T.Cup(100,100,FVector(100,0,0));
    ACupActor* Near=T.Cup(0,100);
    ACupActor* Far=T.Cup(0,100,FVector(0,0,-10));
    S->EmitVolume(Source,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);
    S->Simulate(.3);
    TestEqual(TEXT("Nearest mouth wins"),Near->GetPreciseVolumeML(),10.0);
    TestEqual(TEXT("Downstream cup not double credited"),Far->GetPreciseVolumeML(),0.0);
    Near->SetActorLocation(FVector(20,0,0));Far->SetActorLocation(FVector(20,0,-10));
    S->EmitVolume(Source,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);
    S->Simulate(.5);S->Simulate(3);
    TestEqual(TEXT("Moved-away cup gets no liquid"),Near->GetPreciseVolumeML(),10.0);
    TestEqual(TEXT("Miss settles as spill"),S->GetBalance().SpilledML,10.0);
    Near->SetActorLocation(FVector::ZeroVector);S->Simulate(.01);
    AActor* Blocker=T.World->SpawnActor<AActor>();
    UBoxComponent* Box=NewObject<UBoxComponent>(Blocker);
    Blocker->SetRootComponent(Box);Box->SetBoxExtent(FVector(10,10,1));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->RegisterComponent();Blocker->SetActorLocation(FVector(0,0,5));
    FHitResult Hit;TestTrue(TEXT("Real world obstruction trace"),T.World->LineTraceSingleByChannel(Hit,FVector(0,0,10),FVector(0,0,0),ECC_Visibility));
    S->EmitVolume(Source,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);S->Simulate(.3);
    TestEqual(TEXT("Table blocks target credit"),Near->GetPreciseVolumeML(),10.0);
    TestEqual(TEXT("Table impact is accounted"),S->GetBalance().SpilledML,20.0);
    Blocker->Destroy();
    S->EmitVolume(Source,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);
    TestTrue(TEXT("Source destroy succeeds before BeginPlay"),Source->Destroy());
    TestTrue(TEXT("Source destruction closes remaining volume"),FMath::Abs(S->GetBalance().ErrorML)<1.e-7);
    S->Simulate(.3);
    TestEqual(TEXT("Tail survives deleted source"),Near->GetPreciseVolumeML(),20.0);
    TestTrue(TEXT("Lifecycle ledger balanced"),FMath::Abs(S->GetBalance().ErrorML)<1.e-7);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidCurveAndMixTest,"CyberCafe.Liquid.RibbonCurveAndMixSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidCurveAndMixTest::RunTest(const FString&)
{
    const FVector D=CyberCafeLiquid::RibbonDisplacement(.3,FVector::ForwardVector);
    TestTrue(TEXT("Actual Ribbon horizontal displacement"),FMath::Abs(D.X-6.2239151565)<1.e-6);
    TestTrue(TEXT("Actual Ribbon vertical displacement"),FMath::Abs(D.Z+9.4529238651)<1.e-6);
    double Alpha;
    TestFalse(TEXT("Rotating mouth does not interpolate a false local chord"),CyberCafeLiquid::CrossMouth(FVector(3.005,0,0),FVector(3.005,0,0),FTransform(FRotator(5,0,0)),FTransform(FRotator(-5,0,0)),3,Alpha));
    FTestLiquidWorld T;
    ACupActor* Source=T.Cup(100,100,FVector(100,0,0));Source->LiquidColor=FLinearColor::Red;
    ACupActor* Target=T.Cup(10,100);Target->LiquidColor=FLinearColor::Blue;
    T.Transfers()->EmitVolume(Source,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);
    Source->LiquidColor=FLinearColor::Green;
    T.Transfers()->Simulate(.3);
    TestEqual(TEXT("Volume-based mix total"),Target->GetPreciseVolumeML(),20.0);
    TestTrue(TEXT("Mix uses emitted color, not source's later color"),Target->LiquidColor.Equals(FLinearColor(.5,0,.5,1),.00001));
    ACupActor* Second=T.Cup(20,100,FVector(200,0,0));
    T.Transfers()->EmitVolume(Source,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);
    T.Transfers()->EmitVolume(Second,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector);
    T.Transfers()->Simulate(.3);
    TestEqual(TEXT("Two sources into one cup"),Target->GetPreciseVolumeML(),40.0);
    TArray<FBasicParticleData> Fake;Fake.SetNum(100);
    Source->ReceiveParticleData_Implementation(Fake,nullptr,FVector::ZeroVector);
    TestEqual(TEXT("Legacy particle callback cannot add any volume"),Target->GetPreciseVolumeML(),40.0);
    TestTrue(TEXT("Mix ledger balanced"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<1.e-7);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidBlueprintRuntimeTest,"CyberCafe.Liquid.ExistingBlueprintWorldTick",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidBlueprintRuntimeTest::RunTest(const FString&)
{
    FTestLiquidWorld T;
    UClass* BottleClass=LoadClass<ABottleActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Interaction/Bottle/Bottle04/BP_Bottle04_Milk.BP_Bottle04_Milk_C"));
    UClass* CupClass=LoadClass<ACupActor>(nullptr,TEXT("/Game/VRTemplate/Blueprints/Interaction/Cup/BP_Cup03.BP_Cup03_C"));
    if(!TestNotNull(TEXT("Existing milk BP loads"),BottleClass)||!TestNotNull(TEXT("Existing cup BP loads"),CupClass))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(T.World);
    UGameInstance* GI=NewObject<UGameInstance>();T.World->SetGameInstance(GI);GI->Init();
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABottleActor* Source=T.World->SpawnActor<ABottleActor>(BottleClass,FVector(0,0,100),FRotator(0,0,90),Params);
    ACupActor* Target=T.World->SpawnActor<ACupActor>(CupClass,FVector(100,0,0),FRotator::ZeroRotator,Params);
    Source->bStartCapped=false;Source->ContainerMesh->SetSimulatePhysics(false);Target->ContainerMesh->SetSimulatePhysics(false);Target->bCanPour=false;
    T.World->InitializeActorsForPlay(FURL());T.World->GetWorldSettings()->NotifyBeginPlay();
    const double Initial=Source->GetPreciseVolumeML();
    Source->Tick(0.f);
    const FTransform Pour=Source->PourFX->GetComponentTransform();
    const FVector DesiredMouth=Pour.GetLocation()+CyberCafeLiquid::RibbonDisplacement(.3,Pour.GetUnitAxis(EAxis::X));
    Target->SetActorLocation(Target->GetActorLocation()+DesiredMouth-Target->PourEntryPoint->GetComponentLocation());
    T.Transfers()->Simulate(.0001);
    TArray<FString> Timeline;
    auto Capture=[&](double Time)
    {
        const FLiquidBalance B=T.Transfers()->GetBalance();
        Timeline.Add(FString::Printf(TEXT("{\"time\":%.3f,\"sourceML\":%.8f,\"targetML\":%.8f,\"flightML\":%.8f,\"spillML\":%.8f,\"errorML\":%.12f}"),Time,Source->GetPreciseVolumeML(),Target->GetPreciseVolumeML(),B.InFlightML,B.SpilledML,B.ErrorML));
    };
    Capture(0);
    for(int I=0;I<100;++I){++GFrameCounter;T.World->Tick(LEVELTICK_All,.01f);if(I%5==4)Capture((I+1)*.01);}
    Source->bCanPour=false;
    for(int I=0;I<120;++I){++GFrameCounter;T.World->Tick(LEVELTICK_All,.01f);if(I%5==4)Capture(1+(I+1)*.01);}
    TestTrue(TEXT("Real actor Tick debits approximately 60 mL"),FMath::Abs((Initial-Source->GetPreciseVolumeML())-60)<.001);
    TestTrue(TEXT("Existing real cup receives milk through its mouth"),Target->GetPreciseVolumeML()>40);
    TestFalse(TEXT("Stop emitting after bCanPour disabled"),Source->bIsPouring);
    TestTrue(TEXT("Existing BP world ledger closes"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<.00001);
    TestEqual(TEXT("All visual-tail packets settled"),T.Transfers()->GetBalance().InFlightML,0.0);
    const FLiquidBalance B=T.Transfers()->GetBalance();
    const FString JSON=FString::Printf(TEXT("{\"sourceDebitedML\":%.8f,\"receivedML\":%.8f,\"spilledML\":%.8f,\"errorML\":%.12f,\"headless\":true}"),Initial-Source->GetPreciseVolumeML(),Target->GetPreciseVolumeML(),B.SpilledML,B.ErrorML);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("LiquidConservation");IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("blueprint_world.json")));
    FFileHelper::SaveStringToFile(TEXT("{\"samples\":[")+FString::Join(Timeline,TEXT(","))+TEXT("]}"),*(Dir/TEXT("timeline.json")));
    T.World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(T.World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidHitchTest,"CyberCafe.Liquid.LongFrameAndPartialTail",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidHitchTest::RunTest(const FString&)
{
    FTestLiquidWorld T;
    ACupActor* Source=T.Cup(120,200,FVector(100,0,0));Source->PourInitialSpeedCms=0;Source->bCanPour=true;Source->PourRatePerSecond=60;
    ACupActor* Target=T.Cup(0,200);
    for(double Dt:{.35,.01,.24,.4})
    {T.Transfers()->EmitPour(Source,Dt,FTransform(FVector(0,0,20)));T.Transfers()->Simulate(Dt);}
    T.Transfers()->Simulate(1);
    TestTrue(TEXT("Long frame retains full time-integrated debit"),FMath::Abs(Source->GetPreciseVolumeML()-60)<1.e-7);
    TestTrue(TEXT("Long frame accepts full volume"),FMath::Abs(Target->GetPreciseVolumeML()-60)<1.e-7);
    TestTrue(TEXT("Long frame ledger closes"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<1.e-7);
    ACupActor* Tiny=T.Cup(.25,100,FVector(100,0,0));
    TestEqual(TEXT("Packet capped by source tail"),T.Transfers()->EmitVolume(Tiny,10,FVector(0,0,10),FVector(0,0,-50),FVector::ZeroVector),.25);
    T.Transfers()->Simulate(.3);
    TestEqual(TEXT("Tail empties exactly"),Tiny->GetPreciseVolumeML(),0.0);
    TestTrue(TEXT("Tiny amount is credited, not discarded"),FMath::Abs(Target->GetPreciseVolumeML()-60.25)<1.e-7);
    TestTrue(TEXT("Tail ledger closes"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<1.e-7);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiquidSetupAndReentryTest,"CyberCafe.Liquid.InitialCapacityAndCallbackReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLiquidSetupAndReentryTest::RunTest(const FString&)
{
    FTestLiquidWorld T;
    ACupActor* Configured=T.World->SpawnActor<ACupActor>();
    Configured->ContainerMesh->SetSimulatePhysics(false);Configured->bCanPour=false;
    Configured->MaxVolumeML=750;Configured->FillAmount=.5f;
    Configured->SetCapacityML(1000);
    TestEqual(TEXT("Pre-BeginPlay capacity setup preserves initial fill"),Configured->GetPreciseVolumeML(),500.0);
    Configured->InitializeLiquidState();
    TestEqual(TEXT("Configured capacity is used when ledger initializes"),Configured->GetPreciseVolumeML(),500.0);
    ACupActor* Source=T.Cup(100,100,FVector(100,0,0));Source->bCanPour=true;Source->PourRatePerSecond=60;Source->PourInitialSpeedCms=0;
    ACupActor* Target=T.Cup(0,100);
    ULiquidConservationTestProbe* Probe=NewObject<ULiquidConservationTestProbe>(T.World);
    Probe->Source=Source;Probe->bStopOnFirstChange=true;
    Source->OnLiquidChanged.AddDynamic(Probe,&ULiquidConservationTestProbe::HandleChanged);
    T.Transfers()->EmitPour(Source,.1,FTransform(FVector(0,0,10)));
    TestEqual(TEXT("Actual dynamic callback only fires for first slice"),Probe->Notifications,1);
    TestTrue(TEXT("Stopping inside UI callback prevents later slices"),FMath::Abs(Source->GetPreciseVolumeML()-99.5)<1.e-8);
    TestTrue(TEXT("Observer never sees half a transaction"),Probe->bBalancedAtEveryChange);
    T.Transfers()->Simulate(1);
    TestTrue(TEXT("Already emitted half mL still arrives"),FMath::Abs(Target->GetPreciseVolumeML()-.5)<1.e-8);
    ACupActor* Deleted=T.Cup(10,100,FVector(100,0,0));Deleted->bCanPour=true;Deleted->PourRatePerSecond=60;Deleted->PourInitialSpeedCms=0;
    ULiquidConservationTestProbe* DestroyProbe=NewObject<ULiquidConservationTestProbe>(T.World);
    DestroyProbe->Source=Deleted;DestroyProbe->bDestroyOnFirstChange=true;
    Deleted->OnLiquidChanged.AddDynamic(DestroyProbe,&ULiquidConservationTestProbe::HandleChanged);
    T.Transfers()->EmitPour(Deleted,.1,FTransform(FVector(0,0,10)));
    TestEqual(TEXT("Destroy callback fires exactly once"),DestroyProbe->Notifications,1);
    TestTrue(TEXT("Destroying source inside delegate is balanced"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<.00001);
    T.Transfers()->Simulate(1);
    TestTrue(TEXT("Deleted source's tail still arrives"),FMath::Abs(Target->GetPreciseVolumeML()-1)<1.e-8);
    TestTrue(TEXT("Reentry ledger closes"),FMath::Abs(T.Transfers()->GetBalance().ErrorML)<.00001);
    return true;
}
#endif
