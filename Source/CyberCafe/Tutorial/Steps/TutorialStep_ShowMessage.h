// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialStep.h"
#include "TutorialStep_ShowMessage.generated.h"

/**
 * UTutorialStep_ShowMessage
 *
 * 纯叙事步骤：显示一段提示文本，等 Duration 秒后自动完成；
 * 可选允许玩家按任意键提前跳过。
 *
 * 典型用法（教学开场的场景介绍）：
 *   - 一条文本 = 一个 Step（例如"欢迎 / 右边是牛奶 / 左边是杯子 / 可以远程抓取"）
 *   - 配置 HintText + Duration，按顺序放到 Director.Steps
 *
 * 完成条件（二选一先满足即可）：
 *   - 经过 Duration 秒
 *   - 玩家按下任意键（当 bAllowSkipByInput = true 时）
 */
UCLASS(DisplayName = "Step: Show Message")
class CYBERCAFE_API UTutorialStep_ShowMessage : public UTutorialStep
{
    GENERATED_BODY()

public:
    /** 文本持续显示的秒数，到时自动完成。<=0 则必须靠按键跳过 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage",
        meta = (ClampMin = "0.0"))
    float Duration = 3.0f;

    /** 允许玩家按任意手柄键 / 键盘键提前跳过 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage")
    bool bAllowSkipByInput = true;

    //~ Begin UTutorialStep
    virtual void EnterStep(ATutorialDirector* InDirector) override;
    virtual void ExitStep() override;
    //~ End UTutorialStep

protected:
    /** 定时器到期回调（Duration 用完）*/
    void HandleDurationElapsed();

    /** 玩家按下任意键回调（AnyKey 事件）*/
    UFUNCTION()
    void HandleAnyKeyPressed(FKey Key);

private:
    /** 定时器句柄：Duration 到时触发 HandleDurationElapsed */
    FTimerHandle DurationTimer;
};
