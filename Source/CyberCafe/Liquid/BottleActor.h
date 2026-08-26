// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Liquid/LiquidContainerActor.h"
#include "BottleActor.generated.h"

class UHapticFeedbackEffect_Base;
class ABottleCapActor;

/**
 * ABottleActor
 * 酒瓶：可倒液容器的具体实现之一。
 *
 * 倒液的核心逻辑（倾斜检测、SphereTrace、加/扣液、水花、P_Ribbon 控制）全部在基类
 * ALiquidContainerActor 中实现，本子类只做两件事：
 *   1. 构造时给"瓶子风格"的默认值（大容量、快出液速率等）；
 *   2. 提供"倒酒时给持瓶手的触觉反馈"这一酒瓶专属功能。
 *
 * 额外支持"可拆卸瓶盖"：
 *   - 通过 CapClass 指定盖子的 Actor 蓝图/类；
 *   - BeginPlay 会 Spawn 一个盖子并 Attach 到 ContainerMesh 的 CapSocketName Socket；
 *   - 盖着状态下 bCanPour=false，无法倒液；玩家扭下盖子后 bCanPour 恢复。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ABottleActor : public ALiquidContainerActor
{
    GENERATED_BODY()

public:
    ABottleActor();

    virtual void BeginPlay() override;

    //=====================================================================
    // 编辑器可调（瓶子专属）
    //=====================================================================

    /** 倒酒时给持瓶手柄的触觉反馈(可选) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Haptics")
    TObjectPtr<UHapticFeedbackEffect_Base> PourHaptic;

    //=====================================================================
    // 瓶盖（可选）
    //=====================================================================

    /**
     * 瓶盖 Actor 类。为空时该瓶子无盖，倒液不受限制。
     * 可以是 C++ 的 ABottleCapActor 或其蓝图子类。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap")
    TSubclassOf<ABottleCapActor> CapClass;

    /**
     * 瓶口 Socket 名（在 ContainerMesh 的 StaticMesh 里定义，例如 "CapSocket"）。
     * 盖子会 Attach 到该 Socket；扭转/距离计算也基于该 Socket 的 Transform。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap")
    FName CapSocketName;

    /** 瓶子初始是否处于"盖着"状态。默认 true（未开封）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Cap")
    bool bStartCapped;

    /** 运行时生成的瓶盖实例引用 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Bottle|Cap")
    TObjectPtr<ABottleCapActor> CapActor;

    /** 当前是否盖着盖子（与 bCanPour 联动） */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Bottle|Cap")
    bool bIsCapped;

    //=====================================================================
    // 瓶盖回调（由 ABottleCapActor 调用）
    //=====================================================================

    /** 盖子盖回瓶口 → 禁止倒液 */
    UFUNCTION(BlueprintCallable, Category = "Bottle|Cap")
    void OnCapAttached();

    /** 盖子被拧下 → 允许倒液 */
    UFUNCTION(BlueprintCallable, Category = "Bottle|Cap")
    void OnCapDetached();

protected:
    /** 覆写：在基类流程之上，追加触觉反馈的播放 */
    virtual void StartPouring() override;
};
