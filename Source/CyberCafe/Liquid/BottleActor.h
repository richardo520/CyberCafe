// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Liquid/LiquidContainerActor.h"
#include "BottleActor.generated.h"

class UHapticFeedbackEffect_Base;

/**
 * ABottleActor
 * 酒瓶：可倒液容器的具体实现之一。
 *
 * 倒液的核心逻辑（倾斜检测、SphereTrace、加/扣液、水花、P_Ribbon 控制）全部在基类
 * ALiquidContainerActor 中实现，本子类只做两件事：
 *   1. 构造时给"瓶子风格"的默认值（大容量、快出液速率等）；
 *   2. 提供"倒酒时给持瓶手的触觉反馈"这一酒瓶专属功能。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ABottleActor : public ALiquidContainerActor
{
    GENERATED_BODY()

public:
    ABottleActor();

    //=====================================================================
    // 编辑器可调（瓶子专属）
    //=====================================================================

    /** 倒酒时给持瓶手柄的触觉反馈(可选) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle|Haptics")
    TObjectPtr<UHapticFeedbackEffect_Base> PourHaptic;

protected:
    /** 覆写：在基类流程之上，追加触觉反馈的播放 */
    virtual void StartPouring() override;
};
