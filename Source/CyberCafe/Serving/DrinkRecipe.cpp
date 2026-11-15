// @trace RC-02
#include "Serving/DrinkRecipe.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "Serving/DrinkFeedbackFormatter.h"

UDrinkRecipe::UDrinkRecipe()
{
    DisplayName=FText::FromString(TEXT("拿铁"));
    AcceptedCupClass=ACupActor::StaticClass();
    FDrinkIngredientRule Coffee;Coffee.IngredientId=TEXT("Coffee");
    FDrinkIngredientRule Milk;Milk.IngredientId=TEXT("Milk");
    Rules={Coffee,Milk};
}
FDrinkValidationResult UDrinkRecipe::Evaluate(ALiquidContainerActor* Cup) const
{
    FDrinkPreparationSnapshot Snapshot;
    Snapshot.bAvailable=IsValid(Cup);
    if(Snapshot.bAvailable)
    {
        Snapshot.bBroken=Cup->IsBroken();
        Snapshot.bMatchesCupClass=!AcceptedCupClass||Cup->IsA(AcceptedCupClass);
        Snapshot.TotalML=Cup->GetPreciseVolumeML();
        Snapshot.Composition=Cup->GetComposition();
        Snapshot.LiquidRevision=Cup->GetLiquidRevision();
        if(const UCupIceComponent* Ice=Cup->FindComponentByClass<UCupIceComponent>())
        {Snapshot.IceCount=Ice->GetIceCount();Snapshot.IceRevision=Ice->GetPreparationRevision();}
    }
    FDrinkValidationResult Result=EvaluateSnapshot(Snapshot);
    // Legacy display field; no acceptance rule depends on a named ingredient.
    Result.CoffeeFraction=Result.TotalML>0?Result.Composition.Get(TEXT("Coffee"))/Result.TotalML:0;
    Result.Feedback=FDrinkFeedbackFormatter::Format(*this,Result);
    return Result;
}
