// @trace RC-01
#pragma once
#include "CoreMinimal.h"
#include "LiquidComposition.generated.h"

/** Actual mL per ingredient. Rendering/material names never determine identity. */
USTRUCT(BlueprintType)
struct CYBERCAFE_API FLiquidComposition
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Liquid|Composition")
    TMap<FName,double> Ingredients;
    double Total() const {double S=0;for(const auto& P:Ingredients)S+=P.Value;return S;}
    double Get(FName Id) const {const double* V=Ingredients.Find(Id);return V?*V:0.0;}
    static FLiquidComposition Pure(FName Id,double ML)
    {
        FLiquidComposition R;
        if(FMath::IsFinite(ML)&&ML>0) R.Ingredients.Add(Id.IsNone()?FName(TEXT("Unknown")):Id,ML);
        return R;
    }
    FLiquidComposition Portion(double ML) const
    {
        FLiquidComposition R;const double Sum=Total();
        if(Sum<=0||!FMath::IsFinite(ML)||ML<=0)return R;
        const double Take=FMath::Min(ML,Sum);
        if(Take==Sum)return *this;
        TArray<FName> Keys;Ingredients.GetKeys(Keys);Keys.Sort(FNameLexicalLess());
        double Remaining=Take;
        for(int32 I=0;I<Keys.Num();++I)
        {
            const double V=I==Keys.Num()-1?Remaining:FMath::Min(Remaining,Get(Keys[I])*Take/Sum);
            if(V>0)R.Ingredients.Add(Keys[I],V);
            Remaining-=V;
        }
        return R;
    }
    void Add(const FLiquidComposition& Other)
    {for(const auto& P:Other.Ingredients)if(P.Value>0&&FMath::IsFinite(P.Value))Ingredients.FindOrAdd(P.Key)+=P.Value;}
    void Remove(const FLiquidComposition& Other)
    {
        for(const auto& P:Other.Ingredients)
            if(double* V=Ingredients.Find(P.Key)){*V=FMath::Max(0.0,*V-P.Value);if(*V==0)Ingredients.Remove(P.Key);}
    }
};
