// @trace MT-01 MT-02. Teach real menu input without altering any order.
#pragma once
#include "CoreMinimal.h"
#include "Tutorial/TutorialStep.h"
#include "TutorialStep_WaitForRecipeMenu.generated.h"
class AOrderHologramActor;

UCLASS(Blueprintable, EditInlineNew)
class CYBERCAFE_API UTutorialStep_WaitForRecipeMenu : public UTutorialStep
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Menu")
    TObjectPtr<AOrderHologramActor> OrderHologram;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Menu")
    FName RequiredPinyinIndex=TEXT("BNT");
    virtual void EnterStep(ATutorialDirector* InDirector) override;
    virtual void ExitStep() override;
    UFUNCTION(BlueprintPure, Category="Tutorial|Menu") bool HasOpenedMenu() const {return bOpenedMenu;}
    UFUNCTION(BlueprintPure, Category="Tutorial|Menu") bool HasViewedRequiredRecipe() const {return bViewedRecipe;}
    UFUNCTION(BlueprintPure, Category="Tutorial|Menu") bool WasUnavailable() const {return bUnavailable;}
private:
    void HandleMenuInteraction(AOrderHologramActor* Screen);
    void Unsubscribe();
    void ShowProgress(const FText& Text);
    FDelegateHandle MenuHandle;
    TWeakObjectPtr<AOrderHologramActor> SubscribedScreen;
    FText ConfiguredCompletedText;
    bool bActive=false,bOpenedMenu=false,bViewedRecipe=false,bUnavailable=false;
};
