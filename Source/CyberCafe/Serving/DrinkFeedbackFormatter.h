#pragma once
#include "CoreMinimal.h"
#include "Serving/DrinkRecipe.h"

/** Text is a presentation policy, never an input to numeric acceptance. */
struct CYBERCAFE_API FDrinkFeedbackFormatter
{
    static FText Format(const UDrinkRecipe& Recipe, const FDrinkValidationResult& Result);
};
