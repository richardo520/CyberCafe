#include "Customers/RegularCustomerEncounter.h"
#include "Customers/RegularCustomerMemory.h"
#include "Customers/RegularCustomerReactions.h"
#include "Customers/RegularCustomerAttention.h"
#include "Customers/RegularCustomerConversation.h"
#include "Dialogue/RegularDialogueClient.h"
#include "Customers/CustomerOrderComponent.h"
#include "Customers/CustomerDrinkComponent.h"
#include "Core/Events/CyberCafeEventSubsystem.h"
#include "Tutorial/TutorialNPC.h"
#include "Serving/ServingSpotActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/GlassRecoveryActor.h"
#include "Liquid/LiquidConsumptionComponent.h"
#include "Serving/DrinkMenuData.h"
#include "EngineUtils.h"
#include "UI/RegularCustomerWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
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
    DialogueClient = CreateDefaultSubobject<URegularDialogueClient>(TEXT("CloudDialogue"));
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
    DialogueNoticeAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("DialogueNoticeAudio"));
    DialogueNoticeAudio->SetupAttachment(GetRootComponent());
    DialogueNoticeAudio->bAutoActivate = false;
    DialogueNoticeAudio->bAutoDestroy = false;
    DialogueNoticeAudio->SetVolumeMultiplier(.4f);
    static ConstructorHelpers::FObjectFinder<USoundBase> NoticeSound(TEXT("/Game/CyberCafe/UI/Audio/S_NarrativeVoiceTick.S_NarrativeVoiceTick"));
    DialogueNoticeAudio->SetSound(NoticeSound.Object);
    static ConstructorHelpers::FObjectFinder<USoundAttenuation> NoticeAttenuation(TEXT("/Game/CyberCafe/UI/Audio/SA_NarrativeVoice.SA_NarrativeVoice"));
    DialogueNoticeAudio->AttenuationSettings = NoticeAttenuation.Object;
    static ConstructorHelpers::FObjectFinder<UDrinkRecipe> Orange(TEXT("/Game/CyberCafe/Gameplay/Serving/Recipes/Menu/DA_OrangeCoffee.DA_OrangeCoffee"));
    static ConstructorHelpers::FObjectFinder<UDrinkRecipe> Berry(TEXT("/Game/CyberCafe/Gameplay/Serving/Recipes/Menu/DA_StrawberryLatte.DA_StrawberryLatte"));
    FollowupRecipes = {Orange.Object, Berry.Object};
    CustomerName = RegularText(TEXT("夜班常客"));
}

void ARegularCustomerEncounter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    UpdatePanelScale();
}

void ARegularCustomerEncounter::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    UpdatePanelScale();
}

void ARegularCustomerEncounter::UpdatePanelScale()
{
    if (!Panel) return;
    // Preserve the authored width (widget horizontal axis is local Y), while
    // isolating glyphs from nonuniform parent scaling and billboard rotation.
    const double Scale = FMath::Clamp(double(PanelScale), .01, .3) * FMath::Max(FMath::Abs(GetActorScale3D().Y), .01);
    Panel->SetAbsolute(false, false, true);
    Panel->SetWorldScale3D(FVector(Scale));
}

void ARegularCustomerEncounter::BeginPlay()
{
    Super::BeginPlay();
    UpdatePanelScale();
    if (IsValid(Customer) && Customer->GetMesh())
    {
        DialogueNoticeAudio->AttachToComponent(Customer->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale,
            Customer->GetMesh()->DoesSocketExist(TEXT("head")) ? FName(TEXT("head")) : NAME_None);
    }
    Panel->InitWidget();
    if (bAutoStart) StartEncounter();
    else Panel->SetVisibility(false);
}

void ARegularCustomerEncounter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsHidden() || (Panel && !Panel->IsVisible())) StopDialogueNotice();
    UpdatePanelScale();
    FaceLocalPlayer();
    if (DeferredCloudUntil > 0)
    {
        if (DialogueClient->GetRequestTime() > DeferredCloudUntil || Stage != ERegularCustomerStage::Preparing || bEnding)
        { DeferredCloudLine.Empty(); DeferredCloudUntil = 0; bWaitingForCloud = false; ++PresentationSerial; DialogueClient->Cancel(); RefreshLink(); }
        else if (!DeferredCloudLine.IsEmpty() && !Reactions->IsPlayerReadingMenu())
        { const FString Ready = MoveTemp(DeferredCloudLine); DeferredCloudUntil = 0; CommitLine(Ready, DeferredSource, DeferredReason); }
    }
    LinkRefreshElapsed += DeltaSeconds;
    if (LinkRefreshElapsed >= .1f) { LinkRefreshElapsed = 0; RefreshLink(); }
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
    InitialRecipe = Recipe;
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
        Greeting = FString::Printf(TEXT("又见面了。上次那杯%s我还记得，今天想先来杯%s。"), *Name, *CurrentRecipeName());
        if (Previous.LastTopic == TEXT("NightShift")) Greeting += TEXT(" 还记得上次夜班那辆赶末班线的车吗？后来有了新消息。");
        else if (Previous.LastTopic == TEXT("City")) Greeting += TEXT(" 上次聊的那块霓虹招牌，现在又有新故事了。");
        if (Previous.bLastObservedGrinding) Greeting += TEXT(" 也记得你上次磨豆时的声音。");
    }
    if (OrderDisplay) OrderDisplay->RevealFirstOrderRecipe(Recipe);
    Present(Greeting, TEXT("Greeting"));
    return true;
}

bool ARegularCustomerEncounter::Choose(int32 Index)
{
    if (bEnding || !Choices.IsValidIndex(Index)) return false;
    LastPlayerReply = Choices[Index].ToString();
    if (Stage == ERegularCustomerStage::Greeting)
    {
        if (Index == 0)
        {
            Stage = ERegularCustomerStage::TopicChoice;
            Choices = {RegularText(PreviousVisit.GetTopicVisits(TEXT("NightShift")) > 0 ? TEXT("上次那位乘客后来怎么样了？") : TEXT("夜班很累吧，慢慢说。")),
                RegularText(PreviousVisit.GetTopicVisits(TEXT("City")) > 0 ? TEXT("那块霓虹招牌后来怎么样了？") : TEXT("今晚城里有什么新鲜事？"))};
            Present(PreviousVisit.CompletedVisits > 0 ? TEXT("今天有一段后续想告诉你。你想先听哪件事？") :
                TEXT("刚从高架桥下的维修站出来。机器的噪音停了，脑子里倒还在响。你想听哪一段？"), TEXT("TopicPrompt"));
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
    if (Stage == ERegularCustomerStage::Completed || Stage == ERegularCustomerStage::Chatting)
    {
        ContinueConversation(Index);
        return true;
    }
    if (Stage == ERegularCustomerStage::ReorderOffer)
    {
        if (Index == 0) return BeginNextDrink();
        TurnsSinceOffer = -1;
        Stage = ERegularCustomerStage::Chatting;
        Choices = {RegularText(TEXT("继续说，我听着。")), RegularText(TEXT("换个话题吧。"))};
        Present(TEXT("好，那就先不点。难得不赶时间，我想再坐一会儿，听听这里的声音，也跟你聊聊。"), TEXT("Conversation"));
        return true;
    }
    if (Stage == ERegularCustomerStage::Interrupted && Index == 0)
    {
        Choices.Empty(); RefreshPanel();
        ++PresentationSerial; DialogueClient->Cancel(); DeferredCloudLine.Empty(); DeferredCloudUntil = 0;
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
    DrinkOrderId = FGuid::NewGuid();
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
    if (DrinksThisVisit > 0) Response = FString::Printf(TEXT("那就来杯%s。配方按全息订单屏来，慢慢做，我还在这里陪你聊。"), *CurrentRecipeName());
    if (Attention) Attention->Acknowledge();
    Present(Response, TEXT("OrderStory"));
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

void ARegularCustomerEncounter::HandleReaction(FName Cue, const FString& Text, AActor* Focus)
{
    if (bEnding || Stage != ERegularCustomerStage::Preparing) return;
    if (Attention && Focus) Attention->LookAtActivity(Focus);
    Present(Text, Cue);
}

void ARegularCustomerEncounter::HandleFinished(ALiquidContainerActor* Cup, bool bCompleted, double ConsumedML)
{
    if (bEnding || !Accepted.bAccepted || (Stage != ERegularCustomerStage::Tasting && Stage != ERegularCustomerStage::Preparing) || Cup != AcceptedCup.Get()) return;
    const bool Success = bCompleted && Accepted.bAccepted && Customer && Customer->CustomerOrder &&
        Customer->CustomerOrder->GetState() == ECustomerOrderState::Completed &&
        FMath::IsFinite(ConsumedML) && ConsumedML > 0;
    Unbind();
    Choices = Success ? TArray<FText>{RegularText(TEXT("说说你接下来的打算。")), RegularText(TEXT("聊聊这座城吧。"))}
        : TArray<FText>{RegularText(TEXT("重新进入这次体验"))};
    if (!Success)
    {
        Stage = ERegularCustomerStage::Interrupted;
        Present(TEXT("这一口没能喝完，先放下吧。这次不会记成已经喝过的饮品。"));
        return;
    }
    Stage = ERegularCustomerStage::Completed;
    if (Attention) Attention->SetAttentionEnabled(true);
    ++DrinksThisVisit;
    TurnsSinceOffer = 0;
    const FString DrinkName = CurrentRecipeName();
    if (Memory) Memory->RememberCompleted(CustomerId, Visit, Accepted.RecipeId, DrinkName, Topic, Accepted.CoffeeFraction, Accepted.IceCount, Reactions->DidObserveGrinding(), DrinkOrderId);
    if (Attention) Attention->Acknowledge();
    const bool bClassicLatte = Recipe && Recipe->Rules.Num() == 2 && Recipe->Rules.ContainsByPredicate([](const FDrinkIngredientRule& R) { return R.IngredientId == TEXT("Milk"); });
    const FString Balance = bClassicLatte ? (Accepted.CoffeeFraction > .51 ? TEXT("这一杯咖啡的存在感稍微多一点") :
        (Accepted.CoffeeFraction < .49 ? TEXT("这一杯牛奶的比例稍微多一点") : TEXT("咖啡和牛奶的比例很平衡"))) : FString::Printf(TEXT("这杯%s的配方合适"), *DrinkName);
    Present(FString::Printf(TEXT("%s，出品时有%d块冰。这杯我记住了。我还不想走，再陪我聊一会儿吧。"), *Balance, Accepted.IceCount), TEXT("Completed"));
}

FString ARegularCustomerEncounter::CurrentRecipeName() const
{
    return Recipe && !Recipe->DisplayName.IsEmpty() ? Recipe->DisplayName.ToString() : TEXT("饮品");
}

void ARegularCustomerEncounter::ContinueConversation(int32 Choice)
{
    if (Stage == ERegularCustomerStage::Completed) LoungeTopic = Choice == 0 ? FName(TEXT("NightShift")) : FName(TEXT("City"));
    else if (Choice == 1) LoungeTopic = LoungeTopic == TEXT("NightShift") ? FName(TEXT("City")) : FName(TEXT("NightShift"));
    ++TurnsSinceOffer;
    if (TurnsSinceOffer >= 3) { OfferNextDrink(); return; }
    Stage = ERegularCustomerStage::Chatting;
    Choices = {RegularText(TEXT("继续说，我听着。")), RegularText(TEXT("换个话题吧。"))};
    Present(RegularCustomerConversation::Reply(LoungeTopic, ConversationTurns), TEXT("Conversation"));
    ConversationTurns = (ConversationTurns + 1) % 6000;
    if (Attention) Attention->Acknowledge();
}

void ARegularCustomerEncounter::OfferNextDrink()
{
    TArray<UDrinkRecipe*> Available;
    for (UDrinkRecipe* Candidate : FollowupRecipes) if (IsValid(Candidate) && Candidate != Recipe &&
        (!Candidate->AcceptedCupClass || (AcceptedCup.IsValid() && AcceptedCup->IsA(Candidate->AcceptedCupClass)))) Available.Add(Candidate);
    NextRecipe = Available.IsEmpty() ? InitialRecipe.Get() : Available[(DrinksThisVisit - 1) % Available.Num()];
    Stage = ERegularCustomerStage::ReorderOffer;
    Choices = {RegularText(TEXT("好，我给你做。")), RegularText(TEXT("先不点，继续聊吧。"))};
    const FString Name = NextRecipe && !NextRecipe->DisplayName.IsEmpty() ? NextRecipe->DisplayName.ToString() : CurrentRecipeName();
    Present(FString::Printf(TEXT("聊着聊着，又想在这里多坐一会儿。想再来杯%s，你现在方便做吗？不着急，先聊天也好。"), *Name), TEXT("ReorderOffer"));
}

bool ARegularCustomerEncounter::BeginNextDrink()
{
    if (!IsValid(NextRecipe) || !IsValid(Customer) || !IsValid(ServingSpot) ||
        Customer->CustomerOrder->IsOrderActive() || Customer->CustomerDrink->IsDrinking() ||
        ServingSpot->GetOrderSerial() != OwnedOrderSerial || ServingSpot->GetServedCup() != AcceptedCup.Get()) return false;
    ALiquidContainerActor* PreviousCup = AcceptedCup.Get();
    if (!IsValid(PreviousCup) || PreviousCup->Consumption->HasActiveConsumer()) return false;
    ALiquidContainerActor* FreshCup = nullptr;
    for (TActorIterator<AGlassRecoveryActor> It(GetWorld()); It && !FreshCup; ++It) FreshCup = It->ReplenishServedCup(PreviousCup);
    if (!FreshCup)
    {
        Present(TEXT("新杯子还没准备好。我们先接着聊，等方便了再做也不迟。"));
        return true;
    }
    // The player accepted the next order: clear only the completed cup and its serving slot.
    ServingSpot->ResetServing();
    PreviousCup->Destroy();
    AcceptedCup.Reset(); Accepted = FDrinkValidationResult();
    Recipe = NextRecipe; NextRecipe = nullptr; Topic = NAME_None;
    return BeginDrinkOrder();
}

void ARegularCustomerEncounter::Present(const FString& Text, FName Cue)
{
    StopDialogueNotice();
    ++PresentationSerial; DialogueClient->Cancel(); DeferredCloudLine.Empty(); DeferredCloudUntil = 0;
    bWaitingForCloud = false;
    const bool Ambient = Cue == TEXT("Cup") || Cue == TEXT("Pour") || Cue == TEXT("Grind");
    const bool Eligible = Ambient || Cue == TEXT("Greeting") || Cue == TEXT("TopicPrompt") || Cue == TEXT("OrderStory") || Cue == TEXT("Completed") || Cue == TEXT("Conversation") || Cue == TEXT("ReorderOffer");
    if (!Eligible || bEnding) { CommitLine(Text); return; }
    FRegularDialogueContext Context;
    Context.AuthoredLine = Text; Context.Cue = Cue.ToString(); Context.CustomerId = CustomerId.ToString();
    Context.bHasPastVisit = PreviousVisit.CompletedVisits > 0;
    Context.bObservedGrinding = Reactions->DidObserveGrinding();
    Context.Facts.Add(TEXT("previous_completed_visits"), FString::FromInt(PreviousVisit.CompletedVisits));
    Context.Facts.Add(TEXT("previous_drink"), PreviousVisit.LastDrinkName);
    Context.Facts.Add(TEXT("previous_topic"), PreviousVisit.LastTopic.ToString());
    Context.Facts.Add(TEXT("selected_topic"), Topic.ToString());
    Context.Facts.Add(TEXT("previous_completed_topic_chapters"), FString::FromInt(PreviousVisit.GetTopicVisits(Topic)));
    Context.Facts.Add(TEXT("tasting_completed_this_visit"), DrinksThisVisit > 0 ? TEXT("true") : TEXT("false"));
    Context.Facts.Add(TEXT("previous_grinding_observed"), PreviousVisit.bLastObservedGrinding ? TEXT("true") : TEXT("false"));
    Context.Facts.Add(TEXT("stage"), StaticEnum<ERegularCustomerStage>()->GetNameStringByValue(int64(Stage)));
    Context.Facts.Add(TEXT("choices_are_fixed"), TEXT("true"));
    Context.Facts.Add(TEXT("completed_drinks_this_visit"), FString::FromInt(DrinksThisVisit));
    Context.Facts.Add(TEXT("current_drink"), CurrentRecipeName());
    Context.Facts.Add(TEXT("last_player_reply"), LastPlayerReply.Left(100));
    Context.Facts.Add(TEXT("recent_conversation"), RecentDialogue.Right(450));
    Context.Facts.Add(TEXT("proposed_drink"), NextRecipe ? NextRecipe->DisplayName.ToString() : FString());
    if (Text.Contains(CurrentRecipeName())) Context.RequiredTerms.AddUnique(CurrentRecipeName());
    if (NextRecipe && !NextRecipe->DisplayName.IsEmpty() && Text.Contains(NextRecipe->DisplayName.ToString())) Context.RequiredTerms.AddUnique(NextRecipe->DisplayName.ToString());
    for (const TCHAR* Term : {TEXT("冰拿铁"), TEXT("咖啡"), TEXT("牛奶"), TEXT("有冰"), TEXT("磨豆"), TEXT("杯子"), TEXT("明信片"),
        TEXT("乘客"), TEXT("维修站"), TEXT("招牌"), TEXT("台阶"), TEXT("长椅"), TEXT("学徒"), TEXT("工具柜")})
        if (Text.Contains(Term)) Context.RequiredTerms.Add(Term);
    if (Cue == TEXT("Completed"))
    {
        int32 End = INDEX_NONE;
        if (Text.FindChar(TCHAR(0x3002), End)) Context.RequiredTerms.Add(Text.Left(End + 1)); // Preserve measured drink feedback verbatim.
        Context.Facts.Add(TEXT("accepted_volume_ml"), FString::SanitizeFloat(Accepted.TotalML));
        Context.Facts.Add(TEXT("accepted_ice_count"), FString::FromInt(Accepted.IceCount));
        Context.Facts.Add(TEXT("accepted_coffee_fraction"), FString::SanitizeFloat(Accepted.CoffeeFraction));
    }
    bWaitingForCloud = true;
    if (Ambient) { DeferredCloudUntil = DialogueClient->GetRequestTime() + 8; RefreshLink(); }
    else { bLinePlaceholder = true; Line = RegularText(TEXT("……")); RefreshPanel(); }
    Reactions->HoldForDialogue(Text);
    const uint64 Serial = PresentationSerial;
    const ERegularCustomerStage ExpectedStage = Stage;
    TWeakObjectPtr<ARegularCustomerEncounter> Weak(this);
    DialogueClient->Generate(Context, [Weak, Serial, ExpectedStage, Ambient](const FString& Result, bool bGenerated)
    {
        if (!Weak.IsValid() || Weak->bEnding || Weak->PresentationSerial != Serial || Weak->Stage != ExpectedStage) return;
        const FString Reason = Weak->DialogueClient->GetLastResult();
        const ERegularDialogueSource Source = RegularDialogueDiagnostics::SourceForResult(bGenerated, Reason);
        if (Ambient)
        {
            if (Weak->DialogueClient->GetRequestTime() > Weak->DeferredCloudUntil)
            { Weak->bWaitingForCloud = false; Weak->RefreshLink(); return; }
            if (Weak->Reactions->IsPlayerReadingMenu())
            { Weak->DeferredCloudLine = Result; Weak->DeferredSource = Source; Weak->DeferredReason = Reason; Weak->RefreshLink(); return; }
            Weak->DeferredCloudUntil = 0;
        }
        Weak->CommitLine(Result, Source, Reason);
    });
    RefreshLink();
}

void ARegularCustomerEncounter::CommitLine(const FString& Text, ERegularDialogueSource Source, const FString& Reason)
{
    LineSource = Source; LineReason = Reason; bWaitingForCloud = false; bLinePlaceholder = false;
    Line = RegularText(Text);
    Reactions->HoldForDialogue(Text);
    RecentDialogue = (RecentDialogue + TEXT("\n") + Text).Right(450);
    RefreshPanel();
    if (bEnding || !Panel->IsVisible() || IsHidden() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    if (auto* Widget = Cast<URegularCustomerWidget>(Panel->GetUserWidgetObject()))
    {
        // A head-attached component keeps the sound at the NPC, with bounded voice grains.
        Widget->SetVoiceAnchor(DialogueNoticeAudio);
        Widget->VoiceTickSound = DialogueNoticeAudio->Sound;
        Widget->VoiceAttenuation = DialogueNoticeAudio->AttenuationSettings;
        Widget->VoiceVolume = DialogueNoticeAudio->VolumeMultiplier;
        Widget->bEnableVoice = bEnableDialogueNotice && IsValid(Customer);
        Widget->VoiceEveryCharacters = 1;
        Widget->MinVoiceIntervalSeconds = .04f;
        Widget->PlayTypewriter(Line, CharactersPerSecond);
    }
}

void ARegularCustomerEncounter::StopDialogueNotice()
{
    if (Panel) if (auto* Widget = Cast<URegularCustomerWidget>(Panel->GetUserWidgetObject())) Widget->StopTypewriter();
    if (DialogueNoticeAudio) DialogueNoticeAudio->Stop();
}

void ARegularCustomerEncounter::RefreshPanel()
{
    if (URegularCustomerWidget* Widget = Cast<URegularCustomerWidget>(Panel->GetUserWidgetObject()))
    {
        FString Status;
        if (Stage == ERegularCustomerStage::Preparing)
            Status = FString::Printf(TEXT("正在制作：%s。杯型和配方详见全息订单屏。"), *CurrentRecipeName());
        else if (Stage == ERegularCustomerStage::Tasting) Status = TEXT("正在品尝……");
        else if (Stage == ERegularCustomerStage::Completed)
            Status = Memory && Memory->IsPersisted() ? TEXT("已记住这杯饮品，继续聊聊吧。") : TEXT("本次饮品仅保留在内存中，存档未成功写入。");
        else if (Stage == ERegularCustomerStage::Chatting) Status = TEXT("她还在这里陪你。空手指向选项，扳机回应。");
        else if (Stage == ERegularCustomerStage::ReorderOffer) Status = TEXT("是否再做一杯由你决定；答应后才会创建新订单。旧杯会收走，新杯回到工作台。");
        else Status = TEXT("空手指向选项，轻扣扳机回应。");
        Widget->Show(CustomerName, Line, RegularText(Status), Choices);
        RefreshLink();
    }
}

FRegularDialogueLinkView ARegularCustomerEncounter::GetLinkView() const
{
    FRegularDialogueLinkView View;
    View.Source = LineSource; View.CurrentReason = LineReason;
    View.bWaiting = bWaitingForCloud; View.bPlaceholder = bLinePlaceholder;
    View.bReadyForReading = bWaitingForCloud && !DeferredCloudLine.IsEmpty();
    if (DialogueClient) View.Diagnostics = DialogueClient->GetDiagnostics();
    return View;
}

void ARegularCustomerEncounter::RefreshLink()
{
    if (Panel) if (auto* Widget = Cast<URegularCustomerWidget>(Panel->GetUserWidgetObject())) Widget->ShowLink(GetLinkView(), bLinkDetailsExpanded);
}

void ARegularCustomerEncounter::ToggleLinkDetails()
{
    bLinkDetailsExpanded = !bLinkDetailsExpanded;
    const FVector2D Size = URegularCustomerWidget::PanelSize(bLinkDetailsExpanded);
    Panel->SetDrawSize(Size);
    // Keep the original 960x560 dialogue anchored in place while the drawer grows downward.
    Panel->SetPivot(FVector2D(.5, 280. / Size.Y));
    RefreshLink();
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
    bWaitingForCloud = false;
    StopDialogueNotice();
    ++PresentationSerial; DialogueClient->Cancel(); DeferredCloudLine.Empty(); DeferredCloudUntil = 0;
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
    const FVector2D Size = Panel->GetDrawSize();
    const FVector2D Origin = Size * Panel->GetPivot();
    const FIntPoint Pixel(FMath::FloorToInt(Origin.X - Local.Y), FMath::FloorToInt(Origin.Y - Local.Z));
    if (URegularCustomerWidget::LinkToggleRect().Contains(Pixel)) return URegularCustomerWidget::LinkToggleAction;
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
    const FVector2D Size = Panel->GetDrawSize();
    const FVector2D Pixel = Size * Panel->GetPivot() - FVector2D(Local.Y, Local.Z);
    if (Pixel.X < 0 || Pixel.Y < 0 || Pixel.X > Size.X || Pixel.Y > Size.Y) return false;
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
    if (Hit.Action == URegularCustomerWidget::LinkToggleAction) { ToggleLinkDetails(); return true; }
    const bool AcceptedChoice = Choose(Hit.Action);
    if (!AcceptedChoice) LastClickFrame = Previous;
    return AcceptedChoice;
}
