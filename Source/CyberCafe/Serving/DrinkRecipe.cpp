// @trace RC-02
#include "Serving/DrinkRecipe.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"

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
    double MinSum=0,MaxSum=0;TSet<FName> Ids;
    if(RecipeId.IsNone()||!FMath::IsFinite(TargetVolumeML)||!FMath::IsFinite(MinVolumeML)||!FMath::IsFinite(MaxVolumeML)||MinVolumeML<=0||MinVolumeML>TargetVolumeML||TargetVolumeML>MaxVolumeML||Rules.IsEmpty())
        return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("当前订单配置有误，请检查配方。"));
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
    if(AcceptedCupClass&&!Cup->IsA(AcceptedCupClass))return Fail(EDrinkValidationReason::WrongCup,TEXT("请把饮料倒进杯子，再端过来。"));
    if(R.TotalML<=1.e-9)return Fail(EDrinkValidationReason::Empty,TEXT("这还是个空杯，先倒入咖啡和牛奶吧。"));
    if(FMath::Abs(R.Composition.Total()-R.TotalML)>1.e-6)return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("杯中成分记录与份量不一致，请检查原料配置。"));
    for(const auto& P:R.Composition.Ingredients)
    {
        if(!FMath::IsFinite(P.Value)||P.Value<0)return Fail(EDrinkValidationReason::InvalidRecipe,TEXT("杯中成分记录无效。"));
        // Numeric epsilon is tiny; not a gameplay allowance for contamination.
        if(P.Value>1.e-8&&!Ids.Contains(P.Key))
        {
            if(P.Key==TEXT("Unknown"))return Fail(EDrinkValidationReason::WrongIngredient,TEXT("这杯里有未识别的原料，请换一杯，只用咖啡和牛奶。"));
            return Fail(EDrinkValidationReason::WrongIngredient,TEXT("这杯混入了其他原料，需要重新做一杯，只用咖啡和牛奶。"));
        }
    }
    for(const auto& Rule:Rules)
        if(R.Composition.Get(Rule.IngredientId)<=1.e-9)
            return Fail(EDrinkValidationReason::MissingIngredient,Rule.IngredientId==TEXT("Milk")?TEXT("还没有牛奶，补上牛奶，让咖啡和牛奶各占一半。") : TEXT("还没有咖啡，补一些咖啡再端过来。"));
    if(R.TotalML<MinVolumeML-1.e-6)return Fail(EDrinkValidationReason::TooLittle,FString::Printf(TEXT("这杯还少一些，请保持咖啡和牛奶对半，补到至少 %.0f 毫升。"),MinVolumeML));
    if(R.TotalML>MaxVolumeML+1.e-6)return Fail(EDrinkValidationReason::TooMuch,FString::Printf(TEXT("份量有点多，请倒掉一些，保留 %.0f 到 %.0f 毫升。"),MinVolumeML,MaxVolumeML));
    for(const auto& Rule:Rules)
    {
        const double F=R.Composition.Get(Rule.IngredientId)/R.TotalML;
        if(F<Rule.MinFraction-1.e-9||F>Rule.MaxFraction+1.e-9)
            return Fail(EDrinkValidationReason::WrongRatio,R.CoffeeFraction<.45 ? TEXT("奶味偏重，再补一点咖啡；份量太满的话先倒掉一些。") : TEXT("咖啡比例偏高，再补一点牛奶；份量太满的话先倒掉一些。"));
    }
    R.bAccepted=true;R.Reason=EDrinkValidationReason::Accepted;R.Feedback=FText::FromString(TEXT("这杯拿铁份量和比例都刚好，谢谢！"));return R;
}
