#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RegularCustomerWidget.generated.h"

class UTextBlock;
class UBorder;

UCLASS()
class CYBERCAFE_API URegularCustomerWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Show(const FText& Speaker, const FText& DialogueLine, const FText& Status, const TArray<FText>& Choices);
    static FIntRect ChoiceRect(int32 Index);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    FText CachedSpeaker, CachedLine, CachedStatus;
    TArray<FText> CachedChoices;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SpeakerText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BodyText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ChoiceTexts;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> ChoiceBorders;
};
