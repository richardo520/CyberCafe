#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "OrderHologramWidget.generated.h"

class AOrderHologramActor;
class UCanvasPanel;
class UFont;
class UImage;
class USizeBox;
class UTextBlock;
class UWidgetSwitcher;

/** 屏内按钮只提交动作；是否接受和教学通知仍由 Actor 裁决。 */
UCLASS()
class CYBERCAFE_API URecipeScreenButton : public UButton
{
    GENERATED_BODY()

public:
    URecipeScreenButton(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(Transient)
    int32 Action = INDEX_NONE;

    UPROPERTY(Transient)
    TWeakObjectPtr<AOrderHologramActor> Host;

private:
    UFUNCTION()
    void HandleClick();
};

/** 原生全息订单/配方屏；空 WBP 子类在运行时继承此控件树。 */
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
    UFUNCTION(BlueprintPure, Category = "Hologram|Menu")
    FText GetMenuText() const { return MenuText; }

    // 1200×750 屏幕坐标的唯一来源；Actor 触碰检测也使用本函数。
    static FIntRect GetScreenActionRect(int32 Action);

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
    UPROPERTY(Transient) TArray<TObjectPtr<URecipeScreenButton>> ScreenButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MenuIndexLabels;
    UPROPERTY(Transient) TObjectPtr<UImage> OrderCupImage;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OrderCupLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OrderIceLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuIndexBadge;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuMetadata;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuDescription;
    UPROPERTY(Transient) TObjectPtr<UImage> MenuCupImage;
    UPROPERTY(Transient) TObjectPtr<UImage> MenuIceImage;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuCupName;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuIceLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuIceHint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuTotal;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuTotalHint;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuIngredientHeading;
    UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> MenuIngredientCards;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MenuIngredientImages;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MenuIngredientNames;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MenuIngredientVolumes;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuIngredientLines;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MenuMethod;
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
