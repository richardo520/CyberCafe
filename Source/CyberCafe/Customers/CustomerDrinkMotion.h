// @trace BC-01 BC-02
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CustomerDrinkMotion.generated.h"
class UAnimSequence;class UStaticMesh;
USTRUCT(BlueprintType)
struct FCustomerBlinkCue
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float Time=0;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float HalfWidth=.12f;
 FCustomerBlinkCue() = default;
 FCustomerBlinkCue(float InTime,float InWidth):Time(InTime),HalfWidth(InWidth){}
};
/** Fixed first-customer motion: coordinates are skeletal mesh component space, cm. */
UCLASS(BlueprintType)
class CYBERCAFE_API UCustomerDrinkMotion:public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UAnimSequence> Animation;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMesh> SupportedCupMesh;
 // Exact opt-in scale for the authored mesh; defaults keep legacy scale-1 checks.
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector SupportedCupScale=FVector::OneVector;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float SampleRate=30;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FTransform> CupSamples;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float GraspTime=1.55f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float ReleaseTime=5.35f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float SipStart=3.2f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float SipEnd=3.7f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float SipVolumeML=20;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float MaxStartOffsetCM=5;
 // Defaults reproduce existing saved motions; other characters override these in their data asset.
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FName BlinkMorph=TEXT("Blink");
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FName VoiceSocket=TEXT("head");
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float SipSoundVolume=.4f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TArray<FCustomerBlinkCue> BlinkCues={FCustomerBlinkCue(3.12f,.12f),FCustomerBlinkCue(6.02f,.12f)};
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float LiftBlendEnd=2.5f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float ReturnBlendStart=4.f;
 float BlinkWeight(float Seconds) const;
 float GripBlend(float Seconds) const;
 bool IsValidMotion() const;
 FTransform Sample(float Seconds) const;
 float Duration() const;
};
