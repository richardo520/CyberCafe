// @trace RC-02
#include "Serving/DrinkRecipe.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"

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
    FDrinkValidationResult R;R.RecipeId=RecipeId;
    auto Fail=[&](EDrinkValidationReason Reason,const FString& Text){R.Reason=Reason;R.Feedback=FText::FromString(Text);return R;};
    if(IsValid(Cup))
    {
        if(const UCupIceComponent* Ice=Cup->FindComponentByClass<UCupIceComponent>())
        {
            R.IceCount=Ice->GetIceCount();
            R.IceRevision=Ice->GetPreparationRevision();
        }
    }
    if(IsValid(Cup)&&Cup->IsBroken())
    {
        R.LiquidRevision=Cup->GetLiquidRevision();
        return Fail(EDrinkValidationReason::Unavailable,TEXT("杯子碎了。换一只完整的再做。"));
    }
    double MinSum=0,MaxSum=0;TSet<FName> Ids;
    if(RecipeId.IsNone()||!FMath::IsFinite(TargetVolumeML)||!FMath::IsFinite(MinVolumeML)||!FMath::IsFinite(MaxVolumeML)||MinVolumeML<=0||MinVolumeML>TargetVolumeML||TargetVolumeML>MaxVolumeML||Rules.IsEmpty())
        return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("当前订单配置有误，请检查配方。"));
    if(ExpectedIceCount!=-1&&ExpectedIceCount!=0&&ExpectedIceCount!=3)
        return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("当前订单的冰块配置有误，仅支持不校验、无冰或3块冰。"));
    for(const auto& Rule:Rules)
    {
        if(Rule.IngredientId.IsNone()||Rule.IngredientId==TEXT("Unknown")||Ids.Contains(Rule.IngredientId)||!FMath::IsFinite(Rule.MinFraction)||!FMath::IsFinite(Rule.MaxFraction)||Rule.MinFraction<0||Rule.MinFraction>Rule.MaxFraction||Rule.MaxFraction>1)
            return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("当前订单的成分比例配置有误。"));
        MinSum+=Rule.MinFraction;MaxSum+=Rule.MaxFraction;Ids.Add(Rule.IngredientId);
    }
    if(MinSum>1+1.e-9||MaxSum<1-1.e-9)return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("当前订单的成分比例无法同时满足。"));
    if(!IsValid(Cup))return Fail(EDrinkValidationReason::Unavailable,TEXT("杯子已不可用，请重新提交。"));
    R.Composition=Cup->GetComposition();R.TotalML=Cup->GetPreciseVolumeML();R.LiquidRevision=Cup->GetLiquidRevision();
    R.CoffeeFraction=R.TotalML>0?R.Composition.Get(TEXT("Coffee"))/R.TotalML:0;
    if(AcceptedCupClass&&!Cup->IsA(AcceptedCupClass))return Fail(EDrinkValidationReason::WrongCup,TEXT("这只容器不合订单。换指定的杯子，再把饮品倒进去。"));
    if(R.TotalML<=1.e-9)return Fail(EDrinkValidationReason::Empty,TEXT("杯子还是空的。照订单加原料，再放回来。"));
    if(FMath::Abs(R.Composition.Total()-R.TotalML)>1.e-6)return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("杯中成分记录与份量不一致，请检查原料配置。"));
    for(const auto& P:R.Composition.Ingredients)
    {
        if(!FMath::IsFinite(P.Value)||P.Value<0)return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("杯中成分记录无效。"));
        // Numeric epsilon is tiny; not a gameplay allowance for contamination.
        if(P.Value>1.e-8&&!Ids.Contains(P.Key))
        {
            if(P.Key==TEXT("Unknown"))return Fail(EDrinkValidationReason::WrongIngredient,TEXT("这杯有未识别的原料。倒空后按配方重做，或换一只空杯。"));
            return Fail(EDrinkValidationReason::WrongIngredient,TEXT("这杯多了配方以外的原料。倒空后重做，或换一只空杯。"));
        }
    }
    for(const auto& Rule:Rules)
        if(R.Composition.Get(Rule.IngredientId)<=1.e-9)
            return Fail(EDrinkValidationReason::MissingIngredient,Rule.IngredientId==TEXT("Milk")?TEXT("牛奶还没加。对照订单补上牛奶，再送来。") : TEXT("订单里的原料还没齐。对照配方补上，再送来。"));
    if(R.TotalML<MinVolumeML-1.e-6)return Fail(EDrinkValidationReason::TooLittle,FString::Printf(TEXT("还差一点。照订单的配比，补到至少 %.0f mL。"),MinVolumeML));
    if(R.TotalML>MaxVolumeML+1.e-6)return Fail(EDrinkValidationReason::TooMuch,FString::Printf(TEXT("杯里多了一些。倒掉一点，保留 %.0f–%.0f mL，再检查配比。"),MinVolumeML,MaxVolumeML));
    for(const auto& Rule:Rules)
    {
        const double F=R.Composition.Get(Rule.IngredientId)/R.TotalML;
        if(F<Rule.MinFraction-1.e-9||F>Rule.MaxFraction+1.e-9)
            return Fail(EDrinkValidationReason::WrongRatio,TEXT("配比还没对上。照订单调整；杯子太满时先倒掉一些。"));
    }
    if(ExpectedIceCount>=0&&R.IceCount<ExpectedIceCount)
        return Fail(EDrinkValidationReason::WrongIce,FString::Printf(TEXT("冰块不足：订单需要 %d 块，杯中只有 %d 块。请补齐冰块，再送来。"),ExpectedIceCount,R.IceCount));
    if(ExpectedIceCount>=0&&R.IceCount>ExpectedIceCount)
        return Fail(EDrinkValidationReason::WrongIce,ExpectedIceCount==0
            ? FString::Printf(TEXT("这杯要求无冰，杯中有 %d 块冰。倒空后重做，或换一只空杯。"),R.IceCount)
            : FString::Printf(TEXT("冰块太多：订单需要 %d 块，杯中有 %d 块。倒出多余冰块，必要时重新调制。"),ExpectedIceCount,R.IceCount));
    R.bAccepted=true;R.Reason=EDrinkValidationReason::Accepted;
    R.Feedback=FText::FromString(ExpectedIceCount<0?TEXT("饮品份量和配比符合订单。"):TEXT("饮品份量、配比和冰块数量符合订单。"));return R;
}
