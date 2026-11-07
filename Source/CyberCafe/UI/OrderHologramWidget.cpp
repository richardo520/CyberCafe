#include "UI/OrderHologramWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "Liquid/LiquidContainerActor.h"
#include "Serving/DrinkMenuData.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/OrderHologramActor.h"
#include "Styling/SlateBrush.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OrderHologramWidget"

namespace
{
    constexpr float PanelWidth = 1200.f;
    constexpr float PanelHeight = 750.f;
    constexpr float HeaderHeight = 88.f;
    constexpr int32 MaxHistoryRows = 3;
    constexpr int32 MaxMenuRows = 10;
    constexpr int32 MaxIngredientCards = 3;

    const FLinearColor Paper = FLinearColor::FromSRGBColor(FColor(17, 29, 36));
    const FLinearColor Panel = FLinearColor::FromSRGBColor(FColor(21, 37, 45));
    const FLinearColor Card = FLinearColor::FromSRGBColor(FColor(32, 52, 61));
    const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(227, 237, 234));
    const FLinearColor Muted = FLinearColor::FromSRGBColor(FColor(183, 201, 201));
    const FLinearColor Line = FLinearColor::FromSRGBColor(FColor(66, 90, 100));
    const FLinearColor Teal = FLinearColor::FromSRGBColor(FColor(128, 213, 197));
    const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor(42, 75, 80));
    const FLinearColor SelectedBorder = FLinearColor::FromSRGBColor(FColor(80, 124, 122));
    const FLinearColor Pressed = FLinearColor::FromSRGBColor(FColor(53, 93, 96));

    void SetScreenButtonStyle(URecipeScreenButton* Button, bool bSelected)
    {
        FButtonStyle Style;
        Style.SetNormal(FSlateRoundedBoxBrush(bSelected ? Accent : Panel, 9.f,
            bSelected ? SelectedBorder : FLinearColor::Transparent, 1.f));
        Style.SetHovered(FSlateRoundedBoxBrush(Accent, 9.f, Line, 1.f));
        Style.SetPressed(FSlateRoundedBoxBrush(Pressed, 9.f, Teal, 1.f));
        Style.SetDisabled(FSlateRoundedBoxBrush(Paper, 9.f));
        Style.SetNormalPadding(FMargin(0.f));
        Style.SetPressedPadding(FMargin(0.f));
        Button->SetStyle(Style);
        Button->SetBackgroundColor(FLinearColor::White);
        Button->SetColorAndOpacity(FLinearColor::White);
        if (UTextBlock* Label = Cast<UTextBlock>(Button->GetContent()))
        {
            Label->SetColorAndOpacity(FSlateColor(bSelected ? Teal : Ink));
        }
    }

    void SetTexture(UImage* Image, UTexture2D* Texture)
    {
        if (Image)
        {
            Image->SetBrushFromTexture(IsValid(Texture) ? Texture : nullptr, false);
            Image->SetVisibility(IsValid(Texture) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
        }
    }

    FText NumberText(double Value)
    {
        FNumberFormattingOptions Options;
        Options.SetUseGrouping(false);
        Options.SetMinimumFractionalDigits(0);
        Options.SetMaximumFractionalDigits(2);
        return FText::AsNumber(Value, &Options);
    }

    FText IceText(int32 Count)
    {
        if (Count == 0) return LOCTEXT("NoIce", "不加冰");
        if (Count > 0) return LOCTEXT("IceRequired", "加冰即可");
        return LOCTEXT("IceUnconfigured", "冰量未配置");
    }

    FText IngredientName(FName Id)
    {
        if (Id == TEXT("Coffee"))
        {
            return LOCTEXT("Coffee", "咖啡");
        }
        if (Id == TEXT("Milk"))
        {
            return LOCTEXT("Milk", "牛奶");
        }
        return FText::FromName(Id);
    }
}

URecipeScreenButton::URecipeScreenButton(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    InitIsFocusable(false);
    OnClicked.AddDynamic(this, &URecipeScreenButton::HandleClick);
}

void URecipeScreenButton::HandleClick()
{
    if (AOrderHologramActor* Source = Host.Get())
    {
        Source->ActivateScreenAction(Action);
    }
}

FIntRect UOrderHologramWidget::GetScreenActionRect(int32 Action)
{
    switch (Action)
    {
    case 0: return FIntRect(230, 14, 410, 76);
    case 1: return FIntRect(425, 14, 625, 76);
    case 2: return FIntRect(50, 510, 620, 573);
    default: break;
    }
    if (Action >= 10 && Action <= 12)
    {
        const int32 X = 24 + (Action - 10) * 84;
        return FIntRect(X, 132, X + 80, 170);
    }
    if (Action >= 100 && Action < 100 + MaxMenuRows)
    {
        const int32 Y = 184 + (Action - 100) * 48;
        return FIntRect(24, Y, 278, Y + 46);
    }
    return FIntRect(0, 0, 0, 0);
}

UOrderHologramWidget::UOrderHologramWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static ConstructorHelpers::FObjectFinder<UFont> FontAsset(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    PresentationFont = FontAsset.Object;
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    PresentedStatus = LOCTEXT("NoOrder", "暂无当前订单");
    PresentedRecipeName = LOCTEXT("NoRecipe", "暂无配方");
    PresentedCustomerName = LOCTEXT("NoCustomerName", "未配置客人名");
    PresentedCustomerNotes = LOCTEXT("NoNotes", "暂无配置备注");
    OrderText = PresentedStatus;
    HistoryText = LOCTEXT("NoHistory", "暂无本局交付");
}

TSharedRef<SWidget> UOrderHologramWidget::RebuildWidget()
{
    // 先实例化 WBP 的树，再补空根，避免 Super 初始化时替换已构建的树。
    Initialize();
    BuildWidgetTree();
    return Super::RebuildWidget();
}

void UOrderHologramWidget::NativeOnInitialized()
{
    BuildWidgetTree();
    Super::NativeOnInitialized();
}

void UOrderHologramWidget::BuildWidgetTree()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
    }
    if (WidgetTree->RootWidget)
    {
        return;
    }

    HistoryRows.Reset();
    HistoryNameTextBlocks.Reset();
    HistoryVolumeTextBlocks.Reset();
    ScreenButtons.Reset();
    MenuIndexLabels.Reset();
    MenuIngredientCards.Reset();
    MenuIngredientImages.Reset();
    MenuIngredientNames.Reset();
    MenuIngredientVolumes.Reset();

    const auto MakeCanvas = [this](FName Name)
    {
        UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), Name);
        Canvas->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        return Canvas;
    };
    const auto Place = [](UCanvasPanel* Parent, UWidget* Child, FVector2D Position, FVector2D Size)
    {
        UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Child);
        Slot->SetAnchors(FAnchors(0.f, 0.f));
        Slot->SetAlignment(FVector2D::ZeroVector);
        Slot->SetAutoSize(false);
        Slot->SetPosition(Position);
        Slot->SetSize(Size);
        Child->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        return Slot;
    };
    const auto MakeText = [this](FName Name, int32 Size, FLinearColor Color, bool bWrap)
    {
        UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        // 批准稿字号按像素；Slate 字号按 point，须消除 96 DPI 的 4/3 放大。
        const float FontPoints = static_cast<float>(Size) * 72.f / static_cast<float>(FontConstants::RenderDPI);
        Text->SetFont(FSlateFontInfo(PresentationFont.Get(), FontPoints));
        Text->SetColorAndOpacity(FSlateColor(Color));
        Text->SetAutoWrapText(bWrap);
        Text->SetLineHeightPercentage(1.05f);
        Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Text->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        Text->SetVisibility(ESlateVisibility::HitTestInvisible);
        return Text;
    };
    const auto MakeImage = [this](FName Name)
    {
        UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
        Image->SetVisibility(ESlateVisibility::Hidden);
        Image->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        return Image;
    };
    const auto MakeBorder = [this](FName Name, FLinearColor Color, bool bRounded)
    {
        UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
        if (bRounded)
        {
            Border->SetBrush(FSlateRoundedBoxBrush(Color, 12.f, Line, 1.f));
        }
        else
        {
            FSlateBrush Brush;
            Brush.DrawAs = ESlateBrushDrawType::Image;
            Brush.TintColor = FSlateColor(Color);
            Border->SetBrush(Brush);
        }
        Border->SetBrushColor(FLinearColor::White);
        Border->SetPadding(FMargin(0.f));
        Border->SetVisibility(ESlateVisibility::HitTestInvisible);
        return Border;
    };
    const auto MakeCard = [&MakeCanvas, &MakeBorder, &Place](UCanvasPanel* Parent, FName Name,
        FVector2D Position, FVector2D Size)
    {
        UCanvasPanel* Canvas = MakeCanvas(Name);
        Place(Parent, Canvas, Position, Size);
        Place(Canvas, MakeBorder(FName(*(Name.ToString() + TEXT("Background"))), Card, true),
            FVector2D::ZeroVector, Size);
        return Canvas;
    };
    const auto MakeButton = [this, &MakeText, &Place](UCanvasPanel* Parent, int32 Action, FName Name,
        FName LabelName, FText Caption, int32 FontSize, FVector2D ScreenOrigin, bool bLeftAligned)
    {
        URecipeScreenButton* Button = WidgetTree->ConstructWidget<URecipeScreenButton>(URecipeScreenButton::StaticClass(), Name);
        Button->Action = Action;
        Button->SetVisibility(ESlateVisibility::Visible);
        Button->SetIsEnabled(false);
        UTextBlock* Label = MakeText(LabelName, FontSize, Ink, false);
        Label->SetText(Caption);
        Label->SetJustification(bLeftAligned ? ETextJustify::Left : ETextJustify::Center);
        UButtonSlot* ContentSlot = CastChecked<UButtonSlot>(Button->AddChild(Label));
        ContentSlot->SetPadding(FMargin(bLeftAligned ? 10.f : 4.f, 0.f));
        ContentSlot->SetHorizontalAlignment(HAlign_Fill);
        ContentSlot->SetVerticalAlignment(VAlign_Center);
        const FIntRect Rect = GetScreenActionRect(Action);
        // 页内坐标只减去父画布原点，不另写一套命中坐标。
        Place(Parent, Button, FVector2D(Rect.Min.X, Rect.Min.Y) - ScreenOrigin,
            FVector2D(Rect.Width(), Rect.Height()));
        SetScreenButtonStyle(Button, false);
        ScreenButtons.Add(Button);
        return Label;
    };

    // 固定像素画布由 Actor 的 WidgetComponent 映射到世界空间。
    USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OrderHologramRoot"));
    Root->SetWidthOverride(PanelWidth);
    Root->SetHeightOverride(PanelHeight);
    Root->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UCanvasPanel* Screen = MakeCanvas(TEXT("OrderHologramCanvas"));
    Root->SetContent(Screen);
    WidgetTree->RootWidget = Root;
    Place(Screen, MakeBorder(TEXT("OpaqueBody"), Panel, false), FVector2D::ZeroVector,
        FVector2D(PanelWidth, PanelHeight));
    Place(Screen, MakeBorder(TEXT("HeaderDivider"), Line, false), FVector2D(0.f, 87.f), FVector2D(PanelWidth, 1.f));

    UTextBlock* Brand = MakeText(TEXT("ScreenBrand"), 28, Teal, false);
    Brand->SetText(LOCTEXT("Brand", "檐下"));
    Place(Screen, Brand, FVector2D(28.f, 17.f), FVector2D(180.f, 39.f));
    UTextBlock* BrandSubline = MakeText(TEXT("ScreenBrandSubline"), 11, Muted, false);
    BrandSubline->SetText(LOCTEXT("BrandSubline", "CAFE · DRINKS"));
    Place(Screen, BrandSubline, FVector2D(29.f, 58.f), FVector2D(180.f, 20.f));
    MakeButton(Screen, 0, TEXT("OrderTabButton"), TEXT("OrderTabLabel"), LOCTEXT("CurrentOrder", "当前订单"), 25, FVector2D::ZeroVector, false);
    MakeButton(Screen, 1, TEXT("RecipeTabButton"), TEXT("RecipeTabLabel"), LOCTEXT("RecipeTab", "饮品配方"), 25, FVector2D::ZeroVector, false);
    UTextBlock* ScreenHint = MakeText(TEXT("ScreenTouchHint"), 19, Muted, false);
    ScreenHint->SetText(LOCTEXT("ScreenTouchHint", "触碰即可查看"));
    ScreenHint->SetJustification(ETextJustify::Right);
    Place(Screen, ScreenHint, FVector2D(884.f, 31.f), FVector2D(288.f, 30.f));

    PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("OrderMenuSwitcher"));
    PageSwitcher->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Place(Screen, PageSwitcher, FVector2D(0.f, HeaderHeight), FVector2D(PanelWidth, PanelHeight - HeaderHeight));
    const FVector2D PageOrigin(0.f, HeaderHeight);
    UCanvasPanel* OrderPage = MakeCanvas(TEXT("OrderPage"));
    UWidgetSwitcherSlot* OrderSlot = CastChecked<UWidgetSwitcherSlot>(PageSwitcher->AddChild(OrderPage));
    OrderSlot->SetHorizontalAlignment(HAlign_Fill);
    OrderSlot->SetVerticalAlignment(VAlign_Fill);
    MakeCard(OrderPage, TEXT("OrderColumn"), FVector2D(26.f, 24.f), FVector2D(618.f, 616.f));
    MakeCard(OrderPage, TEXT("CustomerColumn"), FVector2D(668.f, 24.f), FVector2D(506.f, 616.f));
    const auto PlaceOrderText = [&Place, OrderPage, PageOrigin](UTextBlock* Text, float X, float Y, float Width, float Height)
    {
        Place(OrderPage, Text, FVector2D(X, Y) - PageOrigin, FVector2D(Width, Height));
    };
    UTextBlock* OrderHeading = MakeText(TEXT("OrderHeading"), 18, Muted, false);
    OrderHeading->SetText(LOCTEXT("CurrentOrder", "当前订单"));
    PlaceOrderText(OrderHeading, 50.f, 129.f, 250.f, 28.f);
    StatusTextBlock = MakeText(TEXT("OrderStatus"), 21, Teal, false);
    PlaceOrderText(StatusTextBlock, 330.f, 129.f, 290.f, 34.f);
    RecipeNameTextBlock = MakeText(TEXT("RecipeName"), 36, Ink, false);
    PlaceOrderText(RecipeNameTextBlock, 50.f, 178.f, 570.f, 58.f);
    TargetTextBlock = MakeText(TEXT("TargetVolume"), 26, Ink, false);
    PlaceOrderText(TargetTextBlock, 50.f, 244.f, 570.f, 40.f);
    OrderCupImage = MakeImage(TEXT("OrderCupIcon"));
    Place(OrderPage, OrderCupImage, FVector2D(50.f, 288.f) - PageOrigin, FVector2D(128.f, 128.f));
    OrderCupLabel = MakeText(TEXT("OrderCupName"), 18, Ink, true);
    PlaceOrderText(OrderCupLabel, 50.f, 425.f, 128.f, 47.f);
    OrderIceLabel = MakeText(TEXT("OrderIceCount"), 18, Teal, false);
    PlaceOrderText(OrderIceLabel, 190.f, 288.f, 430.f, 30.f);
    IngredientsTextBlock = MakeText(TEXT("OrderIngredients"), 22, Ink, true);
    PlaceOrderText(IngredientsTextBlock, 190.f, 330.f, 430.f, 164.f);
    MakeButton(OrderPage, 2, TEXT("OrderRecipeButton"), TEXT("OrderRecipeButtonLabel"),
        LOCTEXT("OpenOrderRecipe", "查看这杯的配方"), 24, PageOrigin, false);
    ToleranceTextBlock = MakeText(TEXT("OrderTolerance"), 19, Muted, true);
    PlaceOrderText(ToleranceTextBlock, 50.f, 589.f, 570.f, 134.f);

    UTextBlock* CustomerHeading = MakeText(TEXT("CustomerHeading"), 18, Muted, false);
    CustomerHeading->SetText(LOCTEXT("CustomerProfile", "客人档案"));
    PlaceOrderText(CustomerHeading, 692.f, 129.f, 458.f, 28.f);
    CustomerNameTextBlock = MakeText(TEXT("CustomerName"), 30, Ink, false);
    PlaceOrderText(CustomerNameTextBlock, 692.f, 169.f, 458.f, 45.f);
    CustomerIdTextBlock = MakeText(TEXT("CustomerId"), 18, Muted, false);
    PlaceOrderText(CustomerIdTextBlock, 692.f, 215.f, 458.f, 28.f);
    UTextBlock* NotesHeading = MakeText(TEXT("NotesHeading"), 17, Muted, false);
    NotesHeading->SetText(LOCTEXT("ConfiguredNotes", "配置备注"));
    PlaceOrderText(NotesHeading, 692.f, 253.f, 458.f, 26.f);
    CustomerNotesTextBlock = MakeText(TEXT("CustomerNotes"), 21, Ink, true);
    PlaceOrderText(CustomerNotesTextBlock, 692.f, 285.f, 458.f, 87.f);
    UTextBlock* HistoryHeading = MakeText(TEXT("HistoryHeading"), 22, Teal, false);
    HistoryHeading->SetText(LOCTEXT("RecentSessionHistory", "最近交付 · 本局"));
    PlaceOrderText(HistoryHeading, 692.f, 385.f, 458.f, 34.f);
    EmptyHistoryTextBlock = MakeText(TEXT("EmptyHistory"), 22, Muted, true);
    EmptyHistoryTextBlock->SetText(LOCTEXT("NoHistory", "暂无本局交付"));
    PlaceOrderText(EmptyHistoryTextBlock, 692.f, 430.f, 458.f, 62.f);
    for (int32 Index = 0; Index < MaxHistoryRows; ++Index)
    {
        USizeBox* Row = WidgetTree->ConstructWidget<USizeBox>();
        Row->SetHeightOverride(88.f);
        Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        UCanvasPanel* Record = MakeCanvas(FName(*FString::Printf(TEXT("HistoryRecord%d"), Index)));
        Row->SetContent(Record);
        Place(OrderPage, Row, FVector2D(692.f, 430.f + Index * 92.f) - PageOrigin, FVector2D(458.f, 88.f));
        UTextBlock* Name = MakeText(FName(*FString::Printf(TEXT("HistoryName%d"), Index)), 22, Ink, false);
        UTextBlock* Volume = MakeText(FName(*FString::Printf(TEXT("HistoryVolume%d"), Index)), 22, Muted, false);
        Place(Record, Name, FVector2D::ZeroVector, FVector2D(458.f, 36.f));
        Place(Record, Volume, FVector2D(0.f, 39.f), FVector2D(458.f, 36.f));
        Place(Record, MakeBorder(NAME_None, Line, false), FVector2D(0.f, 86.f), FVector2D(458.f, 1.f));
        HistoryRows.Add(Row);
        HistoryNameTextBlocks.Add(Name);
        HistoryVolumeTextBlocks.Add(Volume);
    }

    UCanvasPanel* MenuPage = MakeCanvas(TEXT("RecipeMenuPage"));
    UWidgetSwitcherSlot* MenuSlot = CastChecked<UWidgetSwitcherSlot>(PageSwitcher->AddChild(MenuPage));
    MenuSlot->SetHorizontalAlignment(HAlign_Fill);
    MenuSlot->SetVerticalAlignment(VAlign_Fill);
    UCanvasPanel* IndexColumn = MakeCanvas(TEXT("MenuIndexColumn"));
    UCanvasPanel* Details = MakeCanvas(TEXT("MenuDetailsColumn"));
    Place(MenuPage, IndexColumn, FVector2D::ZeroVector, FVector2D(300.f, 662.f));
    Place(MenuPage, Details, FVector2D(300.f, 0.f), FVector2D(900.f, 662.f));
    Place(IndexColumn, MakeBorder(TEXT("MenuColumnDivider"), Line, false), FVector2D(299.f, 0.f), FVector2D(1.f, 662.f));
    UTextBlock* Heading = MakeText(TEXT("MenuHeading"), 17, Muted, false);
    Heading->SetText(LOCTEXT("RecipeMenuHeading", "饮品索引"));
    Place(IndexColumn, Heading, FVector2D(24.f, 16.f), FVector2D(180.f, 26.f));
    UTextBlock* IndexHint = MakeText(TEXT("MenuIndexHint"), 17, Muted, false);
    IndexHint->SetText(LOCTEXT("MenuIndexAlphabet", "A–Z"));
    IndexHint->SetJustification(ETextJustify::Right);
    Place(IndexColumn, IndexHint, FVector2D(210.f, 16.f), FVector2D(68.f, 26.f));
    const FText Filters[] = { LOCTEXT("FilterAll", "全部"), LOCTEXT("FilterCoffee", "咖啡"), LOCTEXT("FilterCocktail", "鸡尾酒") };
    for (int32 Index = 0; Index < 3; ++Index)
    {
        MakeButton(IndexColumn, 10 + Index, FName(*FString::Printf(TEXT("MenuFilterButton%d"), Index)),
            FName(*FString::Printf(TEXT("MenuFilterLabel%d"), Index)), Filters[Index], 17, PageOrigin, false);
    }
    for (int32 Index = 0; Index < MaxMenuRows; ++Index)
    {
        MenuIndexLabels.Add(MakeButton(IndexColumn, 100 + Index, FName(*FString::Printf(TEXT("MenuIndexButton%d"), Index)),
            FName(*FString::Printf(TEXT("MenuIndex%d"), Index)), FText::GetEmpty(), 22, PageOrigin, true));
    }
    UTextBlock* MenuNote = MakeText(TEXT("MenuNote"), 16, Muted, true);
    MenuNote->SetText(LOCTEXT("MenuDataOnly", "点饮品，查看配方。\n查配方不会更换当前订单。"));
    Place(IndexColumn, MenuNote, FVector2D(31.f, 596.f), FVector2D(247.f, 54.f));

    MenuTitle = MakeText(TEXT("MenuDrinkTitle"), 36, Ink, false);
    MenuTitle->SetText(LOCTEXT("NoMenuRecipe", "暂无可用配方"));
    Place(Details, MenuTitle, FVector2D(30.f, 22.f), FVector2D(674.f, 53.f));
    MenuIndexBadge = MakeText(TEXT("MenuIndexBadge"), 20, Teal, false);
    MenuIndexBadge->SetJustification(ETextJustify::Right);
    Place(Details, MenuIndexBadge, FVector2D(720.f, 31.f), FVector2D(150.f, 30.f));
    MenuDescription = MakeText(TEXT("MenuDrinkDescription"), 21, Muted, true);
    Place(Details, MenuDescription, FVector2D(30.f, 78.f), FVector2D(840.f, 37.f));

    UCanvasPanel* CupCard = MakeCard(Details, TEXT("MenuCupCard"), FVector2D(30.f, 122.f), FVector2D(312.f, 151.f));
    MenuCupImage = MakeImage(TEXT("MenuCupIcon"));
    Place(CupCard, MenuCupImage, FVector2D(10.f, 12.f), FVector2D(128.f, 128.f));
    UTextBlock* CupHeading = MakeText(TEXT("MenuCupHeading"), 15, Muted, false);
    CupHeading->SetText(LOCTEXT("SpecifiedCup", "指定杯型"));
    Place(CupCard, CupHeading, FVector2D(145.f, 17.f), FVector2D(157.f, 23.f));
    MenuCupName = MakeText(TEXT("MenuCupName"), 22, Ink, true);
    Place(CupCard, MenuCupName, FVector2D(145.f, 43.f), FVector2D(157.f, 58.f));
    MenuMetadata = MakeText(TEXT("MenuCupVolume"), 16, Muted, true);
    Place(CupCard, MenuMetadata, FVector2D(145.f, 105.f), FVector2D(157.f, 38.f));
    UCanvasPanel* IceCard = MakeCard(Details, TEXT("MenuIceCard"), FVector2D(354.f, 122.f), FVector2D(282.f, 151.f));
    MenuIceImage = MakeImage(TEXT("MenuIceIcon"));
    Place(IceCard, MenuIceImage, FVector2D(8.f, 25.f), FVector2D(94.f, 94.f));
    UTextBlock* IceHeading = MakeText(TEXT("MenuIceHeading"), 15, Muted, false);
    IceHeading->SetText(LOCTEXT("CupIceRequirement", "杯内冰量"));
    Place(IceCard, IceHeading, FVector2D(110.f, 20.f), FVector2D(160.f, 23.f));
    MenuIceLabel = MakeText(TEXT("MenuIceCount"), 23, Ink, false);
    Place(IceCard, MenuIceLabel, FVector2D(110.f, 52.f), FVector2D(160.f, 36.f));
    MenuIceHint = MakeText(TEXT("MenuIceHint"), 16, Muted, true);
    Place(IceCard, MenuIceHint, FVector2D(110.f, 101.f), FVector2D(160.f, 40.f));
    UCanvasPanel* TotalCard = MakeCard(Details, TEXT("MenuTotalCard"), FVector2D(648.f, 122.f), FVector2D(222.f, 151.f));
    UTextBlock* TotalHeading = MakeText(TEXT("MenuTotalHeading"), 16, Muted, false);
    TotalHeading->SetText(LOCTEXT("LiquidTotal", "液体总量"));
    Place(TotalCard, TotalHeading, FVector2D(18.f, 20.f), FVector2D(186.f, 25.f));
    MenuTotal = MakeText(TEXT("MenuTargetVolume"), 35, Teal, false);
    Place(TotalCard, MenuTotal, FVector2D(18.f, 52.f), FVector2D(186.f, 49.f));
    MenuTotalHint = MakeText(TEXT("MenuTotalHint"), 16, Muted, false);
    Place(TotalCard, MenuTotalHint, FVector2D(18.f, 113.f), FVector2D(186.f, 25.f));

    MenuIngredientHeading = MakeText(TEXT("MenuIngredientHeading"), 19, Ink, false);
    Place(Details, MenuIngredientHeading, FVector2D(30.f, 289.f), FVector2D(252.f, 29.f));
    MenuIngredientLines = MakeText(TEXT("MenuIngredientsML"), 15, Muted, false);
    MenuIngredientLines->SetJustification(ETextJustify::Right);
    Place(Details, MenuIngredientLines, FVector2D(290.f, 293.f), FVector2D(580.f, 24.f));
    for (int32 Index = 0; Index < MaxIngredientCards; ++Index)
    {
        UCanvasPanel* IngredientCard = MakeCard(Details, FName(*FString::Printf(TEXT("MenuIngredientCard%d"), Index)),
            FVector2D(30.f + Index * 284.f, 330.f), FVector2D(272.f, 214.f));
        UTextBlock* Number = MakeText(FName(*FString::Printf(TEXT("MenuIngredientNumber%d"), Index)), 17, Muted, false);
        Number->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Index + 1)));
        Place(IngredientCard, Number, FVector2D(13.f, 10.f), FVector2D(38.f, 27.f));
        UImage* Image = MakeImage(FName(*FString::Printf(TEXT("MenuIngredientIcon%d"), Index)));
        Place(IngredientCard, Image, FVector2D(72.f, 8.f), FVector2D(128.f, 128.f));
        UTextBlock* Name = MakeText(FName(*FString::Printf(TEXT("MenuIngredientName%d"), Index)), 22, Ink, false);
        Name->SetJustification(ETextJustify::Center);
        Place(IngredientCard, Name, FVector2D(12.f, 140.f), FVector2D(248.f, 32.f));
        UTextBlock* Volume = MakeText(FName(*FString::Printf(TEXT("MenuIngredientVolume%d"), Index)), 29, Teal, false);
        Volume->SetJustification(ETextJustify::Center);
        Place(IngredientCard, Volume, FVector2D(12.f, 175.f), FVector2D(248.f, 38.f));
        IngredientCard->SetVisibility(ESlateVisibility::Collapsed);
        MenuIngredientCards.Add(IngredientCard);
        MenuIngredientImages.Add(Image);
        MenuIngredientNames.Add(Name);
        MenuIngredientVolumes.Add(Volume);
    }
    MenuMethod = MakeText(TEXT("MenuPourMethod"), 18, Muted, true);
    Place(Details, MenuMethod, FVector2D(30.f, 552.f), FVector2D(840.f, 30.f));
    Place(Details, MakeBorder(TEXT("MenuFooterDivider"), Line, false), FVector2D(30.f, 590.f), FVector2D(840.f, 1.f));
    MenuTolerance = MakeText(TEXT("MenuAcceptanceRange"), 16, Muted, true);
    Place(Details, MenuTolerance, FVector2D(30.f, 595.f), FVector2D(840.f, 42.f));
    MenuFooter = MakeText(TEXT("MenuCurrentOrderHint"), 15, Muted, false);
    Place(Details, MenuFooter, FVector2D(30.f, 640.f), FVector2D(840.f, 22.f));
    PageSwitcher->SetActiveWidgetIndex(0);
    SynchronizePresentation();
}

void UOrderHologramWidget::ApplyPresentation(const AOrderHologramActor* Source)
{
    Initialize();
    BuildWidgetTree();

    PresentedStatus = LOCTEXT("NoOrder", "暂无当前订单");
    PresentedRecipeName = LOCTEXT("NoRecipe", "暂无配方");
    PresentedTarget = FText::GetEmpty();
    PresentedIngredients = FText::GetEmpty();
    PresentedTolerance = FText::GetEmpty();
    PresentedCustomerName = LOCTEXT("NoCustomerName", "未配置客人名");
    PresentedCustomerId = FText::GetEmpty();
    PresentedCustomerNotes = LOCTEXT("NoNotes", "暂无配置备注");
    PresentedHistoryNames.Reset();
    PresentedHistoryVolumes.Reset();

    if (IsValid(Source))
    {
        if (Source->IsDelivered())
        {
            PresentedStatus = LOCTEXT("Delivered", "已交付");
        }
        else if (Source->IsPending())
        {
            PresentedStatus = LOCTEXT("Pending", "待制作 / 待交付");
        }

        if (!Source->CustomerName.IsEmpty())
        {
            PresentedCustomerName = Source->CustomerName;
        }
        if (!Source->CustomerId.IsNone())
        {
            PresentedCustomerId = FText::Format(LOCTEXT("CustomerIdentifier", "ID：{0}"), FText::FromName(Source->CustomerId));
        }
        TArray<FString> Notes;
        for (const FText& Note : Source->CustomerNotes)
        {
            if (!Note.IsEmpty())
            {
                Notes.Add(Note.ToString());
            }
        }
        if (!Notes.IsEmpty())
        {
            PresentedCustomerNotes = FText::FromString(FString::Join(Notes, TEXT("\n")));
        }

        const UDrinkRecipe* Recipe = Source->GetCurrentRecipe();
        if (IsValid(Recipe))
        {
            PresentedRecipeName = Recipe->DisplayName.IsEmpty() ? FText::FromName(Recipe->RecipeId) : Recipe->DisplayName;
            double MidpointSum = 0.0;
            double SmallestPositiveMidpoint = TNumericLimits<double>::Max();
            // 这里只保护显示运算；是否验收仍由配方 Evaluate 裁决。
            bool bValidRecipe = FMath::IsFinite(Recipe->TargetVolumeML) && Recipe->TargetVolumeML > 0.0
                && FMath::IsFinite(Recipe->MinVolumeML) && FMath::IsFinite(Recipe->MaxVolumeML)
                && !Recipe->Rules.IsEmpty();
            for (const FDrinkIngredientRule& Rule : Recipe->Rules)
            {
                if (!FMath::IsFinite(Rule.MinFraction) || !FMath::IsFinite(Rule.MaxFraction)
                    || Rule.MinFraction < 0.0 || Rule.MinFraction > Rule.MaxFraction || Rule.MaxFraction > 1.0)
                {
                    bValidRecipe = false;
                    break;
                }
                const double Midpoint = (Rule.MinFraction + Rule.MaxFraction) * 0.5;
                MidpointSum += Midpoint;
                if (Midpoint > 0.0)
                {
                    SmallestPositiveMidpoint = FMath::Min(SmallestPositiveMidpoint, Midpoint);
                }
            }
            bValidRecipe = bValidRecipe && MidpointSum > 0.0;
            if (bValidRecipe)
            {
                PresentedTarget = FText::Format(LOCTEXT("Target", "目标总量  {0} mL"), NumberText(Recipe->TargetVolumeML));
                TArray<FString> Ingredients;
                TArray<FString> RatioNames;
                TArray<FString> RatioValues;
                TArray<FString> Tolerances;
                Tolerances.Add(LOCTEXT("ToleranceHeading", "验收范围（与配方同源）").ToString());
                Tolerances.Add(FText::Format(LOCTEXT("VolumeTolerance", "总量  {0}–{1} mL"),
                    NumberText(Recipe->MinVolumeML), NumberText(Recipe->MaxVolumeML)).ToString());
                for (const FDrinkIngredientRule& Rule : Recipe->Rules)
                {
                    const double Midpoint = (Rule.MinFraction + Rule.MaxFraction) * 0.5;
                    const double SuggestedML = Recipe->TargetVolumeML * (Midpoint / MidpointSum);
                    Ingredients.Add(FText::Format(LOCTEXT("IngredientAmount", "{0}（{1}） 约 {2} mL"),
                        IngredientName(Rule.IngredientId), FText::FromName(Rule.IngredientId), NumberText(SuggestedML)).ToString());
                    RatioNames.Add(IngredientName(Rule.IngredientId).ToString());
                    RatioValues.Add(NumberText(Midpoint / SmallestPositiveMidpoint).ToString());
                    Tolerances.Add(FText::Format(LOCTEXT("FractionTolerance", "{0}（{1}） {2}–{3}%"),
                        IngredientName(Rule.IngredientId), FText::FromName(Rule.IngredientId),
                        NumberText(Rule.MinFraction * 100.0), NumberText(Rule.MaxFraction * 100.0)).ToString());
                }
                Ingredients.Add(FString::Join(RatioNames, TEXT(" : ")) + TEXT(" = ") + FString::Join(RatioValues, TEXT(":")));
                PresentedIngredients = FText::FromString(FString::Join(Ingredients, TEXT("\n")));
                PresentedTolerance = FText::FromString(FString::Join(Tolerances, TEXT("\n")));
            }
            else
            {
                PresentedIngredients = LOCTEXT("InvalidRecipe", "当前配方配置无效");
            }
        }

        // 记录来自当前 Actor 的本局账本，倒序仅取最近三条，不推算次数或评价。
        const TArray<FHologramDeliveryRecord>& Records = Source->GetDeliveryRecords();
        for (int32 Index = Records.Num() - 1; Index >= 0 && PresentedHistoryNames.Num() < MaxHistoryRows; --Index)
        {
            const FHologramDeliveryRecord& Record = Records[Index];
            const FText Name = Record.DrinkName.IsEmpty() ? FText::FromName(Record.RecipeId) : Record.DrinkName;
            PresentedHistoryNames.Add(FText::Format(LOCTEXT("DeliveredRecord", "{0} · 已交付"), Name));
            PresentedHistoryVolumes.Add(FMath::IsFinite(Record.VolumeML) && Record.VolumeML >= 0.0
                ? FText::Format(LOCTEXT("ActualVolume", "实际 {0} mL"), NumberText(Record.VolumeML))
                : LOCTEXT("MissingActualVolume", "实际份量未记录"));
        }
    }

    TArray<FString> OrderLines;
    OrderLines.Add(PresentedStatus.ToString());
    if (IsValid(Source) && IsValid(Source->GetCurrentRecipe()))
    {
        OrderLines.Add(PresentedRecipeName.ToString());
        if (!PresentedTarget.IsEmpty())
        {
            OrderLines.Add(PresentedTarget.ToString());
        }
        if (!PresentedIngredients.IsEmpty())
        {
            OrderLines.Add(PresentedIngredients.ToString());
        }
        if (!PresentedTolerance.IsEmpty())
        {
            OrderLines.Add(PresentedTolerance.ToString());
        }
    }
    OrderText = FText::FromString(FString::Join(OrderLines, TEXT("\n")));

    TArray<FString> HistoryLines;
    for (int32 Index = 0; Index < PresentedHistoryNames.Num(); ++Index)
    {
        HistoryLines.Add(PresentedHistoryNames[Index].ToString());
        HistoryLines.Add(PresentedHistoryVolumes[Index].ToString());
    }
    HistoryText = HistoryLines.IsEmpty() ? LOCTEXT("NoHistory", "暂无本局交付")
        : FText::FromString(FString::Join(HistoryLines, TEXT("\n")));

    const bool bValidSource = IsValid(Source);
    const bool bMenuMode = bValidSource && Source->IsRecipeMenuMode();
    const UDrinkMenuData* Menu = bValidSource && IsValid(Source->RecipeMenu) ? Source->RecipeMenu.Get() : nullptr;
    const UDrinkRecipe* CurrentRecipe = bValidSource ? Source->GetCurrentRecipe() : nullptr;
    const FDrinkMenuEntry* CurrentMenuEntry = nullptr;
    if (Menu && IsValid(CurrentRecipe))
    {
        for (const FDrinkMenuEntry& Entry : Menu->Entries)
        {
            if (Entry.Recipe.Get() == CurrentRecipe)
            {
                CurrentMenuEntry = &Entry;
                break;
            }
        }
    }
    if (PageSwitcher) PageSwitcher->SetActiveWidgetIndex(bMenuMode ? 1 : 0);
    SetTexture(OrderCupImage, CurrentMenuEntry ? CurrentMenuEntry->CupIcon : nullptr);
    if (OrderCupLabel) OrderCupLabel->SetText(CurrentMenuEntry ? CurrentMenuEntry->CupName : FText::GetEmpty());
    if (OrderIceLabel) OrderIceLabel->SetText(CurrentMenuEntry ? IceText(CurrentMenuEntry->Recipe->ExpectedIceCount) : FText::GetEmpty());

    // Actor 已经完成过滤和实际索引映射，Widget 不按分类再次过滤或排序。
    const TArray<int32> VisibleIndices = Menu ? Source->GetVisibleMenuIndices() : TArray<int32>();
    const int32 Filter = Menu ? Source->GetMenuFilter() : INDEX_NONE;
    const FDrinkMenuEntry* SelectedEntry = Menu ? Source->GetSelectedMenuEntry() : nullptr;
    const bool bAcceptInput = bValidSource && Source->IsPanelOpen();
    for (URecipeScreenButton* Button : ScreenButtons)
    {
        Button->Host = bValidSource ? const_cast<AOrderHologramActor*>(Source) : nullptr;
        bool bEnabled = bAcceptInput;
        bool bSelected = false;
        if (Button->Action == 0) bSelected = !bMenuMode;
        else if (Button->Action == 1)
        {
            bEnabled = bEnabled && Menu != nullptr;
            bSelected = bMenuMode;
        }
        else if (Button->Action == 2) bEnabled = bEnabled && CurrentMenuEntry != nullptr;
        else if (Button->Action >= 10 && Button->Action <= 12)
        {
            bEnabled = bEnabled && Menu != nullptr;
            bSelected = Filter == Button->Action - 10;
        }
        else if (Button->Action >= 100 && Button->Action < 100 + MaxMenuRows)
        {
            const int32 Row = Button->Action - 100;
            const bool bVisible = Menu && VisibleIndices.IsValidIndex(Row) && Menu->Entries.IsValidIndex(VisibleIndices[Row]);
            Button->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
            bEnabled = bEnabled && bVisible;
            if (bVisible)
            {
                const FDrinkMenuEntry& Entry = Menu->Entries[VisibleIndices[Row]];
                bSelected = &Entry == SelectedEntry;
                MenuIndexLabels[Row]->SetText(FText::Format(LOCTEXT("MenuIndexCaption", "{0}  {1}"),
                    FText::FromName(Entry.PinyinIndex), Entry.DisplayName));
            }
            else
            {
                MenuIndexLabels[Row]->SetText(FText::GetEmpty());
            }
        }
        Button->SetIsEnabled(bEnabled);
        SetScreenButtonStyle(Button, bSelected);
    }

    // 切换、清空数据或选择无效配方时，不能留下上一杯的图和 mL。
    MenuText = FText::GetEmpty();
    if (MenuTitle) MenuTitle->SetText(LOCTEXT("NoMenuRecipe", "暂无可用配方"));
    UTextBlock* MenuFields[] = { MenuIndexBadge.Get(), MenuMetadata.Get(), MenuDescription.Get(), MenuCupName.Get(),
        MenuIceLabel.Get(), MenuIceHint.Get(), MenuTotal.Get(), MenuTotalHint.Get(), MenuIngredientHeading.Get(),
        MenuIngredientLines.Get(), MenuMethod.Get(), MenuTolerance.Get(), MenuFooter.Get() };
    for (UTextBlock* Field : MenuFields)
    {
        if (Field) Field->SetText(FText::GetEmpty());
    }
    SetTexture(MenuCupImage, nullptr);
    SetTexture(MenuIceImage, nullptr);
    for (int32 Index = 0; Index < MenuIngredientCards.Num(); ++Index)
    {
        MenuIngredientCards[Index]->SetVisibility(ESlateVisibility::Collapsed);
        SetTexture(MenuIngredientImages[Index], nullptr);
        MenuIngredientNames[Index]->SetText(FText::GetEmpty());
        MenuIngredientVolumes[Index]->SetText(FText::GetEmpty());
    }

    if (SelectedEntry && IsValid(SelectedEntry->Recipe))
    {
        const UDrinkRecipe* Recipe = SelectedEntry->Recipe.Get();
        const FText Title = SelectedEntry->DisplayName;
        const FText Index = FText::FromName(SelectedEntry->PinyinIndex);
        const FText Ice = IceText(Recipe->ExpectedIceCount);
        const FText Total = FMath::IsFinite(Recipe->TargetVolumeML) && Recipe->TargetVolumeML > 0.0
            ? FText::Format(LOCTEXT("MenuTotal", "{0} mL"), NumberText(Recipe->TargetVolumeML))
            : LOCTEXT("InvalidMenuTarget", "总量配置无效");
        const ALiquidContainerActor* Cup = Recipe->AcceptedCupClass
            ? Recipe->AcceptedCupClass->GetDefaultObject<ALiquidContainerActor>() : nullptr;
        const bool bValidCapacity = IsValid(Cup) && FMath::IsFinite(Cup->MaxVolumeML) && Cup->MaxVolumeML > 0.f;
        const FText Capacity = bValidCapacity
            ? FText::Format(LOCTEXT("MenuCapacity", "容量 {0} mL"), NumberText(Cup->MaxVolumeML))
            : LOCTEXT("MenuCapacityUnconfigured", "杯容量未配置");
        TArray<FString> IngredientLines;
        TArray<FString> IngredientSummary;
        for (int32 IngredientIndex = 0; IngredientIndex < SelectedEntry->Ingredients.Num(); ++IngredientIndex)
        {
            const FDrinkMenuIngredient& Ingredient = SelectedEntry->Ingredients[IngredientIndex];
            const FText Name = UDrinkMenuData::IngredientDisplayName(Ingredient.IngredientId);
            const FText Volume = FMath::IsFinite(Ingredient.VolumeML) && Ingredient.VolumeML >= 0.0
                ? FText::Format(LOCTEXT("MenuIngredientVolume", "{0} mL"), NumberText(Ingredient.VolumeML))
                : LOCTEXT("InvalidMenuIngredientVolume", "用量配置无效");
            IngredientLines.Add(FText::Format(LOCTEXT("MenuIngredientLine", "{0}  {1}    {2}"),
                FText::AsNumber(IngredientIndex + 1), Name, Volume).ToString());
            IngredientSummary.Add(Name.ToString() + TEXT(" ") + Volume.ToString());
            if (MenuIngredientCards.IsValidIndex(IngredientIndex))
            {
                MenuIngredientCards[IngredientIndex]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                MenuIngredientNames[IngredientIndex]->SetText(Name);
                MenuIngredientVolumes[IngredientIndex]->SetText(Volume);
                SetTexture(MenuIngredientImages[IngredientIndex], Ingredient.Icon);
            }
        }

        // 范围取原配方规则，并按指定杯的真实 CDO 容量截断上限。
        double Maximum = Recipe->MaxVolumeML;
        if (bValidCapacity && FMath::IsFinite(Maximum)) Maximum = FMath::Min(Maximum, static_cast<double>(Cup->MaxVolumeML));
        const bool bValidRange = FMath::IsFinite(Recipe->MinVolumeML) && FMath::IsFinite(Maximum)
            && Recipe->MinVolumeML >= 0.0 && Maximum >= Recipe->MinVolumeML;
        TArray<FString> Ranges;
        Ranges.Add(bValidRange
            ? FText::Format(LOCTEXT("MenuVolumeRange", "验收 {0}–{1} mL"), NumberText(Recipe->MinVolumeML), NumberText(Maximum)).ToString()
            : LOCTEXT("InvalidMenuRange", "验收总量配置无效").ToString());
        for (const FDrinkIngredientRule& Rule : Recipe->Rules)
        {
            const bool bValidFraction = FMath::IsFinite(Rule.MinFraction) && FMath::IsFinite(Rule.MaxFraction)
                && Rule.MinFraction >= 0.0 && Rule.MinFraction <= Rule.MaxFraction && Rule.MaxFraction <= 1.0;
            Ranges.Add(bValidFraction
                ? FText::Format(LOCTEXT("MenuFractionRange", "{0} {1}–{2}%"),
                    UDrinkMenuData::IngredientDisplayName(Rule.IngredientId), NumberText(Rule.MinFraction * 100.0), NumberText(Rule.MaxFraction * 100.0)).ToString()
                : FText::Format(LOCTEXT("InvalidMenuFraction", "{0} 比例配置无效"), UDrinkMenuData::IngredientDisplayName(Rule.IngredientId)).ToString());
        }
        Ranges.Add(LOCTEXT("NoExtraIngredients", "不允许额外原料").ToString());
        const FText Range = FText::FromString(FString::Join(Ranges, TEXT(" · ")));
        const FText IceHint = Recipe->ExpectedIceCount > 0
            ? LOCTEXT("IcePresenceHint", "有冰即可，不限块数")
            : (Recipe->ExpectedIceCount == 0 ? LOCTEXT("PourWithoutIce", "直接加入原料") : FText::GetEmpty());
        const FText Method = Recipe->ExpectedIceCount > 0
            ? LOCTEXT("MenuMethodWithIce", "取指定杯并加冰，再依次倒入；看杯旁 mL，做好后放到交付位置并松手。")
            : LOCTEXT("MenuMethodWithoutIce", "取指定杯，依次倒入；看杯旁 mL，做好后放到交付位置并松手。");
        const FText CurrentName = CurrentMenuEntry ? CurrentMenuEntry->DisplayName
            : (IsValid(CurrentRecipe) ? PresentedRecipeName : LOCTEXT("NoOrder", "暂无当前订单"));
        FString Footer = FText::Format(LOCTEXT("MenuFooterCurrent", "当前订单：{0} · 查配方不会换单"), CurrentName).ToString();
        // 只引用本局账本的真实实际 mL；查不到记录时不拿配方用量冒充交付量。
        for (int32 RecordIndex = Source->GetDeliveryRecords().Num() - 1; RecordIndex >= 0; --RecordIndex)
        {
            const FHologramDeliveryRecord& Record = Source->GetDeliveryRecords()[RecordIndex];
            if (Record.RecipeId == Recipe->RecipeId)
            {
                Footer += TEXT(" · ") + (FMath::IsFinite(Record.VolumeML) && Record.VolumeML >= 0.0
                    ? FText::Format(LOCTEXT("MenuActualVolume", "本局最近实际 {0} mL"), NumberText(Record.VolumeML)).ToString()
                    : LOCTEXT("MissingActualVolume", "实际份量未记录").ToString());
                break;
            }
        }
        if (MenuTitle) MenuTitle->SetText(Title);
        if (MenuIndexBadge) MenuIndexBadge->SetText(Index);
        if (MenuDescription) MenuDescription->SetText(FText::Format(LOCTEXT("MenuCategoryDescription", "{0} · {1}"), SelectedEntry->Category, SelectedEntry->Description));
        if (MenuCupName) MenuCupName->SetText(SelectedEntry->CupName);
        if (MenuMetadata) MenuMetadata->SetText(Capacity);
        if (MenuIceLabel) MenuIceLabel->SetText(Ice);
        if (MenuIceHint) MenuIceHint->SetText(IceHint);
        if (MenuTotal) MenuTotal->SetText(Total);
        if (MenuTotalHint) MenuTotalHint->SetText(LOCTEXT("LiquidExcludesIce", "液体 mL 不含冰块"));
        if (MenuIngredientHeading) MenuIngredientHeading->SetText(Recipe->ExpectedIceCount > 0
            ? LOCTEXT("IngredientsAfterIce", "加冰后，按顺序倒入") : LOCTEXT("IngredientsInOrder", "按顺序倒入"));
        if (MenuIngredientLines) MenuIngredientLines->SetText(FText::FromString(FString::Join(IngredientSummary, TEXT(" · "))));
        if (MenuMethod) MenuMethod->SetText(Method);
        if (MenuTolerance) MenuTolerance->SetText(Range);
        if (MenuFooter) MenuFooter->SetText(FText::FromString(Footer));
        SetTexture(MenuCupImage, SelectedEntry->CupIcon);
        SetTexture(MenuIceImage, Recipe->ExpectedIceCount > 0 ? Menu->IceIcon : nullptr);
        MenuText = FText::FromString(Index.ToString() + TEXT(" · ") + Title.ToString()
            + TEXT("\n") + SelectedEntry->Category.ToString() + TEXT(" · ") + SelectedEntry->Description.ToString()
            + TEXT("\n") + SelectedEntry->CupName.ToString() + TEXT(" · ") + Capacity.ToString()
            + TEXT("\n") + Ice.ToString() + TEXT(" · ") + IceHint.ToString()
            + TEXT(" · ") + Total.ToString() + TEXT("（液体 mL 不含冰块）")
            + TEXT("\n") + FString::Join(IngredientLines, TEXT("\n"))
            + TEXT("\n") + Method.ToString() + TEXT("\n") + Range.ToString() + TEXT("\n") + Footer);
    }
    SynchronizePresentation();
}

FText UOrderHologramWidget::GetOrderText() const
{
    return OrderText;
}

FText UOrderHologramWidget::GetHistoryText() const
{
    return HistoryText;
}

void UOrderHologramWidget::SynchronizePresentation()
{
    if (StatusTextBlock) StatusTextBlock->SetText(PresentedStatus);
    if (RecipeNameTextBlock) RecipeNameTextBlock->SetText(PresentedRecipeName);
    if (TargetTextBlock) TargetTextBlock->SetText(PresentedTarget);
    if (IngredientsTextBlock) IngredientsTextBlock->SetText(PresentedIngredients);
    if (ToleranceTextBlock) ToleranceTextBlock->SetText(PresentedTolerance);
    if (CustomerNameTextBlock) CustomerNameTextBlock->SetText(PresentedCustomerName);
    if (CustomerIdTextBlock) CustomerIdTextBlock->SetText(PresentedCustomerId);
    if (CustomerNotesTextBlock) CustomerNotesTextBlock->SetText(PresentedCustomerNotes);
    if (EmptyHistoryTextBlock)
    {
        EmptyHistoryTextBlock->SetVisibility(PresentedHistoryNames.IsEmpty()
            ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    for (int32 Index = 0; Index < HistoryRows.Num(); ++Index)
    {
        const bool bVisible = PresentedHistoryNames.IsValidIndex(Index);
        HistoryRows[Index]->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        HistoryNameTextBlocks[Index]->SetText(bVisible ? PresentedHistoryNames[Index] : FText::GetEmpty());
        HistoryVolumeTextBlocks[Index]->SetText(bVisible ? PresentedHistoryVolumes[Index] : FText::GetEmpty());
    }
}

#undef LOCTEXT_NAMESPACE
