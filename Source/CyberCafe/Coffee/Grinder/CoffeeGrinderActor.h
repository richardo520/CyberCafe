// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoffeeGrinderActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UGrabComponent;
class UNiagaraComponent;
class UAudioComponent;
class USoundBase;
class AGrinderHandleActor;
class AGrinderDrawerActor;

/** 有符号实际柄角，仅用于观察；有效研磨另由净角采样与主体方向保护判定。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGrinderHandleTurnedSignature, float, DeltaAngleDeg, float, TotalAngleDeg);
/** 实际豆粉转换之后广播；DrainGround 不广播，避免抽屉回调递归。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGrinderGrindProgressSignature, float, BeanGramsLeft, float, GroundGramsInGrinder);

/** 保留蓝图组件外形；挂点局部 +Z 为柄轴，抽屉沿局部 +Y 拉出。 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ACoffeeGrinderActor : public AActor
{
    GENERATED_BODY()

public:
    ACoffeeGrinderActor();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UStaticMeshComponent> BodyMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UGrabComponent> GrabComp;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<USceneComponent> HandleMountPoint;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<USceneComponent> DrawerMountPoint;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<USceneComponent> BeanEntryPoint;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UStaticMeshComponent> BeanPileMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UNiagaraComponent> GrindFX;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UAudioComponent> GrindSFX;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grinder|Children")
    TSubclassOf<AGrinderHandleActor> HandleClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grinder|Children")
    TSubclassOf<AGrinderDrawerActor> DrawerClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind", meta = (ClampMin = "0.0"))
    float BeanCapacityGrams;
    /** 默认仍为 15/(6*360) g/度，不以固定计时替代玩家操作。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind", meta = (ClampMin = "0.0"))
    float GramsPerDegree;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float GrindEfficiency;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind")
    bool bOneWayEffective = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind")
    int32 EffectiveDirectionSign = 1;

    /** 保留旧分档/缩放参数；位移模式不压缩网格。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind", meta = (ClampMin = "0"))
    int32 BeanPileStepCount;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BeanPileMinDisplayRatio;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind", meta = (ClampMin = "0.0"))
    float BeanPileHeightMultiplier;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind")
    bool bTranslateBeanPile = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind", meta = (ClampMin = "0.0"))
    float BeanPileTravelCM = 2.f;

    /** 旧开关保留；无论其值如何，无豆也不会播放 LoadedGrindSound。空转声需显式配置。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Grind")
    bool bSilentWhenEmpty;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio")
    TObjectPtr<USoundBase> LoadedGrindSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio")
    TObjectPtr<USoundBase> EmptyGrindSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.0"))
    float GrindSFXFadeInTime;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.0"))
    float GrindSFXFadeOutTime;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.01"))
    float GrindStopDelay;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio")
    bool bModulateVolumeBySpeed;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.0", EditCondition = "bModulateVolumeBySpeed"))
    float MinSpeedDegPerSec;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.0", EditCondition = "bModulateVolumeBySpeed"))
    float MaxSpeedDegPerSec;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.0", ClampMax = "5.0", EditCondition = "bModulateVolumeBySpeed"))
    float MinVolumeMultiplier;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.0", ClampMax = "5.0", EditCondition = "bModulateVolumeBySpeed"))
    float MaxVolumeMultiplier;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grinder|Audio", meta = (ClampMin = "0.1", EditCondition = "bModulateVolumeBySpeed"))
    float SpeedSmoothing;

    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    TObjectPtr<AGrinderHandleActor> HandleRef;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    TObjectPtr<AGrinderDrawerActor> DrawerRef;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    float AccumulatedHandleAngleDeg;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    float CurrentBeanGrams;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    float CurrentGroundGrams;
    /** 消耗 = 累计粉产出 + 损耗；粉转移/倒出不会改写累计产出。 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    double GrindLossGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    double TotalBeanConsumedGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    double TotalGroundOutputGrams = 0.0;
    /** 工作台清理的豆粉独立记账，不改写研磨产出、消耗或损耗。 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    double DiscardedBeanGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    double DiscardedGroundGrams = 0.0;
    UPROPERTY(Transient)
    FVector BeanPileInitialScale;
    UPROPERTY(Transient)
    FVector BeanPileInitialLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    float LastTurnGameTime;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    float SmoothedAngularSpeedDeg;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    bool bGrindSFXActive;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    float GrindSFXStartTime;

    UPROPERTY(BlueprintAssignable, Category = "Grinder|Events")
    FOnGrinderHandleTurnedSignature OnHandleTurned;
    UPROPERTY(BlueprintAssignable, Category = "Grinder|Events")
    FOnGrinderGrindProgressSignature OnGrindProgress;

    /** 旧接口：使用当前帧 Dt 进入 AdvanceGrinding，并保留柄事件。 */
    UFUNCTION(BlueprintCallable, Category = "Grinder")
    void OnHandleRotated(float DeltaAngleDeg);
    /** 输入有符号已过滤净角，返回实际消耗豆量；0/反向/非法输入立即停声。 */
    UFUNCTION(BlueprintCallable, Category = "Grinder|Grind")
    float AdvanceGrinding(float SignedAngleDeg, float Dt);
    UFUNCTION(BlueprintPure, Category = "Grinder|Grind")
    double GetGrindLossGrams() const { return GrindLossGrams; }
    UFUNCTION(BlueprintPure, Category = "Grinder|Grind")
    double GetTotalBeanConsumedGrams() const { return TotalBeanConsumedGrams; }
    UFUNCTION(BlueprintPure, Category = "Grinder|Grind")
    double GetTotalGroundOutputGrams() const { return TotalGroundOutputGrams; }
    UFUNCTION(BlueprintPure, Category = "Grinder")
    FTransform GetHandleMountWorldTransform() const;
    UFUNCTION(BlueprintPure, Category = "Grinder")
    FTransform GetDrawerMountWorldTransform() const;
    UFUNCTION(BlueprintPure, Category = "Grinder")
    bool IsHeld() const;
    /** 无副作用预检；缺失子件仅在配置可生成时允许恢复。 */
    UFUNCTION(BlueprintPure, Category = "Grinder|Workbench")
    bool CanResetForWorkbench() const;
    UFUNCTION(BlueprintCallable, Category = "Grinder|Workbench")
    bool ResetForWorkbench(const FTransform& WorldTransform);
    UFUNCTION(BlueprintCallable, Category = "Grinder|Grind")
    float TryAddBeans(float Grams);
    UFUNCTION(BlueprintCallable, Category = "Grinder|Grind")
    float DrainGround(float MaxGrams);
    UFUNCTION(BlueprintPure, Category = "Grinder|Grind")
    float GetCurrentBeanGrams() const { return CurrentBeanGrams; }
    UFUNCTION(BlueprintPure, Category = "Grinder|Grind")
    float GetCurrentGroundGrams() const { return CurrentGroundGrams; }

protected:
    virtual void SpawnChildParts();
    void StartGrindSFX();
    void StopGrindSFX();
    /** 旧内部入口仅包装 AdvanceGrinding，不与其互调。 */
    void ProcessGrinding(float AbsDeltaAngleDeg);
    void UpdateBeanPileVisual();

private:
    void SanitizeGrindProperties();
    void UpdateGrindingAudio(float AngleDeg, float Dt, bool bLoaded);
    float ActualTickDt = 1.f / 90.f;
    float TargetAngularSpeedDeg = 0.f;
    bool bPlayingEmptySound = false;
};
