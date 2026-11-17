#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimInstance.h"
#include "RegularCustomerAttention.generated.h"

/** Instance-only head attention; never rotates the calibrated character or its mesh. */
UCLASS()
class CYBERCAFE_API URegularCustomerAttention : public UActorComponent
{
    GENERATED_BODY()
public:
    URegularCustomerAttention();
    bool InitializeLayer(TSubclassOf<UAnimInstance> Layer);
    void LookAtActivity(AActor* Target);
    void Acknowledge();
    void PrepareForTasting();
    void SetAttentionEnabled(bool bValue) { bEnabled = bValue; if (bValue) YieldAt = -1; }
    FVector GetLookTarget() const { return LookTarget; }
    float GetAttentionAlpha() const { return Alpha; }
protected:
    virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<AActor> Activity;
    FVector LookTarget = FVector::ZeroVector;
    float Alpha = 0, NodRemaining = 0;
    double ActivityUntil = 0;
    double YieldAt = -1;
    bool bEnabled = true, bInitializedTarget = false;
};

UCLASS(Blueprintable, Transient)
class CYBERCAFE_API URegularCustomerAttentionAnimation : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Attention") FVector AttentionTarget = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Attention") float AttentionAlpha = 0;
};
