// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Liquid/LiquidContainerActor.h"
#include "CupActor.generated.h"

/**
 * ACupActor
 * 酒杯：可倒液容器的具体实现之一。
 *
 * 与酒瓶共用基类的倒液流程（倾斜出液、SphereTrace、加/扣液、水花）。
 * 本子类只做一件事：构造时给"杯子风格"的默认值——小容量、慢出液、更大倾角阈值，且默认接受被倒入。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ACupActor : public ALiquidContainerActor
{
    GENERATED_BODY()

public:
    ACupActor();
};
