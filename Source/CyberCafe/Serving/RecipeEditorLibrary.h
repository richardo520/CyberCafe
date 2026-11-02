#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RecipeEditorLibrary.generated.h"
class UWorld;
class ABottleActor;

/** Editor-only assembly helper; never executes at runtime. */
UCLASS()
class URecipeEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable,Category="Recipe|Editor")
    static ABottleActor* AddMilkBottleToWorld(UWorld* World, TSubclassOf<ABottleActor> BottleClass, FVector Location);
#endif
};
