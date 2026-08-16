// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VRFunctionLibrary.generated.h"

class UGrabComponent;

/**
 * UVRFunctionLibrary
 * VR相关的通用蓝图静态函数库，对应工程内蓝图 /Game/VRTemplate/Blueprints/VRFunctionLibrary。
 *
 * 目前提供：
 *   - FindTopPrioGrabComponent：从一组GrabComponent中，按GrabType优先级找出最合适的抓取候选。
 */
UCLASS()
class CYBERCAFE_API UVRFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * 从一组GrabComponent中选出优先级最高的一个作为最终抓取目标。
     *
     * 优先级规则（数字越大越优先，与官方VR模板保持一致）：
     *   Snap(4) > SnapInPlace(3) > Free(2) > Custom(1) > None(0，视为无效)
     *
     * @param TargetArray           候选GrabComponent数组
     * @param OutGrabComponent      输出：选中的GrabComponent；无有效候选时为nullptr
     * @param bCanBePotentialTarget 输出：是否存在可作为抓取目标的候选
     */
    UFUNCTION(BlueprintCallable, Category = "VR|Grab",meta = (DisplayName = "Find Top Prio Grab Component"))
    static UGrabComponent* FindTopPrioGrabComponent(AActor* GrabActor);
    
    UFUNCTION(BlueprintCallable, Category = "VR|Grab",Meta = (DisplayName = "Can Be Potential Target "))
    static bool CanBePotentialTarget(AActor* PotentialGrabActor);
};
