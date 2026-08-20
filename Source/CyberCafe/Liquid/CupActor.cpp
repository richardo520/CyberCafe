// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/CupActor.h"

ACupActor::ACupActor()
{
    // 酒杯默认容量：200mL
    MaxVolumeML = 200.f;
    FillAmount  = 0.f;

    // 抓取行为默认继承基类(Snap)，用户可在蓝图子类里改
}
