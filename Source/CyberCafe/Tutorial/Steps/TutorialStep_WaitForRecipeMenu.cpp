#include "Tutorial/Steps/TutorialStep_WaitForRecipeMenu.h"
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"
#include "Tutorial/TutorialDirector.h"
#include "UI/Narrative/NarrativeSubsystem.h"
#include "Engine/World.h"

void UTutorialStep_WaitForRecipeMenu::Unsubscribe()
{
    if(auto* Screen=SubscribedScreen.Get())Screen->OnMenuInteractionNative.Remove(MenuHandle);
    MenuHandle.Reset();SubscribedScreen.Reset();bActive=false;
}
void UTutorialStep_WaitForRecipeMenu::EnterStep(ATutorialDirector* InDirector)
{
    Unsubscribe();if(bUnavailable)CompletedText=ConfiguredCompletedText;
    Super::EnterStep(InDirector);
    bOpenedMenu=false;bViewedRecipe=false;bUnavailable=false;
    ConfiguredCompletedText=CompletedText;
    if(!IsValid(InDirector)||!IsValid(OrderHologram)||OrderHologram->GetWorld()!=GetWorld()||
       !IsValid(OrderHologram->RecipeMenu)||!OrderHologram->RecipeMenu->IsValidMenu()||
       OrderHologram->RecipeMenu->FindPinyinIndex(RequiredPinyinIndex)==INDEX_NONE)
    {
        bUnavailable=true;
        CompletedText=NSLOCTEXT("TutorialMenu","Unavailable","菜单暂不可用。先照制作提示做这杯拿铁。");
        UE_LOG(LogTemp,Warning,TEXT("[TutorialMenu] Menu or required index unavailable: %s"),*RequiredPinyinIndex.ToString());
        MarkCompleted();return;
    }
    bActive=true;SubscribedScreen=OrderHologram;
    MenuHandle=OrderHologram->OnMenuInteractionNative.AddUObject(this,&UTutorialStep_WaitForRecipeMenu::HandleMenuInteraction);
    // Do not count a recipe selected before EnterStep, or force a page/selection.
}
void UTutorialStep_WaitForRecipeMenu::ExitStep()
{
    Unsubscribe();if(bUnavailable)CompletedText=ConfiguredCompletedText;
    Super::ExitStep();
}
void UTutorialStep_WaitForRecipeMenu::ShowProgress(const FText& Text)
{
    if(auto* World=GetWorld())if(auto* Narrative=World->GetSubsystem<UNarrativeSubsystem>())
    {
        Narrative->SetSpeaker(FText::GetEmpty());Narrative->SetVoiceAnchor(nullptr);
        Narrative->SetInputHintEnabled(false);Narrative->ShowText(Text);
    }
}
void UTutorialStep_WaitForRecipeMenu::HandleMenuInteraction(AOrderHologramActor* Screen)
{
    if(!bActive||IsCompleted()||Screen!=SubscribedScreen.Get()||!Screen->IsPanelOpen())return;
    if(!Screen->IsRecipeMenuMode())return;
    bOpenedMenu=true;
    // Explicit current-order recipe navigation is equivalent to clicking that drink.
    // Tabs and retained/scripted default selection alone are not recipe consultation.
    const int32 Action=Screen->GetLastScreenAction();
    if(Action!=2&&(Action<100||Action>=110))return;
    const auto* Entry=Screen->GetSelectedMenuEntry();
    if(Entry&&Entry->PinyinIndex==RequiredPinyinIndex)
    {
        bViewedRecipe=true;Unsubscribe();MarkCompleted();
    }
    else ShowProgress(NSLOCTEXT("TutorialMenu","ClickDrink","点一下冰拿铁，看看用哪只杯子和哪些原料。"));
}
