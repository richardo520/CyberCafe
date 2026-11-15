#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CafeSceneMusicComponent.generated.h"

class USoundBase;
class USoundMix;
class UAudioComponent;

/** Scene-lifetime background music, independent of tutorial progress and spatial effects. */
UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UCafeSceneMusicComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCafeSceneMusicComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

    /** Configure only on the target scene instance. Unconfigured worlds remain silent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Music")
    TObjectPtr<USoundBase> Music;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Music")
    bool bAutoPlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Music", meta=(ClampMin="0.0", ClampMax="1.0"))
    float Volume = .22f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Music", meta=(ClampMin="0.0", ClampMax="10.0"))
    float FadeInSeconds = 1.f;

    /** 场景全部声音共用的增益由资产配置，默认不应用。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SceneAudio")
    TObjectPtr<USoundMix> GlobalGainMix = nullptr;

    UFUNCTION(BlueprintCallable, Category="Scene Music")
    void StartMusic();

    UFUNCTION(BlueprintCallable, Category="Scene Music")
    void StopMusic();

    UFUNCTION(BlueprintCallable, Category="Scene Music")
    void SetMusicVolume(float NewVolume);

    UFUNCTION(BlueprintPure, Category="Scene Music")
    UAudioComponent* GetMusicAudioComponent() const { return AudioComponent; }

    /** Request diagnostics, not proof of audible output on a headset. */
    bool IsMusicRequested() const { return bMusicRequested; }
    int32 GetMusicStartCount() const { return MusicStartCount; }

    /** 仅记录已向有效音频设备提交的请求，不代表实际可听音量。 */
    bool IsGlobalGainMixRequested() const { return bGlobalGainMixRequested; }
    int32 GetGlobalGainMixStartCount() const { return GlobalGainMixStartCount; }
    USoundMix* GetGlobalAudioMixForTest() const { return AppliedGlobalGainMix; }

private:
    void ApplyGlobalGainMix();
    void RemoveGlobalGainMix();

    UPROPERTY(Transient)
    TObjectPtr<USoundMix> AppliedGlobalGainMix = nullptr;
    bool bGlobalGainMixRequested = false;
    int32 GlobalGainMixStartCount = 0;

    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> AudioComponent;
    bool bMusicRequested = false;
    int32 MusicStartCount = 0;
};
