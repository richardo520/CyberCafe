// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BottleCapActor.generated.h"

class UStaticMeshComponent;
class UGrabComponent;
class UMotionControllerComponent;
class UHapticFeedbackEffect_Base;
class USoundBase;
class ABottleActor;

/**
 * ABottleCapActor
 * 酒瓶盖子：作为独立 Actor 出现，可被 VR 手柄单独抓取。
 *
 * 交互流程：
 *   1. 初始时 Attach 到 ABottleActor::ContainerMesh 的 CapSocket 上，物理关闭；
 *      GrabComp 使用 EGrabType::Custom（不自动 Attach 到手/不关物理），
 *      这样"抓住但还没扭下来"时盖子仍跟随瓶口。
 *   2. 玩家抓住后必须扭转手柄——通过对比"抓取起始时刻手柄相对瓶口的旋转"与
 *      "当前手柄相对瓶口的旋转"，累计扭转角度超过 DetachTwistAngle 时，
 *      才把盖子真正 Attach 到手柄上（"拧下来"），并置 bIsAttached=false。
 *   3. 玩家松手时，若盖子当前世界位置距瓶口 Socket < ReattachSnapDistance，
 *      自动吸附回 Socket；否则盖子以物理体形式掉在地上（bSimulateOnDrop=true）。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ABottleCapActor : public AActor
{
    GENERATED_BODY()

public:
    ABottleCapActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 盖子外壳 Mesh，Root */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cap|Components")
    TObjectPtr<UStaticMeshComponent> CapMesh;

    /** 抓取组件（EGrabType::Custom，扭转达到阈值后由本类主动接管） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cap|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    //=====================================================================
    // 配置
    //=====================================================================

    /**
     * "拧下来"所需的累计扭转角度（度）。
     * 玩家抓住盖子后，手柄相对瓶口 Yaw（沿瓶口局部 +Z）方向累计旋转达此值即拔出。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|Detach", meta = (ClampMin = "0.0"))
    float DetachTwistAngle;

    /**
     * 松手后自动吸附回瓶口的距离阈值(cm)。
     * 盖子世界位置与瓶口 Socket 世界位置距离小于此值时，自动 Snap 回瓶口。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|Reattach", meta = (ClampMin = "0.0"))
    float ReattachSnapDistance;

    /** 拧下瓶盖时给持盖手的触觉反馈(可选) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|Feedback")
    TObjectPtr<UHapticFeedbackEffect_Base> DetachHaptic;

    /** 拧下瓶盖时的音效(可选，PopCap Sound) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|Feedback")
    TObjectPtr<USoundBase> DetachSound;

    /** 盖回瓶口时的音效(可选) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|Feedback")
    TObjectPtr<USoundBase> ReattachSound;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 当前是否盖在瓶口上（false 表示已被拧下、正在被手拿着或掉落） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Cap|Runtime")
    bool bIsAttached;

    /** 反向引用：所属瓶子（由 ABottleActor::BeginPlay 在 Spawn 时注入） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Cap|Runtime")
    TObjectPtr<ABottleActor> OwnerBottle;

    /** 瓶口 Socket 名（由 ABottleActor 注入，与瓶子的 CapSocketName 保持一致） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Cap|Runtime")
    FName CapSocketName;

    //=====================================================================
    // API
    //=====================================================================

    /**
     * 由 ABottleActor 在 Spawn 后调用：绑定归属瓶子并把自己 Attach 到瓶口 Socket。
     */
    UFUNCTION(BlueprintCallable, Category = "Cap")
    void AttachToBottle(ABottleActor* InBottle, FName InSocketName);

    /**
     * 强制盖回瓶口（内部由松手回调调用，也可供蓝图脚本手动调用）。
     */
    UFUNCTION(BlueprintCallable, Category = "Cap")
    void ReattachToBottle();

    /**
     * "拧下来"：将盖子 Attach 到 MotionController，关物理，标记 bIsAttached=false，
     * 通知瓶子解锁倒液。
     */
    UFUNCTION(BlueprintCallable, Category = "Cap")
    void DetachFromBottle(UMotionControllerComponent* MotionController);

protected:
    /** GrabComp 广播的抓取回调（订阅在 BeginPlay 里挂上） */
    UFUNCTION()
    void HandleGrabbed();

    /** GrabComp 广播的释放回调 */
    UFUNCTION()
    void HandleDropped();

    /**
     * 计算"当前手柄绕瓶口 +Z 的角度"（度）——将手柄前向投影到瓶口 XY 平面后取相对 X 轴角度。
     * 只有 OwnerBottle 有效时才有意义。
     */
    float GetHandTwistAngleDegrees(UMotionControllerComponent* MC) const;

private:
    /** 抓取瞬间"手柄绕瓶口 +Z 的角度"（度），用于累计扭转 */
    UPROPERTY(Transient)
    float TwistBaselineDegrees;

    /** 是否处于"已抓住但尚未拧下来"的中间状态（Custom Grab 生效但仍附在瓶口 Socket 上） */
    UPROPERTY(Transient)
    bool bGrabbedButNotDetached;
};
