// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Liquid/LiquidContainerActor.h"
#include "CupActor.generated.h"

/**
 * ACupActor
 * 酒杯：接收酒瓶倒入的液体。逻辑完全走基类 AddLiquid()。
 * 首版不做溢出效果，`MaxVolumeML` 满了以后 AddLiquid 会直接返回 0。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ACupActor : public ALiquidContainerActor
{
    GENERATED_BODY()

public:
    ACupActor();
};
