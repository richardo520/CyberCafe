// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"

AGrinderHandleActor::AGrinderHandleActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 把手 Mesh 作为 Root，不模拟物理——它一直挂在主体挂点上
    HandleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandleMesh"));
    SetRootComponent(HandleMesh);
    HandleMesh->SetSimulatePhysics(false);
    // 与瓶盖同样的策略：QueryOnly + AllChannels Overlap，能被抓取射线命中但不推动主体
    HandleMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    HandleMesh->SetCollisionResponseToAllChannels(ECR_Overlap);

    // Custom 抓取：不由 GrabComponent 自动 Attach，纯手动维护约束旋转
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(HandleMesh);
    GrabComp->GrabType = EGrabType::Custom;
    GrabComp->GrabPriority = 1;   // 高于主体抓取，避免误抓到 Body
    // 不在构造函数里禁远程抓取；改由 AttachToGrinder 时关闭。
    // 这样即使未来把手脱离主体（例如维护模式），也能保留远程抓取能力。

    // 默认参数
    MaxHandOffset = 15.f;
    HandleArmRadius = 5.f;
    bOneWayEffective = false;
    EffectiveDirectionSign = 1;
    TurnHaptic = nullptr;
    HapticIntervalDeg = 30.f;

    OwnerGrinder = nullptr;
    CurrentAngleDeg = 0.f;
    LastHandAngleDeg = 0.f;
    bHasValidLastHandAngle = false;
    LastHapticAngleDeg = 0.f;
    MountRef = nullptr;
}

void AGrinderHandleActor::BeginPlay()
{
    Super::BeginPlay();

    // 与瓶盖同样的原因：UGrabComponent::BeginPlay 会把父组件 CollisionProfile 强制改成 PhysicsActor，
    // 我们需要再刷回 QueryOnly + Overlap（不参与刚体，只能被 SphereTrace 命中）
    if (HandleMesh)
    {
        HandleMesh->SetSimulatePhysics(false);
        HandleMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        HandleMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }

    if (GrabComp)
    {
        GrabComp->OnGrabbed.AddDynamic(this, &AGrinderHandleActor::HandleGrabbed);
        GrabComp->OnDropped.AddDynamic(this, &AGrinderHandleActor::HandleDropped);
    }
}

void AGrinderHandleActor::AttachToGrinder(ACoffeeGrinderActor* InOwner, USceneComponent* MountComp)
{
    if (!InOwner || !MountComp)
    {
        return;
    }
    OwnerGrinder = InOwner;
    MountRef = MountComp;

    // SnapToTarget + Weld：把手完全对齐挂点局部零点
    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
    AttachRule.bWeldSimulatedBodies = false;   // Root 未模拟物理，无需 Weld
    AttachToComponent(MountComp, AttachRule);

    // 复位角度
    CurrentAngleDeg = 0.f;
    SetActorRelativeRotation(FRotator::ZeroRotator);

    // 挂在主体上时禁止远程抓取/高亮：只能贴身抓，避免被远程召唤把把手从旋转轴上撕走
    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = false;
    }
}

void AGrinderHandleActor::HandleGrabbed()
{
    // Custom 抓取时不改物理 / 不 Attach，本函数只负责重置差分状态；
    // 真正的旋转在 Tick 里驱动。
    bHasValidLastHandAngle = false;
    LastHapticAngleDeg = CurrentAngleDeg;
}

void AGrinderHandleActor::HandleDropped()
{
    // 松手：把手就地保持当前角度，不做任何脱离。清除差分状态即可。
    bHasValidLastHandAngle = false;
}

void AGrinderHandleActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!GrabComp || !GrabComp->IsHeld() || !MountRef || !OwnerGrinder)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    // ---- 1. 把手柄位置转到挂点局部空间，投影到 XY 平面 ----
    const FTransform MountXform = MountRef->GetComponentTransform();
    const FVector HandWS = MC->GetComponentLocation();
    const FVector HandLocal = MountXform.InverseTransformPosition(HandWS);
    const FVector2D HandXY(HandLocal.X, HandLocal.Y);

    // ---- 2. 距离检测：手偏出旋转半径太多 → 自动松手 ----
    if (MaxHandOffset > 0.f && HandleArmRadius > 0.f)
    {
        const float RadialDist = HandXY.Size();
        if (FMath::Abs(RadialDist - HandleArmRadius) > MaxHandOffset)
        {
            GrabComp->TryRelease();
            return;
        }
    }

    // XY 长度太小（手正好在旋转轴上）时极角不稳定，跳过本帧
    if (HandXY.SquaredLength() < KINDA_SMALL_NUMBER)
    {
        return;
    }

    // ---- 3. 极角差分 ----
    const float HandAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(HandXY.Y, HandXY.X));

    if (!bHasValidLastHandAngle)
    {
        // 首帧：只记录基准，不产生 Δθ
        LastHandAngleDeg = HandAngleDeg;
        bHasValidLastHandAngle = true;
        return;
    }

    const float DeltaAngleDeg = FMath::FindDeltaAngleDegrees(LastHandAngleDeg, HandAngleDeg);
    LastHandAngleDeg = HandAngleDeg;

    if (FMath::IsNearlyZero(DeltaAngleDeg))
    {
        return;
    }

    // ---- 4. 更新把手自身相对旋转（绕挂点局部 +Z） ----
    CurrentAngleDeg += DeltaAngleDeg;
    // 显式构造"绕挂点局部 +Z 轴"的四元数：
    // 由于本 Actor Attach 到 MountRef 上，相对旋转的参考系正是 MountRef 的局部空间，
    // 所以绕本地 Z 用 FQuat(FVector::UpVector, ...) 即可，等价于挂点局部 +Z。
    // 这样即使挂点在蓝图里被旋转过，或 Mesh 的 pivot 有偏差，转轴仍严格锁定在挂点 +Z。
    const FQuat RotQuat(FVector::UpVector, FMath::DegreesToRadians(CurrentAngleDeg));
    SetActorRelativeRotation(RotQuat);

    // ---- 5. 上报给主体（考虑单向有效开关） ----
    float ReportedDelta = DeltaAngleDeg;
    if (bOneWayEffective)
    {
        const float Sign = (EffectiveDirectionSign >= 0) ? 1.f : -1.f;
        if (DeltaAngleDeg * Sign < 0.f)
        {
            ReportedDelta = 0.f;   // 反向不计入研磨
        }
    }
    OwnerGrinder->OnHandleRotated(ReportedDelta);

    // ---- 6. 触觉：按 HapticIntervalDeg 触发 ----
    if (TurnHaptic && HapticIntervalDeg > 0.f)
    {
        if (FMath::Abs(CurrentAngleDeg - LastHapticAngleDeg) >= HapticIntervalDeg)
        {
            LastHapticAngleDeg = CurrentAngleDeg;
            if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
            {
                PC->PlayHapticEffect(TurnHaptic, GrabComp->GetHeldByHand());
            }
        }
    }
}

