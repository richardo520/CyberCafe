// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrinderDrawerActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UGrabComponent;
class UMotionControllerComponent;
class UHapticFeedbackEffect_Base;
class USoundBase;
class ACoffeeGrinderActor;

/**
 * AGrinderDrawerActor
 * 老式咖啡研磨器的接粉抽屉：作为独立 Actor 存在，被 ACoffeeGrinderActor 在 BeginPlay 中
 * Spawn 出来并 Attach 到 DrawerMountPoint 挂点上。
 *
 * 三种运行时状态：
 *   State::Attached  抽屉在主体上（默认）。被抓时按下述规则沿 +X 局部方向滑动。
 *   State::Detached  抽屉被完全拉出（滑动距离 >= PullOutDistance），此时它作为独立
 *                    物理刚体被抓在手里，可自由携带。
 *   State::Free      Detached 状态下松手后落到世界里（可再次抓起）。
 *                    重新靠近 DrawerMountPoint 松手时会自动吸回，回到 Attached。
 *
 * 交互约定（对应 ABottleCapActor 的模式，但语义相反）：
 *   - GrabComp 使用 EGrabType::Custom。
 *   - Attached 状态下每帧：把手柄位置转到 DrawerMountPoint 的局部空间，
 *     沿 +Y 分量作为目标偏移；平滑插值后 SetActorRelativeLocation。
 *   - 当 CurrentOffset >= PullOutDistance 时：DetachFromGrinder()——切成 Detached，
 *     开物理并 Attach 到手柄。
 *   - Detached / Free 状态松手时：若离 DrawerMountPoint 世界距离 <= ReattachSnapDistance
 *     则 ReattachToGrinder()；否则落地。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API AGrinderDrawerActor : public AActor
{
    GENERATED_BODY()

public:
    AGrinderDrawerActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 抽屉 Mesh（Root，Attached 时关物理，Detached 时开物理） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drawer|Components")
    TObjectPtr<UStaticMeshComponent> DrawerMesh;

    /** 抓取组件（EGrabType::Custom） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drawer|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    //=====================================================================
    // 配置
    //=====================================================================

    /**
     * 抽屉最大滑动距离 (cm)：沿挂点局部 +Y 从 0 到本值内属于"合法滑动"，
     * 超过本值就触发"拔出"（Detach 到手）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer|Slide", meta = (ClampMin = "0.0"))
    float PullOutDistance;

    /**
     * 滑动跟随的平滑速率（1/s，越大跟手越紧）。
     * 建议 10~25：太低会明显滞后于手柄，太高会消除手抖但也失去阻尼感。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer|Slide", meta = (ClampMin = "0.1"))
    float SlideInterpSpeed;

    /**
     * 手偏离滑轨的最大侧向容差 (cm)：把手柄位置转到挂点局部空间后，
     * 计算 |Local.X|、|Local.Z| 的大小，超过本值视为"玩家的手已经跑掉"，自动松手。
     * <= 0 表示不检查。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer|Slide", meta = (ClampMin = "0.0"))
    float MaxSideOffset;

    /**
     * 松手时自动吸回挂点的距离阈值 (cm)。
     * 处于 Detached / Free 状态松手时，若抽屉世界位置距 DrawerMountPoint <= 本值，
     * 自动 Snap 回挂点回到 Attached 状态；否则以物理体形式停留。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer|Reattach", meta = (ClampMin = "0.0"))
    float ReattachSnapDistance;

    /** 抽屉拔出（Detach）时的触觉反馈（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer|Feedback")
    TObjectPtr<UHapticFeedbackEffect_Base> DetachHaptic;

    /** 抽屉拔出时的音效（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer|Feedback")
    TObjectPtr<USoundBase> DetachSound;

    /** 抽屉吸回挂点时的音效（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer|Feedback")
    TObjectPtr<USoundBase> ReattachSound;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 抽屉状态 */
    enum class EDrawerState : uint8
    {
        Attached,   // 挂在主体上（Attach 到 MountRef）
        Detached,   // 被抓在手上（Attach 到 MotionController）
        Free        // 独立物理体（无 Attach）
    };

    /** 当前状态（非 UPROPERTY，纯 C++ 枚举） */
    EDrawerState State;

    /** 归属研磨器主体（Spawn 时由主体注入） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Drawer|Runtime")
    TObjectPtr<ACoffeeGrinderActor> OwnerGrinder;

    /** Attached 状态下沿 +Y 的滑动偏移（0 = 合上，PullOutDistance = 即将拔出） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Drawer|Runtime")
    float CurrentOffset;

    /** 抓取瞬间手柄相对挂点局部空间的位置（作为"跟手基准"，避免抓的瞬间抽屉跳跃） */
    UPROPERTY(Transient)
    FVector GrabHandOffsetLocal;

    //=====================================================================
    // API
    //=====================================================================

    /**
     * 由 ACoffeeGrinderActor 在 Spawn 后调用：绑定归属主体，把自己 Attach 到挂点。
     * @param InOwner   主体研磨器
     * @param MountComp 挂点（主体的 DrawerMountPoint）
     */
    UFUNCTION(BlueprintCallable, Category = "Drawer")
    void AttachToGrinder(ACoffeeGrinderActor* InOwner, USceneComponent* MountComp);

    /** 强制吸回挂点（内部松手回调或蓝图脚本调用） */
    UFUNCTION(BlueprintCallable, Category = "Drawer")
    void ReattachToGrinder();

    /**
     * 从主体上拔出：切到 Detached，开物理，Attach 到指定 MotionController。
     * 由 Tick 中判定 CurrentOffset >= PullOutDistance 时自动调用。
     */
    UFUNCTION(BlueprintCallable, Category = "Drawer")
    void DetachFromGrinder(UMotionControllerComponent* MotionController);

protected:
    UFUNCTION()
    void HandleGrabbed();

    UFUNCTION()
    void HandleDropped();

    /** 挂点组件（缓存用） */
    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> MountRef;
};

