#include "CafeSceneMusicComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundMix.h"

UCafeSceneMusicComponent::UCafeSceneMusicComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UCafeSceneMusicComponent::BeginPlay()
{
    Super::BeginPlay();
    ApplyGlobalGainMix();
    if (bAutoPlay) StartMusic();
}

void UCafeSceneMusicComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopMusic();
    RemoveGlobalGainMix();
    Super::EndPlay(EndPlayReason);
}

void UCafeSceneMusicComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    StopMusic();
    RemoveGlobalGainMix();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UCafeSceneMusicComponent::ApplyGlobalGainMix()
{
    if (bGlobalGainMixRequested) return;
    UWorld* World = GetWorld();
    AActor* Owner = GetOwner();
    if (!IsValid(GlobalGainMix) || !IsValid(Owner) || Owner->IsActorBeingDestroyed() ||
        !World || !World->IsGameWorld() || !World->AllowAudioPlayback() ||
        World->GetNetMode() == NM_DedicatedServer || !GEngine || !GEngine->UseSound() ||
        !World->GetAudioDeviceRaw()) return;

    UGameplayStatics::PushSoundMixModifier(World, GlobalGainMix);
    AppliedGlobalGainMix = GlobalGainMix;
    bGlobalGainMixRequested = true;
    ++GlobalGainMixStartCount;
}

void UCafeSceneMusicComponent::RemoveGlobalGainMix()
{
    if (!bGlobalGainMixRequested) return;
    UWorld* World = GetWorld();
    // Owner 在退出场景时可能已销毁；清理只依赖音频设备与实际已 Push 的资产。
    if (IsValid(AppliedGlobalGainMix) && World && World->AllowAudioPlayback() &&
        GEngine && GEngine->UseSound() && World->GetAudioDeviceRaw())
    {
        UGameplayStatics::PopSoundMixModifier(World, AppliedGlobalGainMix);
    }
    bGlobalGainMixRequested = false;
    AppliedGlobalGainMix = nullptr;
}

void UCafeSceneMusicComponent::StartMusic()
{
    ApplyGlobalGainMix();
    if (bMusicRequested) return;
    UWorld* World = GetWorld();
    AActor* Owner = GetOwner();
    if (!IsValid(Music) || !IsValid(Owner) || Owner->IsActorBeingDestroyed() ||
        !World || !World->IsGameWorld() || !World->AllowAudioPlayback() ||
        World->GetNetMode() == NM_DedicatedServer || !World->GetAudioDeviceRaw()) return;

    if (!IsValid(AudioComponent))
    {
        AudioComponent = NewObject<UAudioComponent>(Owner, TEXT("CafeSceneMusicAudio"));
        AudioComponent->bAutoActivate = false;
        AudioComponent->bAutoDestroy = false;
        AudioComponent->bAllowSpatialization = false;
        AudioComponent->bOverrideAttenuation = true;
        AudioComponent->AttenuationOverrides.bAttenuate = false;
        AudioComponent->AttenuationOverrides.bSpatialize = false;
        AudioComponent->bCanPlayMultipleInstances = false;
        AudioComponent->SetUISound(false);
        Owner->AddInstanceComponent(AudioComponent);
        AudioComponent->RegisterComponent();
    }
    AudioComponent->Stop();
    AudioComponent->SetSound(Music);
    AudioComponent->SetPitchMultiplier(1.f);
    SetMusicVolume(Volume);
    const float Fade = FMath::IsFinite(FadeInSeconds) ? FMath::Clamp(FadeInSeconds, 0.f, 10.f) : 1.f;
    // VolumeMultiplier carries the gain; FadeIn fades to unity to avoid applying gain twice.
    AudioComponent->FadeIn(Fade, 1.f, 0.f);
    bMusicRequested = true;
    ++MusicStartCount;
}

void UCafeSceneMusicComponent::StopMusic()
{
    bMusicRequested = false;
    if (IsValid(AudioComponent)) AudioComponent->Stop();
}

void UCafeSceneMusicComponent::SetMusicVolume(float NewVolume)
{
    Volume = FMath::IsFinite(NewVolume) ? FMath::Clamp(NewVolume, 0.f, 1.f) : .22f;
    if (IsValid(AudioComponent)) AudioComponent->SetVolumeMultiplier(Volume);
}
