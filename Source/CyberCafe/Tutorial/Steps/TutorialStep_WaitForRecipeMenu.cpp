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
    if(Screen->IsRecipeMenuMode())
    {
        bOpenedMenu=true;
        const auto* Entry=Screen->GetSelectedMenuEntry();
        if(Entry&&Entry->PinyinIndex==RequiredPinyinIndex)
        {
            bViewedRecipe=true;
            ShowProgress(FText::Format(NSLOCTEXT("TutorialMenu","Return","找到 {0} 了。看右侧的原料和 mL，然后再触碰底座 MENU，切回订单页。"),FText::FromName(RequiredPinyinIndex)));
        }
        else
            ShowProgress(FText::Format(NSLOCTEXT("TutorialMenu","Find","用底座 PREV / NEXT 翻查，或直接触碰屏幕左侧的 {0}。找到后看右侧配方，再用 MENU 切回订单页。"),FText::FromName(RequiredPinyinIndex)));
        return;
    }
    if(bOpenedMenu&&bViewedRecipe)
    {
        Unsubscribe();MarkCompleted();
    }
    else
        ShowProgress(FText::Format(NSLOCTEXT("TutorialMenu","Incomplete","还没查到 {0}。再触碰 MENU，找到它的配方后再回来。"),FText::FromName(RequiredPinyinIndex)));
}
