// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeanJarLidActor.generated.h"

class UStaticMeshComponent;
class UGrabComponent;
class UMotionControllerComponent;
class UHapticFeedbackEffect_Base;
class USoundBase;
class ABeanJarActor;

/**
 * ABeanJarLidActor
 * 咖啡豆罐的盖子：作为独立 Actor 出现，可被 VR 手柄单独抓取。
 *
 * 交互模式几乎完全复用 ABottleCapActor（Custom Grab + 位移拔出 + 松手吸回 / 独立掉落），
 * 但省略了"砸桌开盖"（咖啡罐不需要）。玩家把盖子随便丢在桌上、地上都可以，
 * 罐子会通过 IsOpen() 询问盖子当前是否已离开罐口来决定是否允许舀豆。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ABeanJarLidActor : public AActor
{
    GENERATED_BODY()

public:
    ABeanJarLidActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 盖子 Mesh，Root */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lid|Components")
    TObjectPtr<UStaticMeshComponent> LidMesh;

    /** 抓取组件（Custom：位移达到阈值后由本类主动 Attach 到手） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lid|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    //=====================================================================
    // 配置
    //=====================================================================

    /** "拔下来"的位移阈值 (cm)：抓着盖子后，手离罐口 Socket 距离超过本值即拔出 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lid|Detach", meta = (ClampMin = "0.0"))
    float DetachPullDistance;

    /** 松手吸回罐口的距离阈值 (cm)：<=本值则自动 Snap 回罐口，否则以物理体掉落 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lid|Reattach", meta = (ClampMin = "0.0"))
    float ReattachSnapDistance;

    /** 拔盖触觉反馈（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lid|Feedback")
    TObjectPtr<UHapticFeedbackEffect_Base> DetachHaptic;

    /** 拔盖音效（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lid|Feedback")
    TObjectPtr<USoundBase> DetachSound;

    /** 盖回音效（可选） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lid|Feedback")
    TObjectPtr<USoundBase> ReattachSound;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 当前是否盖在罐口（false = 已拔下 / 手拿 / 掉落，即"罐已开") */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Lid|Runtime")
    bool bIsAttached;

    /** 归属的咖啡罐（由 ABeanJarActor::BeginPlay 注入） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Lid|Runtime")
    TObjectPtr<ABeanJarActor> OwnerJar;

    /** 罐口 Socket 名（与罐子的 LidSocketName 保持一致） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Lid|Runtime")
    FName LidSocketName;

    //=====================================================================
    // API
    //=====================================================================

    /** 供 ABeanJarActor 在 Spawn 后调用：绑定归属罐子并 Attach 到 Socket */
    UFUNCTION(BlueprintCallable, Category = "Lid")
    void AttachToJar(ABeanJarActor* InJar, FName InSocketName);

    /** 强制盖回罐口（内部由松手回调调用，也可蓝图脚本调用） */
    UFUNCTION(BlueprintCallable, Category = "Lid")
    void ReattachToJar();

    /** 拔下来：Attach 到 MotionController，通知罐子解锁舀豆 */
    UFUNCTION(BlueprintCallable, Category = "Lid")
    void DetachFromJar(UMotionControllerComponent* MotionController);

    /** 便捷查询：罐是否处于"已开"状态（盖已离开罐口） */
    UFUNCTION(BlueprintPure, Category = "Lid")
    bool IsOpen() const { return !bIsAttached; }

protected:
    UFUNCTION()
    void HandleGrabbed();

    UFUNCTION()
    void HandleDropped();

private:
    /** "抓住但还没拔下"的中间状态（Custom Grab 生效但仍附在罐口 Socket 上） */
    UPROPERTY(Transient)
    bool bGrabbedButNotDetached;
};
