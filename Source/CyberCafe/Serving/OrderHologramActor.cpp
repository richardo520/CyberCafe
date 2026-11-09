#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkRecipe.h"
#include "Serving/DrinkMenuData.h"
#include "Components/TextRenderComponent.h"
#include "Serving/ServingSpotActor.h"
#include "UI/OrderHologramWidget.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialNPC.h"
#include "Character/VRPawn.h"
#include "GrabComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "Components/WidgetComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Math/RotationMatrix.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "CoreGlobals.h"

namespace
{
    constexpr double MenuPointerEpsilon=1.e-4;
    constexpr double MenuPointerMaxDistance=250.;

    bool IsMenuPointerTransformValid(const FTransform& Transform)
    {
        const FVector Scale=Transform.GetScale3D();
        return !Transform.ContainsNaN()&&Scale.X>0&&Scale.Y>0&&Scale.Z>0;
    }

    bool IsMenuPointerInsideBox(const FVector& Point,const FVector& Extent)
    {
        return !Point.ContainsNaN()&&!Extent.ContainsNaN()&&Extent.X>0&&Extent.Y>0&&Extent.Z>0
            &&FMath::Abs(Point.X)<=Extent.X+MenuPointerEpsilon
            &&FMath::Abs(Point.Y)<=Extent.Y+MenuPointerEpsilon
            &&FMath::Abs(Point.Z)<=Extent.Z+MenuPointerEpsilon;
    }
}

AOrderHologramActor::AOrderHologramActor()
{
    PrimaryActorTick.bCanEverTick=true;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));SetRootComponent(Root);
    BaseMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectorBase"));BaseMesh->SetupAttachment(Root);
    ButtonMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PowerButton"));ButtonMesh->SetupAttachment(Root);
    StatusLamp=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OrderLamp"));StatusLamp->SetupAttachment(Root);
    EmitterMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Emitter"));EmitterMesh->SetupAttachment(Root);
    BeamMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectionBeam"));BeamMesh->SetupAttachment(Root);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    BeamMesh->SetStaticMesh(Cone.Object);BeamMesh->SetRelativeLocation(FVector(0,0,19));BeamMesh->SetRelativeRotation(FRotator(180,0,0));BeamMesh->SetRelativeScale3D(FVector(.6,.1,.3));BeamMesh->SetVisibility(false);
    BaseMesh->SetStaticMesh(Cylinder.Object);ButtonMesh->SetStaticMesh(Cylinder.Object);StatusLamp->SetStaticMesh(Cylinder.Object);EmitterMesh->SetStaticMesh(Cube.Object);
    BaseMesh->SetRelativeLocation(FVector(0,0,2));BaseMesh->SetRelativeScale3D(FVector(.20,.20,.04));
    ButtonMesh->SetRelativeLocation(FVector(7,0,4.15));ButtonMesh->SetRelativeScale3D(FVector(.045,.045,.005));
    StatusLamp->SetRelativeLocation(FVector(4,5,4.1));StatusLamp->SetRelativeScale3D(FVector(.012,.012,.003));
    EmitterMesh->SetRelativeLocation(FVector(-1,0,4.12));EmitterMesh->SetRelativeScale3D(FVector(.008,.12,.003));
    for(auto* Mesh:{BaseMesh.Get(),ButtonMesh.Get(),StatusLamp.Get(),EmitterMesh.Get(),BeamMesh.Get()}){Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);Mesh->SetCastShadow(false);}
    BaseMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);BaseMesh->SetCollisionResponseToAllChannels(ECR_Block);BaseMesh->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    ButtonZone=CreateDefaultSubobject<UBoxComponent>(TEXT("ButtonZone"));ButtonZone->SetupAttachment(Root);ButtonZone->SetRelativeLocation(FVector(7,0,4.5));ButtonZone->SetBoxExtent(FVector(2.7,3.2,1.5));ButtonZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Panel=CreateDefaultSubobject<UWidgetComponent>(TEXT("OrderPanel"));Panel->SetupAttachment(Root);Panel->SetWidgetSpace(EWidgetSpace::World);Panel->SetDrawSize(FVector2D(1200,750));Panel->SetPivot(FVector2D(.5,.5));Panel->SetBlendMode(EWidgetBlendMode::Transparent);Panel->SetTwoSided(false);Panel->SetOpacityFromTexture(1);Panel->SetBackgroundColor(FLinearColor::Transparent);Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);Panel->SetGenerateOverlapEvents(false);Panel->SetWindowFocusable(false);Panel->SetManuallyRedraw(true);Panel->SetVisibility(false);Panel->SetCastShadow(false);
    const FVector MenuPositions[]={FVector(-7,0,4.5),FVector(-3,-6,4.5),FVector(-3,6,4.5)};
    const TCHAR* MenuNames[]={TEXT("MENU"),TEXT("PREV"),TEXT("NEXT")};
    // Each button keeps a separated physical touch zone; screen index rows use
    // the panel's local coordinates below, not a new widget-interaction ray.
    for(int32 I=0;I<3;++I)
    {
        auto* Zone=CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("MenuTouch%d"),I));Zone->SetupAttachment(Root);Zone->SetRelativeLocation(MenuPositions[I]);Zone->SetBoxExtent(FVector(1.7,1.7,1.1));Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if(I==0)MenuModeZone=Zone;else if(I==1)MenuPreviousZone=Zone;else MenuNextZone=Zone;
        auto* Key=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("MenuKey%d"),I));Key->SetupAttachment(Root);Key->SetStaticMesh(Cube.Object);Key->SetRelativeLocation(MenuPositions[I]-FVector(0,0,.38));Key->SetRelativeScale3D(FVector(.038,.038,.004));Key->SetCollisionEnabled(ECollisionEnabled::NoCollision);Key->SetCastShadow(false);MenuKeys.Add(Key);
        auto* Label=CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("MenuLabel%d"),I));Label->SetupAttachment(Root);Label->SetRelativeLocation(MenuPositions[I]-FVector(1.2,1.4,.1));Label->SetRelativeRotation(FRotator(90,90,0));Label->SetWorldSize(.6f);Label->SetText(FText::FromString(MenuNames[I]));Label->SetTextRenderColor(FColor(80,230,215));MenuLabels.Add(Label);
        Key->SetVisibility(false);Key->SetHiddenInGame(true);
        Label->SetVisibility(false);Label->SetHiddenInGame(true);Label->SetText(FText::GetEmpty());
    }
    DisplayWidgetClass=UOrderHologramWidget::StaticClass();CustomerName=NSLOCTEXT("Hologram","Guide","咖啡馆向导");
}
void AOrderHologramActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    const float Scale=FMath::Clamp(PanelWidthCM,20.f,100.f)/1200.f;
    Panel->SetRelativeLocation(PanelCenter);Panel->SetRelativeRotation(PanelRotation);Panel->SetRelativeScale3D(FVector(Scale));
    if(PanelMaterial)Panel->SetMaterial(0,PanelMaterial);
    if(BeamMaterial)BeamMesh->SetMaterial(0,BeamMaterial);
    // Widget horizontal axis is local Y. Keep the projection fan underneath the panel,
    // rather than extending the wide part into the player's depth direction.
    const float BeamTop=PanelCenter.Z-(PanelWidthCM*750.f/1200.f*.5f)*FMath::Cos(FMath::DegreesToRadians(PanelRotation.Pitch));
    const float BeamHeight=FMath::Max(1.f,BeamTop-4.2f);
    BeamMesh->SetRelativeLocation(FVector(0,0,4.2f+BeamHeight*.5f));
    BeamMesh->SetRelativeRotation(FRotator(180,0,0));
    BeamMesh->SetRelativeScale3D(FVector(.06f,PanelWidthCM/100.f,BeamHeight/100.f));
    if(ShellMaterial){BaseMesh->SetMaterial(0,ShellMaterial);ButtonMesh->SetMaterial(0,ShellMaterial);}
    if(SignalMaterial){StatusLamp->SetMaterial(0,SignalMaterial);EmitterMesh->SetMaterial(0,SignalMaterial);}
    if(DisplayWidgetClass)Panel->SetWidgetClass(DisplayWidgetClass);
    // Retain serialized subobjects only for backwards-compatible asset loading.
    // They have no visible geometry and are not sampled by input anymore.
    for(const auto& Key:MenuKeys){Key->SetVisibility(false);Key->SetHiddenInGame(true);Key->SetCollisionEnabled(ECollisionEnabled::NoCollision);}
    for(const auto& Label:MenuLabels){Label->SetVisibility(false);Label->SetHiddenInGame(true);Label->SetText(FText::GetEmpty());}
    for(auto* Zone:{MenuModeZone.Get(),MenuPreviousZone.Get(),MenuNextZone.Get()}){Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);Zone->SetGenerateOverlapEvents(false);}
}
void AOrderHologramActor::BeginPlay()
{
    Super::BeginPlay();OnConstruction(GetActorTransform());
    Panel->InitWidget();
    if(SignalMaterial){LampMID=StatusLamp->CreateDynamicMaterialInstance(0,SignalMaterial);EmitterMID=EmitterMesh->CreateDynamicMaterialInstance(0,SignalMaterial);}
    if(auto* Events=UCyberCafeEventSubsystem::Get(this))Events->OnDrinkServed.AddUniqueDynamic(this,&AOrderHologramActor::HandleServed);
    RefreshDisplay();UpdateFacingFromPlayer(0);UpdateVisual(0);
}
void AOrderHologramActor::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* Events=UCyberCafeEventSubsystem::Get(this))Events->OnDrinkServed.RemoveDynamic(this,&AOrderHologramActor::HandleServed);
    CloseRemaining=-1;++Generation;Super::EndPlay(Reason);
}
bool AOrderHologramActor::BeginOrder(UDrinkRecipe* Recipe,AServingSpotActor* Spot,ATutorialNPC* Customer,FText Name)
{
    if(!IsValid(Recipe)||!IsValid(Spot)||Spot->GetWorld()!=GetWorld()||Spot->ActiveRecipe!=Recipe)return false;
    const uint32 Serial=Spot->GetOrderSerial();
    if(CurrentSpot==Spot&&CurrentOrderSerial==Serial&&CurrentRecipe==Recipe)return false;
    ++Generation;CloseRemaining=-1;CurrentSpot=Spot;CurrentOrderSerial=Serial;CurrentRecipe=Recipe;CurrentCustomer=Customer;
    if(!Name.IsEmpty())CustomerName=Name;
    bPending=true;bDelivered=false;bAutoOpened=false;bFirstOrderPreviewActive=false;
    // A first-order preview dismissed by the player must not pop back up when
    // the teaching flow later establishes the actual serving order.
    const bool RespectPreviewDismissal=bFirstOrderRecipeRevealed&&bFirstOrderPreviewDismissed&&FirstOrderPreviewRecipe.Get()==Recipe;
    // Already open automatically stays automatic, but each new order gets its own close deadline.
    if(!bOpen&&!RespectPreviewDismissal){bAutoOpened=true;SetOpen(true,false);}else if(bOpen&&!bPlayerOpened)bAutoOpened=true;
    RefreshDisplay();return true;
}
void AOrderHologramActor::CancelOrder(AServingSpotActor* Spot,int64 Serial)
{
    if(CurrentSpot!=Spot||CurrentOrderSerial!=Serial||!bPending)return;
    if(IsValid(Spot)&&Spot->HasBeenServed()&&Spot->GetAcceptedOrderSerial()==CurrentOrderSerial)return;
    ++Generation;CloseRemaining=-1;bPending=false;bDelivered=false;CurrentRecipe=nullptr;CurrentSpot.Reset();
    if(bOpen&&!bPlayerOpened)SetOpen(false,false);RefreshDisplay();
}
UDrinkRecipe* AOrderHologramActor::GetPresentedRecipe() const
{
    if(IsValid(CurrentRecipe))return CurrentRecipe;
    return bFirstOrderPreviewActive?FirstOrderPreviewRecipe.Get():nullptr;
}
const FDrinkMenuEntry* AOrderHologramActor::GetSelectedMenuEntry() const
{
    return IsValid(RecipeMenu)&&RecipeMenu->Entries.IsValidIndex(SelectedMenuIndex)?&RecipeMenu->Entries[SelectedMenuIndex]:nullptr;
}
bool AOrderHologramActor::ToggleRecipeMenu()
{
    if(!IsValid(RecipeMenu)||!RecipeMenu->IsValidMenu())return false;
    bRecipeMenuMode=!bRecipeMenuMode;SelectedMenuIndex=FMath::Clamp(SelectedMenuIndex,0,RecipeMenu->Entries.Num()-1);
    // Browsing is an explicit player-open action. No order serial/recipe/snapshot
    // is changed; an automatic delivery close must not interrupt menu reading.
    SetOpen(true,true);return true;
}
bool AOrderHologramActor::BrowseRecipeMenu(int32 Direction)
{
    if(!bRecipeMenuMode||!bOpen||!IsValid(RecipeMenu)||!RecipeMenu->IsValidMenu()||Direction==0)return false;
    const int32 Count=RecipeMenu->Entries.Num();SelectedMenuIndex=(SelectedMenuIndex+(Direction>0?1:-1)+Count)%Count;RefreshDisplay();return true;
}
bool AOrderHologramActor::SelectRecipeByPinyin(FName Index)
{
    if(!IsValid(RecipeMenu)||!RecipeMenu->IsValidMenu())return false;
    const int32 Found=RecipeMenu->FindPinyinIndex(Index);if(Found==INDEX_NONE)return false;
    SelectedMenuIndex=Found;bRecipeMenuMode=true;SetOpen(true,true);return true;
}
bool AOrderHologramActor::RevealFirstOrderRecipe(UDrinkRecipe* Recipe)
{
    if(bFirstOrderRecipeRevealed||bPending||!IsValid(Recipe)||!IsValid(RecipeMenu)||!RecipeMenu->IsValidMenu())return false;
    const int32 Found=RecipeMenu->Entries.IndexOfByPredicate([Recipe](const FDrinkMenuEntry& Entry){return Entry.Recipe.Get()==Recipe;});
    if(Found==INDEX_NONE)return false;
    bFirstOrderRecipeRevealed=true;FirstOrderPreviewRecipe=Recipe;bFirstOrderPreviewDismissed=false;bFirstOrderPreviewActive=true;
    SelectedMenuIndex=Found;MenuFilter=0;bRecipeMenuMode=false;
    // Do not broadcast the input delegate: showing a recipe is not the player clicking it.
    if(!bOpen){bAutoOpened=true;SetOpen(true,false);}else RefreshDisplay();
    return true;
}
void AOrderHologramActor::PressButton()
{
    ButtonPressRemaining=.15f;
    if(!bOpen&&bOpenOnRecipePage&&IsValid(RecipeMenu)&&RecipeMenu->IsValidMenu())bRecipeMenuMode=true;
    SetOpen(!bOpen,true);
}
void AOrderHologramActor::SetOpen(bool Value,bool ByPlayer)
{
    if(Value)UpdateFacingFromPlayer(0);
    if(ByPlayer&&!Value&&bFirstOrderRecipeRevealed)bFirstOrderPreviewDismissed=true;
    bOpen=Value;bPlayerOpened=Value&&ByPlayer;if(ByPlayer||!Value)CloseRemaining=-1;
    if(Value){Panel->SetVisibility(true);Panel->RequestRedraw();}RefreshDisplay();
}
void AOrderHologramActor::HandleServed(AServingSpotActor* Spot,ALiquidContainerActor* Cup)
{
    if(!bPending||!IsValid(Spot)||!IsValid(Cup)||CurrentSpot!=Spot||!CurrentRecipe||Spot->GetWorld()!=GetWorld())return;
    if(Spot->GetAcceptedOrderSerial()!=CurrentOrderSerial||!Spot->HasAcceptedCup(Cup,CurrentRecipe->RecipeId))return;
    const FDrinkValidationResult R=Spot->GetLastValidation();
    if(!R.bAccepted||R.RecipeId!=CurrentRecipe->RecipeId)return;
    bPending=false;bDelivered=true;
    FHologramDeliveryRecord Record;Record.CustomerId=CustomerId;Record.RecipeId=R.RecipeId;Record.DrinkName=CurrentRecipe->DisplayName;Record.VolumeML=R.TotalML;DeliveryRecords.Add(Record);
    // Only session data, only actual accepted snapshot, never invent an NPC rating.
    if(bOpen&&!bPlayerOpened)CloseRemaining=FMath::Max(0.f,AutoCloseSeconds);
    RefreshDisplay();
}
UOrderHologramWidget* AOrderHologramActor::GetDisplayWidget() const{return Cast<UOrderHologramWidget>(Panel->GetUserWidgetObject());}
void AOrderHologramActor::RefreshDisplay()
{
    if(auto* W=GetDisplayWidget())W->ApplyPresentation(this);Panel->RequestRedraw();
    if(LampMID)LampMID->SetVectorParameterValue(TEXT("SignalColor"),IsAmberLit()?FLinearColor(1,.38,.05):FLinearColor(.012,.008,.003));
    if(EmitterMID)EmitterMID->SetVectorParameterValue(TEXT("SignalColor"),bOpen?FLinearColor(.05,.65,.52):FLinearColor(.003,.009,.008));
}
bool AOrderHologramActor::UpdatePanelFacing(const FVector& CameraWorld,float Dt)
{
    if(!bFaceHeadset||!Panel||!FMath::IsFinite(CameraWorld.X)||!FMath::IsFinite(CameraWorld.Y)||!FMath::IsFinite(CameraWorld.Z)||!FMath::IsFinite(Dt)||Dt<0)return false;
    // 始终从父变换下完全展开的固定中心取方向，不依赖动画中的面板位置。
    const USceneComponent* Parent=Panel->GetAttachParent();
    const FVector CenterWorld=Parent?Parent->GetComponentTransform().TransformPosition(PanelCenter):PanelCenter;
    const FVector ToCamera=CameraWorld-CenterWorld;
    const double DistanceSquared=ToCamera.SizeSquared();
    if(ToCamera.ContainsNaN()||!FMath::IsFinite(DistanceSquared)||DistanceSquared<1.)return false;
    const FVector Direction=ToCamera/FMath::Sqrt(DistanceSquared);
    // 正上方或正下方无法用世界上方向确定无滚转坐标系，保留最后有效朝向。
    if(FVector::CrossProduct(Direction,FVector::UpVector).IsNearlyZero())return false;
    const FQuat Target=FRotationMatrix::MakeFromXZ(Direction,FVector::UpVector).ToQuat();
    FQuat Facing=Target;
    if(Dt>0)
    {
        if(!FMath::IsFinite(FacingInterpSpeed))return false;
        FRotator Current=Panel->GetComponentQuat().Rotator();
        if(Current.ContainsNaN())return false;
        Current.Roll=0;
        FRotator Interpolated=FMath::RInterpTo(Current,Target.Rotator(),Dt,FMath::Max(0.f,FacingInterpSpeed));
        Interpolated.Roll=0;
        Facing=Interpolated.Quaternion();
    }
    Panel->SetWorldRotation(Facing);
    return true;
}
void AOrderHologramActor::UpdateFacingFromPlayer(float Dt)
{
    UWorld* World=GetWorld();
    if(!bFaceHeadset||!World)return;
    APlayerController* PC=World->GetFirstPlayerController();
    if(!IsValid(PC)||!PC->IsLocalController())return;
    APlayerCameraManager* Camera=PC->PlayerCameraManager;
    if(IsValid(Camera))UpdatePanelFacing(Camera->GetCameraLocation(),Dt);
}
void AOrderHologramActor::UpdateVisual(float Dt)
{
    const float Target=bOpen?1.f:0.f;const float Previous=AnimationValue;
    AnimationValue=AnimationSeconds<=0?Target:FMath::FInterpConstantTo(AnimationValue,Target,Dt,1.f/AnimationSeconds);
    const float Scale=FMath::Clamp(PanelWidthCM,20.f,100.f)/1200.f;
    Panel->SetRelativeScale3D(FVector(Scale,Scale,Scale*FMath::Max(.001f,AnimationValue)));
    // Keep the lower edge stationary during projection unfolding (panel up is local Z).
    const FVector Up=Panel->GetRelativeRotation().RotateVector(FVector::UpVector);
    Panel->SetRelativeLocation(PanelCenter-Up*((1-AnimationValue)*750.f*Scale*.5f));
    BeamMesh->SetVisibility(AnimationValue>.001f);
    if(AnimationValue==0&&!bOpen)Panel->SetVisibility(false);
    if(Previous!=AnimationValue)Panel->RequestRedraw();
}
void AOrderHologramActor::Tick(float Dt)
{
    Super::Tick(Dt);
    ButtonPressRemaining=FMath::Max(0.f,ButtonPressRemaining-Dt);
    ButtonMesh->SetRelativeLocation(FVector(7,0,ButtonPressRemaining>0?4.0:4.15));
    if(CloseRemaining>=0){CloseRemaining-=FMath::Max(0.f,Dt);if(CloseRemaining<=0){CloseRemaining=-1;if(bDelivered&&bOpen&&!bPlayerOpened)SetOpen(false,false);}}
    UpdateFacingFromPlayer(Dt);UpdateVisual(Dt);PollHands();
}
bool AOrderHologramActor::GetMenuPointerPanelGeometry(FTransform& Transform,FVector2D& DrawSize) const
{
    if(!IsValid(this)||IsActorBeingDestroyed()||IsHidden()||!bOpen||!FMath::IsFinite(AnimationValue)||AnimationValue<=.95f
        ||!IsValid(Panel)||!Panel->IsRegistered()||Panel->bHiddenInGame||!Panel->IsWidgetVisible()
        ||Panel->GetWidgetSpace()!=EWidgetSpace::World||Panel->GetGeometryMode()!=EWidgetGeometryMode::Plane
        ||!IsValid(RecipeMenu)||!RecipeMenu->IsValidMenu()||!IsValid(GetDisplayWidget()))return false;
    Transform=Panel->GetComponentTransform();DrawSize=Panel->GetCurrentDrawSize();
    const FVector2D Pivot=Panel->GetPivot();
    // 固定 UMG 矩形必须对应实际已绘制的 1200×750 面；不伪造未生成的几何。
    return IsMenuPointerTransformValid(Transform)&&!DrawSize.ContainsNaN()&&!Pivot.ContainsNaN()
        &&DrawSize.X==1200&&DrawSize.Y==750&&Pivot.X==.5&&Pivot.Y==.5;
}

int32 AOrderHologramActor::GetMenuPointerActionAtLocalPoint(const FVector& Point) const
{
    if(Point.ContainsNaN())return INDEX_NONE;
    UOrderHologramWidget* Widget=GetDisplayWidget();
    if(!IsValid(Widget))return INDEX_NONE;
    const FVector2D Pixel(600-Point.Y,375-Point.Z);
    TArray<int32> Actions={0,1};
    if(bRecipeMenuMode)
    {
        Actions.Append({10,11,12});
        const int32 Count=FMath::Min(10,GetVisibleMenuIndices().Num());
        for(int32 I=0;I<Count;++I)Actions.Add(100+I);
    }
    else if(IsValid(GetPresentedRecipe()))Actions.Add(2);
    for(int32 Action:Actions)
    {
        const FIntRect Rect=UOrderHologramWidget::GetScreenActionRect(Action);
        if(Pixel.X<Rect.Min.X||Pixel.X>=Rect.Max.X||Pixel.Y<Rect.Min.Y||Pixel.Y>=Rect.Max.Y)continue;
        FString Name;
        if(Action==0)Name=TEXT("OrderTabButton");
        else if(Action==1)Name=TEXT("RecipeTabButton");
        else if(Action==2)Name=TEXT("OrderRecipeButton");
        else if(Action>=10&&Action<=12)Name=FString::Printf(TEXT("MenuFilterButton%d"),Action-10);
        else Name=FString::Printf(TEXT("MenuIndexButton%d"),Action-100);
        const URecipeScreenButton* Button=Cast<URecipeScreenButton>(Widget->GetWidgetFromName(FName(*Name)));
        if(!IsValid(Button)||Button->Action!=Action||Button->Host.Get()!=this||!Button->GetIsEnabled()
            ||Button->GetVisibility()!=ESlateVisibility::Visible)return INDEX_NONE;
        if(Action==2)
        {
            const UDrinkRecipe* Presented=GetPresentedRecipe();
            if(!RecipeMenu->Entries.ContainsByPredicate([Presented](const FDrinkMenuEntry& Entry){return Entry.Recipe.Get()==Presented;}))return INDEX_NONE;
        }
        return Action;
    }
    return INDEX_NONE;
}

int32 AOrderHologramActor::GetPointerActionAtWorldPoint(const FVector& WorldPoint) const
{
    FTransform Transform;FVector2D DrawSize;
    if(WorldPoint.ContainsNaN()||!GetMenuPointerPanelGeometry(Transform,DrawSize))return INDEX_NONE;
    const FVector Local=Transform.InverseTransformPosition(WorldPoint);
    if(Local.ContainsNaN()||FMath::Abs(Local.X)>.01
        ||FMath::Abs(Local.Y)>DrawSize.X*.5+MenuPointerEpsilon||FMath::Abs(Local.Z)>DrawSize.Y*.5+MenuPointerEpsilon)return INDEX_NONE;
    return GetMenuPointerActionAtLocalPoint(Local);
}

bool AOrderHologramActor::TraceMenuPointer(const FVector& Start,const FVector& NormalizedDir,double MaxDistance,FMenuPointerHit& Hit) const
{
    Hit=FMenuPointerHit();
    if(!IsValid(this)||IsActorBeingDestroyed()||Start.ContainsNaN()||NormalizedDir.ContainsNaN()
        ||!FMath::IsFinite(MaxDistance)||MaxDistance<=0)return false;
    const double LengthSquared=NormalizedDir.SizeSquared();
    if(!FMath::IsFinite(LengthSquared)||FMath::Abs(LengthSquared-1.)>MenuPointerEpsilon)return false;
    const FVector Direction=NormalizedDir/FMath::Sqrt(LengthSquared);
    const double Limit=FMath::Min(MaxDistance,MenuPointerMaxDistance);
    bool Found=false;
    FTransform Transform;FVector2D DrawSize;
    if(GetMenuPointerPanelGeometry(Transform,DrawSize))
    {
        const FVector LocalStart=Transform.InverseTransformPosition(Start);
        const FVector LocalDirection=Transform.InverseTransformVector(Direction);
        const double Facing=FVector::DotProduct(Transform.GetRotation().GetAxisX(),Direction);
        if(!LocalStart.ContainsNaN()&&!LocalDirection.ContainsNaN()&&FMath::IsFinite(Facing)
            &&LocalStart.X>0&&Facing<-.01&&LocalDirection.X<0)
        {
            const double Distance=-LocalStart.X/LocalDirection.X;
            if(FMath::IsFinite(Distance)&&Distance>0&&Distance<=Limit+MenuPointerEpsilon)
            {
                FVector Local=LocalStart+LocalDirection*Distance;Local.X=0;
                if(!Local.ContainsNaN()&&FMath::Abs(Local.Y)<=DrawSize.X*.5+MenuPointerEpsilon&&FMath::Abs(Local.Z)<=DrawSize.Y*.5+MenuPointerEpsilon)
                {
                    const FVector Point=Transform.TransformPosition(Local);
                    if(!Point.ContainsNaN())
                    {
                        Hit.Point=Point;Hit.Distance=Distance;Hit.Action=GetMenuPointerActionAtLocalPoint(Local);Hit.bPanel=true;Found=true;
                    }
                }
            }
        }
    }
    // 原实体 ButtonZone 即使隐藏或无碰撞仍是电源几何；不使用球扫或扩大盒体。
    if(IsValid(ButtonZone))
    {
        const FTransform BoxTransform=ButtonZone->GetComponentTransform();
        const FVector Extent=ButtonZone->GetUnscaledBoxExtent();
        if(IsMenuPointerTransformValid(BoxTransform)&&IsMenuPointerInsideBox(FVector::ZeroVector,Extent))
        {
            const FVector LocalStart=BoxTransform.InverseTransformPosition(Start);
            const FVector LocalDirection=BoxTransform.InverseTransformVector(Direction);
            double Enter=-TNumericLimits<double>::Max(),Exit=TNumericLimits<double>::Max();
            bool Intersects=!LocalStart.ContainsNaN()&&!LocalDirection.ContainsNaN();
            for(int32 Axis=0;Intersects&&Axis<3;++Axis)
            {
                if(LocalDirection[Axis]==0)
                {
                    Intersects=FMath::Abs(LocalStart[Axis])<=Extent[Axis]+MenuPointerEpsilon;
                    continue;
                }
                const double A=(-Extent[Axis]-LocalStart[Axis])/LocalDirection[Axis];
                const double B=(Extent[Axis]-LocalStart[Axis])/LocalDirection[Axis];
                if(!FMath::IsFinite(A)||!FMath::IsFinite(B)){Intersects=false;break;}
                Enter=FMath::Max(Enter,FMath::Min(A,B));Exit=FMath::Min(Exit,FMath::Max(A,B));
                Intersects=Enter<=Exit+MenuPointerEpsilon;
            }
            // 起点在盒内时取真实出射面，不把 MaxDistance 截断点当作盒面。
            const double Distance=Enter>0?Enter:Exit;
            if(Intersects&&FMath::IsFinite(Distance)&&Distance>0&&Distance<=Limit+MenuPointerEpsilon&&(!Found||Distance<Hit.Distance))
            {
                const FVector Local=LocalStart+LocalDirection*Distance;
                if(IsMenuPointerInsideBox(Local,Extent))
                {
                    const FVector Point=BoxTransform.TransformPosition(Local);
                    if(!Point.ContainsNaN())
                    {
                        Hit.Point=Point;Hit.Distance=Distance;Hit.Action=INDEX_NONE;Hit.bPanel=false;Hit.bPower=true;Found=true;
                    }
                }
            }
        }
    }
    return Found;
}

bool AOrderHologramActor::TryActivateMenuPointer(EControllerHand Hand,const FMenuPointerHit& Hit)
{
    if(!IsValid(this)||IsActorBeingDestroyed()||(Hand!=EControllerHand::Left&&Hand!=EControllerHand::Right)
        ||Hit.bPanel==Hit.bPower||Hit.Point.ContainsNaN()||!FMath::IsFinite(Hit.Distance)
        ||Hit.Distance<=0||Hit.Distance>MenuPointerMaxDistance+MenuPointerEpsilon||LastAcceptedPointerFrame==GFrameCounter)return false;
    if(Hit.bPower)
    {
        if(Hit.Action!=INDEX_NONE||!IsValid(ButtonZone)||(bTouchLatched&&(!bLeftOutside||!bRightOutside)))return false;
        const FTransform Transform=ButtonZone->GetComponentTransform();
        if(!IsMenuPointerTransformValid(Transform)
            ||!IsMenuPointerInsideBox(Transform.InverseTransformPosition(Hit.Point),ButtonZone->GetUnscaledBoxExtent()))return false;
        LastAcceptedPointerFrame=GFrameCounter;PressButton();return true;
    }
    if(bScreenTouchLatched&&(!bScreenLeftOutside||!bScreenRightOutside))return false;
    const int32 Action=GetPointerActionAtWorldPoint(Hit.Point);
    if(Action==INDEX_NONE||Action!=Hit.Action)return false;
    const uint64 PreviousFrame=LastAcceptedPointerFrame;
    const TWeakObjectPtr<AOrderHologramActor> Guard(this);
    LastAcceptedPointerFrame=GFrameCounter;
    const bool Accepted=ActivateScreenAction(Action);
    // 接受后的回调即使销毁 Actor 也返回成功；失败才恢复帧标记，不消费空白。
    if(!Accepted&&Guard.IsValid()&&!Guard->IsActorBeingDestroyed())Guard->LastAcceptedPointerFrame=PreviousFrame;
    return Accepted;
}

bool AOrderHologramActor::ProcessTouchSample(EControllerHand Hand,const FVector& Point,bool Tracked,bool Holding)
{
    if(Hand!=EControllerHand::Left&&Hand!=EControllerHand::Right)return false;
    if(!Tracked||Point.ContainsNaN())
    {
        bool& Outside=Hand==EControllerHand::Left?bLeftOutside:bRightOutside;
        (Hand==EControllerHand::Left?bLeftNeedsExit:bRightNeedsExit)|=bTouchLatched&&!Outside;
        Outside=true;
        if(bLeftOutside&&bRightOutside)bTouchLatched=false;
        return false;
    }
    const FVector Local=ButtonZone->GetComponentTransform().InverseTransformPosition(Point);
    const FVector Extent=ButtonZone->GetUnscaledBoxExtent();const FVector Outer=Extent+FVector(FMath::Max(0.f,TouchRearmMarginCM));
    const bool Outside=FMath::Abs(Local.X)>Outer.X||FMath::Abs(Local.Y)>Outer.Y||FMath::Abs(Local.Z)>Outer.Z;
    bool& NeedsExit=Hand==EControllerHand::Left?bLeftNeedsExit:bRightNeedsExit;
    if(Outside)NeedsExit=false;else if(NeedsExit)return false;
    if(Holding&&!Outside)return false;
    (Hand==EControllerHand::Left?bLeftOutside:bRightOutside)=Outside;
    if(bLeftOutside&&bRightOutside)bTouchLatched=false;
    const bool Inside=FMath::Abs(Local.X)<=Extent.X&&FMath::Abs(Local.Y)<=Extent.Y&&FMath::Abs(Local.Z)<=Extent.Z;
    if(Inside&&!Holding&&!bTouchLatched&&LastAcceptedPointerFrame!=GFrameCounter){bTouchLatched=true;PressButton();return true;}return false;
}
TArray<int32> AOrderHologramActor::GetVisibleMenuIndices() const
{
    TArray<int32> Rows;if(!IsValid(RecipeMenu))return Rows;
    for(int32 I=0;I<RecipeMenu->Entries.Num();++I)
    {
        const bool Coffee=RecipeMenu->Entries[I].Category.ToString().Contains(TEXT("咖啡"));
        if(MenuFilter==0||(MenuFilter==1&&Coffee)||(MenuFilter==2&&!Coffee))Rows.Add(I);
    }
    return Rows;
}
bool AOrderHologramActor::ActivateScreenAction(int32 Action)
{
    if(!bOpen||!IsValid(RecipeMenu)||!RecipeMenu->IsValidMenu())return false;
    bool Accepted=false;
    if(Action==0){bRecipeMenuMode=false;Accepted=true;}
    else if(Action==1){bRecipeMenuMode=true;Accepted=true;}
    else if(Action==2&&IsValid(GetPresentedRecipe()))
    {
        const UDrinkRecipe* Presented=GetPresentedRecipe();
        const int32 Found=RecipeMenu->Entries.IndexOfByPredicate([Presented](const FDrinkMenuEntry& E){return E.Recipe.Get()==Presented;});
        if(Found!=INDEX_NONE){SelectedMenuIndex=Found;MenuFilter=0;bRecipeMenuMode=true;Accepted=true;}
    }
    else if(bRecipeMenuMode&&Action>=10&&Action<=12){MenuFilter=Action-10;Accepted=true;}
    else if(bRecipeMenuMode&&Action>=100&&Action<110)
    {
        const TArray<int32> Rows=GetVisibleMenuIndices();const int32 Row=Action-100;
        if(Rows.IsValidIndex(Row)){SelectedMenuIndex=Rows[Row];Accepted=true;}
    }
    if(!Accepted)return false;
    LastScreenAction=Action;SetOpen(true,true);
    // No active recipe, order serial or history is changed by viewing UI.
    OnMenuInteractionNative.Broadcast(this);return true;
}
bool AOrderHologramActor::ProcessMenuTouchSample(EControllerHand Hand,const FVector& Point,bool Tracked,bool Holding)
{
    if(Hand!=EControllerHand::Left&&Hand!=EControllerHand::Right)return false;
    if(!Tracked||Point.ContainsNaN())
    {
        bool& Outside=Hand==EControllerHand::Left?bScreenLeftOutside:bScreenRightOutside;
        (Hand==EControllerHand::Left?bScreenLeftNeedsExit:bScreenRightNeedsExit)|=bScreenTouchLatched&&!Outside;
        Outside=true;
        if(bScreenLeftOutside&&bScreenRightOutside)bScreenTouchLatched=false;
        return false;
    }
    bool Outside=true;int32 Hit=INDEX_NONE;
    if(bOpen&&AnimationValue>.95f&&IsValid(RecipeMenu)&&RecipeMenu->IsValidMenu())
    {
        const FVector P=Panel->GetComponentTransform().InverseTransformPosition(Point);const FVector2D Pixel(600-P.Y,375-P.Z);
        TArray<int32> Actions={0,1};
        if(bRecipeMenuMode){Actions.Append({10,11,12});for(int32 I=0;I<GetVisibleMenuIndices().Num();++I)Actions.Add(100+I);}
        else if(IsValid(GetPresentedRecipe()))Actions.Add(2);
        for(int32 Action:Actions)
        {
            const FIntRect Rect=UOrderHologramWidget::GetScreenActionRect(Action);
            const bool Near=FMath::Abs(P.X)<=44&&Pixel.X>=Rect.Min.X-18&&Pixel.X<=Rect.Max.X+18&&Pixel.Y>=Rect.Min.Y-18&&Pixel.Y<=Rect.Max.Y+18;
            if(Near)Outside=false;
            if(FMath::Abs(P.X)<=24&&Pixel.X>=Rect.Min.X&&Pixel.X<Rect.Max.X&&Pixel.Y>=Rect.Min.Y&&Pixel.Y<Rect.Max.Y)Hit=Action;
        }
    }
    bool& NeedsExit=Hand==EControllerHand::Left?bScreenLeftNeedsExit:bScreenRightNeedsExit;
    if(Outside)NeedsExit=false;else if(NeedsExit)return false;
    (Hand==EControllerHand::Left?bScreenLeftOutside:bScreenRightOutside)=Outside;
    if(bScreenLeftOutside&&bScreenRightOutside)bScreenTouchLatched=false;
    if(Hit!=INDEX_NONE&&!Holding&&!bScreenTouchLatched&&LastAcceptedPointerFrame!=GFrameCounter)
    {
        bScreenTouchLatched=true;return ActivateScreenAction(Hit);
    }
    return false;
}
void AOrderHologramActor::PollHands()
{
    if(!TouchPawn.IsValid())for(TActorIterator<AVRPawn> I(GetWorld());I;++I){if(I->IsLocallyControlled()){TouchPawn=*I;break;}}
    AVRPawn* Pawn=TouchPawn.Get();if(!Pawn)return;
    auto Sample=[&](EControllerHand Hand,UMotionControllerComponent* Grip,USkeletalMeshComponent* Mesh,FName Bone,UGrabComponent* Held)
    {
        if(!Grip||!Grip->IsTracked())
        {
            ProcessTouchSample(Hand,FVector::ZeroVector,false,false);
            ProcessMenuTouchSample(Hand,FVector::ZeroVector,false,false);
            return;
        }
        FVector Point=Grip->GetComponentLocation()+Grip->GetForwardVector()*4.f;
        if(Mesh&&Mesh->DoesSocketExist(Bone))Point=Mesh->GetSocketLocation(Bone);
        const bool Holding=IsValid(Held)&&Held->IsHeld();
        const bool PowerPressed=ProcessTouchSample(Hand,Point,true,Holding);
        const bool MenuPressed=ProcessMenuTouchSample(Hand,Point,true,Holding);
        if(PowerPressed||MenuPressed)
            if(auto* PC=Cast<APlayerController>(Pawn->GetController()))if(Pawn->HoverHapticEffect)PC->PlayHapticEffect(Pawn->HoverHapticEffect,Hand,.35f,false);
    };
    Sample(EControllerHand::Left,Pawn->MotionControllerLeftGrip,Pawn->HandLeft,LeftTouchBone,Pawn->HeldComponentLeft);
    Sample(EControllerHand::Right,Pawn->MotionControllerRightGrip,Pawn->HandRight,RightTouchBone,Pawn->HeldComponentRight);
}
