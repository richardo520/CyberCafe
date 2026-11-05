// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"

AGrinderHandleActor::AGrinderHandleActor()
{
    PrimaryActorTick.bCanEverTick = true;
    HandleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandleMesh"));
    SetRootComponent(HandleMesh);
    HandleMesh->SetSimulatePhysics(false);
    HandleMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    HandleMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(HandleMesh);
    GrabComp->GrabType = EGrabType::Custom;
    GrabComp->GrabPriority = 1;
    MaxHandOffset = 15.f;
    HandleArmRadius = 5.f;
    bOneWayEffective = true;
    EffectiveDirectionSign = 1;
    TurnHaptic = nullptr;
    HapticIntervalDeg = 30.f;
    OwnerGrinder = nullptr;
    CurrentAngleDeg = 0.f;
    LastHandAngleDeg = 0.f;
    bHasValidLastHandAngle = false;
    LastHapticAngleDeg = 0.f;
    MountRef = nullptr;
}

void AGrinderHandleActor::BeginPlay()
{
    Super::BeginPlay();
    SanitizeSamplingProperties();
    if (HandleMesh)
    {
        HandleMesh->SetSimulatePhysics(false);
        HandleMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        HandleMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }
    if (GrabComp)
    {
        GrabComp->OnGrabbed.AddDynamic(this, &AGrinderHandleActor::HandleGrabbed);
        GrabComp->OnDropped.AddDynamic(this, &AGrinderHandleActor::HandleDropped);
    }
}

void AGrinderHandleActor::SanitizeSamplingProperties()
{
    DeadRadiusCM = FMath::IsFinite(DeadRadiusCM) ? FMath::Max(DeadRadiusCM, 1.f) : 1.f;
    HandleArmRadius = FMath::IsFinite(HandleArmRadius) ? FMath::Max(HandleArmRadius, 1.f) : 5.f;
    MaxTrackingJumpCM = FMath::IsFinite(MaxTrackingJumpCM) ? FMath::Clamp(MaxTrackingJumpCM, 0.1f, 35.f) : 35.f;
    MaxAngularSpeedDegPerSecond = FMath::IsFinite(MaxAngularSpeedDegPerSecond)
        ? FMath::Clamp(MaxAngularSpeedDegPerSecond, 1.f, 1200.f) : 1200.f;
    MinEffectiveAngleDeg = FMath::IsFinite(MinEffectiveAngleDeg) ? FMath::Clamp(MinEffectiveAngleDeg, 1.2f, 180.f) : 1.2f;
    EffectiveDirectionSign = EffectiveDirectionSign < 0 ? -1 : 1;
    HapticIntervalDeg = FMath::IsFinite(HapticIntervalDeg) ? FMath::Max(HapticIntervalDeg, 0.f) : 0.f;
    checkSlow(DeadRadiusCM >= 1.f && MaxTrackingJumpCM <= 35.f && MaxAngularSpeedDegPerSecond <= 1200.f);
}

void AGrinderHandleActor::ResetHandSampling()
{
    bHasValidLastHandAngle = false;
    DeltaBudgetDeg = 0.0;
    DeltaBudgetTime = 0.0;
}

void AGrinderHandleActor::AttachToGrinder(ACoffeeGrinderActor* InOwner, USceneComponent* MountComp)
{
    if (!InOwner || !MountComp)
    {
        return;
    }
    OwnerGrinder = InOwner;
    MountRef = MountComp;
    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
    AttachRule.bWeldSimulatedBodies = false;
    AttachToComponent(MountComp, AttachRule);
    CurrentAngleDeg = 0.f;
    VisualAngleDeg = 0.0;
    ResetHandSampling();
    SetActorRelativeRotation(FRotator::ZeroRotator);
    AddTickPrerequisiteActor(InOwner);
    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = false;
    }
}

void AGrinderHandleActor::HandleGrabbed()
{
    ResetHandSampling();
    LastHoldingController.Reset();
    LastHapticAngleDeg = CurrentAngleDeg;
}

void AGrinderHandleActor::HandleDropped()
{
    ResetHandSampling();
    LastHoldingController.Reset();
    if (OwnerGrinder)
    {
        OwnerGrinder->AdvanceGrinding(0.f, 0.f);
    }
}

void AGrinderHandleActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!GrabComp || !GrabComp->IsHeld() || !MountRef || !OwnerGrinder)
    {
        ResetHandSampling();
        LastHoldingController.Reset();
        return;
    }
    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        ResetHandSampling();
        LastHoldingController.Reset();
        return;
    }
    if (LastHoldingController.Get() != MC)
    {
        ResetHandSampling();
        LastHoldingController = MC;
    }
    const FVector HandLocal = MountRef->GetComponentTransform().InverseTransformPosition(MC->GetComponentLocation());
    ApplyHandSample(HandLocal, DeltaTime);
}

bool AGrinderHandleActor::ApplyHandSample(const FVector& LocalHand, float Dt)
{
    SanitizeSamplingProperties();
    if (bLastOneWayEffective != bOneWayEffective || LastEffectiveDirectionSign != EffectiveDirectionSign)
    {
        ResetHandSampling();
        bLastOneWayEffective = bOneWayEffective;
        LastEffectiveDirectionSign = EffectiveDirectionSign;
    }
    // 非法采样只重置差分，不松手、不清豆粉、不改变已有柄角。
    if (!FMath::IsFinite(Dt) || Dt <= 0.0001f || Dt >= 0.1f || LocalHand.ContainsNaN())
    {
        ResetHandSampling();
        if (OwnerGrinder) { OwnerGrinder->AdvanceGrinding(0.f, 0.f); }
        return false;
    }
    const double Radius = FVector2D(LocalHand.X, LocalHand.Y).Size();
    const double MinRadius = FMath::Max(static_cast<double>(DeadRadiusCM), static_cast<double>(HandleArmRadius) * 0.4);
    const double MaxRadius = static_cast<double>(HandleArmRadius) * 2.8;
    if (!FMath::IsFinite(Radius) || Radius < MinRadius || Radius > MaxRadius)
    {
        ResetHandSampling();
        if (OwnerGrinder) { OwnerGrinder->AdvanceGrinding(0.f, 0.f); }
        return false;
    }
    const float HandAngleDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(LocalHand.Y, LocalHand.X)));
    if (!bHasValidLastHandAngle)
    {
        LastHandLocal = LocalHand;
        LastHandAngleDeg = HandAngleDeg;
        bHasValidLastHandAngle = true;
        return true;
    }
    const float DeltaAngleDeg = FMath::FindDeltaAngleDegrees(LastHandAngleDeg, HandAngleDeg);
    if (FVector::Dist(LocalHand, LastHandLocal) > MaxTrackingJumpCM || FMath::Abs(DeltaAngleDeg) / Dt > MaxAngularSpeedDegPerSecond)
    {
        ResetHandSampling();
        if (OwnerGrinder) { OwnerGrinder->AdvanceGrinding(0.f, 0.f); }
        return false;
    }
    LastHandLocal = LocalHand;
    LastHandAngleDeg = HandAngleDeg;
    if (DeltaAngleDeg == 0.f)
    {
        // 暂停后下一次移动只重新建立基准，不沿用暂停前的微小预算。
        ResetHandSampling();
        if (OwnerGrinder) { OwnerGrinder->AdvanceGrinding(0.f, Dt); }
        return true;
    }
    VisualAngleDeg += DeltaAngleDeg;
    CurrentAngleDeg = static_cast<float>(VisualAngleDeg);
    SetActorRelativeRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(FMath::Fmod(VisualAngleDeg, 360.0))));

    // 有符号预算保留反向债务；来回小抖动不能把每次正半程兑换成粉。
    const double DirectedDelta = static_cast<double>(DeltaAngleDeg) * EffectiveDirectionSign;
    DeltaBudgetDeg += DirectedDelta;
    if(bOneWayEffective)DeltaBudgetDeg=FMath::Max(DeltaBudgetDeg,-static_cast<double>(MinEffectiveAngleDeg));
    DeltaBudgetTime += Dt;
    const bool bCommit = bOneWayEffective ? DeltaBudgetDeg >= MinEffectiveAngleDeg : FMath::Abs(DeltaBudgetDeg) >= MinEffectiveAngleDeg;
    float Consumed = 0.f;
    if (OwnerGrinder)
    {
        if (bCommit)
        {
            const float SignedCommit = static_cast<float>(DeltaBudgetDeg * EffectiveDirectionSign);
            Consumed = OwnerGrinder->AdvanceGrinding(SignedCommit, static_cast<float>(DeltaBudgetTime));
            DeltaBudgetDeg = 0.0;
            DeltaBudgetTime = 0.0;
        }
        else if (DirectedDelta <= 0.0)
        {
            // 反转和静止立即停止磨豆声，绝不把负预算取绝对值上报。
            OwnerGrinder->AdvanceGrinding(0.f, Dt);
        }
        const double Total = static_cast<double>(OwnerGrinder->AccumulatedHandleAngleDeg) + DeltaAngleDeg;
        if (FMath::IsFinite(Total) && FMath::Abs(Total) <= TNumericLimits<float>::Max())
        {
            OwnerGrinder->AccumulatedHandleAngleDeg = static_cast<float>(Total);
        }
        if (DeltaAngleDeg != 0.f)
        {
            OwnerGrinder->OnHandleTurned.Broadcast(DeltaAngleDeg, OwnerGrinder->AccumulatedHandleAngleDeg);
        }
    }
    if (Consumed > 0.f && TurnHaptic && HapticIntervalDeg > 0.f && GrabComp && GrabComp->IsHeld()
        && FMath::Abs(CurrentAngleDeg - LastHapticAngleDeg) >= HapticIntervalDeg)
    {
        LastHapticAngleDeg = CurrentAngleDeg;
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            PC->PlayHapticEffect(TurnHaptic, GrabComp->GetHeldByHand());
        }
    }
    return true;
}
