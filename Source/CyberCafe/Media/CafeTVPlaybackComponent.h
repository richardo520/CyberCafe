#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CafeTVPlaybackComponent.generated.h"

class AActor;
class UMaterialInterface;
class UMediaPlayer;
class UMediaSoundComponent;
class UMediaSource;
class UMediaTexture;
class UStaticMeshComponent;

UCLASS(ClassGroup=(Media), meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UCafeTVPlaybackComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCafeTVPlaybackComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV")
	TObjectPtr<UMediaSource> MediaSource = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV")
	TObjectPtr<UMediaPlayer> MediaPlayer = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV")
	TObjectPtr<UMediaTexture> MediaTexture = nullptr;

	// 材质资产已引用 MediaTexture；运行时不创建 MID 或修改纹理参数。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV")
	TObjectPtr<UMaterialInterface> ScreenMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV", meta=(ClampMin="1", ClampMax="1"))
	int32 ScreenMaterialIndex = 1;

	// 仅为 TV 基础音量；统一增益由外部 Global SoundMix 管理。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV", meta=(ClampMin="0.0"))
	float Volume = 1.0f;

	// 相对于实际 TV Root 的烘焙屏幕中心；施加到本组件，音源子组件保持零偏移。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV")
	FVector SoundOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cafe|TV|Diagnostics", AdvancedDisplay)
	bool bEnableLoopDiagnostics = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Cafe|TV|Diagnostics")
	int32 MediaOpenedCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Cafe|TV|Diagnostics")
	int32 MediaOpenFailedCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Cafe|TV|Diagnostics")
	int32 MediaEndedCount = 0;

	// 实测时间回绕次数，不将 OnEndReached 通知次数当作循环成功次数。
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Cafe|TV|Diagnostics")
	int32 LoopRewindCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Cafe|TV|Diagnostics")
	bool bLifecycleClosed = true;

	UFUNCTION(BlueprintCallable, Category="Cafe|TV")
	bool StartPlayback();

	UFUNCTION(BlueprintCallable, Category="Cafe|TV")
	void StopPlayback();

	UFUNCTION(BlueprintPure, Category="Cafe|TV")
	UMediaSoundComponent* GetTVAudioComponent() const;

	// 只在 Editor 世界原位装配并标脏，由调用方另行保存；不播放、不改材质资产或槽位。
	UFUNCTION(BlueprintCallable, Category="Cafe|TV|Editor")
	static UCafeTVPlaybackComponent* ConfigureCafeTV(AActor* Television,
		UMediaSource* InMediaSource, UMediaPlayer* InMediaPlayer,
		UMediaTexture* InMediaTexture, UMaterialInterface* InScreenMaterial,
		FVector ScreenCenter);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UStaticMeshComponent* FindOwnerStaticMesh() const;

	UFUNCTION()
	void HandleMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void HandleMediaOpenFailed(FString FailedUrl);

	UFUNCTION()
	void HandleMediaEndReached();

	UPROPERTY(Transient)
	TObjectPtr<UMediaSoundComponent> TVAudioComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> ActivePlayer = nullptr;

	FTimespan PreviousPlaybackTime = FTimespan::Zero();
	bool bHasPreviousPlaybackTime = false;
};
