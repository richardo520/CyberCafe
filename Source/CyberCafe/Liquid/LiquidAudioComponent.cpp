#include "Liquid/LiquidAudioComponent.h"
// Fill out your copyright notice in the Description page of Project Settings.

// @trace LQ-01 LQ-02 LQ-05
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/GlassBreakableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraTypes.h"
#include "NiagaraVariant.h"
#include "NiagaraDataInterfaceExport.h"
#include "GrabComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Core/Events/CyberCafeEventSubsystem.h"


void ULiquidAudioComponent::UpdatePourAudio(double ActualFlowMLPerSecond, const FTransform& SourceTransform)
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    UWorld* World = Vessel.GetWorld();
    if (!IsValid(&Vessel) || Vessel.IsActorBeingDestroyed() || Vessel.bLiquidFrozen || Vessel.bBroken ||
        !Vessel.bEnablePourAudio || !IsValid(Vessel.PourLoopSound.Get()) || !IsValid(Vessel.ContainerMesh.Get()) ||
        !World || !World->AllowAudioPlayback() || !World->GetAudioDeviceRaw())
    {
        Vessel.StopPourAudio(true);
        return;
    }
    if (!Vessel.bIsPouring || !FMath::IsFinite(ActualFlowMLPerSecond) || ActualFlowMLPerSecond <= 0.0 ||
        SourceTransform.GetLocation().ContainsNaN() || SourceTransform.GetRotation().ContainsNaN())
    {
        Vessel.StopPourAudio();
        return;
    }

    if (!IsValid(PourAudioComponent.Get()))
    {
        PourAudioComponent = NewObject<UAudioComponent>(&Vessel, NAME_None, RF_Transient);
        PourAudioComponent->bAutoActivate = false;
        PourAudioComponent->bAutoDestroy = false;
        PourAudioComponent->SetUISound(false);
        PourAudioComponent->bAllowSpatialization = true;
        PourAudioComponent->bOverrideAttenuation = true;
        FSoundAttenuationSettings Attenuation;
        Attenuation.bAttenuate = true;
        Attenuation.bSpatialize = true;
        Attenuation.AttenuationShape = EAttenuationShape::Sphere;
        Attenuation.AttenuationShapeExtents = FVector(35.f);
        Attenuation.FalloffDistance = 180.f;
        Attenuation.SpatializationAlgorithm = SPATIALIZATION_Default;
        PourAudioComponent->AttenuationOverrides = Attenuation;
        PourAudioComponent->SetupAttachment(Vessel.ContainerMesh.Get());
        PourAudioComponent->SetAbsolute(false, false, true);
        Vessel.AddInstanceComponent(PourAudioComponent.Get());
        PourAudioComponent->RegisterComponent();
        bPourAudioRunning = false;
    }

    const float ReferenceFlow = FMath::Clamp(FMath::IsFinite(Vessel.PourAudioReferenceFlow) ? Vessel.PourAudioReferenceFlow : 40.f, 0.01f, 1000000.f);
    const double FlowRatio = ActualFlowMLPerSecond / ReferenceFlow;
    const float BaseVolume = FMath::Clamp(FMath::IsFinite(Vessel.PourAudioVolume) ? Vessel.PourAudioVolume : 0.45f, 0.f, 1.f);
    const float Volume = BaseVolume * FMath::Sqrt(static_cast<float>(FMath::Clamp(FlowRatio, 0.1, 1.25)));
    const float Pitch = 0.94f + 0.1f * static_cast<float>(FMath::Clamp(FlowRatio, 0.0, 1.0));
    PourAudioComponent->SetWorldTransform(FTransform(SourceTransform.GetRotation(), SourceTransform.GetLocation(), FVector::OneVector),
        false, nullptr, ETeleportType::TeleportPhysics);
    PourAudioComponent->SetPitchMultiplier(Pitch);
    bPourAudioRequested = true;
    Vessel.PourAudioActualFlow = ActualFlowMLPerSecond;

    // 使用自己的请求门禁；没有实际 voice 时也不按 IsPlaying 每帧重启。
    if (!bPourAudioRunning || PourAudioComponent->Sound.Get() != Vessel.PourLoopSound.Get())
    {
        PourAudioComponent->Stop();
        PourAudioComponent->SetSound(Vessel.PourLoopSound.Get());
        const float FadeInSeconds = FMath::Clamp(FMath::IsFinite(Vessel.PourAudioFadeIn) ? Vessel.PourAudioFadeIn : 0.06f, 0.f, 10.f);
        // VolumeMultiplier carries the flow gain; FadeIn fades to unity so it
        // does not multiply the configured flow volume a second time.
        PourAudioComponent->SetVolumeMultiplier(Volume);
        PourAudioComponent->FadeIn(FadeInSeconds, 1.f, FMath::FRandRange(0.f, 0.5f));
        ++Vessel.AudioStartCount;
        bPourAudioRunning = true;
    }
    else
    {
        PourAudioComponent->SetVolumeMultiplier(Volume);
    }
}

void ULiquidAudioComponent::StopPourAudio(bool bImmediate)
{
    auto& Vessel = *CastChecked<ALiquidContainerActor>(GetOwner());
    Vessel.bPourAudioStopOnNextTick = false;
    bPourAudioRequested = false;
    Vessel.PourAudioActualFlow = 0.0;
    const bool bWasRunning = bPourAudioRunning;
    bPourAudioRunning = false;
    if (!IsValid(PourAudioComponent.Get())) return;
    if (bImmediate)
    {
        PourAudioComponent->Stop();
    }
    else if (bWasRunning)
    {
        const float FadeOutSeconds = FMath::Clamp(FMath::IsFinite(Vessel.PourAudioFadeOut) ? Vessel.PourAudioFadeOut : 0.12f, 0.f, 10.f);
        PourAudioComponent->FadeOut(FadeOutSeconds, 0.f);
    }
}
