// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialStep.h"
#include "InputCoreTypes.h"
#include "TutorialStep_Grab.generated.h"

class AActor;

/**
 * UTutorialStep_Grab
 *
 * 第一个教学步骤：等待玩家抓取指定物体（或任意物体）。
 *
 * 完成条件：订阅 UCyberCafeEventSubsystem::OnAnyObjectGrabbed，
 *   - 若 RequiredActor 已设置：仅当被抓物体 == RequiredActor 时完成
 *   - 若 RequiredActor 为空：任意物体被抓取即完成
 */
UCLASS(DisplayName = "Step: Grab Object")
class CYBERCAFE_API UTutorialStep_Grab : public UTutorialStep
{
    GENERATED_BODY()

public:
    /**
     * 必须被抓取的 Actor（例如教学关里的那个空咖啡杯）。
     * 留空 → 任意物体被抓都算完成（用于最初的"随便感受下抓取"引导）。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|Grab")
    TObjectPtr<AActor> RequiredActor;

    //~ Begin UTutorialStep
    virtual void EnterStep(ATutorialDirector* InDirector) override;
    virtual void ExitStep() override;
    //~ End UTutorialStep

protected:
    /** 全局事件回调：有物体被抓取时触发 */
    UFUNCTION()
    void HandleAnyObjectGrabbed(AActor* Object, EControllerHand Hand);
};
