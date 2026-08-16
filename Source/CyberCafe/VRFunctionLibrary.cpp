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

UGrabComponent* UVRFunctionLibrary::FindTopPrioGrabComponent(AActor* GrabActor)
{
    if (GrabActor == nullptr) return nullptr;
    
    TArray<UGrabComponent*> Grabs;
    GrabActor->GetComponents<UGrabComponent>(Grabs);
    
    if (Grabs.IsEmpty())
    {
        return nullptr;
    }
    
    UGrabComponent* TopPrioGrabComponent = Grabs[0];
    int32 TopPrio = TopPrioGrabComponent->GrabPriority;

    for (UGrabComponent* Grab : Grabs)
    {
        if (Grab == nullptr)
        {
            continue;
        }

        // 跳过 None 类型，视为不可抓取
        int32 CurPrio = Grab->GrabPriority;

        // 若发现更高优先级，更新最优候选
        if (CurPrio > TopPrio)
        {
            TopPrio = CurPrio;
            TopPrioGrabComponent = Grab;
        }
    }
    
    return TopPrioGrabComponent;
}

bool UVRFunctionLibrary::CanBePotentialTarget(AActor* PotentialGrabActor)
{
    TArray<UGrabComponent*> GrabComponents;
    PotentialGrabActor->GetComponents(GrabComponents);
    
    for (UGrabComponent* GrabComponent : GrabComponents)
    {
        if (!GrabComponent->IsHeld() && !GrabComponent->IsPulled())
        {
            return true;
        }
    }
    return false;
}
