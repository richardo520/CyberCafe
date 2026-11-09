#include "Media/CafeTVPlaybackComponent.h"

#include "AudioDeviceHandle.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogCafeTVPlayback, Log, All);

UCafeTVPlaybackComponent::UCafeTVPlaybackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.201f;
	bAutoActivate = false;
	SetCanEverAffectNavigation(false);
}

void UCafeTVPlaybackComponent::BeginPlay()
{
	Super::BeginPlay();
	StartPlayback();
}

void UCafeTVPlaybackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPlayback();
	Super::EndPlay(EndPlayReason);
}

UStaticMeshComponent* UCafeTVPlaybackComponent::FindOwnerStaticMesh() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (UStaticMeshComponent* RootMesh = Cast<UStaticMeshComponent>(Owner->GetRootComponent()))
	{
		return RootMesh;
	}
	return Owner->FindComponentByClass<UStaticMeshComponent>();
}

bool UCafeTVPlaybackComponent::StartPlayback()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !bEnabled || !IsRegistered())
	{
		return false;
	}
	if (!bLifecycleClosed)
	{
		return true;
	}

	UStaticMeshComponent* OwnerMesh = FindOwnerStaticMesh();
	if (!IsValid(MediaSource) || !IsValid(MediaPlayer) || !IsValid(MediaTexture)
		|| !IsValid(ScreenMaterial) || !OwnerMesh || ScreenMaterialIndex != 1
		|| OwnerMesh->GetNumMaterials() <= ScreenMaterialIndex
		|| MediaTexture->GetMediaPlayer() != MediaPlayer)
	{
		UE_LOG(LogCafeTVPlayback, Error, TEXT("TV 配置无效：需完整媒体硬引用、匹配的纹理播放器及屏幕槽位 1。"));
		return false;
	}

	ActivePlayer = MediaPlayer;
	bLifecycleClosed = false;
	bHasPreviousPlaybackTime = false;
	SetRelativeLocation(SoundOffset);
	OwnerMesh->SetMaterial(ScreenMaterialIndex, ScreenMaterial);

	// 音频设备缺失不阻止 WMF 打开视频，例如独立的无音频解码验证。
	const FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
	if (AudioDevice.IsValid())
	{
		TVAudioComponent = NewObject<UMediaSoundComponent>(GetOwner(), NAME_None, RF_Transient);
		TVAudioComponent->bAutoActivate = false;
		TVAudioComponent->bAutoDestroy = false;
		TVAudioComponent->bStopWhenOwnerDestroyed = true;
		TVAudioComponent->bIsUISound = false;
		TVAudioComponent->bAllowSpatialization = true;
		TVAudioComponent->Channels = EMediaSoundChannels::Mono;
		TVAudioComponent->bOverrideAttenuation = true;
		FSoundAttenuationSettings& Attenuation = TVAudioComponent->AttenuationOverrides;
		Attenuation.bAttenuate = true;
		Attenuation.bSpatialize = true;
		Attenuation.SpatializationAlgorithm = SPATIALIZATION_Default;
		Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
		Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation.AttenuationShapeExtents = FVector(60.0f, 0.0f, 0.0f);
		Attenuation.FalloffDistance = 500.0f;
		TVAudioComponent->SoundClass = GetDefault<UAudioSettings>()->GetDefaultSoundClass();
		TVAudioComponent->SetCanEverAffectNavigation(false);
		GetOwner()->AddInstanceComponent(TVAudioComponent);
		TVAudioComponent->SetupAttachment(this);
		TVAudioComponent->SetRelativeTransform(FTransform::Identity);
		TVAudioComponent->RegisterComponent();
		TVAudioComponent->SetMediaPlayer(ActivePlayer);
		TVAudioComponent->UpdatePlayer();
		TVAudioComponent->SetVolumeMultiplier(FMath::Max(0.0f, Volume));
	}
	else
	{
		UE_LOG(LogCafeTVPlayback, Warning, TEXT("TV 世界没有音频设备；仅打开视频。"));
	}

	// OpenSource 可能同步回调，必须先完成状态、音源及事件绑定。
	ActivePlayer->OnMediaOpened.AddUniqueDynamic(this, &UCafeTVPlaybackComponent::HandleMediaOpened);
	ActivePlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &UCafeTVPlaybackComponent::HandleMediaOpenFailed);
	ActivePlayer->OnEndReached.AddUniqueDynamic(this, &UCafeTVPlaybackComponent::HandleMediaEndReached);
	ActivePlayer->SetLooping(true);
	// 保留主 agent 配置的 PlayOnOpen；实际启动由打开成功回调负责。
	if (!ActivePlayer->OpenSource(MediaSource))
	{
		HandleMediaOpenFailed(FString());
		return false;
	}
	return !bLifecycleClosed;
}

void UCafeTVPlaybackComponent::HandleMediaOpened(FString OpenedUrl)
{
	if (bLifecycleClosed || !IsValid(ActivePlayer))
	{
		return;
	}
	++MediaOpenedCount;
	bHasPreviousPlaybackTime = false;
	if (!ActivePlayer->SetLooping(true))
	{
		UE_LOG(LogCafeTVPlayback, Error, TEXT("TV 播放器不支持循环；已停止。"));
		StopPlayback();
		return;
	}
	if (!ActivePlayer->Play())
	{
		UE_LOG(LogCafeTVPlayback, Error, TEXT("TV 媒体已打开，但 Play 失败；已停止。"));
		StopPlayback();
		return;
	}
	if (IsValid(TVAudioComponent))
	{
		if (!TVAudioComponent->IsActive())
		{
			TVAudioComponent->Start();
		}
		// UpdatePlayer registers the generator's sample sink only while active.
		// Establish that real audio clock immediately, before any observer sinks.
		TVAudioComponent->UpdatePlayer();
		TVAudioComponent->SetComponentTickEnabled(true);
	}
	SetComponentTickEnabled(bEnableLoopDiagnostics);
	UE_LOG(LogCafeTVPlayback, Log, TEXT("TV 已打开：次数=%d，循环=%d，播放=%d，时长=%.3f 秒。"),
		MediaOpenedCount, ActivePlayer->IsLooping(), ActivePlayer->IsPlaying(),
		ActivePlayer->GetDuration().GetTotalSeconds());
}

void UCafeTVPlaybackComponent::HandleMediaOpenFailed(FString FailedUrl)
{
	if (bLifecycleClosed)
	{
		return;
	}
	++MediaOpenFailedCount;
	UE_LOG(LogCafeTVPlayback, Error, TEXT("TV 媒体打开失败：次数=%d，Source=%s。"),
		MediaOpenFailedCount, *GetNameSafe(MediaSource));
	StopPlayback();
}

void UCafeTVPlaybackComponent::HandleMediaEndReached()
{
	if (!bLifecycleClosed)
	{
		++MediaEndedCount;
	}
	// 原生循环负责继续播放；结束通知不触发 Seek、重开或重复启动音源。
}

void UCafeTVPlaybackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bEnableLoopDiagnostics || bLifecycleClosed || !IsValid(ActivePlayer))
	{
		SetComponentTickEnabled(false);
		return;
	}

	const FTimespan CurrentTime = ActivePlayer->GetTime();
	if (bHasPreviousPlaybackTime && ActivePlayer->IsLooping() && ActivePlayer->IsPlaying()
		&& PreviousPlaybackTime > CurrentTime + FTimespan::FromMilliseconds(100.0))
	{
		++LoopRewindCount;
		UE_LOG(LogCafeTVPlayback, Log, TEXT("TV 时间回绕：次数=%d，%.3f -> %.3f 秒，结束通知=%d。"),
			LoopRewindCount, PreviousPlaybackTime.GetTotalSeconds(), CurrentTime.GetTotalSeconds(),
			MediaEndedCount);
	}
	PreviousPlaybackTime = CurrentTime;
	bHasPreviousPlaybackTime = true;
}

void UCafeTVPlaybackComponent::StopPlayback()
{
	bLifecycleClosed = true;
	bHasPreviousPlaybackTime = false;
	SetComponentTickEnabled(false);
	if (IsValid(ActivePlayer))
	{
		ActivePlayer->OnMediaOpened.RemoveDynamic(this, &UCafeTVPlaybackComponent::HandleMediaOpened);
		ActivePlayer->OnMediaOpenFailed.RemoveDynamic(this, &UCafeTVPlaybackComponent::HandleMediaOpenFailed);
		ActivePlayer->OnEndReached.RemoveDynamic(this, &UCafeTVPlaybackComponent::HandleMediaEndReached);
	}
	if (IsValid(TVAudioComponent))
	{
		TVAudioComponent->Stop();
		TVAudioComponent->SetMediaPlayer(nullptr);
		TVAudioComponent->SetComponentTickEnabled(false);
		GetOwner()->RemoveInstanceComponent(TVAudioComponent);
		TVAudioComponent->DestroyComponent();
	}
	TVAudioComponent = nullptr;
	if (IsValid(ActivePlayer))
	{
		ActivePlayer->Close();
	}
	ActivePlayer = nullptr;
}

UMediaSoundComponent* UCafeTVPlaybackComponent::GetTVAudioComponent() const
{
	return TVAudioComponent;
}

UCafeTVPlaybackComponent* UCafeTVPlaybackComponent::ConfigureCafeTV(AActor* Television,
	UMediaSource* InMediaSource, UMediaPlayer* InMediaPlayer,
	UMediaTexture* InMediaTexture, UMaterialInterface* InScreenMaterial, FVector ScreenCenter)
{
#if WITH_EDITOR
	if (!IsValid(Television) || Television->IsTemplate() || !Television->GetWorld()
		|| Television->GetWorld()->WorldType != EWorldType::Editor
		|| !Television->GetRootComponent() || ScreenCenter.ContainsNaN()
		|| !IsValid(InMediaSource) || !IsValid(InMediaPlayer) || !IsValid(InMediaTexture)
		|| !IsValid(InScreenMaterial) || InMediaTexture->GetMediaPlayer() != InMediaPlayer)
	{
		UE_LOG(LogCafeTVPlayback, Error, TEXT("TV 原位装配失败：需 Editor 中的实际 Actor、有效媒体引用及屏幕中心。"));
		return nullptr;
	}
	USceneComponent* Root = Television->GetRootComponent();
	UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Root);
	if (!Mesh)
	{
		Mesh = Television->FindComponentByClass<UStaticMeshComponent>();
	}
	if (!Mesh || Mesh->GetNumMaterials() <= 1)
	{
		return nullptr;
	}

	TInlineComponentArray<UCafeTVPlaybackComponent*> Existing;
	Television->GetComponents(Existing);
	if (Existing.Num() != 0)
	{
		UCafeTVPlaybackComponent* Component = Existing[0];
		if (Existing.Num() == 1 && Component->MediaSource == InMediaSource
			&& Component->MediaPlayer == InMediaPlayer && Component->MediaTexture == InMediaTexture
			&& Component->ScreenMaterial == InScreenMaterial && Component->ScreenMaterialIndex == 1
			&& Component->SoundOffset.Equals(ScreenCenter) && Component->GetAttachParent() == Root
			&& Component->GetRelativeLocation().Equals(ScreenCenter))
		{
			return Component;
		}
		UE_LOG(LogCafeTVPlayback, Error, TEXT("TV 已存在不同配置的播放组件；不覆盖、不追加。"));
		return nullptr;
	}

	Television->Modify();
	UCafeTVPlaybackComponent* Component = NewObject<UCafeTVPlaybackComponent>(Television,
		MakeUniqueObjectName(Television, StaticClass(), TEXT("CafeTVPlayback")), RF_Transactional);
	Component->Modify();
	Component->MediaSource = InMediaSource;
	Component->MediaPlayer = InMediaPlayer;
	Component->MediaTexture = InMediaTexture;
	Component->ScreenMaterial = InScreenMaterial;
	Component->ScreenMaterialIndex = 1;
	Component->SoundOffset = ScreenCenter;
	Component->SetMobility(Root->Mobility);
	Television->AddInstanceComponent(Component);
	Component->SetupAttachment(Root);
	Component->SetRelativeLocation(ScreenCenter);
	Component->OnComponentCreated();
	Component->RegisterComponent();
	Television->MarkPackageDirty();
	return Component;
#else
	return nullptr;
#endif
}
