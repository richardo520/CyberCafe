#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CafeSceneMusicComponent.generated.h"

class USoundBase;
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

private:
    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> AudioComponent;
    bool bMusicRequested = false;
    int32 MusicStartCount = 0;
};
