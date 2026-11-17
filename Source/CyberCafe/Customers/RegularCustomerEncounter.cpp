#include "Customers/RegularCustomerEncounter.h"
#include "Customers/RegularCustomerMemory.h"
#include "Customers/CustomerOrderComponent.h"
#include "Customers/CustomerDrinkComponent.h"
#include "Core/Events/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialNPC.h"
#include "Serving/ServingSpotActor.h"
#include "Liquid/CupActor.h"
#include "UI/RegularCustomerWidget.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
FText RegularText(const FString& Text) { return FText::FromString(Text); }
}

ARegularCustomerEncounter::ARegularCustomerEncounter()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Panel = CreateDefaultSubobject<UWidgetComponent>(TEXT("DialoguePanel"));
    Panel->SetupAttachment(GetRootComponent());
    Panel->SetWidgetSpace(EWidgetSpace::World);
    Panel->SetWidgetClass(URegularCustomerWidget::StaticClass());
    Panel->SetDrawSize(FVector2D(960, 560));
    Panel->SetPivot(FVector2D(.5, .5));
    Panel->SetRelativeScale3D(FVector(.08));
    Panel->SetTwoSided(false);
    Panel->SetBlendMode(EWidgetBlendMode::Transparent);
    Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Panel->SetGenerateOverlapEvents(false);
    CustomerName = RegularText(TEXT("夜班常客"));
}

void ARegularCustomerEncounter::BeginPlay()
{
    Super::BeginPlay();
    Panel->InitWidget();
    if (bAutoStart) StartEncounter();
    else Panel->SetVisibility(false);
}

void ARegularCustomerEncounter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    FaceLocalPlayer();
}

void ARegularCustomerEncounter::FaceLocalPlayer()
{
    if (!bFacePlayer || bEnding || IsHidden() || !Panel || !Panel->IsVisible()) return;
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    APawn* Pawn = PC && PC->IsLocalController() ? PC->GetPawn() : nullptr;
    UCameraComponent* Camera = Pawn ? Pawn->FindComponentByClass<UCameraComponent>() : nullptr;
    if (Camera && Camera->IsActive()) UpdatePanelFacing(Camera->GetComponentLocation());
}

bool ARegularCustomerEncounter::UpdatePanelFacing(const FVector& ViewLocation)
{
    if (!Panel || ViewLocation.ContainsNaN()) return false;
    const FVector Direction = ViewLocation - Panel->GetComponentLocation();
    if (Direction.ContainsNaN() || Direction.IsNearlyZero()) return false;
    // The widget's front is +X. Face the real eye position in yaw and pitch;
    // keep roll at zero and leave its world-space location anchored by the NPC.
    Panel->SetWorldRotation(Direction.Rotation());
    return true;
}

bool ARegularCustomerEncounter::StartEncounter()
{
    if (bEnding || Visit.IsValid() || !GetWorld() || !IsValid(Customer) || Customer->GetWorld() != GetWorld() ||
        !IsValid(ServingSpot) || ServingSpot->GetWorld() != GetWorld() || !IsValid(Recipe) || CustomerId.IsNone() ||
        !Customer->CustomerOrder || !Customer->CustomerDrink || !Customer->CustomerDrink->bEnabled ||
        Customer->CustomerOrder->IsOrderActive() || !ServingSpot->TryClaimOrder(this)) return false;
    Visit = FGuid::NewGuid();
    Memory = GetGameInstance() ? GetGameInstance()->GetSubsystem<URegularCustomerMemorySubsystem>() : nullptr;
    Events = UCyberCafeEventSubsystem::Get(this);
    ServingSpot->SetSubmissionEnabled(false);
    Panel->SetVisibility(true);
    FaceLocalPlayer();
    Stage = ERegularCustomerStage::Greeting;
    Choices = {RegularText(TEXT("今天想聊聊？")), RegularText(TEXT("先给你做一杯。"))};
    const FRegularCustomerMemory Previous = Memory ? Memory->Recall(CustomerId) : FRegularCustomerMemory();
    FString Greeting = TEXT("又是一个长夜。能给我做杯冰拿铁吗？咖啡和牛奶平衡一点，要有冰。");
    if (Previous.CompletedVisits > 0)
    {
        const FString Name = Previous.LastDrinkName.IsEmpty() ? TEXT("冰拿铁") : Previous.LastDrinkName;
        Greeting = FString::Printf(TEXT("又见面了。上次那杯%s我还记得，今天也想来一杯。"), *Name);
        if (Previous.LastTopic == TEXT("NightShift")) Greeting += TEXT(" 你上次听我说夜班的事，让那个晚上轻松了些。");
        else if (Previous.LastTopic == TEXT("City")) Greeting += TEXT(" 你上次说起这座城，我后来真的绕路看了看霓虹。");
    }
    if (OrderDisplay) OrderDisplay->RevealFirstOrderRecipe(Recipe);
    Present(Greeting);
    return true;
}

bool ARegularCustomerEncounter::Choose(int32 Index)
{
    if (bEnding || !Choices.IsValidIndex(Index)) return false;
    if (Stage == ERegularCustomerStage::Greeting)
    {
        if (Index == 0)
        {
            Stage = ERegularCustomerStage::TopicChoice;
            Choices = {RegularText(TEXT("夜班很累吧，慢慢说。")), RegularText(TEXT("今晚城里有什么新鲜事？"))};
            Present(TEXT("刚从高架桥下的维修站出来。机器的噪音停了，脑子里倒还在响。你想听哪一段？"));
            return true;
        }
        Topic = NAME_None;
        return BeginDrinkOrder();
    }
    if (Stage == ERegularCustomerStage::TopicChoice)
    {
        Topic = Index == 0 ? FName(TEXT("NightShift")) : FName(TEXT("City"));
        return BeginDrinkOrder();
    }
    if ((Stage == ERegularCustomerStage::Completed || Stage == ERegularCustomerStage::Interrupted) && Index == 0)
    {
        Choices.Empty(); RefreshPanel();
        UGameplayStatics::OpenLevel(this, FName(*UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())));
        return true;
    }
    return false;
}

bool ARegularCustomerEncounter::BeginDrinkOrder()
{
    if (!IsValid(Customer) || !IsValid(ServingSpot) || !IsValid(Recipe)) return false;
    ServingSpot->ReleaseOrder(this);
    if (!Customer->CustomerOrder->BeginOrder(ServingSpot, Recipe, 1.5f))
    {
        Stage = ERegularCustomerStage::Interrupted;
        Choices = {RegularText(TEXT("重新进入这次体验"))};
        Present(TEXT("出品点暂时没有准备好，重新开始这次见面吧。"));
        return true; // The choice was consumed even though the order could not start.
    }
    OwnedOrderSerial = ServingSpot->GetOrderSerial();
    Customer->CustomerOrder->OnTastingFinished.AddUniqueDynamic(this, &ARegularCustomerEncounter::HandleFinished);
    ServingSpot->OnDrinkValidated.AddUniqueDynamic(this, &ARegularCustomerEncounter::HandleValidation);
    if (Events) Events->OnLiquidPoured.AddUniqueDynamic(this, &ARegularCustomerEncounter::HandlePour);
    if (OrderDisplay) OrderDisplay->BeginOrder(Recipe, ServingSpot, Customer, CustomerName);
    Stage = ERegularCustomerStage::Preparing;
    Choices.Empty();
    NextCommentTime = GetWorld()->GetTimeSeconds() + 12;
    FString Response = TEXT("好，先不打扰你。冰拿铁，咖啡和牛奶各一半，要有冰。我在这里等。");
    if (Topic == TEXT("NightShift")) Response = TEXT("今晚修好了最后一辆车，乘客才赶上末班线。忙了一夜，总算有件值得高兴的事。你慢慢做，我歇一会儿。");
    else if (Topic == TEXT("City")) Response = TEXT("高架桥下那块坏掉的招牌终于亮了。还是老颜色，却像换了一条街。有空你也去看看。");
    Present(Response);
    return true;
}

void ARegularCustomerEncounter::HandleValidation(AServingSpotActor* Spot, ALiquidContainerActor* Cup, const FDrinkValidationResult& Result)
{
    if (bEnding || Stage != ERegularCustomerStage::Preparing || Spot != ServingSpot ||
        !IsValid(Spot) || Spot->GetOrderSerial() != OwnedOrderSerial) return;
    if (Result.bAccepted)
    {
        if (!Customer || !Customer->CustomerOrder->OwnsCup(Cup) || Spot->GetServedCup() != Cup) return;
        Accepted = Result;
        AcceptedCup = Cup;
        Stage = ERegularCustomerStage::Tasting;
        Present(TEXT("谢谢，先让我尝一口。"));
    }
    else if (GetWorld()->GetTimeSeconds() >= NextCommentTime)
    {
        Present(Result.Feedback.IsEmpty() ? TEXT("这杯还需要调整，看看订单屏的配方吧。") : Result.Feedback.ToString());
        NextCommentTime = GetWorld()->GetTimeSeconds() + 12;
    }
}

void ARegularCustomerEncounter::HandlePour(AActor* From, AActor* To, float AcceptedML)
{
    if (bEnding || Stage != ERegularCustomerStage::Preparing || bCommentedPour || !IsValid(To) ||
        !To->IsA<ACupActor>() || !ServingSpot || !GetWorld() || !FMath::IsFinite(AcceptedML) || AcceptedML <= 0 ||
        FVector::DistSquared(To->GetActorLocation(), ServingSpot->GetActorLocation()) > FMath::Square(150.) ||
        GetWorld()->GetTimeSeconds() < NextCommentTime) return;
    bCommentedPour = true;
    Present(TEXT("看着你慢慢把它调好，维修站的噪音好像也远了一点。"));
    NextCommentTime = GetWorld()->GetTimeSeconds() + 20;
}

void ARegularCustomerEncounter::HandleFinished(ALiquidContainerActor* Cup, bool bCompleted, double ConsumedML)
{
    if (bEnding || Stage != ERegularCustomerStage::Tasting || Cup != AcceptedCup.Get()) return;
    const bool Success = bCompleted && Accepted.bAccepted && Customer && Customer->CustomerOrder &&
        Customer->CustomerOrder->GetState() == ECustomerOrderState::Completed &&
        FMath::IsFinite(ConsumedML) && ConsumedML > 0;
    Unbind();
    Choices = {RegularText(Success ? TEXT("下次见（重新进入，验证回访）") : TEXT("重新进入这次体验"))};
    if (!Success)
    {
        Stage = ERegularCustomerStage::Interrupted;
        Present(TEXT("这一口没能喝完，先放下吧。这次不会记成已经喝过的饮品。"));
        return;
    }
    Stage = ERegularCustomerStage::Completed;
    const FString DrinkName = Recipe->DisplayName.IsEmpty() ? TEXT("冰拿铁") : Recipe->DisplayName.ToString();
    if (Memory) Memory->RememberCompleted(CustomerId, Visit, Accepted.RecipeId, DrinkName, Topic, Accepted.CoffeeFraction, Accepted.IceCount);
    const FString Balance = Accepted.CoffeeFraction > .51 ? TEXT("这一杯咖啡的存在感稍微多一点") :
        (Accepted.CoffeeFraction < .49 ? TEXT("这一杯牛奶的比例稍微多一点") : TEXT("咖啡和牛奶的比例很平衡"));
    Present(FString::Printf(TEXT("%s，出品时有%d块冰。这杯我记住了。今晚终于可以缓一口气，下次还来找你。"), *Balance, Accepted.IceCount));
}

void ARegularCustomerEncounter::Present(const FString& Text)
{
    Line = RegularText(Text);
    RefreshPanel();
}

void ARegularCustomerEncounter::RefreshPanel()
{
    if (URegularCustomerWidget* Widget = Cast<URegularCustomerWidget>(Panel->GetUserWidgetObject()))
    {
        FString Status;
        if (Stage == ERegularCustomerStage::Preparing)
            Status = TEXT("冰拿铁：Highball杯 / 咖啡100ml + 牛奶100ml / 要有冰。配方详见全息屏。");
        else if (Stage == ERegularCustomerStage::Tasting) Status = TEXT("正在品尝……");
        else if (Stage == ERegularCustomerStage::Completed)
            Status = Memory && Memory->IsPersisted() ? TEXT("已记住这次见面。") : TEXT("本次见面仅保留在内存中，存档未成功写入。");
        else Status = TEXT("空手指向选项，轻扣扳机回应。");
        Widget->Show(CustomerName, Line, RegularText(Status), Choices);
    }
}

void ARegularCustomerEncounter::Unbind()
{
    if (Customer && Customer->CustomerOrder) Customer->CustomerOrder->OnTastingFinished.RemoveDynamic(this, &ARegularCustomerEncounter::HandleFinished);
    if (ServingSpot) ServingSpot->OnDrinkValidated.RemoveDynamic(this, &ARegularCustomerEncounter::HandleValidation);
    if (Events) Events->OnLiquidPoured.RemoveDynamic(this, &ARegularCustomerEncounter::HandlePour);
}

void ARegularCustomerEncounter::CancelEncounter()
{
    if (Stage == ERegularCustomerStage::Completed || Stage == ERegularCustomerStage::Interrupted || !Visit.IsValid()) return;
    Unbind();
    if (Customer && Customer->CustomerOrder && (Stage == ERegularCustomerStage::Preparing || Stage == ERegularCustomerStage::Tasting))
        Customer->CustomerOrder->CancelOrder();
    if (ServingSpot) ServingSpot->ReleaseOrder(this);
    if (OrderDisplay) OrderDisplay->CancelOrder(ServingSpot, OwnedOrderSerial);
    Stage = ERegularCustomerStage::Interrupted;
    Choices = {RegularText(TEXT("重新进入这次体验"))};
    Present(TEXT("那就下次再聊吧。这次没有记成完成的饮品。"));
}

void ARegularCustomerEncounter::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding = true;
    CancelEncounter();
    Unbind();
    Super::EndPlay(Reason);
}

int32 ARegularCustomerEncounter::ActionAt(const FVector& Point) const
{
    if (bEnding || IsHidden() || !Panel || !Panel->IsVisible() || Point.ContainsNaN()) return INDEX_NONE;
    const FVector Local = Panel->GetComponentTransform().InverseTransformPosition(Point);
    if (FMath::Abs(Local.X) > .1) return INDEX_NONE;
    const FIntPoint Pixel(FMath::FloorToInt(480 - Local.Y), FMath::FloorToInt(280 - Local.Z));
    for (int32 I = 0; I < Choices.Num(); ++I)
        if (URegularCustomerWidget::ChoiceRect(I).Contains(Pixel)) return I;
    return INDEX_NONE;
}

bool ARegularCustomerEncounter::TracePointer(const FVector& Start, const FVector& Direction, double Limit, FMenuPointerHit& Hit) const
{
    Hit = FMenuPointerHit();
    if (bEnding || IsHidden() || !Panel || !Panel->IsVisible() || Start.ContainsNaN() || Direction.ContainsNaN() ||
        !FMath::IsFinite(Limit) || Limit <= 0 || FMath::Abs(Direction.SizeSquared() - 1) > .001) return false;
    const FTransform T = Panel->GetComponentTransform();
    const FVector S = T.InverseTransformPosition(Start), D = T.InverseTransformVector(Direction);
    if (S.ContainsNaN() || D.ContainsNaN() || S.X <= 0 || D.X >= -.001) return false;
    const double Distance = -S.X / D.X;
    if (!FMath::IsFinite(Distance) || Distance <= 0 || Distance > FMath::Min(Limit, 250.)) return false;
    const FVector Local = S + D * Distance;
    if (FMath::Abs(Local.Y) > 480 || FMath::Abs(Local.Z) > 280) return false;
    Hit.Point = Start + Direction * Distance;
    Hit.Distance = Distance; Hit.Action = ActionAt(Hit.Point); Hit.bPanel = true;
    return true;
}

bool ARegularCustomerEncounter::ActivatePointer(EControllerHand Hand, const FMenuPointerHit& Hit)
{
    if ((Hand != EControllerHand::Left && Hand != EControllerHand::Right) || LastClickFrame == GFrameCounter ||
        Hit.Action == INDEX_NONE || ActionAt(Hit.Point) != Hit.Action) return false;
    const uint64 Previous = LastClickFrame;
    LastClickFrame = GFrameCounter;
    const bool AcceptedChoice = Choose(Hit.Action);
    if (!AcceptedChoice) LastClickFrame = Previous;
    return AcceptedChoice;
}
