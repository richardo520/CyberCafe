#include "Customers/RegularCustomerEncounter.h"
#include "Customers/RegularCustomerMemory.h"
#include "Customers/RegularCustomerReactions.h"
#include "Customers/RegularCustomerAttention.h"
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
    Reactions = CreateDefaultSubobject<URegularCustomerReactions>(TEXT("Reactions"));
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
    PreviousVisit = Memory ? Memory->Recall(CustomerId) : FRegularCustomerMemory();
    const FRegularCustomerMemory& Previous = PreviousVisit;
    Reactions->OnReady.AddUObject(this, &ARegularCustomerEncounter::HandleReaction);
    Reactions->OnActivity.AddWeakLambda(this, [this](AActor* Target) { if (Attention) Attention->LookAtActivity(Target); });
    if (AttentionLayer)
    {
        Attention = Customer->FindComponentByClass<URegularCustomerAttention>();
        if (!Attention)
        {
            Attention = NewObject<URegularCustomerAttention>(Customer, NAME_None, RF_Transient);
            Customer->AddInstanceComponent(Attention); Attention->RegisterComponent();
        }
        if (!Attention->InitializeLayer(AttentionLayer))
            UE_LOG(LogTemp, Warning, TEXT("[RegularCustomer] Could not attach head attention; existing animation configuration preserved."));
    }
    FString Greeting = TEXT("又是一个长夜。能给我做杯冰拿铁吗？咖啡和牛奶平衡一点，要有冰。");
    if (Previous.CompletedVisits > 0)
    {
        const FString Name = Previous.LastDrinkName.IsEmpty() ? TEXT("冰拿铁") : Previous.LastDrinkName;
        Greeting = FString::Printf(TEXT("又见面了。上次那杯%s我还记得，今天也想来一杯。"), *Name);
        if (Previous.LastTopic == TEXT("NightShift")) Greeting += TEXT(" 还记得上次夜班那辆赶末班线的车吗？后来有了新消息。");
        else if (Previous.LastTopic == TEXT("City")) Greeting += TEXT(" 上次聊的那块霓虹招牌，现在又有新故事了。");
        if (Previous.bLastObservedGrinding) Greeting += TEXT(" 也记得你上次磨豆时的声音。");
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
            Choices = {RegularText(PreviousVisit.GetTopicVisits(TEXT("NightShift")) > 0 ? TEXT("上次那位乘客后来怎么样了？") : TEXT("夜班很累吧，慢慢说。")),
                RegularText(PreviousVisit.GetTopicVisits(TEXT("City")) > 0 ? TEXT("那块霓虹招牌后来怎么样了？") : TEXT("今晚城里有什么新鲜事？"))};
            Present(PreviousVisit.CompletedVisits > 0 ? TEXT("今天有一段后续想告诉你。你想先听哪件事？") :
                TEXT("刚从高架桥下的维修站出来。机器的噪音停了，脑子里倒还在响。你想听哪一段？"));
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
    Reactions->BeginPreparation(ServingSpot, OrderDisplay);
    if (OrderDisplay) OrderDisplay->BeginOrder(Recipe, ServingSpot, Customer, CustomerName);
    Stage = ERegularCustomerStage::Preparing;
    Choices.Empty();
    FString Response = TEXT("好，先不打扰你。冰拿铁，咖啡和牛奶各一半，要有冰。我在这里等。");
    if (Topic == TEXT("NightShift")) Response = TEXT("今晚修好了最后一辆车，乘客才赶上末班线。忙了一夜，总算有件值得高兴的事。你慢慢做，我歇一会儿。");
    else if (Topic == TEXT("City")) Response = TEXT("高架桥下那块坏掉的招牌终于亮了。还是老颜色，却像换了一条街。有空你也去看看。");
    if (Topic == TEXT("NightShift") && PreviousVisit.GetTopicVisits(Topic) == 1)
        Response = TEXT("上次那位乘客回维修站了，带着一张海边的明信片。原来那班车送她去见一个很久没见的人。我把明信片贴在工具柜上了。");
    else if (Topic == TEXT("NightShift") && PreviousVisit.GetTopicVisits(Topic) >= 2)
        Response = TEXT("工具柜上那张明信片，现在成了大家交班时都要看一眼的东西。新来的学徒说，想把站里其他旧车也修好，让它们再跑远一点。");
    if (Topic == TEXT("City") && PreviousVisit.GetTopicVisits(Topic) == 1)
        Response = TEXT("你还记得那块招牌吗？店主给它加了盏小灯，专门照着雨天容易踩空的台阶。现在经过那里的人，都会慢下来一点。");
    else if (Topic == TEXT("City") && PreviousVisit.GetTopicVisits(Topic) >= 2)
        Response = TEXT("招牌下面多了一张长椅。昨晚见到两个不认识的人在那里躲雨，最后聊到雨停。那条街好像真的有点不一样了。");
    if (Attention) Attention->Acknowledge();
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
        Reactions->StopReactions();
        if (Attention) Attention->PrepareForTasting();
        Stage = ERegularCustomerStage::Tasting;
        Present(TEXT("谢谢，先让我尝一口。"));
    }
    else
    {
        Reactions->QueueRejection(FName(*FString::Printf(TEXT("Reject%d"), int32(Result.Reason))),
            Result.Feedback.IsEmpty() ? TEXT("这杯还需要调整，看看订单屏的配方吧。") : Result.Feedback.ToString());
    }
}

void ARegularCustomerEncounter::HandleReaction(const FString& Text, AActor* Focus)
{
    if (bEnding || Stage != ERegularCustomerStage::Preparing) return;
    if (Attention && Focus) Attention->LookAtActivity(Focus);
    Present(Text);
}

void ARegularCustomerEncounter::HandleFinished(ALiquidContainerActor* Cup, bool bCompleted, double ConsumedML)
{
    if (bEnding || (Stage != ERegularCustomerStage::Tasting && Stage != ERegularCustomerStage::Preparing) || Cup != AcceptedCup.Get()) return;
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
    if (Attention) Attention->SetAttentionEnabled(true);
    const FString DrinkName = Recipe->DisplayName.IsEmpty() ? TEXT("冰拿铁") : Recipe->DisplayName.ToString();
    if (Memory) Memory->RememberCompleted(CustomerId, Visit, Accepted.RecipeId, DrinkName, Topic, Accepted.CoffeeFraction, Accepted.IceCount, Reactions->DidObserveGrinding());
    if (Attention) Attention->Acknowledge();
    const FString Balance = Accepted.CoffeeFraction > .51 ? TEXT("这一杯咖啡的存在感稍微多一点") :
        (Accepted.CoffeeFraction < .49 ? TEXT("这一杯牛奶的比例稍微多一点") : TEXT("咖啡和牛奶的比例很平衡"));
    Present(FString::Printf(TEXT("%s，出品时有%d块冰。这杯我记住了。今晚终于可以缓一口气，下次还来找你。"), *Balance, Accepted.IceCount));
}

void ARegularCustomerEncounter::Present(const FString& Text)
{
    Line = RegularText(Text);
    Reactions->HoldForDialogue(Text);
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
    Reactions->StopReactions();
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
    if (Attention) Attention->SetAttentionEnabled(false);
    Choices = {RegularText(TEXT("重新进入这次体验"))};
    Present(TEXT("那就下次再聊吧。这次没有记成完成的饮品。"));
}

void ARegularCustomerEncounter::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding = true;
    if (Attention) Attention->SetAttentionEnabled(false);
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
