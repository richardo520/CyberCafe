// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialStep.h"
#include "TutorialStep_ShowMessage.generated.h"

/**
 * UTutorialStep_ShowMessage
 *
 * 纯叙事步骤：显示一段提示文本，等 Duration 秒后自动完成；
 * 可选允许玩家按"跳过对话"信号提前跳过。
 *
 * 典型用法（教学开场的场景介绍）：
 *   - 一条文本 = 一个 Step（例如"欢迎 / 右边是牛奶 / 左边是杯子 / 可以远程抓取"）
 *   - 配置 HintText + Duration，按顺序放到 Director.Steps
 *
 * 完成条件（二选一先满足即可）：
 *   - 经过 Duration 秒
 *   - 收到 UCyberCafeEventSubsystem::OnPlayerSkipRequested（当 bAllowSkipByInput = true 时）
 *     该事件由 VRPawn 在玩家扣扳机时广播，详见 AVRPawn::OnSkipDialogPressed
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

    /** 允许玩家通过"跳过对话"事件（扣扳机）提前跳过 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage")
    bool bAllowSkipByInput = true;

    /**
     * Step 刚进入时的"忽略跳过"宽限期（秒）。
     * 防止上一个 Step 还没松扳机的输入"穿透"到当前 Step 导致秒跳过。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ShowMessage",
        meta = (ClampMin = "0.0"))
    float InputGracePeriod = 0.3f;

    //~ Begin UTutorialStep
    virtual void EnterStep(ATutorialDirector* InDirector) override;
    virtual void ExitStep() override;
    //~ End UTutorialStep

protected:
    /** 定时器到期回调（Duration 用完）*/
    void HandleDurationElapsed();

    /** 事件总线回调：收到玩家跳过请求 */
    UFUNCTION()
    void HandlePlayerSkipRequested();

private:
    /** 定时器句柄：Duration 到时触发 HandleDurationElapsed */
    FTimerHandle DurationTimer;

    /** Step 进入时的 World 时间，用于 InputGracePeriod 判定 */
    float EnterGameTime = 0.f;
};
