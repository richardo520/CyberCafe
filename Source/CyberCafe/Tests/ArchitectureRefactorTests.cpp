#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/DrinkFeedbackFormatter.h"
#include "Customers/CustomerDrinkMotion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructuredRecipeTest,
    "CyberCafe.Architecture.StructuredNonCoffeeRulesIgnorePresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStructuredRecipeTest::RunTest(const FString&)
{
    auto* Recipe=NewObject<UDrinkRecipe>();
    Recipe->RecipeId=TEXT("RumLime");
    Recipe->Rules[0].IngredientId=TEXT("Rum");Recipe->Rules[1].IngredientId=TEXT("Lime");
    FDrinkPreparationSnapshot S;S.bAvailable=true;S.TotalML=200;
    S.Composition=FLiquidComposition::Pure(TEXT("Rum"),100);
    S.Composition.Add(FLiquidComposition::Pure(TEXT("Lime"),100));
    const auto Accepted=Recipe->EvaluateSnapshot(S);
    TestTrue(TEXT("Generic recipe accepts without Actor, coffee or milk"),Accepted.bAccepted);
    TestTrue(TEXT("Numeric evaluation creates no feedback text"),Accepted.Feedback.IsEmpty());
    S.Composition=FLiquidComposition::Pure(TEXT("Rum"),140);
    S.Composition.Add(FLiquidComposition::Pure(TEXT("Lime"),60));
    const auto Rejected=Recipe->EvaluateSnapshot(S);
    TestEqual(TEXT("Structured ratio failure identifies ingredient"),Rejected.FailingIngredient,FName(TEXT("Rum")));
    TestEqual(TEXT("Structured result reports actual fraction"),Rejected.ActualFraction,.7);
    Recipe->FailureFeedbackOverrides.Add(EDrinkValidationReason::WrongRatio,FText::FromString(TEXT("Custom response")));
    TestEqual(TEXT("Formatter owns authored text"),FDrinkFeedbackFormatter::Format(*Recipe,Rejected).ToString(),FString(TEXT("Custom response")));
    TestEqual(TEXT("Changing dialogue cannot affect numeric rejection"),Recipe->EvaluateSnapshot(S).Reason,EDrinkValidationReason::WrongRatio);
    TestTrue(TEXT("Evaluation never changes the input composition"),S.Composition.Get(TEXT("Rum"))==140&&S.Composition.Get(TEXT("Lime"))==60);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigurableCustomerMotionTest,
    "CyberCafe.Architecture.MotionPresentationUsesAuthoredCues",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FConfigurableCustomerMotionTest::RunTest(const FString&)
{
    auto* Source=LoadObject<UCustomerDrinkMotion>(nullptr,TEXT("/Game/CyberCafe/Characters/BarGirl/FirstCustomer/DA_BarGirlDrinkMotion.DA_BarGirlDrinkMotion"));
    if(!TestNotNull(TEXT("Saved original motion"),Source))return false;
    auto* Motion=DuplicateObject<UCustomerDrinkMotion>(Source,GetTransientPackage());
    Motion->BlinkCues={FCustomerBlinkCue(1.f,.2f)};
    Motion->LiftBlendEnd=2.2f;Motion->ReturnBlendStart=4.3f;
    Motion->BlinkMorph=TEXT("CustomBlink");Motion->VoiceSocket=TEXT("mouth");
    TestTrue(TEXT("Custom presentation config remains valid"),Motion->IsValidMotion());
    TestEqual(TEXT("Authored blink centre drives weight"),Motion->BlinkWeight(1.f),1.f);
    TestEqual(TEXT("Old hardcoded blink time is no longer used"),Motion->BlinkWeight(3.12f),0.f);
    TestTrue(TEXT("Authored grip interval reaches full blend"),FMath::IsNearlyEqual(Motion->GripBlend(2.2f),1.f));
    Motion->LiftBlendEnd=Motion->GraspTime;
    TestFalse(TEXT("Invalid authored blend interval is rejected"),Motion->IsValidMotion());
    TestTrue(TEXT("Original saved motion was not mutated"),Source->IsValidMotion());
    return true;
}
#endif
