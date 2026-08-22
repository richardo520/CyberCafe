// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/BottleActor.h"
#include "Liquid/CupActor.h"

#include "Components/StaticMeshComponent.h"
#include "MotionControllerComponent.h"
#include "GrabComponent.h"

#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#include "Haptics/HapticFeedbackEffect_Base.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

ABottleActor::ABottleActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 默认值
    PourAngleThreshold  = 60.f;
    PourRatePerSecond   = 60.f;    // 每秒 60mL
    FlowStrength        = 1.f;
    PourTraceDistance   = 60.f;    // 60cm，足够从桌面高度倒到杯子
    bDebugDrawTrace     = false;   // 默认关闭射线调试绘制
    SplashEffectTemplate= nullptr;
    PourHaptic          = nullptr;
    bEnableDecal        = true;
    bNoSplashes         = false;
    bNoList             = true;

    bIsPouring = false;

    // 预挂载的 P_Ribbon Niagara 组件（默认不自动激活）
    PourFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PourFX"));
    if (PourFX)
    {
        PourFX->SetupAttachment(GetRootComponent());
        PourFX->SetAutoActivate(false);
        PourFX->bAutoActivate = false;
    }

    // 酒瓶默认容量：750mL(一瓶红酒)
    MaxVolumeML = 750.f;
    FillAmount  = 1.0f;
}

void ABottleActor::BeginPlay()
{
    Super::BeginPlay();

    // 预写入 P_Ribbon 的 User.* 参数
    // 注意：PourFX 的 Niagara Asset(P_Ribbon) 与 Transform 均由美术在蓝图组件 Details 里配置，
    //       C++ 不再插手，只写运行时参数。
    if (PourFX)
    {
        // 安全检查：提醒美术在蓝图 PourFX 组件的 Niagara Asset 一栏指定 P_Ribbon
        if (!PourFX->GetAsset())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[%s] PourFX 未指定 Niagara Asset，请在蓝图选中 PourFX 组件，Details → Niagara → Asset 里指定 P_Ribbon。"),
                *GetName());
        }

        // 预写入静态参数
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),        LiquidColor);
        PourFX->SetNiagaraVariableFloat      (TEXT("User.FlowStrength"), FlowStrength);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoSplashes"),   bNoSplashes);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoList"),       bNoList);
        PourFX->SetNiagaraVariableBool       (TEXT("User.Decal"),        bEnableDecal);

        // User.Data = self (Actor)，同官方 BP_Pouring 的做法
        PourFX->SetNiagaraVariableObject(TEXT("User.Data"), this);

        // 默认关闭，要倒酒时才 Activate
        PourFX->Deactivate();
    }
}

void ABottleActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 根据瓶身运动状态实时调节液面波动（基类实现）
    UpdateDynamicWaves(DeltaTime);

    const bool bShouldPour =
        (FillAmount > KINDA_SMALL_NUMBER) &&
        (GetTiltAngleDegrees() >= PourAngleThreshold);

    if (bShouldPour && !bIsPouring)
    {
        StartPouring();
    }
    else if (!bShouldPour && bIsPouring)
    {
        StopPouring();
    }

    if (bIsPouring)
    {
        UpdatePouring(DeltaTime);
    }
}

//=====================================================================
// 瓶口变换 / 倾角
//=====================================================================

FTransform ABottleActor::GetPourWorldTransform() const
{
    // 直接使用 PourFX 的世界 Transform——美术在蓝图里拖动的位置就是瓶口。
    // 保证水流发射点与 LineTrace 起点一致，避免两者不同步。
    if (PourFX)
    {
        return PourFX->GetComponentTransform();
    }
    // fallback：PourFX 意外不存在时退回 Root
    if (ContainerMesh)
    {
        return ContainerMesh->GetComponentTransform();
    }
    return GetActorTransform();
}

float ABottleActor::GetTiltAngleDegrees() const
{
    if (!ContainerMesh)
    {
        return 0.f;
    }
    // 瓶身局部 +Z 在世界空间的方向
    const FVector BottleUpWS = ContainerMesh->GetUpVector();
    const float Dot = FVector::DotProduct(BottleUpWS.GetSafeNormal(), FVector::UpVector);
    const float AngleRad = FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f));
    return FMath::RadiansToDegrees(AngleRad);
}

//=====================================================================
// 倒酒开始 / 结束
//=====================================================================

void ABottleActor::StartPouring()
{
    bIsPouring = true;

    if (PourFX)
    {
        // 重新同步一次颜色/强度/开关，防止编辑器运行时改变后未生效
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"),        LiquidColor);
        PourFX->SetNiagaraVariableFloat      (TEXT("User.FlowStrength"), FlowStrength);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoSplashes"),   bNoSplashes);
        PourFX->SetNiagaraVariableBool       (TEXT("User.NoList"),       bNoList);
        PourFX->SetNiagaraVariableBool       (TEXT("User.Decal"),        bEnableDecal);
        PourFX->SetNiagaraVariableObject     (TEXT("User.Data"),         this);

        PourFX->Activate(/*bReset=*/true);
    }

    // 播放持瓶手柄的触觉反馈(可选)
    if (PourHaptic && GrabComp && GrabComp->IsHeld())
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            PC->PlayHapticEffect(PourHaptic, GrabComp->GetHeldByHand());
        }
    }
}

void ABottleActor::StopPouring()
{
    bIsPouring = false;

    if (PourFX)
    {
        // 不销毁，只关闭；下次 Activate 可直接重启
        PourFX->Deactivate();
    }
}

//=====================================================================
// 倒酒 Tick 逻辑
//=====================================================================

void ABottleActor::UpdatePouring(float DeltaTime)
{
    if (!ContainerMesh)
    {
        return;
    }

    const FTransform PourXform = GetPourWorldTransform();
    const FVector PourLocationWS = PourXform.GetLocation();

    // 更新 Niagara 的实时颜色(液体颜色会因混色变化)
    if (PourFX)
    {
        PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"), LiquidColor);
    }

    // 本帧应流出的液体量
    const float DesiredML = PourRatePerSecond * DeltaTime;
    const float ActualML  = ConsumeLiquid(DesiredML);
    if (ActualML <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    // 从瓶口沿世界 -Z 做 LineTrace，判断下方是否有酒杯
    const FVector TraceStart = PourLocationWS;
    const FVector TraceEnd   = PourLocationWS + FVector(0.f, 0.f, -PourTraceDistance);

    FHitResult Hit;
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BottlePourTrace), /*bTraceComplex=*/false, this);
    QueryParams.AddIgnoredActor(this);

    const bool bHit = GetWorld()->LineTraceSingleByChannel(
        Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

#if ENABLE_DRAW_DEBUG
    // 编辑器下方便调试，需在 BP 里把 bDebugDrawTrace 打开才会显示
    if (bDebugDrawTrace)
    {
        DrawDebugLine(GetWorld(), TraceStart, bHit ? Hit.ImpactPoint : TraceEnd,
                      bHit ? FColor::Green : FColor::Red, false, 0.f, 0, 0.2f);
    }
#endif

    if (bHit)
    {
        if (ACupActor* Cup = Cast<ACupActor>(Hit.GetActor()))
        {
            // 1) 把瓶子的液体材质"简单粗暴"地赋给杯子（含 MI 的颜色一起传递）
            //    ：注意——只在杯子当前材质与瓶子不同的时候才换，避免每帧调 ReinitializeSystem。
            if (LiquidMaterialAsset && Cup->LiquidMaterialAsset != LiquidMaterialAsset)
            {
                Cup->SetLiquidMaterialAsset(LiquidMaterialAsset, /*bReadColor=*/true);
            }

            // 2) 加液体到杯子（走基类混色逻辑，颜色权威由 LiquidColor 决定）
            Cup->AddLiquid(ActualML, LiquidColor);

            // 3) 在命中点弹出一次水花（P_Splash），同步颜色
            if (SplashEffectTemplate)
            {
                UNiagaraComponent* SplashComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    GetWorld(),
                    SplashEffectTemplate,
                    Hit.ImpactPoint,
                    Hit.ImpactNormal.Rotation(),
                    FVector(1.f),
                    /*bAutoDestroy=*/true);
                if (SplashComp)
                {
                    // 若 P_Splash 支持 User.Color 则会生效；不支持时 Niagara 会静默忽略。
                    SplashComp->SetNiagaraVariableLinearColor(TEXT("User.Color"), LiquidColor);
                }
            }
        }
        // 命中非杯子 → 液体"洒到地上"，P_Ribbon 内部会自己处理地面 Decal（若 bEnableDecal=true）
    }
    // 未命中 → 液体飞入虚空，扣掉即可
}
