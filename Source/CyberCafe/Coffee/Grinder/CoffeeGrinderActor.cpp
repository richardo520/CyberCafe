// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"

ACoffeeGrinderActor::ACoffeeGrinderActor()
{
    PrimaryActorTick.bCanEverTick = true;
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    SetRootComponent(BodyMesh);
    BodyMesh->SetSimulatePhysics(true);
    BodyMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(BodyMesh);
    GrabComp->GrabType = EGrabType::Free;
    GrabComp->GrabPriority = 0;
    HandleMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HandleMountPoint"));
    HandleMountPoint->SetupAttachment(BodyMesh);
    DrawerMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("DrawerMountPoint"));
    DrawerMountPoint->SetupAttachment(BodyMesh);
    BeanEntryPoint = CreateDefaultSubobject<USceneComponent>(TEXT("BeanEntryPoint"));
    BeanEntryPoint->SetupAttachment(BodyMesh);
    BeanPileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeanPileMesh"));
    BeanPileMesh->SetupAttachment(BeanEntryPoint);
    BeanPileMesh->SetSimulatePhysics(false);
    BeanPileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BeanPileMesh->SetVisibility(false);
    GrindFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("GrindFX"));
    GrindFX->SetupAttachment(BodyMesh);
    GrindFX->bAutoActivate = false;
    GrindSFX = CreateDefaultSubobject<UAudioComponent>(TEXT("GrindSFX"));
    GrindSFX->SetupAttachment(BodyMesh);
    GrindSFX->bAutoActivate = false;
    GrindSFX->SetVolumeMultiplier(1.f);

    GrindSFXFadeInTime = 0.08f;
    GrindSFXFadeOutTime = 0.15f;
    GrindStopDelay = 0.2f;
    bModulateVolumeBySpeed = true;
    MinSpeedDegPerSec = 60.f;
    MaxSpeedDegPerSec = 480.f;
    MinVolumeMultiplier = 0.7f;
    MaxVolumeMultiplier = 1.f;
    SpeedSmoothing = 12.f;
    LoadedGrindSound = nullptr;
    EmptyGrindSound = nullptr;
    AccumulatedHandleAngleDeg = 0.f;
    LastTurnGameTime = -1000.f;
    SmoothedAngularSpeedDeg = 0.f;
    bGrindSFXActive = false;
    GrindSFXStartTime = -1000.f;
    HandleRef = nullptr;
    DrawerRef = nullptr;
    // 保留原投料和六圈默认值。
    BeanCapacityGrams = 15.f;
    GramsPerDegree = 15.f / (6.f * 360.f);
    GrindEfficiency = 1.f;
    BeanPileStepCount = 3;
    BeanPileMinDisplayRatio = 0.4f;
    BeanPileHeightMultiplier = 1.f;
    bSilentWhenEmpty = true;
    CurrentBeanGrams = 0.f;
    CurrentGroundGrams = 0.f;
    BeanPileInitialScale = FVector::OneVector;
}

void ACoffeeGrinderActor::BeginPlay()
{
    Super::BeginPlay();
    SanitizeGrindProperties();
    if (GrindSFX)
    {
        // 旧 BP 的组件 Sound 仍可作为有豆音效，空转声不复用磨豆声。
        if (!LoadedGrindSound) { LoadedGrindSound = GrindSFX->Sound; }
        GrindSFX->SetSound(LoadedGrindSound);
    }
    if (BeanPileMesh)
    {
        BeanPileInitialScale = BeanPileMesh->GetRelativeScale3D();
        BeanPileInitialLocation = BeanPileMesh->GetRelativeLocation();
    }
    UpdateBeanPileVisual();
    SpawnChildParts();
}

void ACoffeeGrinderActor::SpawnChildParts()
{
    UWorld* World = GetWorld();
    if (!World) { return; }
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (HandleClass && HandleMountPoint)
    {
        AGrinderHandleActor* Handle = World->SpawnActor<AGrinderHandleActor>(HandleClass, HandleMountPoint->GetComponentTransform(), SpawnParams);
        if (Handle)
        {
            Handle->AttachToGrinder(this, HandleMountPoint);
            HandleRef = Handle;
        }
    }
    if (DrawerClass && DrawerMountPoint)
    {
        AGrinderDrawerActor* Drawer = World->SpawnActor<AGrinderDrawerActor>(DrawerClass, DrawerMountPoint->GetComponentTransform(), SpawnParams);
        if (Drawer)
        {
            Drawer->AttachToGrinder(this, DrawerMountPoint);
            DrawerRef = Drawer;
        }
    }
}

void ACoffeeGrinderActor::SanitizeGrindProperties()
{
    // 修正真实业务参数/存量，而不是仅钳制显示。容量被调低时保留已有合法存量。
    BeanCapacityGrams = FMath::IsFinite(BeanCapacityGrams) ? FMath::Max(BeanCapacityGrams, 0.f) : 0.f;
    GramsPerDegree = FMath::IsFinite(GramsPerDegree) ? FMath::Max(GramsPerDegree, 0.f) : 0.f;
    GrindEfficiency = FMath::IsFinite(GrindEfficiency) ? FMath::Clamp(GrindEfficiency, 0.f, 1.f) : 0.f;
    CurrentBeanGrams = FMath::IsFinite(CurrentBeanGrams) ? FMath::Max(CurrentBeanGrams, 0.f) : 0.f;
    CurrentGroundGrams = FMath::IsFinite(CurrentGroundGrams) ? FMath::Max(CurrentGroundGrams, 0.f) : 0.f;
    EffectiveDirectionSign = EffectiveDirectionSign < 0 ? -1 : 1;
    BeanPileStepCount = FMath::Clamp(BeanPileStepCount, 0, 1000);
    BeanPileTravelCM = FMath::IsFinite(BeanPileTravelCM) ? FMath::Max(BeanPileTravelCM, 0.f) : 2.f;
    BeanPileMinDisplayRatio = FMath::IsFinite(BeanPileMinDisplayRatio) ? FMath::Clamp(BeanPileMinDisplayRatio, 0.f, 1.f) : 0.f;
    BeanPileHeightMultiplier = FMath::IsFinite(BeanPileHeightMultiplier) ? FMath::Max(BeanPileHeightMultiplier, 0.f) : 1.f;
    GrindSFXFadeInTime = FMath::IsFinite(GrindSFXFadeInTime) ? FMath::Max(GrindSFXFadeInTime, 0.f) : 0.f;
    GrindSFXFadeOutTime = FMath::IsFinite(GrindSFXFadeOutTime) ? FMath::Max(GrindSFXFadeOutTime, 0.f) : 0.f;
    GrindStopDelay = FMath::IsFinite(GrindStopDelay) ? FMath::Max(GrindStopDelay, 0.01f) : 0.2f;
    MinSpeedDegPerSec = FMath::IsFinite(MinSpeedDegPerSec) ? FMath::Clamp(MinSpeedDegPerSec, 0.f, 1199.f) : 60.f;
    MaxSpeedDegPerSec = FMath::IsFinite(MaxSpeedDegPerSec) ? FMath::Clamp(MaxSpeedDegPerSec, MinSpeedDegPerSec + 1.f, 1200.f) : 1200.f;
    MinVolumeMultiplier = FMath::IsFinite(MinVolumeMultiplier) ? FMath::Clamp(MinVolumeMultiplier, 0.f, 5.f) : 0.f;
    MaxVolumeMultiplier = FMath::IsFinite(MaxVolumeMultiplier) ? FMath::Clamp(MaxVolumeMultiplier, MinVolumeMultiplier, 5.f) : MinVolumeMultiplier;
    SpeedSmoothing = FMath::IsFinite(SpeedSmoothing) ? FMath::Clamp(SpeedSmoothing, 0.1f, 100.f) : 12.f;
    checkSlow(GrindEfficiency >= 0.f && GrindEfficiency <= 1.f && CurrentBeanGrams >= 0.f && CurrentGroundGrams >= 0.f);
}

void ACoffeeGrinderActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    SanitizeGrindProperties();
    if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) { return; }
    ActualTickDt = DeltaTime;
    const UWorld* World = GetWorld();
    if (!World) { return; }
    const float Now = World->GetTimeSeconds();
    const bool bIdle = Now - LastTurnGameTime > GrindStopDelay;
    if (bIdle) { TargetAngularSpeedDeg = 0.f; }
    SmoothedAngularSpeedDeg = FMath::FInterpTo(SmoothedAngularSpeedDeg, TargetAngularSpeedDeg, DeltaTime, SpeedSmoothing);
    if (bIdle)
    {
        StopGrindSFX();
        return;
    }
    if (bGrindSFXActive && !bPlayingEmptySound && CurrentBeanGrams <= 0.f)
    {
        StopGrindSFX();
        return;
    }
    if (GrindSFX && bGrindSFXActive)
    {
        const float Alpha = FMath::GetMappedRangeValueClamped(FVector2D(MinSpeedDegPerSec, MaxSpeedDegPerSec), FVector2D(0.f, 1.f), SmoothedAngularSpeedDeg);
        GrindSFX->SetPitchMultiplier(FMath::Lerp(0.85f, 1.15f, Alpha));
        if (bModulateVolumeBySpeed && Now - GrindSFXStartTime >= GrindSFXFadeInTime)
        {
            GrindSFX->SetVolumeMultiplier(FMath::Lerp(MinVolumeMultiplier, MaxVolumeMultiplier, Alpha));
        }
    }
}

void ACoffeeGrinderActor::OnHandleRotated(float DeltaAngleDeg)
{
    const UWorld* World = GetWorld();
    const float Dt = World ? World->GetDeltaSeconds() : ActualTickDt;
    AdvanceGrinding(DeltaAngleDeg, Dt);
    if (!FMath::IsFinite(DeltaAngleDeg) || DeltaAngleDeg == 0.f) { return; }
    const double Total = static_cast<double>(AccumulatedHandleAngleDeg) + DeltaAngleDeg;
    if (FMath::IsFinite(Total) && FMath::Abs(Total) <= TNumericLimits<float>::Max())
    {
        AccumulatedHandleAngleDeg = static_cast<float>(Total);
    }
    OnHandleTurned.Broadcast(DeltaAngleDeg, AccumulatedHandleAngleDeg);
}

float ACoffeeGrinderActor::AdvanceGrinding(float SignedAngleDeg, float Dt)
{
    SanitizeGrindProperties();
    if (!FMath::IsFinite(SignedAngleDeg) || !FMath::IsFinite(Dt) || Dt <= 0.f || SignedAngleDeg == 0.f
        || (bOneWayEffective && SignedAngleDeg * EffectiveDirectionSign <= 0.f))
    {
        StopGrindSFX();
        TargetAngularSpeedDeg = 0.f;
        return 0.f;
    }
    const float Angle = FMath::Abs(SignedAngleDeg);
    if (CurrentBeanGrams <= 0.f)
    {
        UpdateGrindingAudio(Angle, Dt, false);
        return 0.f;
    }
    const double Want = static_cast<double>(Angle) * GramsPerDegree;
    const float Remaining = static_cast<float>(FMath::Max(0.0, static_cast<double>(CurrentBeanGrams) - Want));
    const float Consumed = CurrentBeanGrams - Remaining;
    if (Consumed <= 0.f)
    {
        StopGrindSFX();
        return 0.f;
    }
    const double Output = static_cast<double>(Consumed) * GrindEfficiency;
    const double NewGround = static_cast<double>(CurrentGroundGrams) + Output;
    if (!FMath::IsFinite(NewGround) || NewGround > TNumericLimits<float>::Max())
    {
        StopGrindSFX();
        return 0.f;
    }
    CurrentBeanGrams = Remaining;
    CurrentGroundGrams = static_cast<float>(NewGround);
    TotalBeanConsumedGrams += Consumed;
    TotalGroundOutputGrams += Output;
    GrindLossGrams += static_cast<double>(Consumed) - Output;
    UpdateBeanPileVisual();
    // 真正最后一粒被消耗后，立即停声，不等下一帧/下一次转柄。
    if (CurrentBeanGrams <= 0.f) { StopGrindSFX(); TargetAngularSpeedDeg = 0.f; }
    else { UpdateGrindingAudio(Angle, Dt, true); }
    OnGrindProgress.Broadcast(CurrentBeanGrams, CurrentGroundGrams);
    return Consumed;
}

void ACoffeeGrinderActor::UpdateGrindingAudio(float AngleDeg, float Dt, bool bLoaded)
{
    USoundBase* DesiredSound = bLoaded ? LoadedGrindSound.Get() : EmptyGrindSound.Get();
    if (!DesiredSound)
    {
        StopGrindSFX();
        TargetAngularSpeedDeg = 0.f;
        return;
    }
    const UWorld* World = GetWorld();
    if (!World || !GrindSFX) { return; }
    if (GrindSFX->Sound != DesiredSound)
    {
        StopGrindSFX();
        GrindSFX->SetSound(DesiredSound);
    }
    bPlayingEmptySound = !bLoaded;
    // 由采样实际 Dt 驱动，不用距上次声音事件的时间差估速。
    TargetAngularSpeedDeg = static_cast<float>(FMath::Min(static_cast<double>(AngleDeg) / Dt, 1200.0));
    SmoothedAngularSpeedDeg = FMath::FInterpTo(SmoothedAngularSpeedDeg, TargetAngularSpeedDeg, FMath::Min(Dt, 0.1f), SpeedSmoothing);
    LastTurnGameTime = World->GetTimeSeconds();
    StartGrindSFX();
}

void ACoffeeGrinderActor::StartGrindSFX()
{
    if (bGrindSFXActive || !GrindSFX || !GrindSFX->Sound) { return; }
    GrindSFX->SetVolumeMultiplier(bModulateVolumeBySpeed ? MinVolumeMultiplier : MaxVolumeMultiplier);
    if (GrindSFXFadeInTime > 0.f) { GrindSFX->FadeIn(GrindSFXFadeInTime, MaxVolumeMultiplier); }
    else { GrindSFX->Play(); }
    bGrindSFXActive = true;
    GrindSFXStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void ACoffeeGrinderActor::StopGrindSFX()
{
    // 反转/磨空必须即时无磨豆声；不用每帧反复 FadeOut 延长尾音。
    if (GrindSFX && (bGrindSFXActive || GrindSFX->IsPlaying())) { GrindSFX->Stop(); }
    if (GrindFX && GrindFX->IsActive()) { GrindFX->Deactivate(); }
    bGrindSFXActive = false;
    bPlayingEmptySound = false;
}

FTransform ACoffeeGrinderActor::GetHandleMountWorldTransform() const
{
    return HandleMountPoint ? HandleMountPoint->GetComponentTransform() : GetActorTransform();
}

FTransform ACoffeeGrinderActor::GetDrawerMountWorldTransform() const
{
    return DrawerMountPoint ? DrawerMountPoint->GetComponentTransform() : GetActorTransform();
}

bool ACoffeeGrinderActor::IsHeld() const
{
    return GrabComp ? GrabComp->IsHeld() : false;
}

float ACoffeeGrinderActor::TryAddBeans(float Grams)
{
    SanitizeGrindProperties();
    if (!FMath::IsFinite(Grams) || Grams <= 0.f) { return 0.f; }
    const float Space = FMath::Max(BeanCapacityGrams - CurrentBeanGrams, 0.f);
    const float Before = CurrentBeanGrams;
    CurrentBeanGrams += FMath::Min(Grams, Space);
    UpdateBeanPileVisual();
    return CurrentBeanGrams - Before;
}

float ACoffeeGrinderActor::DrainGround(float MaxGrams)
{
    SanitizeGrindProperties();
    if (!FMath::IsFinite(MaxGrams) || MaxGrams <= 0.f || CurrentGroundGrams <= 0.f) { return 0.f; }
    const float Before = CurrentGroundGrams;
    CurrentGroundGrams = FMath::Max(0.f, CurrentGroundGrams - FMath::Min(MaxGrams, CurrentGroundGrams));
    // 不广播 OnGrindProgress；当前广播中的抽屉搬粉不会再次递归触发自己。
    return Before - CurrentGroundGrams;
}

void ACoffeeGrinderActor::ProcessGrinding(float AbsDeltaAngleDeg)
{
    AdvanceGrinding(AbsDeltaAngleDeg * EffectiveDirectionSign, ActualTickDt);
}

void ACoffeeGrinderActor::UpdateBeanPileVisual()
{
    if (!BeanPileMesh) { return; }
    const float Ratio = BeanCapacityGrams > 0.f ? FMath::Clamp(CurrentBeanGrams / BeanCapacityGrams, 0.f, 1.f) : 0.f;
    float DisplayRatio = Ratio;
    if (BeanPileStepCount > 0)
    {
        DisplayRatio = FMath::Clamp(FMath::CeilToFloat(Ratio * BeanPileStepCount) / static_cast<float>(BeanPileStepCount), 0.f, 1.f);
    }
    FVector Location = BeanPileInitialLocation;
    FVector Scale = BeanPileInitialScale;
    if (bTranslateBeanPile)
    {
        Location.Z -= BeanPileTravelCM * (1.f - DisplayRatio);
    }
    else
    {
        DisplayRatio = FMath::Lerp(BeanPileMinDisplayRatio, 1.f, DisplayRatio);
        Scale.Z *= DisplayRatio * BeanPileHeightMultiplier;
    }
    BeanPileMesh->SetRelativeLocation(Location);
    BeanPileMesh->SetRelativeScale3D(Scale);
    BeanPileMesh->SetVisibility(CurrentBeanGrams > 0.f, true);
}
