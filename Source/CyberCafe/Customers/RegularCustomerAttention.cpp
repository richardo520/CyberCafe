#include "Customers/RegularCustomerAttention.h"
#include "Customers/CustomerDrinkComponent.h"
#include "Tutorial/TutorialNPC.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

URegularCustomerAttention::URegularCustomerAttention()
{
    PrimaryComponentTick.bCanEverTick = true;
}

bool URegularCustomerAttention::InitializeLayer(TSubclassOf<UAnimInstance> Layer)
{
    auto* NPC = Cast<ATutorialNPC>(GetOwner());
    if (!NPC || !NPC->GetMesh() || !Layer || !Layer->IsChildOf(URegularCustomerAttentionAnimation::StaticClass()) ||
        NPC->GetMesh()->GetBoneIndex(TEXT("head")) == INDEX_NONE) return false;
    const auto Existing = NPC->GetMesh()->GetPostProcessAnimBPClassToBeUsed();
    if (Existing && Existing != Layer) return false; // Preserve any unrelated authored post-process layer.
    NPC->GetMesh()->AddTickPrerequisiteComponent(this);
    NPC->GetMesh()->SetOverridePostProcessAnimBP(Layer, true);
    return NPC->GetMesh()->GetPostProcessInstance() != nullptr;
}

void URegularCustomerAttention::LookAtActivity(AActor* Target)
{
    if (IsValid(Target) && Target->GetWorld() == GetWorld())
    { Activity = Target; ActivityUntil = GetWorld()->GetTimeSeconds() + 3; }
}

void URegularCustomerAttention::Acknowledge() { NodRemaining = .75f; }

void URegularCustomerAttention::PrepareForTasting()
{
    Acknowledge();
    if (GetWorld()) YieldAt = GetWorld()->GetTimeSeconds() + .85;
}

void URegularCustomerAttention::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt, TickType, TickFunction);
    if (!FMath::IsFinite(Dt) || Dt <= 0 || !GetWorld()) return;
    auto* Drink = GetOwner()->FindComponentByClass<UCustomerDrinkComponent>();
    if (!bEnabled || (YieldAt >= 0 && GetWorld()->GetTimeSeconds() >= YieldAt) || (Drink && Drink->IsDrinking()))
    { Alpha = 0; NodRemaining = 0; return; }
    FVector Target;
    bool bHasTarget = false;
    if (Activity.IsValid() && GetWorld()->GetTimeSeconds() <= ActivityUntil)
    { Target = Activity->GetActorLocation(); bHasTarget = true; }
    else
    {
        const auto* PC = UGameplayStatics::GetPlayerController(this, 0);
        const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        const auto* Camera = Pawn ? Pawn->FindComponentByClass<UCameraComponent>() : nullptr;
        if (Camera && Camera->IsActive()) { Target = Camera->GetComponentLocation(); bHasTarget = true; }
    }
    if (!bHasTarget || Target.ContainsNaN()) { Alpha = 0; return; }
    if (NodRemaining > 0)
    {
        NodRemaining = FMath::Max(0.f, NodRemaining - Dt);
        Target.Z -= 12 * FMath::Sin(PI * (1 - NodRemaining / .75f));
    }
    LookTarget = bInitializedTarget ? FMath::VInterpTo(LookTarget, Target, Dt, 7) : Target;
    bInitializedTarget = true;
    Alpha = FMath::FInterpTo(Alpha, .8f, Dt, 5);
    if (YieldAt >= 0)
        Alpha = FMath::Min(Alpha, .8f * float(FMath::Clamp((YieldAt - GetWorld()->GetTimeSeconds()) / .25, 0., 1.)));
}

void URegularCustomerAttention::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnabled = false; Alpha = 0;
    if (auto* NPC = Cast<ATutorialNPC>(GetOwner())) if (NPC->GetMesh()) NPC->GetMesh()->RemoveTickPrerequisiteComponent(this);
    Super::EndPlay(Reason);
}

void URegularCustomerAttentionAnimation::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const AActor* Owner = GetOwningActor();
    const auto* Source = Owner ? Owner->FindComponentByClass<URegularCustomerAttention>() : nullptr;
    const auto* Drink = Owner ? Owner->FindComponentByClass<UCustomerDrinkComponent>() : nullptr;
    AttentionAlpha = Source && (!Drink || !Drink->IsDrinking()) ? Source->GetAttentionAlpha() : 0;
    if (Source) AttentionTarget = Source->GetLookTarget();
}
