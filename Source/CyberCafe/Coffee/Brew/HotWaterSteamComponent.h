#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HotWaterSteamComponent.generated.h"

class UParticleSystem;
class UParticleSystemComponent;
class ULiquidTransferSubsystem;
struct FHotWaterImpact;

// 仅手冲壶显式启用的落点视觉，不表示液体温度或热量。
UCLASS(ClassGroup=(Effects), meta=(BlueprintSpawnableComponent))
class CYBERCAFE_API UHotWaterSteamComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHotWaterSteamComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hot Water Steam")
    TObjectPtr<UParticleSystem> SteamSystem = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hot Water Steam", meta=(ClampMin="0.001"))
    float SteamScale = .18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hot Water Steam", meta=(ClampMin="0.0", Units="s"))
    float ImpactHoldSeconds = .18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hot Water Steam")
    bool bEnabled = true;

    void StopSteam(bool bImmediate = false);
    UFUNCTION(BlueprintPure, Category="Hot Water Steam|Debug")
    UParticleSystemComponent* GetSteamComponent() const;
    UFUNCTION(BlueprintPure, Category="Hot Water Steam|Debug")
    bool IsSteamRequested() const { return bSteamRequested; }
    UFUNCTION(BlueprintPure, Category="Hot Water Steam|Debug")
    FVector GetLastImpactPoint() const { return LastImpactPoint; }
    UFUNCTION(BlueprintPure, Category="Hot Water Steam|Debug")
    double GetLastImpactWaterML() const { return LastImpactWaterML; }
    UFUNCTION(BlueprintPure, Category="Hot Water Steam|Debug")
    int32 GetSteamStartCount() const { return SteamStartCount; }

private:
    void HandleImpact(const FHotWaterImpact& Impact);
    void RefreshSteam();
    void UnbindImpact();
    void ReleaseSteam();
    UFUNCTION()
    void HandleSystemFinished(UParticleSystemComponent* FinishedComponent);

    UPROPERTY(Transient)
    TObjectPtr<UParticleSystemComponent> SteamParticleComponent = nullptr;
    TWeakObjectPtr<ULiquidTransferSubsystem> ImpactSubsystem;
    FDelegateHandle ImpactDelegateHandle;
    FVector LastImpactPoint = FVector::ZeroVector;
    double LastImpactWaterML = 0;
    double LastImpactAge = 0;
    int32 SteamStartCount = 0;
    bool bSteamRequested = false;
    bool bSteamRunning = false;
};
