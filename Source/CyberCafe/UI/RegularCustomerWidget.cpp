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
    PanelBackground = WidgetTree->ConstructWidget<UBorder>();
    PanelBackground->SetBrushColor(FLinearColor(.012f, .025f, .045f, .96f));
    Place(PanelBackground, FVector2D::ZeroVector, PanelSize(bDetailsExpanded));
    const auto Text = [this, &Place](int32 Size, FLinearColor Color, FVector2D Position, FVector2D Extent)
    {
        UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
        FSlateFontInfo Font = T->GetFont(); Font.Size = Size; T->SetFont(Font);
        T->SetColorAndOpacity(Color); T->SetAutoWrapText(true);
        Place(T, Position, Extent); return T;
    };
    SpeakerText = Text(30, FLinearColor(.25f, .95f, 1), FVector2D(36, 24), FVector2D(510, 54));
    UBorder* LinkButton = WidgetTree->ConstructWidget<UBorder>();
    LinkButton->SetBrushColor(FLinearColor(.035f, .09f, .14f, 1));
    Place(LinkButton, FVector2D(LinkToggleRect().Min), FVector2D(LinkToggleRect().Size()));
    LinkLamp = WidgetTree->ConstructWidget<UBorder>();
    Place(LinkLamp, FVector2D(584, 43), FVector2D(16, 16));
    LinkLabel = Text(18, FLinearColor::White, FVector2D(610, 36), FVector2D(306, 38));
    BodyText = Text(27, FLinearColor(.95f, .96f, 1), FVector2D(36, 94), FVector2D(888, 180));
    Text_Body = BodyText;
    Text_Speaker = SpeakerText;
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
    DetailsBackground = WidgetTree->ConstructWidget<UBorder>();
    DetailsBackground->SetBrushColor(FLinearColor(.022f, .055f, .085f, 1));
    Place(DetailsBackground, FVector2D(36, 572), FVector2D(888, 208));
    DetailsText = Text(19, FLinearColor(.72f, .86f, .9f), FVector2D(56, 584), FVector2D(848, 190));
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    Show(CachedSpeaker, CachedLine, CachedStatus, CachedChoices);
    ShowLink(LinkView, bDetailsExpanded);
    BodyText->SetText(IsTyping() ? FText::FromString(GetVisibleText()) : CachedLine);
    return Result;
}

FString URegularCustomerWidget::GetLinkLabel() const
{
    const FString State = LinkView.bWaiting ? (LinkView.bReadyForReading ? TEXT("回复就绪") : TEXT("正在生成")) :
        (LinkView.Source == ERegularDialogueSource::AI ? TEXT("AI 台词") :
        (LinkView.Source == ERegularDialogueSource::Fallback ? TEXT("已回退") : TEXT("本地台词")));
    return FString::Printf(TEXT("神经链路 · %s %s"), *State, bDetailsExpanded ? TEXT("−") : TEXT("+"));
}

FString URegularCustomerWidget::GetLinkDetails() const
{
    const auto& D = LinkView.Diagnostics;
    const FString Current = LinkView.bPlaceholder ? TEXT("等待新台词，尚未展示") : RegularDialogueDiagnostics::SourceLabel(LinkView.Source);
    const FString Waiting = LinkView.bWaiting ? (LinkView.bReadyForReading ? TEXT("已就绪，等待读完菜单") :
        FString::Printf(TEXT("正在生成（%.1f 秒）"), D.PendingSeconds)) : TEXT("无等待中的新台词");
    const FString Recent = D.LastRequestResult != TEXT("not_requested") ? FString::Printf(TEXT("%s · %.2f 秒"),
        *RegularDialogueDiagnostics::ReasonLabel(D.LastRequestResult), D.LastRequestSeconds) : TEXT("尚无已结束请求");
    const FString Reason = LinkView.bPlaceholder ? TEXT("等待回复通过检查后展示") : RegularDialogueDiagnostics::ReasonLabel(LinkView.CurrentReason);
    return FString::Printf(TEXT("模型：%s\n当前句：%s\n新台词：%s\n最近请求：%s\n本句说明：%s\n会话请求：%d 次  |  最近60秒：%d / %d 次"),
        *D.Model, *Current, *Waiting, *Recent, *Reason, D.TotalRequests, D.RecentRequests, D.RequestLimit);
}

FLinearColor URegularCustomerWidget::LinkColor(const FRegularDialogueLinkView& View)
{
    if (View.bWaiting) return FLinearColor(.25f, .58f, 1.f);
    if (View.Source == ERegularDialogueSource::AI) return FLinearColor(.15f, 1.f, .8f);
    if (View.Source == ERegularDialogueSource::Fallback) return FLinearColor(1.f, .57f, .18f);
    return FLinearColor(.5f, .6f, .65f);
}

void URegularCustomerWidget::ShowLink(const FRegularDialogueLinkView& View, bool bExpanded)
{
    LinkView = View; bDetailsExpanded = bExpanded;
    if (!LinkLabel) return;
    LinkLabel->SetText(FText::FromString(GetLinkLabel()));
    const FLinearColor Color = LinkColor(LinkView);
    LinkLabel->SetColorAndOpacity(Color); LinkLamp->SetBrushColor(Color);
    if (!LinkView.bWaiting || LinkView.bReadyForReading) LinkLamp->SetRenderOpacity(1);
    const ESlateVisibility DrawerVisibility = bExpanded ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
    DetailsBackground->SetVisibility(DrawerVisibility); DetailsText->SetVisibility(DrawerVisibility);
    if (bExpanded) DetailsText->SetText(FText::FromString(GetLinkDetails()));
    if (auto* BackgroundSlot = Cast<UCanvasPanelSlot>(PanelBackground->Slot)) BackgroundSlot->SetSize(PanelSize(bExpanded));
}

void URegularCustomerWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    PulseTime = FMath::Fmod(PulseTime + DeltaSeconds, 100.f);
    if (LinkLamp && LinkView.bWaiting && !LinkView.bReadyForReading)
        LinkLamp->SetRenderOpacity(.7f + .3f * FMath::Sin(PulseTime * 4.f));
}

void URegularCustomerWidget::Show(const FText& Speaker, const FText& DialogueLine, const FText& Status, const TArray<FText>& Choices)
{
    const bool bNewLine = !CachedLine.EqualTo(DialogueLine);
    CachedSpeaker = Speaker; CachedLine = DialogueLine; CachedStatus = Status; CachedChoices = Choices;
    if (!BodyText) return;
    SpeakerText->SetText(Speaker); StatusText->SetText(Status);
    if (bNewLine) SetText(DialogueLine);
    for (int32 I = 0; I < ChoiceTexts.Num(); ++I)
    {
        const bool bVisible = Choices.IsValidIndex(I);
        ChoiceTexts[I]->SetText(bVisible ? Choices[I] : FText::GetEmpty());
        ChoiceTexts[I]->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        ChoiceBorders[I]->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}
