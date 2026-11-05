#include "Coffee/Brew/CoffeeBrewEditorLibrary.h"
#if WITH_EDITOR
#include "Coffee/Brew/HandDripperActor.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Liquid/CupActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
bool UCoffeeBrewEditorLibrary::InstallPractice(UWorld* World,TSubclassOf<AHandDripperActor> DripperClass,TSubclassOf<AHandPourKettleActor> KettleClass,TSubclassOf<ACupActor> CupClass)
{
 if(!World||World->IsGameWorld()||!World->PersistentLevel||!DripperClass||!KettleClass||!CupClass)return false;
 for(const auto& A:World->PersistentLevel->Actors)if(A&&A->GetFName()==TEXT("HandDripper_Practice"))return true;
 FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 P.Name=TEXT("HandDripper_Practice");auto* Filter=World->SpawnActor<AHandDripperActor>(DripperClass,FVector(-151,94,107),FRotator::ZeroRotator,P);
 P.Name=TEXT("HandKettle_Practice");auto* Kettle=World->SpawnActor<AHandPourKettleActor>(KettleClass,FVector(-180,112,92),FRotator(0,90,0),P);
 P.Name=TEXT("HandCoffeeCup_Practice");auto* Cup=World->SpawnActor<ACupActor>(CupClass,FVector(-151,94,97.12),FRotator::ZeroRotator,P);
 if(!Filter||!Kettle||!Cup)return false;
 Filter->SetActorLabel(TEXT("Hand dripper - grounds bloom brew"));Kettle->SetActorLabel(TEXT("Hand pour kettle - Water 500mL"));Cup->SetActorLabel(TEXT("Hand brewed coffee receiver - separate practice"));
 P.Name=TEXT("HandDripperStand_Practice");auto* Stand=World->SpawnActor<AActor>(FVector(-151,94,92),FRotator::ZeroRotator,P);
 auto* Root=NewObject<USceneComponent>(Stand,TEXT("StandRoot"));Stand->SetRootComponent(Root);Stand->AddInstanceComponent(Root);Root->RegisterComponent();Root->SetWorldLocation(FVector(-151,94,92));
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 const FVector Positions[]={FVector(-5.2,0,7.2),FVector(5.2,0,7.2),FVector(-5.2,0,14.5),FVector(5.2,0,14.5)};
 const FVector Scales[]={FVector(.01,.07,.144),FVector(.01,.07,.144),FVector(.015,.10,.01),FVector(.015,.10,.01)};
 for(int I=0;I<4;++I){auto* M=NewObject<UStaticMeshComponent>(Stand);M->SetStaticMesh(Cube);M->SetupAttachment(Root);M->SetRelativeLocation(Positions[I]);M->SetRelativeScale3D(Scales[I]);M->SetCollisionEnabled(I==2?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);Stand->AddInstanceComponent(M);M->RegisterComponent();}
 Stand->SetActorLabel(TEXT("Hand dripper stand - simple prototype"));Stand->MarkPackageDirty();World->MarkPackageDirty();return true;
}
#endif
