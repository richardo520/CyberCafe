#include "Serving/DrinkRecipe.h"

FDrinkValidationResult UDrinkRecipe::EvaluateSnapshot(const FDrinkPreparationSnapshot& S) const
{
    FDrinkValidationResult R;
    R.RecipeId=RecipeId;
    R.TotalML=S.TotalML;
    R.Composition=S.Composition;
    R.LiquidRevision=S.LiquidRevision;
    R.IceCount=S.IceCount;
    R.IceRevision=S.IceRevision;
    const auto Fail=[&](EDrinkValidationReason Reason,EDrinkValidationDetail Detail=EDrinkValidationDetail::None)
    { R.Reason=Reason;R.Detail=Detail;return R; };
    if(S.bAvailable&&S.bBroken)return Fail(EDrinkValidationReason::Unavailable,EDrinkValidationDetail::BrokenContainer);
    if(RecipeId.IsNone()||!FMath::IsFinite(TargetVolumeML)||!FMath::IsFinite(MinVolumeML)||!FMath::IsFinite(MaxVolumeML)||
        MinVolumeML<=0||MinVolumeML>TargetVolumeML||TargetVolumeML>MaxVolumeML||Rules.IsEmpty())
        return Fail(EDrinkValidationReason::InvalidRecipe,EDrinkValidationDetail::InvalidVolumeConfiguration);
    if(ExpectedIceCount<-1)return Fail(EDrinkValidationReason::InvalidRecipe,EDrinkValidationDetail::InvalidIceConfiguration);
    double MinSum=0,MaxSum=0;
    TSet<FName> Ids;
    for(const auto& Rule:Rules)
    {
        if(Rule.IngredientId.IsNone()||Rule.IngredientId==TEXT("Unknown")||Ids.Contains(Rule.IngredientId)||!FMath::IsFinite(Rule.MinFraction)||
            !FMath::IsFinite(Rule.MaxFraction)||Rule.MinFraction<0||Rule.MinFraction>Rule.MaxFraction||Rule.MaxFraction>1)
            return Fail(EDrinkValidationReason::InvalidRecipe,EDrinkValidationDetail::InvalidIngredientConfiguration);
        MinSum+=Rule.MinFraction;MaxSum+=Rule.MaxFraction;Ids.Add(Rule.IngredientId);
    }
    if(MinSum>1+1.e-9||MaxSum<1-1.e-9)return Fail(EDrinkValidationReason::InvalidRecipe,EDrinkValidationDetail::InvalidFractionSum);
    if(!S.bAvailable)return Fail(EDrinkValidationReason::Unavailable);
    if(!S.bMatchesCupClass)return Fail(EDrinkValidationReason::WrongCup);
    if(!FMath::IsFinite(R.TotalML)||R.TotalML<0)return Fail(EDrinkValidationReason::InvalidRecipe,EDrinkValidationDetail::InvalidComposition);
    if(R.TotalML<=1.e-9)return Fail(EDrinkValidationReason::Empty);
    if(FMath::Abs(R.Composition.Total()-R.TotalML)>1.e-6)return Fail(EDrinkValidationReason::InvalidRecipe,EDrinkValidationDetail::InconsistentComposition);
    for(const auto& P:R.Composition.Ingredients)
    {
        if(!FMath::IsFinite(P.Value)||P.Value<0)return Fail(EDrinkValidationReason::InvalidRecipe,EDrinkValidationDetail::InvalidComposition);
        if(P.Value>1.e-8&&!Ids.Contains(P.Key))
        {
            R.FailingIngredient=P.Key;
            return Fail(EDrinkValidationReason::WrongIngredient,P.Key==TEXT("Unknown")?EDrinkValidationDetail::UnknownIngredient:EDrinkValidationDetail::None);
        }
    }
    for(const auto& Rule:Rules)
        if(R.Composition.Get(Rule.IngredientId)<=1.e-9)
        {R.FailingIngredient=Rule.IngredientId;return Fail(EDrinkValidationReason::MissingIngredient);}
    if(R.TotalML<MinVolumeML-1.e-6)return Fail(EDrinkValidationReason::TooLittle);
    if(R.TotalML>MaxVolumeML+1.e-6)return Fail(EDrinkValidationReason::TooMuch);
    for(const auto& Rule:Rules)
    {
        const double Fraction=R.Composition.Get(Rule.IngredientId)/R.TotalML;
        if(Fraction<Rule.MinFraction-1.e-9||Fraction>Rule.MaxFraction+1.e-9)
        {
            R.FailingIngredient=Rule.IngredientId;R.ActualFraction=Fraction;R.MinFraction=Rule.MinFraction;R.MaxFraction=Rule.MaxFraction;
            return Fail(EDrinkValidationReason::WrongRatio);
        }
    }
    if((ExpectedIceCount>0&&R.IceCount<1)||(ExpectedIceCount==0&&R.IceCount>0))return Fail(EDrinkValidationReason::WrongIce);
    R.bAccepted=true;R.Reason=EDrinkValidationReason::Accepted;return R;
}
