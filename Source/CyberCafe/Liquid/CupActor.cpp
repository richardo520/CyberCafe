// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/CupActor.h"

ACupActor::ACupActor()
{
    // 杯子风格的默认值（覆盖基类默认）
    // - 容量 200mL
    // - 初始空杯
    // - 倒液角度 80°（杯子较矮，得倒得更狠才出液）
    // - 出液速率 30mL/s（比瓶子慢一半，杯子水少倒慢一点更真实）
    // - 水流强度 0.5（比瓶子细）
    // - 允许作为接液方（杯子 → 杯子，或瓶子 → 杯子）
    MaxVolumeML             = 200.f;
    FillAmount              = 0.f;

    PourAngleThreshold      = 80.f;
    PourRatePerSecond       = 30.f;
    FlowStrength            = 0.5f;
    PourTraceDistance       = 40.f;   // 杯子较矮，检测距离短一些
    PourTraceRadius         = 4.f;
    PourTraceForwardOffset  = 0.f;

    bCanPour                = true;
    bAcceptLiquidFromOthers = true;   // 杯子默认可接液

    // 抓取行为默认继承基类(Snap)，用户可在蓝图子类里改
}
