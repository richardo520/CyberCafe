#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IceEditorLibrary.generated.h"
class UWorld;class AIceStationActor;class AIceBucketActor;class AIceScoopActor;class AIceCubeActor;class ACupActor;
UCLASS()
class CYBERCAFE_API UIceEditorLibrary:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
#if WITH_EDITOR
 UFUNCTION(BlueprintCallable) static void UseLegacyFbxImport();
 UFUNCTION(BlueprintCallable) static void RefreshStation(AIceStationActor* Station);
 UFUNCTION(BlueprintCallable) static AIceStationActor* InstallStation(UWorld* World,TSubclassOf<AIceBucketActor> BucketClass,TSubclassOf<AIceScoopActor> ScoopClass,TSubclassOf<ACupActor> CupClass);
#endif
};
