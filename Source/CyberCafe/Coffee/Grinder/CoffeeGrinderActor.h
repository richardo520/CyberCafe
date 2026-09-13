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
class AGrinderHandleActor;
class AGrinderDrawerActor;

/**
 * 研磨进度事件
 * @param DeltaAngleDeg 本次上报的把手转动角度（有符号，正 = 顺时针有效方向）
 * @param TotalAngleDeg 累计转过的角度（有符号累加，仅用于外部观察 / 调试）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGrinderHandleTurnedSignature, float, DeltaAngleDeg, float, TotalAngleDeg);

/**
 * ACoffeeGrinderActor
 * 老式咖啡研磨器主体。作为整套研磨工具（主体 + 手摇把手 + 抽屉）的宿主 Actor：
 *   - 自身可被 VR 手柄抓取（物理模拟，跟 ABottleActor 类似）。
 *   - 在 BeginPlay 中根据 TSubclassOf 自动 Spawn 出把手 Actor 和抽屉 Actor，
 *     并 Attach 到 HandleMountPoint / DrawerMountPoint 两个 USceneComponent 挂点上。
 *   - 提供 OnHandleRotated() 供把手 Actor 每帧上报转动角度；
 *     数量转换（豆 → 粉）暂不实现，先只广播事件 / 打 log，方便后续接豆量粉量系统。
 *
 * 挂点约定（美术在蓝图里调整这两个 USceneComponent 的 RelativeTransform 即可）：
 *   - HandleMountPoint 局部 +Z 为把手的旋转轴，把手在 XY 平面内绕 Z 转。
 *   - DrawerMountPoint 局部 +Y 为抽屉的拉出方向。
 *   - BeanEntryPoint   预留：豆子入口位置（未来接入倒豆系统时使用）。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ACoffeeGrinderActor : public AActor
{
    GENERATED_BODY()

public:
    ACoffeeGrinderActor();

    virtual void BeginPlay() override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 研磨器主体 Mesh（Root，模拟物理，可抓取） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    /** 抓取组件（EGrabType::Free，跟瓶子/杯子一致） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    /**
     * 把手挂点。美术在蓝图里将其位置拖到研磨器顶端把手轴的中心，
     * 并确保局部 +Z 指向"把手旋转轴"方向（一般就是研磨器竖直向上）。
     * AGrinderHandleActor 会 Attach 到本组件，并使用它的局部空间做旋转极角计算。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<USceneComponent> HandleMountPoint;

    /**
     * 抽屉挂点。美术在蓝图里将其位置拖到抽屉合上时的初始位置，
     * 并确保局部 +Y 指向"抽屉被拉出的方向"（一般是研磨器正面朝外）。
     * AGrinderDrawerActor 会 Attach 到本组件，并使用它的局部空间做滑动约束。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<USceneComponent> DrawerMountPoint;

    /** 豆子入口挂点（预留，暂无逻辑） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<USceneComponent> BeanEntryPoint;

    /** 研磨时的粉尘 / 特效（可选，美术在蓝图里指定 NiagaraSystem） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UNiagaraComponent> GrindFX;

    /** 研磨时的循环音效（可选，蓝图里绑 SoundBase，音量/播放由后续逻辑控制） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grinder|Components")
    TObjectPtr<UAudioComponent> GrindSFX;

    //=====================================================================
    // 子部件类（在主体蓝图里指派）
    //=====================================================================

    /** 把手 Actor 类（蓝图里指派为 BP_GrinderHandle 之类） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grinder|Children")
    TSubclassOf<AGrinderHandleActor> HandleClass;

    /** 抽屉 Actor 类（蓝图里指派为 BP_GrinderDrawer 之类） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grinder|Children")
    TSubclassOf<AGrinderDrawerActor> DrawerClass;

    //=====================================================================
    // 运行时引用
    //=====================================================================

    /** BeginPlay Spawn 出来的把手 Actor 实例 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    TObjectPtr<AGrinderHandleActor> HandleRef;

    /** BeginPlay Spawn 出来的抽屉 Actor 实例 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    TObjectPtr<AGrinderDrawerActor> DrawerRef;

    /** 累计把手转过的角度（有符号累加，仅调试展示 / 事件参数用） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Grinder|Runtime")
    float AccumulatedHandleAngleDeg;

    //=====================================================================
    // 事件
    //=====================================================================

    /** 把手每帧转动上报（当把手被抓取并转动时触发） */
    UPROPERTY(BlueprintAssignable, Category = "Grinder|Events")
    FOnGrinderHandleTurnedSignature OnHandleTurned;

    //=====================================================================
    // API
    //=====================================================================

    /**
     * 由 AGrinderHandleActor 每帧上报本帧转过的角度。
     * @param DeltaAngleDeg 本帧转过的角度（有符号，来自 Atan2 相邻帧差分）。
     *
     * 当前实现：累加 AccumulatedHandleAngleDeg 并广播 OnHandleTurned；
     * 豆 → 粉 的数量转换等交给后续阶段实现。
     */
    UFUNCTION(BlueprintCallable, Category = "Grinder")
    void OnHandleRotated(float DeltaAngleDeg);

    /** 便捷访问：把手挂点世界变换 */
    UFUNCTION(BlueprintPure, Category = "Grinder")
    FTransform GetHandleMountWorldTransform() const;

    /** 便捷访问：抽屉挂点世界变换 */
    UFUNCTION(BlueprintPure, Category = "Grinder")
    FTransform GetDrawerMountWorldTransform() const;

    /** 是否已被抓取 */
    UFUNCTION(BlueprintPure, Category = "Grinder")
    bool IsHeld() const;

protected:
    /** 在 BeginPlay 中 Spawn 子部件并把它们 Attach 到对应挂点上 */
    virtual void SpawnChildParts();
};

