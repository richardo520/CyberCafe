// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"

ACoffeeGrinderActor::ACoffeeGrinderActor()
{
    // 开启 Tick：用于"超时静音"和"音量随转速调制"
    PrimaryActorTick.bCanEverTick = true;

    // 主体 Mesh 作为 Root，模拟物理，允许被抓取（跟 ABottleActor 一致）
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    SetRootComponent(BodyMesh);
    BodyMesh->SetSimulatePhysics(true);
    BodyMesh->SetCollisionProfileName(TEXT("PhysicsActor"));

    // 抓取组件：主体走 Free 模式，跟瓶子一致
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(BodyMesh);
    GrabComp->GrabType = EGrabType::Free;
    GrabComp->GrabPriority = 0;

    // 挂点：把手在顶端，抽屉在底部前侧，豆入口在顶端（Bean）
    HandleMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HandleMountPoint"));
    HandleMountPoint->SetupAttachment(BodyMesh);

    DrawerMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("DrawerMountPoint"));
    DrawerMountPoint->SetupAttachment(BodyMesh);

    BeanEntryPoint = CreateDefaultSubobject<USceneComponent>(TEXT("BeanEntryPoint"));
    BeanEntryPoint->SetupAttachment(BodyMesh);

    // FX：默认不激活
    GrindFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("GrindFX"));
    GrindFX->SetupAttachment(BodyMesh);
    GrindFX->bAutoActivate = false;

    // SFX：默认不自动播放（等我们 OnHandleRotated 里 FadeIn）
    GrindSFX = CreateDefaultSubobject<UAudioComponent>(TEXT("GrindSFX"));
    GrindSFX->SetupAttachment(BodyMesh);
    GrindSFX->bAutoActivate = false;
    // 初始音量：调制开启时会覆盖，未开启时使用蓝图里配的 VolumeMultiplier
    GrindSFX->SetVolumeMultiplier(1.f);

    // ---- 音效调制默认参数 ----
    GrindSFXFadeInTime = 0.08f;
    GrindSFXFadeOutTime = 0.15f;
    GrindStopDelay = 0.2f;

    bModulateVolumeBySpeed = true;
    MinSpeedDegPerSec = 60.f;
    MaxSpeedDegPerSec = 480.f;
    MinVolumeMultiplier = 0.35f;
    MaxVolumeMultiplier = 1.f;
    SpeedSmoothing = 12.f;

    // ---- 运行时初值 ----
    AccumulatedHandleAngleDeg = 0.f;
    LastTurnGameTime = -1000.f;   // 一个"很早以前"，确保初始不会误判成刚转过
    SmoothedAngularSpeedDeg = 0.f;
    bGrindSFXActive = false;
    HandleRef = nullptr;
    DrawerRef = nullptr;
}

void ACoffeeGrinderActor::BeginPlay()
{
    Super::BeginPlay();

    SpawnChildParts();
}

void ACoffeeGrinderActor::SpawnChildParts()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // ---- 把手 ----
    if (HandleClass && HandleMountPoint)
    {
        const FTransform MountXform = HandleMountPoint->GetComponentTransform();
        AGrinderHandleActor* Handle = World->SpawnActor<AGrinderHandleActor>(
            HandleClass, MountXform, SpawnParams);
        if (Handle)
        {
            Handle->AttachToGrinder(this, HandleMountPoint);
            HandleRef = Handle;
        }
    }

    // ---- 抽屉 ----
    if (DrawerClass && DrawerMountPoint)
    {
        const FTransform MountXform = DrawerMountPoint->GetComponentTransform();
        AGrinderDrawerActor* Drawer = World->SpawnActor<AGrinderDrawerActor>(
            DrawerClass, MountXform, SpawnParams);
        if (Drawer)
        {
            Drawer->AttachToGrinder(this, DrawerMountPoint);
            DrawerRef = Drawer;
        }
    }
}

void ACoffeeGrinderActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1) 平滑瞬时角速度：以 SpeedSmoothing 为速率，向 0 衰减；OnHandleRotated 里会脉冲注入
    if (bModulateVolumeBySpeed)
    {
        SmoothedAngularSpeedDeg = FMath::FInterpTo(
            SmoothedAngularSpeedDeg, 0.f, DeltaTime, SpeedSmoothing);
    }

    // 2) 判定"停下了没"：距离上次转动上报是否超过 GrindStopDelay
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    const float Now = World->GetTimeSeconds();
    const bool bIdle = (Now - LastTurnGameTime) > GrindStopDelay;

    if (bIdle)
    {
        // 空闲：如果音效还在响就淡出
        if (bGrindSFXActive)
        {
            StopGrindSFX();
        }
        return;
    }

    // 3) 未空闲：根据当前平滑角速度更新音量
    if (bModulateVolumeBySpeed && GrindSFX && bGrindSFXActive)
    {
        const float SpeedAlpha = FMath::GetMappedRangeValueClamped(
            FVector2D(MinSpeedDegPerSec, MaxSpeedDegPerSec),
            FVector2D(0.f, 1.f),
            SmoothedAngularSpeedDeg);
        const float Vol = FMath::Lerp(MinVolumeMultiplier, MaxVolumeMultiplier, SpeedAlpha);
        GrindSFX->SetVolumeMultiplier(Vol);
    }
}

void ACoffeeGrinderActor::OnHandleRotated(float DeltaAngleDeg)
{
    if (FMath::IsNearlyZero(DeltaAngleDeg))
    {
        return;
    }

    AccumulatedHandleAngleDeg += DeltaAngleDeg;

    // ---- 音效驱动：起播（若未播）+ 更新最近转动时间 + 注入瞬时角速度 ----
    const UWorld* World = GetWorld();
    if (World)
    {
        const float Now = World->GetTimeSeconds();

        // 瞬时角速度估算：本帧 Δθ / dt。这里用引擎当前帧 DeltaTime 会更精确，
        // 但从蓝图 / 子 Actor 调过来拿不到那个 DeltaTime。近似用 (Now - LastTurnGameTime)。
        // 当空闲很久后突然又转 → Dt 会很大，导致算出很小的角速度，这刚好和
        // "开始转还没有起播" 的语义一致；随后每帧继续调用会稳定到真实速度。
        const float Dt = FMath::Max(Now - LastTurnGameTime, KINDA_SMALL_NUMBER);
        const float InstantSpeed = FMath::Abs(DeltaAngleDeg) / Dt;

        if (bModulateVolumeBySpeed)
        {
            // 用 max 而不是 FInterpTo：让"刚有输入"能立即抬起音量，Tick 里再向 0 衰减
            SmoothedAngularSpeedDeg = FMath::Max(SmoothedAngularSpeedDeg, InstantSpeed);
        }

        LastTurnGameTime = Now;

        if (!bGrindSFXActive)
        {
            StartGrindSFX();
        }
    }

    // 豆 → 粉 转换等实际研磨逻辑：后续再接。这里先广播事件方便蓝图 / 调试对接。
    OnHandleTurned.Broadcast(DeltaAngleDeg, AccumulatedHandleAngleDeg);
}

void ACoffeeGrinderActor::StartGrindSFX()
{
    if (!GrindSFX || !GrindSFX->Sound)
    {
        return;
    }

    // 起播前先把音量置到最小档，避免"最大响度突然出现"再被调制拉下来
    const float StartVol = bModulateVolumeBySpeed ? MinVolumeMultiplier : 1.f;
    GrindSFX->SetVolumeMultiplier(StartVol);

    if (GrindSFXFadeInTime > 0.f)
    {
        // FadeIn 内部会 Play 并把音量从 0 升到 FadeVolumeLevel
        GrindSFX->FadeIn(GrindSFXFadeInTime, 1.f);
    }
    else
    {
        GrindSFX->Play();
    }

    bGrindSFXActive = true;
}

void ACoffeeGrinderActor::StopGrindSFX()
{
    if (!GrindSFX)
    {
        return;
    }

    if (GrindSFXFadeOutTime > 0.f)
    {
        GrindSFX->FadeOut(GrindSFXFadeOutTime, 0.f);
    }
    else
    {
        GrindSFX->Stop();
    }

    bGrindSFXActive = false;
}

FTransform ACoffeeGrinderActor::GetHandleMountWorldTransform() const
{
    return HandleMountPoint ? HandleMountPoint->GetComponentTransform() : GetActorTransform();
}

FTransform ACoffeeGrinderActor::GetDrawerMountWorldTransform() const
{
    return DrawerMountPoint ? DrawerMountPoint->GetComponentTransform() : GetActorTransform();
}

bool ACoffeeGrinderActor::IsHeld() const
{
    return GrabComp ? GrabComp->IsHeld() : false;
}

