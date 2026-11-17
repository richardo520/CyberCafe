#include "Editor/RegularCustomerPreviewLibrary.h"
#if WITH_EDITOR
#include "UI/RegularCustomerWidget.h"
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
    return FFileHelper::SaveArrayToFile(PNG, *(Directory / TEXT("choice-preview.png")));
}
#endif
