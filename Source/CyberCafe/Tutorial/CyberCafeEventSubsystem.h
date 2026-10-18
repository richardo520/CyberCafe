// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "InputCoreTypes.h"
#include "CyberCafeEventSubsystem.generated.h"

class AActor;

/**
 * 任意物体被抓取（近距抓取）成功时广播。
 * @param Object 被抓取物体所属的 Actor
 * @param Hand   发起抓取的手（Left / Right / AnyHand）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAnyObjectGrabbedSignature,
    AActor*, Object, EControllerHand, Hand);

/**
 * 任意物体被远程拉取（Pull）成功进入拉拽状态时广播。
 * 对应"远处抓取"教学的检测事件。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAnyObjectPulledSignature,
    AActor*, Object, EControllerHand, Hand);

/**
 * 任意倒液事件：源容器把一定量液体注入到目标容器成功（AcceptedML > 0）时广播。
 * @param FromActor   源容器 Actor（通常是 ALiquidContainerActor 子类）；可能为 nullptr
 *                    （例如通过蓝图直接调用 AddLiquid 加液时，没有明确来源）
 * @param ToActor     目标容器 Actor
 * @param AcceptedML  本次实际接收的毫升数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnLiquidPouredSignature,
    AActor*, FromActor, AActor*, ToActor, float, AcceptedML);

/**
 * UCyberCafeEventSubsystem
 *
 * CyberCafe 全局事件总线（GameInstanceSubsystem，关卡切换也不销毁）。
 * 用来把分散在 GrabComponent / LiquidContainerActor 等交互模块里的事件
 * 统一聚合成少量"全局事件"，方便教学 Director（TutorialDirector）、成就系统、
 * 分析统计等上层模块只订阅一个地方即可感知所有关键交互。
 *
 * 使用方式：
 *   // 任意带 UWorld 的位置：
 *   if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
 *   {
 *       Sys->OnAnyObjectGrabbed.AddDynamic(this, &UMyClass::HandleGrabbed);
 *   }
 *
 * 规则：
 *   - 本子系统本身不做任何业务判断，纯粹是广播中转
 *   - 事件的触发方（GrabComponent / LiquidContainerActor）负责在合适时机 Broadcast
 */
UCLASS()
class CYBERCAFE_API UCyberCafeEventSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** 便捷获取：传入任意带 World 的 UObject，返回当前 GameInstance 上的本子系统实例 */
    UFUNCTION(BlueprintPure, Category = "CyberCafe|Events",
        meta = (WorldContext = "WorldContextObject", DisplayName = "Get CyberCafeEventSubsystem"))
    static UCyberCafeEventSubsystem* Get(const UObject* WorldContextObject);

    /** 任意物体被抓取 */
    UPROPERTY(BlueprintAssignable, Category = "CyberCafe|Events")
    FOnAnyObjectGrabbedSignature OnAnyObjectGrabbed;

    /** 任意物体被远程拉取 */
    UPROPERTY(BlueprintAssignable, Category = "CyberCafe|Events")
    FOnAnyObjectPulledSignature OnAnyObjectPulled;

    /** 任意倒液事件 */
    UPROPERTY(BlueprintAssignable, Category = "CyberCafe|Events")
    FOnLiquidPouredSignature OnLiquidPoured;
};
