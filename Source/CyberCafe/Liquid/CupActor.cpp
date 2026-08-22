// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/CupActor.h"

ACupActor::ACupActor()
{
    // 开启 Tick 以驱动基类的动态液面波动
    PrimaryActorTick.bCanEverTick = true;

    // 酒杯默认容量：200mL
    MaxVolumeML = 200.f;
    FillAmount  = 0.f;

    // 抓取行为默认继承基类(Snap)，用户可在蓝图子类里改
}

void ACupActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 根据杯身运动状态实时调节液面波动
    UpdateDynamicWaves(DeltaTime);
}
