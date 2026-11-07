#include "UI/OrderHologramWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Serving/DrinkMenuData.h"
#include "Engine/Font.h"
#include "Fonts/SlateFontInfo.h"
#include "Serving/DrinkRecipe.h"
#include "Liquid/LiquidContainerActor.h"
#include "Serving/OrderHologramActor.h"
#include "Styling/SlateBrush.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OrderHologramWidget"

namespace
{
    constexpr float PanelWidth = 1200.f;
    constexpr float PanelHeight = 750.f;
    constexpr int32 MaxHistoryRows = 3;

    FText NumberText(double Value)
    {
        FNumberFormattingOptions Options;
        Options.SetUseGrouping(false);
        Options.SetMinimumFractionalDigits(0);
        Options.SetMaximumFractionalDigits(2);
        return FText::AsNumber(Value, &Options);
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

UOrderHologramWidget::UOrderHologramWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static ConstructorHelpers::FObjectFinder<UFont> FontAsset(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    PresentationFont = FontAsset.Object;
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);

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

    const FLinearColor Cyan(0.04f, 0.78f, 0.88f, 0.95f);
    const FLinearColor BodyColor(0.008f, 0.015f, 0.025f, 0.98f);
    const FLinearColor Ink(0.92f, 0.96f, 1.f, 1.f);
    const FLinearColor SecondaryInk(0.70f, 0.80f, 0.87f, 1.f);

    const auto MakeText = [this](FName Name, int32 Size, FLinearColor Color, bool bWrap)
    {
        UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        // 字号是每个控件独立的值；仅共享只读的复合字体资产及其回退。
        FSlateFontInfo Font(PresentationFont.Get(), static_cast<float>(Size));
        Text->SetFont(Font);
        Text->SetColorAndOpacity(FSlateColor(Color));
        Text->SetAutoWrapText(bWrap);
        Text->SetLineHeightPercentage(1.05f);
        Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Text->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        return Text;
    };

    FSlateBrush SolidBrush;
    SolidBrush.DrawAs = ESlateBrushDrawType::Image;
    SolidBrush.TintColor = FSlateColor(FLinearColor::White);
    const auto MakeBorder = [this, &SolidBrush](FName Name, FLinearColor Color, FMargin ContentPadding)
    {
        UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
        Border->SetBrush(SolidBrush);
        Border->SetBrushColor(Color);
        Border->SetPadding(ContentPadding);
        Border->SetHorizontalAlignment(HAlign_Fill);
        Border->SetVerticalAlignment(VAlign_Fill);
        return Border;
    };

    const auto AddFixed = [this](UVerticalBox* Parent, UWidget* Child, float Height)
    {
        USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
        Box->SetHeightOverride(Height);
        Box->SetClipping(EWidgetClipping::ClipToBoundsAlways);
        Box->SetContent(Child);
        UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(Box);
        Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        Slot->SetHorizontalAlignment(HAlign_Fill);
        return Box;
    };

    // 20 px/cm：由 Actor 的 WidgetComponent 将此画布映射为 60 x 37.5 cm。
    USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OrderHologramRoot"));
    Root->SetWidthOverride(PanelWidth);
    Root->SetHeightOverride(PanelHeight);
    Root->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    Root->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Root;

    UBorder* Frame = MakeBorder(TEXT("CyanFrame"), Cyan, FMargin(2.f));
    UBorder* Body = MakeBorder(TEXT("OpaqueBody"), BodyColor, FMargin(24.f));
    Root->SetContent(Frame);
    Frame->SetContent(Body);

    UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
    PageSwitcher=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("OrderMenuSwitcher"));
    Body->SetContent(PageSwitcher);PageSwitcher->AddChild(Columns);
    UVerticalBox* MainColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OrderColumn"));
    UVerticalBox* SideColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CustomerColumn"));
    MainColumn->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    SideColumn->SetClipping(EWidgetClipping::ClipToBoundsAlways);

    FSlateChildSize MainSize(ESlateSizeRule::Fill);
    MainSize.Value = 0.65f;
    UHorizontalBoxSlot* MainSlot = Columns->AddChildToHorizontalBox(MainColumn);
    MainSlot->SetSize(MainSize);
    MainSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
    MainSlot->SetVerticalAlignment(VAlign_Fill);

    USizeBox* Divider = WidgetTree->ConstructWidget<USizeBox>();
    Divider->SetWidthOverride(2.f);
    Divider->SetContent(MakeBorder(TEXT("ColumnDivider"), FLinearColor(0.04f, 0.45f, 0.55f, 0.65f), FMargin(0.f)));
    Columns->AddChildToHorizontalBox(Divider)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

    FSlateChildSize SideSize(ESlateSizeRule::Fill);
    SideSize.Value = 0.35f;
    UHorizontalBoxSlot* SideSlot = Columns->AddChildToHorizontalBox(SideColumn);
    SideSlot->SetSize(SideSize);
    SideSlot->SetPadding(FMargin(24.f, 0.f, 0.f, 0.f));
    SideSlot->SetVerticalAlignment(VAlign_Fill);

    UTextBlock* OrderHeading = MakeText(TEXT("OrderHeading"), 48, Ink, false);
    OrderHeading->SetText(LOCTEXT("CurrentOrder", "当前订单"));
    AddFixed(MainColumn, OrderHeading, 80.f);
    StatusTextBlock = MakeText(TEXT("OrderStatus"), 32, Cyan, false);
    AddFixed(MainColumn, StatusTextBlock, 52.f);
    RecipeNameTextBlock = MakeText(TEXT("RecipeName"), 48, Ink, false);
    AddFixed(MainColumn, RecipeNameTextBlock, 80.f);
    TargetTextBlock = MakeText(TEXT("TargetVolume"), 32, Ink, true);
    AddFixed(MainColumn, TargetTextBlock, 52.f);

    IngredientsTextBlock = MakeText(TEXT("OrderIngredients"), 32, Ink, true);
    USizeBox* IngredientsBox = WidgetTree->ConstructWidget<USizeBox>();
    IngredientsBox->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    IngredientsBox->SetContent(IngredientsTextBlock);
    UVerticalBoxSlot* IngredientsSlot = MainColumn->AddChildToVerticalBox(IngredientsBox);
    IngredientsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    IngredientsSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 12.f));
    IngredientsSlot->SetHorizontalAlignment(HAlign_Fill);

    ToleranceTextBlock = MakeText(TEXT("OrderTolerance"), 24, SecondaryInk, true);
    AddFixed(MainColumn, ToleranceTextBlock, 176.f);

    UTextBlock* CustomerHeading = MakeText(TEXT("CustomerHeading"), 48, Ink, false);
    CustomerHeading->SetText(LOCTEXT("CustomerProfile", "客人档案"));
    AddFixed(SideColumn, CustomerHeading, 80.f);
    CustomerNameTextBlock = MakeText(TEXT("CustomerName"), 32, Ink, false);
    AddFixed(SideColumn, CustomerNameTextBlock, 52.f);
    CustomerIdTextBlock = MakeText(TEXT("CustomerId"), 24, SecondaryInk, false);
    AddFixed(SideColumn, CustomerIdTextBlock, 36.f);
    UTextBlock* NotesHeading = MakeText(TEXT("NotesHeading"), 24, SecondaryInk, false);
    NotesHeading->SetText(LOCTEXT("ConfiguredNotes", "配置备注"));
    AddFixed(SideColumn, NotesHeading, 36.f);
    CustomerNotesTextBlock = MakeText(TEXT("CustomerNotes"), 30, Ink, true);
    AddFixed(SideColumn, CustomerNotesTextBlock, 112.f);

    UTextBlock* HistoryHeading = MakeText(TEXT("HistoryHeading"), 30, Cyan, false);
    HistoryHeading->SetText(LOCTEXT("RecentSessionHistory", "最近交付 · 本局"));
    AddFixed(SideColumn, HistoryHeading, 64.f);
    UVerticalBox* HistoryColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    UVerticalBoxSlot* HistorySlot = SideColumn->AddChildToVerticalBox(HistoryColumn);
    HistorySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    HistorySlot->SetHorizontalAlignment(HAlign_Fill);
    HistoryColumn->SetClipping(EWidgetClipping::ClipToBoundsAlways);

    EmptyHistoryTextBlock = MakeText(TEXT("EmptyHistory"), 32, SecondaryInk, true);
    EmptyHistoryTextBlock->SetText(LOCTEXT("NoHistory", "暂无本局交付"));
    AddFixed(HistoryColumn, EmptyHistoryTextBlock, 64.f);
    for (int32 Index = 0; Index < MaxHistoryRows; ++Index)
    {
        UVerticalBox* Record = WidgetTree->ConstructWidget<UVerticalBox>();
        UTextBlock* Name = MakeText(FName(*FString::Printf(TEXT("HistoryName%d"), Index)), 30, Ink, false);
        UTextBlock* Volume = MakeText(FName(*FString::Printf(TEXT("HistoryVolume%d"), Index)), 32, SecondaryInk, false);
        AddFixed(Record, Name, 48.f);
        AddFixed(Record, Volume, 52.f);
        HistoryRows.Add(AddFixed(HistoryColumn, Record, 104.f));
        HistoryNameTextBlocks.Add(Name);
        HistoryVolumeTextBlocks.Add(Volume);
    }

    // Menu uses the exact same 1200x750 panel. Index rows: x28..342,
    // y134..614 at 48px pitch, matching actor screen-touch hit testing.
    auto* MenuColumns=WidgetTree->ConstructWidget<UHorizontalBox>();PageSwitcher->AddChild(MenuColumns);
    auto* IndexColumn=WidgetTree->ConstructWidget<UVerticalBox>();auto* IndexWidth=WidgetTree->ConstructWidget<USizeBox>();IndexWidth->SetWidthOverride(320);IndexWidth->SetContent(IndexColumn);MenuColumns->AddChildToHorizontalBox(IndexWidth)->SetPadding(FMargin(0,0,20,0));
    auto* Heading=MakeText(TEXT("MenuHeading"),36,Cyan,false);Heading->SetText(LOCTEXT("RecipeMenuHeading","配方菜单 · A–Z"));AddFixed(IndexColumn,Heading,64);
    auto* Hint=MakeText(TEXT("MenuIndexHint"),22,SecondaryInk,false);Hint->SetText(LOCTEXT("IndexHint","点索引查看 / 底座 PREV NEXT"));AddFixed(IndexColumn,Hint,44);
    for(int32 I=0;I<10;++I){auto* Label=MakeText(FName(*FString::Printf(TEXT("MenuIndex%d"),I)),24,SecondaryInk,false);MenuIndexLabels.Add(Label);AddFixed(IndexColumn,Label,48);}
    auto* Notes=MakeText(TEXT("MenuNote"),20,SecondaryInk,true);Notes->SetText(LOCTEXT("MenuDataOnly","拼音首字母索引\n只查配方，不更换当前订单"));AddFixed(IndexColumn,Notes,100);
    auto* Details=WidgetTree->ConstructWidget<UVerticalBox>();MenuColumns->AddChildToHorizontalBox(Details)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    MenuTitle=MakeText(TEXT("MenuDrinkTitle"),46,Ink,false);AddFixed(Details,MenuTitle,74);
    MenuMetadata=MakeText(TEXT("MenuCupVolume"),28,Cyan,false);AddFixed(Details,MenuMetadata,48);
    MenuDescription=MakeText(TEXT("MenuDrinkDescription"),26,SecondaryInk,true);AddFixed(Details,MenuDescription,82);
    MenuIngredientLines=MakeText(TEXT("MenuIngredientsML"),34,Ink,true);AddFixed(Details,MenuIngredientLines,180);
    auto* Method=MakeText(TEXT("MenuPourMethod"),24,SecondaryInk,true);Method->SetText(LOCTEXT("Method","按上方顺序逐项倒入；以 mL 和成分比例为准。\n原型无需摇壶 / 拉花 / 分层。"));AddFixed(Details,Method,76);
    MenuTolerance=MakeText(TEXT("MenuAcceptanceRange"),22,SecondaryInk,true);AddFixed(Details,MenuTolerance,128);
    MenuFooter=MakeText(TEXT("MenuCurrentOrderHint"),22,Cyan,true);AddFixed(Details,MenuFooter,104);
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
    if(PageSwitcher)PageSwitcher->SetActiveWidgetIndex(IsValid(Source)&&Source->IsRecipeMenuMode()?1:0);
    MenuText=FText::GetEmpty();
    if(IsValid(Source)&&IsValid(Source->RecipeMenu))
    {
        const UDrinkMenuData* Menu=Source->RecipeMenu;
        for(int32 I=0;I<MenuIndexLabels.Num();++I)
        {
            const bool Valid=Menu->Entries.IsValidIndex(I);
            MenuIndexLabels[I]->SetText(Valid?FText::FromString(Menu->Entries[I].PinyinIndex.ToString()+TEXT("  ")+Menu->Entries[I].DisplayName.ToString()):FText::GetEmpty());
            MenuIndexLabels[I]->SetColorAndOpacity(FSlateColor(I==Source->GetSelectedMenuIndex()?FLinearColor(.04,.78,.88,1):FLinearColor(.7,.8,.87,1)));
        }
        if(const auto* E=Source->GetSelectedMenuEntry();E&&IsValid(E->Recipe))
        {
            const auto* R=E->Recipe.Get();
            const FText Title=FText::FromString(E->PinyinIndex.ToString()+TEXT(" · ")+E->DisplayName.ToString());
            const FText Meta=FText::FromString(E->Category.ToString()+TEXT("  /  ")+E->CupName.ToString()+TEXT("  /  ")+NumberText(R->TargetVolumeML).ToString()+TEXT(" mL"));
            TArray<FString> Lines;for(int32 I=0;I<E->Ingredients.Num();++I){const auto& Ingredient=E->Ingredients[I];Lines.Add(FString::Printf(TEXT("%d  %s    %s mL"),I+1,*UDrinkMenuData::IngredientDisplayName(Ingredient.IngredientId).ToString(),*NumberText(Ingredient.VolumeML).ToString()));}
            double Max=R->MaxVolumeML;if(R->AcceptedCupClass)Max=FMath::Min(Max,static_cast<double>(R->AcceptedCupClass->GetDefaultObject<ALiquidContainerActor>()->MaxVolumeML));
            FString Range=TEXT("配方校验：")+NumberText(R->MinVolumeML).ToString()+TEXT("–")+NumberText(Max).ToString()+TEXT(" mL；不允许额外原料。");
            for(const auto& Rule:R->Rules)Range+=TEXT("\n")+UDrinkMenuData::IngredientDisplayName(Rule.IngredientId).ToString()+TEXT(" ")+NumberText(Rule.MinFraction*100).ToString()+TEXT("–")+NumberText(Rule.MaxFraction*100).ToString()+TEXT("%");
            FString Footer=FString::Printf(TEXT("%02d / %02d    MENU 返回订单 · PREV / NEXT 翻查"),Source->GetSelectedMenuIndex()+1,Menu->Entries.Num());
            if(IsValid(Source->GetCurrentRecipe()))Footer+=TEXT("\n当前订单：")+Source->GetCurrentRecipe()->DisplayName.ToString()+(Source->IsPending()?TEXT(" · 待交付"):TEXT(" · 已结束"));
            if(MenuTitle)MenuTitle->SetText(Title);if(MenuMetadata)MenuMetadata->SetText(Meta);if(MenuDescription)MenuDescription->SetText(E->Description);if(MenuIngredientLines)MenuIngredientLines->SetText(FText::FromString(FString::Join(Lines,TEXT("\n"))));if(MenuTolerance)MenuTolerance->SetText(FText::FromString(Range));if(MenuFooter)MenuFooter->SetText(FText::FromString(Footer));
            MenuText=FText::FromString(Title.ToString()+TEXT("\n")+Meta.ToString()+TEXT("\n")+FString::Join(Lines,TEXT("\n"))+TEXT("\n")+Range);
        }
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
