#include "Serving/DrinkFeedbackFormatter.h"

FText FDrinkFeedbackFormatter::Format(const UDrinkRecipe& Recipe, const FDrinkValidationResult& R)
{
    if (R.bAccepted)
        return Recipe.AcceptedFeedback.IsEmpty() ? FText::FromString(Recipe.ExpectedIceCount<0 ? TEXT("饮品份量和配比符合订单。") : TEXT("饮品份量、配比和冰要求符合订单。")) : Recipe.AcceptedFeedback;
    if (R.Reason==EDrinkValidationReason::MissingIngredient && R.Composition.Get(TEXT("Milk"))<=1.e-9 &&
        Recipe.Rules.ContainsByPredicate([](const FDrinkIngredientRule& Rule){return Rule.IngredientId==TEXT("Milk");}) && !Recipe.MissingMilkFeedback.IsEmpty()) return Recipe.MissingMilkFeedback;
    if (const FText* Authored=Recipe.FailureFeedbackOverrides.Find(R.Reason); Authored && !Authored->IsEmpty()) return *Authored;
    if (R.Reason==EDrinkValidationReason::WrongRatio && Recipe.Rules.Num()==2 &&
        Recipe.Rules.ContainsByPredicate([](const FDrinkIngredientRule& Rule){return Rule.IngredientId==TEXT("Coffee");}) &&
        Recipe.Rules.ContainsByPredicate([](const FDrinkIngredientRule& Rule){return Rule.IngredientId==TEXT("Milk");}))
    {
        const bool bCoffeeHigh=(R.FailingIngredient==TEXT("Coffee")&&R.ActualFraction>R.MaxFraction+1.e-9) ||
            (R.FailingIngredient==TEXT("Milk")&&R.ActualFraction<R.MinFraction-1.e-9);
        const FText& Authored=bCoffeeHigh?Recipe.CoffeeHighFeedback:Recipe.MilkHighFeedback;
        if(!Authored.IsEmpty())return Authored;
    }
    FString Text;
    switch(R.Detail)
    {
    case EDrinkValidationDetail::BrokenContainer: Text=TEXT("杯子碎了。换一只完整的再做。"); break;
    case EDrinkValidationDetail::InvalidVolumeConfiguration: Text=TEXT("当前订单配置有误，请检查配方。"); break;
    case EDrinkValidationDetail::InvalidIceConfiguration: Text=TEXT("当前订单的冰块配置有误：-1 不检查，0 无冰，正数表示需要有冰。"); break;
    case EDrinkValidationDetail::InvalidIngredientConfiguration: Text=TEXT("当前订单的成分比例配置有误。"); break;
    case EDrinkValidationDetail::InvalidFractionSum: Text=TEXT("当前订单的成分比例无法同时满足。"); break;
    case EDrinkValidationDetail::InconsistentComposition: Text=TEXT("杯中成分记录与份量不一致，请检查原料配置。"); break;
    case EDrinkValidationDetail::InvalidComposition: Text=TEXT("杯中成分记录无效。"); break;
    case EDrinkValidationDetail::UnknownIngredient: Text=TEXT("这杯有未识别的原料。倒空后按配方重做，或换一只空杯。"); break;
    default: break;
    }
    if(Text.IsEmpty())switch(R.Reason)
    {
    case EDrinkValidationReason::Unavailable: Text=TEXT("杯子已不可用，请重新提交。"); break;
    case EDrinkValidationReason::WrongCup: Text=TEXT("这只容器不合订单。换指定的杯子，再把饮品倒进去。"); break;
    case EDrinkValidationReason::Empty: Text=TEXT("杯子还是空的。照订单加原料，再放回来。"); break;
    case EDrinkValidationReason::WrongIngredient: Text=TEXT("这杯多了配方以外的原料。倒空后重做，或换一只空杯。"); break;
    case EDrinkValidationReason::MissingIngredient: Text=R.FailingIngredient==TEXT("Milk")?TEXT("牛奶还没加。对照订单补上牛奶，再送来。"):TEXT("订单里的原料还没齐。对照配方补上，再送来。"); break;
    case EDrinkValidationReason::TooLittle: Text=FString::Printf(TEXT("还差一点。照订单的配比，补到至少 %.0f mL。"),Recipe.MinVolumeML); break;
    case EDrinkValidationReason::TooMuch: Text=FString::Printf(TEXT("杯里多了一些。倒掉一点，保留 %.0f–%.0f mL，再检查配比。"),Recipe.MinVolumeML,Recipe.MaxVolumeML); break;
    case EDrinkValidationReason::WrongRatio: Text=TEXT("配比还没对上。照订单调整；杯子太满时先倒掉一些。"); break;
    case EDrinkValidationReason::WrongIce: Text=Recipe.ExpectedIceCount>0?TEXT("这杯需要加冰。有冰即可，不限块数；补一些冰再送来。"):FString::Printf(TEXT("这杯要求无冰，杯中有 %d 块冰。倒空后重做，或换一只空杯。"),R.IceCount); break;
    default: Text=TEXT("当前订单配置有误，请检查配方。"); break;
    }
    return FText::FromString(Text);
}
