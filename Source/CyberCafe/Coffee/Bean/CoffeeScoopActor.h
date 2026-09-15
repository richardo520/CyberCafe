// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoffeeScoopActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class USphereComponent;
class UGrabComponent;
class ABeanJarActor;
class ACoffeeGrinderActor;

/**
 * ACoffeeScoopActor
 * 咖啡勺：VR 手柄可 Snap 抓取，通过 Tick 判定"舀豆"和"倒豆"两个动作。
 *
 * 舀豆判定：
 *   1) 勺子被玩家握着（GrabComp->IsHeld()）
 *   2) 勺兜朝上（GetActorUpVector().Z >= UprightDot）
 *   3) BowlSphere 与开盖的 ABeanJarActor 的 Overlap Volume 相交
 *   4) 勺兜位置低于罐口一定深度（HandLocal.Z <= ScoopDepthBelowLidCm）
 *   → 每 Tick 从罐子里"吸"一小口豆到勺子，直到勺满
 *
 * 倒豆判定：
 *   1) 勺子有豆（ScoopLoadGrams > 0）
 *   2) 勺兜朝下（GetActorUpVector().Z <= PourDot）
 *   3) BowlPoint 位置到某 ACoffeeGrinderActor::BeanEntryPoint 的水平距离 <= PourAlignRadiusCm
 *      且 BowlPoint 位于 BeanEntryPoint 上方 (Local Z > 0)
 *   → 每 Tick 从勺子往研磨器倒一小口，直到勺空或研磨器顶仓满
 *
 * 未命中研磨器口的倾倒：豆子直接被扣掉（视为洒到地上），不生成物理豆颗粒。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ACoffeeScoopActor : public AActor
{
    GENERATED_BODY()

public:
    ACoffeeScoopActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 勺子 Mesh（Root，模拟物理，Snap 抓取） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scoop|Components")
    TObjectPtr<UStaticMeshComponent> ScoopMesh;

    /** 抓取组件（Snap 到勺柄握把 SnapPoint） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scoop|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    /**
     * 勺兜采样中心：蓝图里拖到勺子凹陷部分正中央 & 高度略高于勺兜底面。
     * 舀豆 / 倒豆 时用它做 Overlap 判定与朝向参考。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scoop|Components")
    TObjectPtr<USceneComponent> BowlPoint;

    /** 勺兜 Overlap 球（NoCollision Query 用），中心与 BowlPoint 一致 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scoop|Components")
    TObjectPtr<USphereComponent> BowlSphere;

    /**
     * 勺兜内的豆可视化 Mesh（可选）。蓝图里拖到勺子凹陷内部合适位置，
     * 随 ScoopLoadGrams / ScoopCapacityGrams 线性缩放 Z；空勺时自动隐藏。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scoop|Components")
    TObjectPtr<UStaticMeshComponent> ScoopBeanMesh;

    //=====================================================================
    // 配置
    //=====================================================================

    /** 勺子最大装豆量 (g)。默认 5g 一勺 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoop|Beans", meta = (ClampMin = "0.0"))
    float ScoopCapacityGrams;

    /**
     * 舀豆速率 (g/秒)。控制勺子接触豆堆时"多快装满"。
     * 5g 上限 + 20g/s → 0.25s 装满；给玩家一点\"舀动作\"感。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoop|Beans", meta = (ClampMin = "0.0"))
    float ScoopFillRateGramsPerSec;

    /**
     * 倒豆速率 (g/秒)。控制勺子倒进研磨器时的速度。
     * 5g / 30g/s ≈ 0.17s 全倒完。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoop|Beans", meta = (ClampMin = "0.0"))
    float PourRateGramsPerSec;

    /**
     * "勺兜朝上"判定阈值：GetActorUpVector().Z >= UprightDot 才允许舀豆。
     * 1.0 = 必须完全垂直向上；0.5 ≈ 60° 内均可。默认 0.5。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoop|Detection", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float UprightDot;

    /**
     * "勺兜朝下"判定阈值：GetActorUpVector().Z <= PourDot 才允许倒豆。
     * -1.0 = 必须完全朝下；-0.1 ≈ 稍微一倾即可。默认 -0.1（VR 里更好操作）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoop|Detection", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float PourDot;

    /**
     * 勺兜相对于罐口 Socket 向下多深才算"伸进罐里"（cm）。
     * <=0 表示放宽为"只要 Overlap 到就算"。默认 0（简化处理，靠罐内 BeanPileMesh 的碰撞体积把控）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoop|Detection", meta = (ClampMin = "0.0"))
    float ScoopDepthBelowLidCm;

    /**
     * 倒豆判定：BowlPoint 与 BeanEntryPoint 之间的最大水平距离 (cm)。
     * 越大越宽容（新手友好），越小越像现实里"必须对准"。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoop|Detection", meta = (ClampMin = "0.0"))
    float PourAlignRadiusCm;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 勺子当前装的豆量 (g) */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Scoop|Runtime")
    float ScoopLoadGrams;

    //=====================================================================
    // API
    //=====================================================================

    /** 便捷查询：勺子是否已被抓取 */
    UFUNCTION(BlueprintPure, Category = "Scoop")
    bool IsHeld() const;

protected:
    /** 缓存 ScoopBeanMesh 初始 Scale，运行时按比例缩 Z */
    UPROPERTY(Transient)
    FVector ScoopBeanInitialScale;

    /** 舀豆：从与 BowlSphere Overlap 的第一个开盖罐子里吸豆 */
    void TickScoopFromJar(float DeltaTime);

    /** 倒豆：若对准了某研磨器的 BeanEntryPoint 则倒进去；否则视为洒落丢弃 */
    void TickPourToGrinder(float DeltaTime);

    /** 查找 BowlSphere Overlap 到的、第一个已开盖且尚有豆的 ABeanJarActor */
    ABeanJarActor* FindOverlappingOpenJar() const;

    /** 找到 BowlPoint 附近对准的研磨器（返回 null 表示没对准） */
    ACoffeeGrinderActor* FindAlignedGrinder() const;

    /** 根据 ScoopLoadGrams / ScoopCapacityGrams 更新 ScoopBeanMesh 可视化 */
    void UpdateScoopBeanVisual();
};
