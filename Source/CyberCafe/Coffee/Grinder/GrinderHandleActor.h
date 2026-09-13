// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrinderHandleActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UGrabComponent;
class UMotionControllerComponent;
class UHapticFeedbackEffect_Base;
class ACoffeeGrinderActor;

/**
 * AGrinderHandleActor
 * 老式咖啡研磨器的手摇把手：作为独立 Actor 存在，被 ACoffeeGrinderActor 在 BeginPlay 中
 * Spawn 出来并 Attach 到 HandleMountPoint 挂点上。
 *
 * 交互设计：
 *   - GrabComp 使用 EGrabType::Custom：抓取瞬间不 Attach 到手柄，把手依然挂在研磨器上，
 *     位置由本类每帧在 Tick 里根据"手在旋转平面上的极角"重新计算。
 *   - 每帧把手柄位置转到 HandleMountPoint 的局部空间，投影到 XY 平面算极角 θ_hand；
 *     Δθ = FindDeltaAngleDegrees(LastHandAngle, θ_hand)；
 *     CurrentAngleDeg += Δθ；
 *     SetActorRelativeRotation(FRotator(0, CurrentAngleDeg, 0))  // 绕挂点的 Z 轴自旋（Yaw）
 *     并调用 OwnerGrinder->OnHandleRotated(Δθ) 上报数据。
 *   - 由于计算/驱动完全在"挂点局部空间"中进行，即使玩家举着主体到处走，
 *     把手也永远绕主体的旋转轴旋转，天然满足"主体不固定"的需求。
 *
 * 松手：由 UGrabComponent::OnDropped 触发，无任何脱离逻辑——把手永远跟主体。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API AGrinderHandleActor : public AActor
{
    GENERATED_BODY()

public:
    AGrinderHandleActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 把手 Mesh（Root，不模拟物理——它永远 Attach 在挂点上） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handle|Components")
    TObjectPtr<UStaticMeshComponent> HandleMesh;

    /** 抓取组件（EGrabType::Custom） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handle|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    //=====================================================================
    // 配置
    //=====================================================================

    /**
     * 手偏出旋转半径的最大容差 (cm)。
     * 计算手柄相对挂点的水平投影长度 |Local.XY|，若与设计半径 HandleArmRadius 相差
     * 超过 MaxHandOffset，则视为"玩家的手已经跑掉"，自动 TryRelease() 松手。
     * <= 0 表示不检查。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle|Config", meta = (ClampMin = "0.0"))
    float MaxHandOffset;

    /**
     * 把手臂长 (cm)：从挂点中心到把手末端手柄的水平距离，用于 MaxHandOffset 判定。
     * 视觉上通常与美术模型的把手长度一致；若 MaxHandOffset <= 0 时本值不生效。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle|Config", meta = (ClampMin = "0.0"))
    float HandleArmRadius;

    /**
     * 是否只允许"单方向"的转动被视为有效研磨转动。
     * true  : 反向转动（Δθ 与 EffectiveDirectionSign 反号）不上报给主体，
     *         视觉上把手依然反向转动，但研磨器不会因此磨豆。
     * false : 双向都算有效。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle|Config")
    bool bOneWayEffective;

    /**
     * 有效转动方向的符号（+1 或 -1）。
     * +1 : Δθ > 0 (相对挂点局部 +Z 逆时针方向) 视为有效
     * -1 : Δθ < 0 视为有效
     * 仅在 bOneWayEffective = true 时生效。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle|Config")
    int32 EffectiveDirectionSign;

    /** 转动触觉反馈（可选，抓着把手转动时可以周期性播放，让玩家感受"咔哒" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle|Feedback")
    TObjectPtr<UHapticFeedbackEffect_Base> TurnHaptic;

    /** 每转过多少度触发一次 TurnHaptic 触觉（>0 才启用；用于模拟"咔哒感"） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle|Feedback", meta = (ClampMin = "0.0"))
    float HapticIntervalDeg;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 归属研磨器主体（Spawn 时由主体注入） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Handle|Runtime")
    TObjectPtr<ACoffeeGrinderActor> OwnerGrinder;

    /** 相对挂点的当前旋转角度（度，累积） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Handle|Runtime")
    float CurrentAngleDeg;

    /** 上一帧手在挂点局部旋转平面（XY）上的极角，用于差分算 Δθ */
    UPROPERTY(Transient)
    float LastHandAngleDeg;

    /** 是否已经采样到有效的 LastHandAngleDeg（刚抓住时的第一帧不做差分） */
    UPROPERTY(Transient)
    bool bHasValidLastHandAngle;

    /** 上次触发触觉时的 CurrentAngleDeg，用于按 HapticIntervalDeg 触发咔哒感 */
    UPROPERTY(Transient)
    float LastHapticAngleDeg;

    //=====================================================================
    // API
    //=====================================================================

    /**
     * 由 ACoffeeGrinderActor 在 Spawn 后调用：绑定归属主体，把自己 Attach 到挂点。
     * @param InOwner 主体研磨器
     * @param MountComp 挂点（主体的 HandleMountPoint）
     */
    UFUNCTION(BlueprintCallable, Category = "Handle")
    void AttachToGrinder(ACoffeeGrinderActor* InOwner, USceneComponent* MountComp);

protected:
    /** GrabComp 广播回调 */
    UFUNCTION()
    void HandleGrabbed();

    UFUNCTION()
    void HandleDropped();

    /** 挂点组件（缓存用，避免每帧走主体去取） */
    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> MountRef;
};

