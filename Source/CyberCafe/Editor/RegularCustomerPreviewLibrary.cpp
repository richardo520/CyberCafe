#include "Editor/RegularCustomerPreviewLibrary.h"
#if WITH_EDITOR
#include "UI/RegularCustomerWidget.h"
#include "Dialogue/RegularDialogueSettings.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Interfaces/ISlateRHIRendererModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/StrongObjectPtr.h"

bool URegularCustomerPreviewLibrary::RenderChoicePreview()
{
    if (!GEditor) return false;
    if (!FSlateApplication::IsInitialized())
        FSlateApplication::InitializeAsStandaloneApplication(
            FModuleManager::LoadModuleChecked<ISlateRHIRendererModule>(TEXT("SlateRHIRenderer")).CreateSlateRHIRenderer());
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) { UE_LOG(LogTemp, Warning, TEXT("[RegularPreview] No editor world.")); return false; }
    TStrongObjectPtr<URegularCustomerWidget> Widget(CreateWidget<URegularCustomerWidget>(World, URegularCustomerWidget::StaticClass()));
    if (!Widget.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("[RegularPreview] Widget creation failed.")); return false; }
    Widget->Show(FText::FromString(TEXT("夜班常客")),
        FText::FromString(TEXT("又是一个长夜。能给我做杯冰拿铁吗？咖啡和牛奶平衡一点，要有冰。")),
        FText::FromString(TEXT("空手指向选项，轻扣扳机回应。")),
        {FText::FromString(TEXT("今天想聊聊？")), FText::FromString(TEXT("先给你做一杯。"))});
    TSharedRef<SWidget> Slate = Widget->TakeWidget();
    Slate->SlatePrepass(1);
    FWidgetRenderer Renderer(true);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(Renderer.DrawWidget(Slate, FVector2D(960, 560)));
    FlushRenderingCommands();
    if (!Target.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("[RegularPreview] No render target.")); return false; }
    FBufferArchive PNG;
    if (!FImageUtils::ExportRenderTarget2DAsPNG(Target.Get(), PNG)) { UE_LOG(LogTemp, Warning, TEXT("[RegularPreview] PNG export failed.")); return false; }
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("RegularCustomer");
    IFileManager::Get().MakeDirectory(*Directory, true);
    if (!FFileHelper::SaveArrayToFile(PNG, *(Directory / TEXT("choice-preview.png")))) return false;
    // Synthetic diagnostic values for visual review only. Never invokes an API.
    const auto ExportLink = [&](const TCHAR* Filename, const FRegularDialogueLinkView& View)
    {
        Widget->ShowLink(View, true);
        Slate->SlatePrepass(1);
        TStrongObjectPtr<UTextureRenderTarget2D> LinkTarget(Renderer.DrawWidget(Slate, URegularCustomerWidget::PanelSize(true)));
        FlushRenderingCommands();
        FBufferArchive Bytes;
        return LinkTarget.IsValid() && FImageUtils::ExportRenderTarget2DAsPNG(LinkTarget.Get(), Bytes) &&
            FFileHelper::SaveArrayToFile(Bytes, *(Directory / Filename));
    };
    FRegularDialogueLinkView View;
    View.Source = ERegularDialogueSource::AI; View.CurrentReason = TEXT("validated");
    View.Diagnostics.Model = GetDefault<URegularDialogueSettings>()->Model;
    View.Diagnostics.TotalRequests = 12; View.Diagnostics.RecentRequests = 3;
    View.Diagnostics.LastRequestResult = TEXT("validated"); View.Diagnostics.LastRequestSeconds = .74;
    if (!ExportLink(TEXT("ai-link-preview.png"), View)) return false;
    View.Source = ERegularDialogueSource::Fallback; View.CurrentReason = TEXT("timeout");
    View.Diagnostics.LastRequestResult = TEXT("timeout"); View.Diagnostics.LastRequestSeconds = 6;
    if (!ExportLink(TEXT("ai-link-fallback-preview.png"), View)) return false;
    View.bWaiting = true; View.bPlaceholder = true; View.Diagnostics.bPending = true; View.Diagnostics.PendingSeconds = 1.4;
    Widget->Show(FText::FromString(TEXT("夜班常客")), FText::FromString(TEXT("……")), FText::FromString(TEXT("空手指向选项，轻扣扳机回应。")),
        {FText::FromString(TEXT("今天想聊聊？")), FText::FromString(TEXT("先给你做一杯。"))});
    return ExportLink(TEXT("ai-link-pending-preview.png"), View);
}
#endif
