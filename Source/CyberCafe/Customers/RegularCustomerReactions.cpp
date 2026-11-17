#include "Customers/RegularCustomerReactions.h"
#include "Core/Events/CyberCafeEventSubsystem.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Liquid/CupActor.h"
#include "Serving/ServingSpotActor.h"
#include "Serving/OrderHologramActor.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"

URegularCustomerReactions::URegularCustomerReactions()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void URegularCustomerReactions::BeginPreparation(AServingSpotActor* Spot, AOrderHologramActor* Display)
{
    StopReactions();
    if (!IsValid(Spot) || Spot->GetWorld() != GetWorld()) return;
    Station = Spot; Menu = Display; bPreparing = true;
    Seen.Empty(); SpokenCount = 0; AmbientCount = 0; bObservedGrinding = false;
    Events = UCyberCafeEventSubsystem::Get(this);
    if (Events)
    {
        Events->OnAnyObjectGrabbed.AddUniqueDynamic(this, &URegularCustomerReactions::HandleGrab);
        Events->OnLiquidPoured.AddUniqueDynamic(this, &URegularCustomerReactions::HandlePour);
    }
    double Closest = FMath::Square(250.);
    for (TActorIterator<ACoffeeGrinderActor> It(GetWorld()); It; ++It)
    {
        const double Distance = FVector::DistSquared(It->GetActorLocation(), Spot->GetActorLocation());
        if (Distance < Closest) { Closest = Distance; Grinder = *It; }
    }
    if (Grinder.IsValid())
    {
        PreviousGrounds = Grinder->TotalGroundOutputGrams;
        Grinder->OnGrindProgress.AddUniqueDynamic(this, &URegularCustomerReactions::HandleGrind);
    }
    if (Menu.IsValid()) MenuHandle = Menu->OnMenuInteractionNative.AddWeakLambda(this, [this](AOrderHologramActor*)
        { MenuReadingUntil = GetWorld()->GetTimeSeconds() + 5; });
}

void URegularCustomerReactions::StopReactions()
{
    bPreparing = false; PendingLine.Empty(); PendingId = NAME_None; PendingFocus.Reset();
    if (Events)
    {
        Events->OnAnyObjectGrabbed.RemoveDynamic(this, &URegularCustomerReactions::HandleGrab);
        Events->OnLiquidPoured.RemoveDynamic(this, &URegularCustomerReactions::HandlePour);
    }
    if (Grinder.IsValid()) Grinder->OnGrindProgress.RemoveDynamic(this, &URegularCustomerReactions::HandleGrind);
    if (Menu.IsValid()) Menu->OnMenuInteractionNative.Remove(MenuHandle);
    MenuHandle.Reset(); Grinder.Reset(); Menu.Reset(); Station.Reset();
}

void URegularCustomerReactions::HoldForDialogue(const FString& Text)
{
    if (GetWorld()) NextSpeech = FMath::Max(NextSpeech, GetWorld()->GetTimeSeconds() + FMath::Clamp(Text.Len() / 7., 6., 14.));
}

void URegularCustomerReactions::Queue(FName Id, const FString& Text, AActor* Focus, int32 Priority)
{
    if (!bPreparing || !Station.IsValid() || (Priority < 70 && AmbientCount >= 4) || Seen.Contains(Id) || Text.IsEmpty()) return;
    if (!PendingLine.IsEmpty() && GetWorld()->GetTimeSeconds() <= PendingUntil && Priority < PendingPriority) return;
    Seen.Add(Id); PendingId = Id; PendingLine = Text; PendingFocus = Focus; PendingPriority = Priority;
    PendingUntil = GetWorld()->GetTimeSeconds() + 18;
}

void URegularCustomerReactions::QueueRejection(FName Reason, const FString& Text)
{
    Queue(Reason, Text, nullptr, 70);
}

bool URegularCustomerReactions::IsReadingMenu() const
{
    if (GetWorld()->GetTimeSeconds() < MenuReadingUntil) return true;
    if (!Menu.IsValid() || !Menu->IsPanelOpen() || !Menu->Panel) return false;
    const auto* PC = UGameplayStatics::GetPlayerController(this, 0);
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    const auto* Camera = Pawn ? Pawn->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!Camera || !Camera->IsActive()) return false;
    FMenuPointerHit Hit;
    return Menu->TraceMenuPointer(Camera->GetComponentLocation(), Camera->GetForwardVector(), 250, Hit) && Hit.bPanel;
}

void URegularCustomerReactions::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Dt, TickType, ThisTickFunction);
    if (!bPreparing || PendingLine.IsEmpty() || !GetWorld()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now > PendingUntil)
    {
        if (PendingPriority >= 70) Seen.Remove(PendingId); // A later submission can request fresh feedback.
        PendingLine.Empty(); PendingFocus.Reset(); return;
    }
    if (Now < NextSpeech || IsReadingMenu()) return;
    const FString Text = MoveTemp(PendingLine);
    const TWeakObjectPtr<AActor> Focus = PendingFocus;
    PendingLine.Empty(); PendingFocus.Reset();
    NextSpeech = Now + 12; ++SpokenCount;
    if (PendingPriority < 70) ++AmbientCount;
    OnReady.Broadcast(Text, Focus.Get());
}

void URegularCustomerReactions::HandleGrab(AActor* Object, EControllerHand Hand)
{
    if (!IsValid(Object) || !Object->IsA<ACupActor>() || !Station.IsValid() ||
        FVector::DistSquared(Object->GetActorLocation(), Station->GetActorLocation()) > FMath::Square(150.)) return;
    Queue(TEXT("Cup"), TEXT("就看你把杯子拿起来，我已经开始期待这一口了。慢慢来。"), Object, 20);
    OnActivity.Broadcast(Object);
}

void URegularCustomerReactions::HandlePour(AActor* From, AActor* To, float ML)
{
    if (!IsValid(To) || !To->IsA<ACupActor>() || !FMath::IsFinite(ML) || ML <= 0 || !Station.IsValid() ||
        FVector::DistSquared(To->GetActorLocation(), Station->GetActorLocation()) > FMath::Square(150.)) return;
    Queue(TEXT("Pour"), TEXT("看着你把它一点点调好，维修站的噪音好像也远了一点。"), To, 30);
    OnActivity.Broadcast(To);
}

void URegularCustomerReactions::HandleGrind(float Beans, float Grounds)
{
    if (!FMath::IsFinite(Grounds) || !Grinder.IsValid()) return;
    const double Produced = Grinder->TotalGroundOutputGrams;
    // The drawer can drain the internal reservoir during this same broadcast.
    // Observe production, not the transient amount still inside the grinder.
    if (FMath::IsFinite(Produced) && Produced > 0 && !FMath::IsNearlyEqual(Produced, PreviousGrounds, .000001))
    {
        bObservedGrinding = true;
        OnActivity.Broadcast(Grinder.Get());
        Queue(TEXT("Grind"), TEXT("磨豆的声音比维修站好听多了。这里的节奏，倒让我放松下来。"), Grinder.Get(), 40);
    }
    PreviousGrounds = Produced;
}

void URegularCustomerReactions::EndPlay(const EEndPlayReason::Type Reason)
{
    StopReactions(); OnReady.Clear(); OnActivity.Clear();
    Super::EndPlay(Reason);
}
