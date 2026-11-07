#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OrderHologramWidget.generated.h"

class AOrderHologramActor;
class UFont;
class USizeBox;
class UTextBlock;
class UWidgetSwitcher;

/** 只读全息订单摘要；空 WBP 子类在运行时继承此原生控件树。 */
UCLASS(BlueprintType, Blueprintable)
class CYBERCAFE_API UOrderHologramWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UOrderHologramWidget(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category = "Hologram|Presentation")
    void ApplyPresentation(const AOrderHologramActor* Source);

    UFUNCTION(BlueprintPure, Category = "Hologram|Presentation")
    FText GetOrderText() const;

    UFUNCTION(BlueprintPure, Category = "Hologram|Presentation")
    FText GetHistoryText() const;
    UFUNCTION(BlueprintPure,Category="Hologram|Menu") FText GetMenuText() const {return MenuText;}

    // 幂等构树：只补空根，不覆盖蓝图已有的布局。
    void BuildWidgetTree();

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeOnInitialized() override;

private:
    void SynchronizePresentation();

    UPROPERTY()
    TObjectPtr<UFont> PresentationFont;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StatusTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> RecipeNameTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> TargetTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> IngredientsTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> ToleranceTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> CustomerNameTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> CustomerIdTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> CustomerNotesTextBlock;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> EmptyHistoryTextBlock;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USizeBox>> HistoryRows;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> HistoryNameTextBlocks;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> HistoryVolumeTextBlocks;

    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> PageSwitcher;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MenuIndexLabels;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuMetadata;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuDescription;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuIngredientLines;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuTolerance;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuFooter;
    FText MenuText;
    FText OrderText;
    FText HistoryText;
    FText PresentedStatus;
    FText PresentedRecipeName;
    FText PresentedTarget;
    FText PresentedIngredients;
    FText PresentedTolerance;
    FText PresentedCustomerName;
    FText PresentedCustomerId;
    FText PresentedCustomerNotes;
    TArray<FText> PresentedHistoryNames;
    TArray<FText> PresentedHistoryVolumes;
};
