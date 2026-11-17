#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RegularCustomerPreviewLibrary.generated.h"

/** Developer-only rendered UI preview, never invoked by the game. */
UCLASS()
class CYBERCAFE_API URegularCustomerPreviewLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable, Category="Regular Customer|Editor", meta=(DevelopmentOnly))
    static bool RenderChoicePreview();
#endif
};
