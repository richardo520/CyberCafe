#pragma once
#include "CoreMinimal.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "Dialogue/RegularDialogueDiagnostics.h"
#include "RegularCustomerWidget.generated.h"

class UTextBlock;
class UBorder;

UCLASS()
class CYBERCAFE_API URegularCustomerWidget : public UNarrativeWidget
{
    GENERATED_BODY()
public:
    void Show(const FText& Speaker, const FText& DialogueLine, const FText& Status, const TArray<FText>& Choices);
    static FIntRect ChoiceRect(int32 Index);
    static FIntRect LinkToggleRect() { return FIntRect(570, 24, 924, 78); }
    static FVector2D PanelSize(bool bExpanded) { return FVector2D(960, bExpanded ? 800 : 560); }
    static constexpr int32 LinkToggleAction = 1001;
    void ShowLink(const FRegularDialogueLinkView& View, bool bExpanded);
    FString GetLinkLabel() const;
    FString GetLinkDetails() const;
    static FLinearColor LinkColor(const FRegularDialogueLinkView& View);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
private:
    FRegularDialogueLinkView LinkView;
    bool bDetailsExpanded = false;
    float PulseTime = 0;
    UPROPERTY(Transient) TObjectPtr<UBorder> LinkLamp;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LinkLabel;
    UPROPERTY(Transient) TObjectPtr<UBorder> DetailsBackground;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailsText;
    UPROPERTY(Transient) TObjectPtr<UBorder> PanelBackground;
    FText CachedSpeaker, CachedLine, CachedStatus;
    TArray<FText> CachedChoices;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SpeakerText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BodyText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ChoiceTexts;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> ChoiceBorders;
};
