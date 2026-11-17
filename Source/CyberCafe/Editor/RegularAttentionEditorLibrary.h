#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RegularAttentionEditorLibrary.generated.h"
class UAnimInstance;
class USkeletalMeshComponent;
UCLASS()
class CYBERCAFE_API URegularAttentionEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable, Category="Regular Customer|Editor", meta=(DevelopmentOnly))
    static TSubclassOf<UAnimInstance> CreateHeadAttentionLayer(USkeletalMeshComponent* Mesh);
#endif
};
