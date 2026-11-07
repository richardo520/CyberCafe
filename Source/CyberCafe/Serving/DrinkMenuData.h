// @trace MN-01. Display aliases never replace authority recipe/ingredient IDs.
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DrinkMenuData.generated.h"
class UDrinkRecipe;
class UTexture2D;
USTRUCT(BlueprintType)
struct FDrinkMenuIngredient
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName IngredientId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) double VolumeML=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UTexture2D> Icon;
};
USTRUCT(BlueprintType)
struct FDrinkMenuEntry
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName PinyinIndex;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Pinyin;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FText DisplayName;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FText Category;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FText CupName;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UTexture2D> CupIcon;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FText Description;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UDrinkRecipe> Recipe;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FDrinkMenuIngredient> Ingredients;
};
UCLASS(BlueprintType)
class CYBERCAFE_API UDrinkMenuData:public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FDrinkMenuEntry> Entries;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UTexture2D> IceIcon;
 UFUNCTION(BlueprintPure) bool IsValidMenu() const;
 UFUNCTION(BlueprintPure) int32 FindPinyinIndex(FName Index) const;
 static FText IngredientDisplayName(FName Id);
};
