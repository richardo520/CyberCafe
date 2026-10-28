// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialStep.h"
#include "TutorialStep_ConsumeServedDrink.generated.h"

/**
 * UTutorialStep_ConsumeServedDrink
 *
 * 教学最后的"客人喝掉饮品"演出 Step（纯表现层）：
 *   1) 进入时拿 Director->LastServedCup
 *   2) 等 PreDrinkDelay 秒（代表 NPC 走过来端起、品尝的间隔）
 *   3) 让杯子 SetActorHiddenInGame(true) + 关闭碰撞（可选 Destroy）
 *   4) 等 AfterDrinkDelay 秒（给文字评价留阅读时间）
 *   5) MarkCompleted
 *
 * 文字部分由 Director 用本 Step 的 HintText / CompletedText 驱动，
 * 本 Step 不直接碰 UI。
 */
UCLASS(DisplayName = "Step: Consume Served Drink")
class CYBERCAFE_API UTutorialStep_ConsumeServedDrink : public UTutorialStep
{
    GENERATED_BODY()

public:
    /** "NPC 端起到喝下"之间的间隔（秒）*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ConsumeDrink",
        meta = (ClampMin = "0.0"))
    float PreDrinkDelay = 1.0f;

    /** "喝完杯子消失"到 Step 完成之间的间隔（秒），给评价文字留阅读时间 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ConsumeDrink",
        meta = (ClampMin = "0.0"))
    float AfterDrinkDelay = 1.5f;

    /**
     * 杯子消失方式：
     *   - false：仅 SetActorHiddenInGame(true) + 关碰撞（可被外部复位）
     *   - true ：Destroy() 彻底销毁
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|ConsumeDrink")
    bool bDestroyCupInsteadOfHide = false;

    //~ Begin UTutorialStep
    virtual void EnterStep(ATutorialDirector* InDirector) override;
    virtual void ExitStep() override;
    //~ End UTutorialStep

protected:
    /** PreDrinkDelay 到期：让杯子消失 */
    void HandleDrinkConsumed();

    /** AfterDrinkDelay 到期：完成 Step */
    void HandleStepFinish();

private:
    FTimerHandle PreDrinkTimer;
    FTimerHandle AfterDrinkTimer;
};
