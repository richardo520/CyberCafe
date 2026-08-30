// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/CupActor.h"
#include "UI/LiquidVolumeWidget.h"
#include "Components/WidgetComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Camera/PlayerCameraManager.h"

ACupActor::ACupActor()
{
    // 需要 Tick 来让 Widget 面向玩家相机
    PrimaryActorTick.bCanEverTick = true;

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

    // ------------------------------------------------------------------
    // 容量 UI 组件：3D Widget（World Space），挂在 Root 上方
    // ------------------------------------------------------------------
    VolumeWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("VolumeWidgetComp"));
    VolumeWidgetComp->SetupAttachment(GetRootComponent());
    VolumeWidgetComp->SetRelativeLocation(VolumeWidgetOffset);
    VolumeWidgetComp->SetWidgetSpace(EWidgetSpace::World);
    VolumeWidgetComp->SetDrawSize(VolumeWidgetDrawSize);
    VolumeWidgetComp->SetPivot(FVector2D(0.5f, 0.5f));
    VolumeWidgetComp->SetTwoSided(true);          // 从背面也能看见
    VolumeWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VolumeWidgetComp->SetGenerateOverlapEvents(false);
    VolumeWidgetComp->SetVisibility(false);       // 默认隐藏，等有液体再显示
}

void ACupActor::BeginPlay()
{
    Super::BeginPlay();

    // 1) 绑定基类的液体变化事件（AddLiquid / ConsumeLiquid / SetLiquidMaterialAsset 都会广播）
    OnLiquidChanged.AddDynamic(this, &ACupActor::HandleLiquidChanged);

    // 2) 应用蓝图配置的 Widget 类 & 尺寸 & 偏移（这些字段是编辑器可改的，构造函数里读到的是默认值）
    if (VolumeWidgetComp)
    {
        VolumeWidgetComp->SetRelativeLocation(VolumeWidgetOffset);
        VolumeWidgetComp->SetDrawSize(VolumeWidgetDrawSize);

        if (VolumeWidgetClass)
        {
            VolumeWidgetComp->SetWidgetClass(VolumeWidgetClass);
        }
    }

    // 3) 初始同步一次：根据当前 FillAmount 决定显示/隐藏并推送数值
    PushVolumeToWidget();
    RefreshVolumeUIVisibility();
}

void ACupActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 只有当前可见才需要更新朝向（不可见没意义、也省一次相机查询）
    if (bFaceCamera && VolumeWidgetComp && VolumeWidgetComp->IsVisible())
    {
        UpdateWidgetFacing();
    }
}

void ACupActor::HandleLiquidChanged(float /*NewFillAmount*/, FLinearColor /*NewColor*/)
{
    // 液体变化就刷新一次文本 + 可见性
    PushVolumeToWidget();
    RefreshVolumeUIVisibility();
}

void ACupActor::RefreshVolumeUIVisibility()
{
    if (!VolumeWidgetComp)
    {
        return;
    }

    // 只要杯里有液体就显示（用 KINDA_SMALL_NUMBER 兜住浮点误差）
    const bool bShouldShow = FillAmount > KINDA_SMALL_NUMBER;
    if (VolumeWidgetComp->IsVisible() != bShouldShow)
    {
        VolumeWidgetComp->SetVisibility(bShouldShow);
    }
}

void ACupActor::PushVolumeToWidget()
{
    if (!VolumeWidgetComp)
    {
        return;
    }

    // GetUserWidgetObject 拿到的是 WBP 实例，Cast 成我们自己的 ULiquidVolumeWidget
    if (ULiquidVolumeWidget* W = Cast<ULiquidVolumeWidget>(VolumeWidgetComp->GetUserWidgetObject()))
    {
        W->UpdateVolume(GetCurrentVolumeML(), MaxVolumeML);
    }
}

void ACupActor::UpdateWidgetFacing()
{
    // 取本地玩家相机
    APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!CamMgr)
    {
        return;
    }

    const FVector CamLoc    = CamMgr->GetCameraLocation();
    const FVector WidgetLoc = VolumeWidgetComp->GetComponentLocation();

    // Widget 的 "正面" 是 +X（面片法线）。想让它面向相机，就把 +X 指向相机。
    FRotator LookRot = UKismetMathLibrary::FindLookAtRotation(WidgetLoc, CamLoc);
    // 只保留 Yaw + Pitch，去掉 Roll，避免歪脖子；VR 中通常也保留 Pitch 会更自然
    LookRot.Roll = 0.f;

    VolumeWidgetComp->SetWorldRotation(LookRot);
}
