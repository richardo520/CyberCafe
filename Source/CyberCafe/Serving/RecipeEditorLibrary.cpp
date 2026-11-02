#include "Serving/RecipeEditorLibrary.h"
#if WITH_EDITOR
#include "Liquid/BottleActor.h"
#include "Engine/World.h"
ABottleActor* URecipeEditorLibrary::AddMilkBottleToWorld(UWorld* World,TSubclassOf<ABottleActor> BottleClass,FVector Location)
{
    if(!World||World->IsGameWorld()||!BottleClass)return nullptr;
    FActorSpawnParameters Params;Params.Name=TEXT("BP_Bottle04_Milk_Tutorial");Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABottleActor* A=World->SpawnActor<ABottleActor>(BottleClass,Location,FRotator::ZeroRotator,Params);
    if(A){A->SetActorLabel(TEXT("Tutorial Milk Bottle"));A->MarkPackageDirty();World->MarkPackageDirty();}
    return A;
}
#endif
