#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkRecipe.h"
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
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

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
}
void AOrderHologramActor::BeginPlay()
{
    Super::BeginPlay();OnConstruction(GetActorTransform());
    Panel->InitWidget();
    if(SignalMaterial){LampMID=StatusLamp->CreateDynamicMaterialInstance(0,SignalMaterial);EmitterMID=EmitterMesh->CreateDynamicMaterialInstance(0,SignalMaterial);}
    if(auto* Events=UCyberCafeEventSubsystem::Get(this))Events->OnDrinkServed.AddUniqueDynamic(this,&AOrderHologramActor::HandleServed);
    RefreshDisplay();UpdateVisual(0);
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
    bPending=true;bDelivered=false;bAutoOpened=false;
    // Already open automatically stays automatic, but each new order gets its own close deadline.
    if(!bOpen){bAutoOpened=true;SetOpen(true,false);}else if(!bPlayerOpened)bAutoOpened=true;
    RefreshDisplay();return true;
}
void AOrderHologramActor::CancelOrder(AServingSpotActor* Spot,int64 Serial)
{
    if(CurrentSpot!=Spot||CurrentOrderSerial!=Serial||!bPending)return;
    if(IsValid(Spot)&&Spot->HasBeenServed()&&Spot->GetAcceptedOrderSerial()==CurrentOrderSerial)return;
    ++Generation;CloseRemaining=-1;bPending=false;bDelivered=false;CurrentRecipe=nullptr;CurrentSpot.Reset();
    if(bOpen&&!bPlayerOpened)SetOpen(false,false);RefreshDisplay();
}
void AOrderHologramActor::PressButton(){ButtonPressRemaining=.15f;SetOpen(!bOpen,true);}
void AOrderHologramActor::SetOpen(bool Value,bool ByPlayer)
{
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
void AOrderHologramActor::UpdateVisual(float Dt)
{
    const float Target=bOpen?1.f:0.f;const float Previous=AnimationValue;
    AnimationValue=AnimationSeconds<=0?Target:FMath::FInterpConstantTo(AnimationValue,Target,Dt,1.f/AnimationSeconds);
    const float Scale=FMath::Clamp(PanelWidthCM,20.f,100.f)/1200.f;
    Panel->SetRelativeScale3D(FVector(Scale,Scale,Scale*FMath::Max(.001f,AnimationValue)));
    // Keep the lower edge stationary during projection unfolding (panel up is local Z).
    const FVector Up=PanelRotation.RotateVector(FVector::UpVector);
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
    UpdateVisual(Dt);PollHands();
}
bool AOrderHologramActor::ProcessTouchSample(EControllerHand Hand,const FVector& Point,bool Tracked,bool Holding)
{
    if(!Tracked||(Hand!=EControllerHand::Left&&Hand!=EControllerHand::Right)||Point.ContainsNaN())return false;
    const FVector Local=ButtonZone->GetComponentTransform().InverseTransformPosition(Point);
    const FVector Extent=ButtonZone->GetUnscaledBoxExtent();const FVector Outer=Extent+FVector(FMath::Max(0.f,TouchRearmMarginCM));
    const bool Outside=FMath::Abs(Local.X)>Outer.X||FMath::Abs(Local.Y)>Outer.Y||FMath::Abs(Local.Z)>Outer.Z;
    if(Holding&&!Outside)return false;
    (Hand==EControllerHand::Left?bLeftOutside:bRightOutside)=Outside;
    if(bLeftOutside&&bRightOutside)bTouchLatched=false;
    const bool Inside=FMath::Abs(Local.X)<=Extent.X&&FMath::Abs(Local.Y)<=Extent.Y&&FMath::Abs(Local.Z)<=Extent.Z;
    if(Inside&&!Holding&&!bTouchLatched){bTouchLatched=true;PressButton();return true;}return false;
}
void AOrderHologramActor::PollHands()
{
    if(!TouchPawn.IsValid())for(TActorIterator<AVRPawn> I(GetWorld());I;++I){if(I->IsLocallyControlled()){TouchPawn=*I;break;}}
    AVRPawn* Pawn=TouchPawn.Get();if(!Pawn)return;
    auto Sample=[&](EControllerHand Hand,UMotionControllerComponent* Grip,USkeletalMeshComponent* Mesh,FName Bone,UGrabComponent* Held)
    {
        if(!Grip||!Grip->IsTracked())return;
        FVector Point=Grip->GetComponentLocation()+Grip->GetForwardVector()*4.f;
        if(Mesh&&Mesh->DoesSocketExist(Bone))Point=Mesh->GetSocketLocation(Bone);
        if(ProcessTouchSample(Hand,Point,true,IsValid(Held)&&Held->IsHeld()))
            if(auto* PC=Cast<APlayerController>(Pawn->GetController()))if(Pawn->HoverHapticEffect)PC->PlayHapticEffect(Pawn->HoverHapticEffect,Hand,.35f,false);
    };
    Sample(EControllerHand::Left,Pawn->MotionControllerLeftGrip,Pawn->HandLeft,LeftTouchBone,Pawn->HeldComponentLeft);
    Sample(EControllerHand::Right,Pawn->MotionControllerRightGrip,Pawn->HandRight,RightTouchBone,Pawn->HeldComponentRight);
}
