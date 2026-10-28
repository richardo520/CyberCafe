// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ServingSpotActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USceneComponent;
class ALiquidContainerActor;

/**
 * AServingSpotActor
 *
 * 通用"上菜点"：场景里的一个固定位置，玩家把液体容器（通常是 ACupActor）
 * 放进它的检测范围、并且松开手之后，杯子会被瞬间"吸附"到这个点上，
 * 并广播全局事件 UCyberCafeEventSubsystem::OnDrinkServed。
 *
 * 本 Actor 本身只负责"检测 + 吸附 + 广播"，不处理任何上层业务逻辑：
 *   - 教学系统订阅 OnDrinkServed 推进流程
 *   - 正式营业关卡 / 订单评分系统也可以订阅同一个事件
 *
 * 吸附行为：
 *   - 检测条件：杯子 Overlap 到 DetectBox 且 Cup->IsHeld() == false（已松手）
 *   - 吸附动作：禁用物理 → 关闭碰撞 → AttachToComponent(SnapToTarget) → Broadcast
 *   - 单次触发：bHasBeenServed 置 true，之后忽略进一步 Overlap；可通过 ResetServing 复用
 *
 * 美术/策划使用：
 *   - 场景里直接拖该 Actor，调整它的世界 Transform 即为"杯子吸附后的最终姿态"
 *   - DetectBox 的 BoxExtent 可以在 Details 面板里改，决定检测范围
 */
UCLASS(Blueprintable)
class CYBERCAFE_API AServingSpotActor : public AActor
{
    GENERATED_BODY()

public:
    AServingSpotActor();

    //~ Begin AActor
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    //~ End AActor

    //=====================================================================
    // 组件
    //=====================================================================

    /** 根节点：吸附目标 Transform（杯子吸附后其 Actor Transform 与之对齐）*/
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ServingSpot|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    /** 吸附点示意用的 Mesh（可选；美术在蓝图里给个半透明圆盘等）*/
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ServingSpot|Components")
    TObjectPtr<UStaticMeshComponent> IndicatorMesh;

    /** 检测范围：Overlap 到 ALiquidContainerActor 才认为"杯子进入了上菜区"*/
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ServingSpot|Components")
    TObjectPtr<UBoxComponent> DetectBox;

    //=====================================================================
    // 配置
    //=====================================================================

    /**
     * 是否只接受指定类的容器（例如只接受 ACupActor，不接受 ABottleActor）。
     * 为空则接受所有 ALiquidContainerActor 子类。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ServingSpot|Config")
    TSubclassOf<ALiquidContainerActor> AcceptedCupClass;

    /**
     * 单次触发：一次上菜完成后，是否自动"锁死"不再接受新的上菜。
     * 教学关卡：true（防止玩家重复触发）
     * 正式营业关卡：可能设为 false（一个上菜点可以连续出很多杯）
     * 外部可随时调 ResetServing() 重置。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ServingSpot|Config")
    bool bOneShot = true;

    /**
     * 吸附后是否关闭杯子的物理 + 碰撞，让它"钉死"在上菜点。
     * 关闭后玩家无法再把杯子抓起来，保证流程状态稳定。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ServingSpot|Config")
    bool bLockCupAfterServing = true;

    //=====================================================================
    // 运行时查询
    //=====================================================================

    /** 当前是否已经服务过一次（bOneShot = true 下进入锁死状态）*/
    UFUNCTION(BlueprintPure, Category = "ServingSpot")
    bool HasBeenServed() const { return bHasBeenServed; }

    /** 当前"候选杯子"（已进入 DetectBox，正在等玩家松手）*/
    UFUNCTION(BlueprintPure, Category = "ServingSpot")
    ALiquidContainerActor* GetCandidateCup() const { return CandidateCup.Get(); }

    /** 当前已被服务上来的杯子（Attach 到本 Spot 的那一只）*/
    UFUNCTION(BlueprintPure, Category = "ServingSpot")
    ALiquidContainerActor* GetServedCup() const { return ServedCup.Get(); }

    //=====================================================================
    // 外部 API
    //=====================================================================

    /**
     * 重置本 Spot 的"已服务"状态：
     *   - 清空 ServedCup（不会主动销毁，交给外部决定杯子的命运）
     *   - 允许再次接受新的上菜
     * 可供教学/关卡在"客人喝完杯子消失"后调用，让 Spot 回到可用状态。
     */
    UFUNCTION(BlueprintCallable, Category = "ServingSpot")
    void ResetServing();

protected:
    /** Overlap 开始：记录候选杯子 */
    UFUNCTION()
    void HandleDetectBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    /** Overlap 结束：清空候选杯子（如果就是当前候选的那只）*/
    UFUNCTION()
    void HandleDetectEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    /** 真正执行吸附 + 广播 */
    void PerformServe(ALiquidContainerActor* Cup);

    /** 是否可以接受这只杯子（类型过滤 + 状态过滤）*/
    bool IsCupAcceptable(ALiquidContainerActor* Cup) const;

private:
    /** 已完成一次上菜（bOneShot 下用于锁死）*/
    UPROPERTY(Transient)
    bool bHasBeenServed = false;

    /** 等待玩家松手的候选杯子 */
    UPROPERTY(Transient)
    TWeakObjectPtr<ALiquidContainerActor> CandidateCup;

    /** 已被服务上来的杯子（Attach 到本 Spot）*/
    UPROPERTY(Transient)
    TWeakObjectPtr<ALiquidContainerActor> ServedCup;
};
