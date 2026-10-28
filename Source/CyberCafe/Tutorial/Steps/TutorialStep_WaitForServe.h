// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialStep.h"
#include "TutorialStep_WaitForServe.generated.h"

class AServingSpotActor;
class ALiquidContainerActor;

/**
 * UTutorialStep_WaitForServe
 *
 * 等待玩家把一杯饮品"上菜"到任意（或指定）ServingSpot。
 * 完成条件：收到 UCyberCafeEventSubsystem::OnDrinkServed 广播。
 *
 * 若配置了 RequiredSpot，则只对该 Spot 产生的事件响应；为空则接受任何 Spot。
 *
 * Step 完成时会把被服务的 Cup 缓存到 Director（LastServedCup），
 * 供下一个 Step（例如 TutorialStep_ConsumeServedDrink）使用。
 */
UCLASS(DisplayName = "Step: Wait For Serve")
class CYBERCAFE_API UTutorialStep_WaitForServe : public UTutorialStep
{
    GENERATED_BODY()

public:
    /** 可选：仅对该上菜点的事件响应；为空接受所有上菜点 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step|WaitForServe")
    TObjectPtr<AServingSpotActor> RequiredSpot;

    //~ Begin UTutorialStep
    virtual void EnterStep(ATutorialDirector* InDirector) override;
    virtual void ExitStep() override;
    //~ End UTutorialStep

protected:
    UFUNCTION()
    void HandleDrinkServed(AServingSpotActor* ServingSpot, ALiquidContainerActor* Cup);
};
