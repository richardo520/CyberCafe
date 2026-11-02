// @trace RC-02
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Liquid/LiquidComposition.h"
#include "DrinkRecipe.generated.h"
class ALiquidContainerActor;

UENUM(BlueprintType)
enum class EDrinkValidationReason : uint8
{Accepted,Empty,WrongIngredient,MissingIngredient,TooLittle,TooMuch,WrongRatio,WrongCup,InvalidRecipe,Unavailable};

USTRUCT(BlueprintType)
struct CYBERCAFE_API FDrinkIngredientRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Recipe") FName IngredientId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Recipe") double MinFraction=0.45;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Recipe") double MaxFraction=0.55;
};
USTRUCT(BlueprintType)
struct CYBERCAFE_API FDrinkValidationResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="Recipe") bool bAccepted=false;
    UPROPERTY(BlueprintReadOnly,Category="Recipe") EDrinkValidationReason Reason=EDrinkValidationReason::Unavailable;
    UPROPERTY(BlueprintReadOnly,Category="Recipe") FText Feedback;
    UPROPERTY(BlueprintReadOnly,Category="Recipe") double TotalML=0;
    UPROPERTY(BlueprintReadOnly,Category="Recipe") double CoffeeFraction=0;
    UPROPERTY(BlueprintReadOnly,Category="Recipe") FName RecipeId;
    UPROPERTY(BlueprintReadOnly,Category="Recipe") int32 LiquidRevision=0;
    UPROPERTY(BlueprintReadOnly,Category="Recipe") FLiquidComposition Composition;
};

/** Pure rule evaluation, independent of actors that detect the serving area. */
UCLASS(BlueprintType)
class CYBERCAFE_API UDrinkRecipe : public UDataAsset
{
    GENERATED_BODY()
public:
    UDrinkRecipe();
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recipe") FName RecipeId=TEXT("Latte");
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recipe") FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recipe") double TargetVolumeML=200;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recipe") double MinVolumeML=180;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recipe") double MaxVolumeML=220;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recipe") TArray<FDrinkIngredientRule> Rules;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Recipe") TSubclassOf<ALiquidContainerActor> AcceptedCupClass;
    UFUNCTION(BlueprintPure,Category="Recipe") FDrinkValidationResult Evaluate(ALiquidContainerActor* Cup) const;
};
