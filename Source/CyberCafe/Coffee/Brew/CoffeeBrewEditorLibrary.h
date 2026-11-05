#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoffeeBrewEditorLibrary.generated.h"
class UWorld;class AHandDripperActor;class AHandPourKettleActor;class ACupActor;
UCLASS()
class CYBERCAFE_API UCoffeeBrewEditorLibrary:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
#if WITH_EDITOR
 UFUNCTION(BlueprintCallable) static bool InstallPractice(UWorld* World,TSubclassOf<AHandDripperActor> DripperClass,TSubclassOf<AHandPourKettleActor> KettleClass,TSubclassOf<ACupActor> CupClass);
#endif
};
