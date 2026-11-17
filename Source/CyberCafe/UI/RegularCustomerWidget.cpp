#include "UI/RegularCustomerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

FIntRect URegularCustomerWidget::ChoiceRect(int32 Index)
{
    return Index >= 0 && Index < 2 ? FIntRect(36, 370 + Index * 84, 924, 438 + Index * 84) : FIntRect();
}

TSharedRef<SWidget> URegularCustomerWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;
    const auto Place = [Canvas](UWidget* W, FVector2D Position, FVector2D Size)
    {
        UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(W);
        Slot->SetPosition(Position); Slot->SetSize(Size);
    };
    UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrushColor(FLinearColor(.012f, .025f, .045f, .96f));
    Place(Background, FVector2D::ZeroVector, FVector2D(960, 560));
    const auto Text = [this, &Place](int32 Size, FLinearColor Color, FVector2D Position, FVector2D Extent)
    {
        UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
        FSlateFontInfo Font = T->GetFont(); Font.Size = Size; T->SetFont(Font);
        T->SetColorAndOpacity(Color); T->SetAutoWrapText(true);
        Place(T, Position, Extent); return T;
    };
    SpeakerText = Text(30, FLinearColor(.25f, .95f, 1), FVector2D(36, 24), FVector2D(888, 54));
    BodyText = Text(27, FLinearColor(.95f, .96f, 1), FVector2D(36, 94), FVector2D(888, 180));
    StatusText = Text(21, FLinearColor(.55f, .8f, .85f), FVector2D(36, 284), FVector2D(888, 74));
    ChoiceTexts.Empty(); ChoiceBorders.Empty();
    for (int32 I = 0; I < 2; ++I)
    {
        const FIntRect R = ChoiceRect(I);
        UBorder* B = WidgetTree->ConstructWidget<UBorder>();
        B->SetBrushColor(FLinearColor(.035f, .15f, .20f, 1));
        Place(B, FVector2D(R.Min), FVector2D(R.Size()));
        ChoiceBorders.Add(B);
        ChoiceTexts.Add(Text(25, FLinearColor::White, FVector2D(R.Min.X + 20, R.Min.Y + 15), FVector2D(848, 45)));
    }
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    Show(CachedSpeaker, CachedLine, CachedStatus, CachedChoices);
    return Result;
}

void URegularCustomerWidget::Show(const FText& Speaker, const FText& Line, const FText& Status, const TArray<FText>& Choices)
{
    CachedSpeaker = Speaker; CachedLine = Line; CachedStatus = Status; CachedChoices = Choices;
    if (!BodyText) return;
    SpeakerText->SetText(Speaker); BodyText->SetText(Line); StatusText->SetText(Status);
    for (int32 I = 0; I < ChoiceTexts.Num(); ++I)
    {
        const bool bVisible = Choices.IsValidIndex(I);
        ChoiceTexts[I]->SetText(bVisible ? Choices[I] : FText::GetEmpty());
        ChoiceTexts[I]->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        ChoiceBorders[I]->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}
