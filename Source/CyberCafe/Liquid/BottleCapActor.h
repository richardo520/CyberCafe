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
 * 交互流程（拔出/盖回都做成"低操作难度"）：
 *   1. 初始时 Attach 到 ABottleActor::ContainerMesh 的 CapSocket 上，物理关闭；
 *      GrabComp 使用 EGrabType::Custom（抓取瞬间不 Attach 到手，仍保持在瓶口）。
 *   2. 抓住后，只要手柄相对瓶口 Socket 的距离 > DetachPullDistance，
 *      就把盖子真正 Attach 到手上（"拔下来"），并置 bIsAttached=false。
 *      —— 玩家只需"往外拉"即可拔盖，无需扭转。
 *   3. 玩家松手时，若盖子当前位置距瓶口 Socket <= ReattachSnapDistance，
 *      自动吸附回 Socket；否则盖子以物理体形式掉落。
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
     * "拔下来"所需的位移距离(cm)：
     * 抓住盖子后，若手柄距离瓶口 Socket 超过该值，盖子自动从瓶口脱离并落到手上。
     * 太小会不小心拔掉，太大玩家拉不动。默认 3cm。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|Detach", meta = (ClampMin = "0.0"))
    float DetachPullDistance;

    /**
     * 松手时自动吸回瓶口的距离阈值(cm)。
     * 玩家松手瞬间，若盖子世界位置距瓶口 Socket <= 此值，自动 Snap 回瓶口；
     * 否则盖子作为独立物体掉落。
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
    // "砸桌开盖"（SlamOpen）：握着瓶子带力撞击时，瓶盖向上飞出
    //=====================================================================

    /**
     * 触发砸桌开盖的法向冲量阈值 (kg·cm/s，即 |NormalImpulse|)。
     * 参考：1kg 物体 4m/s 撞墙 ≈ 冲量 400～800。
     * 设小 → 轻碰就能开盖（容易误触发）；设大 → 需要重砸。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|SlamOpen", meta = (ClampMin = "0.0"))
    float SlamOpenImpulseThreshold = 800.f;

    /**
     * 瓶盖飞出的初速度大小 (cm/s)。
     * 建议 300 ~ 800（现实可乐瓶盖弹开大约 3~5m/s）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|SlamOpen", meta = (ClampMin = "0.0"))
    float SlamOpenLaunchSpeed = 400.f;

    /**
     * 瓶盖飞出后的自旋角速度 (deg/s)，增强观感。 
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|SlamOpen", meta = (ClampMin = "0.0"))
    float SlamOpenSpinSpeedDegs = 720.f;

    /**
     * 飞出方向上"随机圆锥拖杆"的半角度 (deg)：
     * 0   = 飞出方向完全确定（主方向）
     * 15° = 在主方向周围 ±15° 随机（推荐）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|SlamOpen", meta = (ClampMin = "0.0", ClampMax = "45.0"))
    float SlamOpenScatterDegs = 15.f;

    /**
     * 撞击反冲方向在飞出速度中的权重（0~1）：
     * 0    = 完全沿瓶口向上（垂直喷发感）
     * 0.3  = 以向上为主，叠加少量反冲（推荐）
     * 1    = 完全沿撞击反方向（物理感最强但不像拔盖）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|SlamOpen", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SlamOpenReboundWeight = 0.3f;

    /** 砸桌开盖时的音效（可选；为空时回退到 DetachSound） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cap|SlamOpen")
    TObjectPtr<USoundBase> SlamOpenSound;

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
     * "拔下来"：将盖子 Attach 到 MotionController，关物理，标记 bIsAttached=false，
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
     * 瓶身碰撞回调（订阅 OwnerBottle->ContainerMesh->OnComponentHit）。
     * 当玩家握着瓶子撞击到足够强的环境时，自动弹飞盖子。
     */
    UFUNCTION()
    void HandleBottleHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
                         UPrimitiveComponent* OtherComp, FVector NormalImpulse,
                         const FHitResult& Hit);

    /**
     * “砸桌开盖”实际执行：盖子从瓶口脱离，开启物理，施加向上+反冲+随机方向的速度将其弹出。
     * @param HitNormal  撞击面的法向（世界空间），用于计算反冲方向
     */
    void SlamOpenCap(const FVector& HitNormal);

private:
    /** 是否处于"已抓住但尚未拔下来"的中间状态（Custom Grab 生效但仍附在瓶口 Socket 上） */
    UPROPERTY(Transient)
    bool bGrabbedButNotDetached;
};
