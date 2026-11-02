#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NarrativeEditorLibrary.generated.h"

/** Asset-authoring helpers only. No editor dependencies or functions in game targets. */
UCLASS()
class CYBERCAFE_API UNarrativeEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable, Category = "Narrative|Editor", meta = (DevelopmentOnly))
    static int32 RepairWidgetIdentity(UObject* WidgetBlueprintAsset);
#endif
};
