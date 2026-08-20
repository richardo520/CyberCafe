// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Liquid/LiquidContainerActor.h"
#include "BottleActor.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UHapticFeedbackEffect_Base;
class ACupActor;

/**
 * ABottleActor
 * 酒瓶：当瓶口向下倾斜超过阈值、且瓶内还有液体时，从瓶口发出 Niagara 出液流；
 * 沿瓶口 -Z 方向 LineTrace，命中 ACupActor 则给杯子加液、给瓶子扣液。
 *
 * 说明：
 *   - 松手后瓶子如果仍在物理模拟下继续倾斜，倒酒逻辑照样触发(方案确认)。
 *   - 出液流 Niagara 需在资产上暴露以下 User 参数：
 *       User.LiquidColor (LinearColor)
 *       User.PourRate    (Float)
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ABottleActor : public ALiquidContainerActor
{
    GENERATED_BODY()

public:
    ABottleActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    //=====================================================================
    // 编辑器可调
    //=====================================================================

    /**
     * 瓶口相对 ContainerMesh 的偏移(局部坐标，cm)。
     * 如果指定了 PourSocketName，则优先使用 Socket。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pour")
    FVector PourOffset;

    /** 瓶口 Socket 名(可选，指定后优先于 PourOffset) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pour")
    FName PourSocketName;

    /**
     * 触发倒酒的倾斜角度阈值(度)。
     * 瓶身局部 +Z 与世界 +Z 的夹角超过此值即出液。默认 60°。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pour", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float PourAngleThreshold;

    /** 出液速率(mL/秒) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pour", meta = (ClampMin = "0.0"))
    float PourRatePerSecond;

    /** 从瓶口沿世界 -Z 方向的最大检测距离(cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Pour", meta = (ClampMin = "1.0"))
    float PourTraceDistance;

    /** 出液流 Niagara 特效资产 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|FX")
    TObjectPtr<UNiagaraSystem> PourEffectTemplate;

    /** 倒酒时给持瓶手柄的触觉反馈(可选) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Haptics")
    TObjectPtr<UHapticFeedbackEffect_Base> PourHaptic;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 当前是否处于倒酒状态 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Bottle|Runtime")
    bool bIsPouring;

    /** 当前激活的出液粒子实例 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Bottle|Runtime")
    TObjectPtr<UNiagaraComponent> ActivePourFX;

protected:
    /** 计算瓶口的世界变换(优先用 Socket，否则用 PourOffset) */
    FTransform GetPourWorldTransform() const;

    /** 计算当前瓶身与世界+Z的夹角(度)：0=直立，180=完全倒置 */
    float GetTiltAngleDegrees() const;

    /** 启动倒酒 VFX / 状态 */
    void StartPouring();

    /** 停止倒酒 VFX / 状态 */
    void StopPouring();

    /** 每 Tick 执行的倒酒实际逻辑(FX 位置更新 + LineTrace 判定 + 加/扣液) */
    void UpdatePouring(float DeltaTime);
};
