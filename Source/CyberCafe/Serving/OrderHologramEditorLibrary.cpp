#include "Serving/OrderHologramEditorLibrary.h"
#if WITH_EDITOR
#include "Serving/OrderHologramActor.h"
#include "UI/OrderHologramWidget.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/WidgetComponent.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/StaticMeshComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/ISlateRHIRendererModule.h"
#include "EngineFontServices.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "UObject/UnrealType.h"
AOrderHologramActor* UOrderHologramEditorLibrary::AddDisplay(UWorld* World,TSubclassOf<AOrderHologramActor> Class,FVector Location,FRotator Rotation)
{
    if(!World||World->IsGameWorld()||!Class)return nullptr;
    FActorSpawnParameters Params;Params.Name=TEXT("BP_OrderHologram_Tutorial");Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* A=World->SpawnActor<AOrderHologramActor>(Class,Location,Rotation,Params);
    if(A){A->SetActorLabel(TEXT("Tutorial Hologram Order Display"));A->MarkPackageDirty();World->MarkPackageDirty();}return A;
}
bool UOrderHologramEditorLibrary::CaptureDisplay(AOrderHologramActor* A,const FString& Filename)
{
    if(!A||!A->DisplayWidgetClass)return false;
    // Python commandlets skip Slate startup even with AllowCommandletRendering.
    // Initialize only the renderer, without opening an editor or creating a window.
    if(!FSlateApplication::IsInitialized())
    {
        FSlateApplication::InitializeAsStandaloneApplication(FModuleManager::LoadModuleChecked<ISlateRHIRendererModule>(TEXT("SlateRHIRenderer")).CreateSlateRHIRenderer());
    }
    if(!FEngineFontServices::IsInitialized())FEngineFontServices::Create();
    auto* Widget=NewObject<UOrderHologramWidget>(A->GetWorld(),A->DisplayWidgetClass);
    Widget->Initialize();Widget->BuildWidgetTree();Widget->ApplyPresentation(A);
    const FBoolProperty* GammaProperty=FindFProperty<FBoolProperty>(UWidgetComponent::StaticClass(),TEXT("bApplyGammaCorrection"));
    const bool bWidgetGamma=GammaProperty && GammaProperty->GetPropertyValue_InContainer(A->Panel.Get());
    FWidgetRenderer Renderer(bWidgetGamma);
    UTextureRenderTarget2D* RT=Renderer.DrawWidget(Widget->TakeWidget(),FVector2D(1200,750));
    if(!RT)return false;
    TArray<FColor> Pixels;
    const bool Read=RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
    if(!Read||Pixels.Num()!=1200*750)return false;
    TArray64<uint8> Bytes;
    FImageUtils::PNGCompressImageArray(1200,750,TArrayView64<const FColor>(Pixels.GetData(),Pixels.Num()),Bytes);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    return FFileHelper::SaveArrayToFile(Bytes,*Filename);
}
bool UOrderHologramEditorLibrary::EmptyWidgetBlueprint(UObject* Object,bool bReset)
{
    UWidgetBlueprint* BP=Cast<UWidgetBlueprint>(Object);
    if(!BP||!BP->WidgetTree)return false;
    if(bReset){BP->WidgetTree->Modify();BP->WidgetTree->RootWidget=nullptr;BP->MarkPackageDirty();}
    return BP->WidgetTree->RootWidget==nullptr;
}
FString UOrderHologramEditorLibrary::InspectPlacement(AOrderHologramActor* Display)
{
    if(!Display||!Display->GetWorld())return TEXT("{}");
    UWorld* World=Display->GetWorld();
    // An offline loaded World has unregistered components: runtime world transforms are zero until registration.
    if(!World->bIsWorldInitialized)
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World->InitWorld(IV);
    }
    World->UpdateWorldComponents(true,false);
    const FVector Origin=Display->GetActorLocation();
    FCollisionQueryParams Params;Params.AddIgnoredActor(Display);Params.bTraceComplex=true;
    FHitResult Hit;
    const bool bSupport=World->LineTraceSingleByChannel(Hit,Origin+FVector(0,0,10),Origin-FVector(0,0,30),ECC_Visibility,Params);
    const float Lower=Origin.Z+Display->PanelCenter.Z-(Display->PanelWidthCM*750.f/1200.f*.5f)*FMath::Cos(FMath::DegreesToRadians(Display->PanelRotation.Pitch));
    bool bVisualSupport=false;float VisualSupportZ=-BIG_NUMBER;
    // Check the actual bar triangles as well as collision; imported props may
    // use only complex collision, which a default simple trace cannot see.
    for(TActorIterator<AStaticMeshActor> I(World);I;++I)
    {
        if(I->GetActorLabel()!=TEXT("A01_Bar"))continue;
        UStaticMeshComponent* MeshComp=I->GetStaticMeshComponent();
        UStaticMesh* Mesh=MeshComp?MeshComp->GetStaticMesh():nullptr;
        if(!Mesh||!Mesh->GetRenderData()||Mesh->GetRenderData()->LODResources.Num()==0)continue;
        const FStaticMeshLODResources& LOD=Mesh->GetRenderData()->LODResources[0];
        const auto& Positions=LOD.VertexBuffers.PositionVertexBuffer;
        const FIndexArrayView Indices=LOD.IndexBuffer.GetArrayView();
        const FTransform MeshTransform=MeshComp->GetComponentTransform();
        for(int32 Index=0;Index+2<Indices.Num();Index+=3)
        {
            const FVector A=MeshTransform.TransformPosition(FVector(Positions.VertexPosition(Indices[Index])));
            const FVector B=MeshTransform.TransformPosition(FVector(Positions.VertexPosition(Indices[Index+1])));
            const FVector C=MeshTransform.TransformPosition(FVector(Positions.VertexPosition(Indices[Index+2])));
            const double Den=(B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
            if(FMath::Abs(Den)<1e-8)continue;
            const double U=((B.Y-C.Y)*(Origin.X-C.X)+(C.X-B.X)*(Origin.Y-C.Y))/Den;
            const double V=((C.Y-A.Y)*(Origin.X-C.X)+(A.X-C.X)*(Origin.Y-C.Y))/Den;
            const double W=1-U-V;
            if(U>=-1e-6&&V>=-1e-6&&W>=-1e-6)
            {
                const float Z=U*A.Z+V*B.Z+W*C.Z;
                if(Z<=Origin.Z+12&&Z>=Origin.Z-30&&Z>VisualSupportZ){bVisualSupport=true;VisualSupportZ=Z;}
            }
        }
    }
    TArray<FString> Items;
    for(TActorIterator<ACupActor> I(World);I;++I)
    {
        const FVector Rim=I->PourEntryPoint->GetComponentLocation();
        Items.Add(FString::Printf(TEXT("{\"name\":\"%s\",\"rimZ\":%.6f}"),*I->GetName(),Rim.Z));
    }
    return FString::Printf(TEXT("{\"registered\":true,\"base\":[%.4f,%.4f,%.4f],\"supportHit\":%s,\"supportZ\":%.4f,\"supportActor\":\"%s\",\"barTriangleHit\":%s,\"barTriangleZ\":%.4f,\"baseToBarGapCM\":%.4f,\"lowerEdgeZ\":%.4f,\"cups\":[%s]}"),Origin.X,Origin.Y,Origin.Z,bSupport?TEXT("true"):TEXT("false"),Hit.ImpactPoint.Z,Hit.GetActor()?*Hit.GetActor()->GetName():TEXT(""),bVisualSupport?TEXT("true"):TEXT("false"),bVisualSupport?VisualSupportZ:0.f,bVisualSupport?Origin.Z-VisualSupportZ:0.f,Lower,*FString::Join(Items,TEXT(",")));
}
#endif
