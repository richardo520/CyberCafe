// Fill out your copyright notice in the Description page of Project Settings.

#include "VRFunctionLibrary.h"
#include "GrabComponent.h"

namespace
{
    /**
     * 将EGrabType映射为可比较的优先级数值。
     * 官方VR模板中Snap优先级最高，Custom最低，None视为无效。
     */
    static int32 GetGrabTypePriority(EGrabType Type)
    {
        switch (Type)
        {
        case EGrabType::Snap:        return 4;
        case EGrabType::SnapInPlace: return 3;
        case EGrabType::Free:        return 2;
        case EGrabType::Custom:      return 1;
        case EGrabType::None:
        default:                     return 0;
        }
    }
}

void UVRFunctionLibrary::FindTopPrioGrabComponent(
    const TArray<UGrabComponent*>& TargetArray,
    UGrabComponent*& OutGrabComponent,
    bool& bCanBePotentialTarget)
{
    OutGrabComponent = nullptr;
    bCanBePotentialTarget = false;

    // 使用局部变量记录当前最高优先级，对应蓝图里的 TopPrio(Integer)
    int32 TopPrio = 0;

    for (UGrabComponent* Grab : TargetArray)
    {
        if (Grab == nullptr)
        {
            continue;
        }

        // 跳过 None 类型，视为不可抓取
        const int32 CurPrio = GetGrabTypePriority(Grab->GrabType);
        if (CurPrio <= 0)
        {
            continue;
        }

        // 若发现更高优先级，更新最优候选
        if (CurPrio > TopPrio)
        {
            TopPrio = CurPrio;
            OutGrabComponent = Grab;
            bCanBePotentialTarget = true;
        }
    }
}
