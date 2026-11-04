#include "Liquid/IceEditorLibrary.h"
#if WITH_EDITOR
#include "Liquid/IceInteraction.h"
#include "Liquid/CupActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "HAL/IConsoleManager.h"
void UIceEditorLibrary::UseLegacyFbxImport()
{
 if(IConsoleVariable* C=IConsoleManager::Get().FindConsoleVariable(TEXT("Interchange.FeatureFlags.Import.FBX")))C->Set(0,ECVF_SetByCode);
}
void UIceEditorLibrary::RefreshStation(AIceStationActor* Station)
{
 if(!Station||Station->GetWorld()->IsGameWorld())return;
 if(Station->Bucket)Station->Bucket->OnConstruction(Station->Bucket->GetActorTransform());
 if(Station->Scoop)Station->Scoop->SetActorLocation(FVector(93,72,95.35));
 Station->MarkPackageDirty();Station->GetWorld()->MarkPackageDirty();
}
AIceStationActor* UIceEditorLibrary::InstallStation(UWorld* World,TSubclassOf<AIceBucketActor> BucketClass,TSubclassOf<AIceScoopActor> ScoopClass,TSubclassOf<ACupActor> CupClass)
{
 if(!World||World->IsGameWorld()||!BucketClass||!ScoopClass||!CupClass||!World->PersistentLevel)return nullptr;
 for(const auto& A:World->PersistentLevel->Actors)if(A&&A->IsA<AIceStationActor>())return nullptr;
 FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 P.Name=TEXT("IceBucket_Practice");auto* Bucket=World->SpawnActor<AIceBucketActor>(BucketClass,FVector(65,94,92),FRotator::ZeroRotator,P);
 P.Name=TEXT("IceScoop_Practice");auto* Scoop=World->SpawnActor<AIceScoopActor>(ScoopClass,FVector(93,72,95),FRotator(0,160,0),P);
 P.Name=TEXT("IceCup_Practice");auto* Cup=World->SpawnActor<ACupActor>(CupClass,FVector(42,101,97.12),FRotator::ZeroRotator,P);
 P.Name=TEXT("IceStation_Practice");auto* Station=World->SpawnActor<AIceStationActor>(AIceStationActor::StaticClass(),FVector(65,94,92),FRotator::ZeroRotator,P);
 if(!Bucket||!Scoop||!Cup||!Station)return nullptr;
 Bucket->SetActorLabel(TEXT("Ice bucket - unlimited practice"));Scoop->SetActorLabel(TEXT("Ice scoop - 3 cubes"));Cup->SetActorLabel(TEXT("Ice drink practice cup - existing Cup03"));Station->SetActorLabel(TEXT("Ice practice station"));
 Station->Bucket=Bucket;Station->Scoop=Scoop;Station->Cup=Cup;Station->CupInteriorDepthCM=8.8f;Station->CupInteriorRadiusCM=4.1f;Scoop->Bucket=Bucket;
 Bucket->MarkPackageDirty();Scoop->MarkPackageDirty();Cup->MarkPackageDirty();Station->MarkPackageDirty();World->MarkPackageDirty();return Station;
}
#endif
