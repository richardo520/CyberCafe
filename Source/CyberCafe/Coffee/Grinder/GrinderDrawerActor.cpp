// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Sound/SoundBase.h"

AGrinderDrawerActor::AGrinderDrawerActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 抽屉 Mesh 作为 Root。默认 Attached 状态下关物理；Detach 后再开
    DrawerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DrawerMesh"));
    SetRootComponent(DrawerMesh);
    DrawerMesh->SetSimulatePhysics(false);
    DrawerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DrawerMesh->SetCollisionResponseToAllChannels(ECR_Overlap);

    // Custom 抓取：由本类手动维护滑动 / 拔出 / 落地
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(DrawerMesh);
    GrabComp->GrabType = EGrabType::Custom;
    GrabComp->GrabPriority = 1;
    GrabComp->bAllowRemoteGrab = false;   // 子部件仅支持贴身抓取，禁止远程召唤

    // 默认参数
    PullOutDistance = 8.f;
    SlideInterpSpeed = 18.f;
    MaxSideOffset = 10.f;
    ReattachSnapDistance = 6.f;
    DetachHaptic = nullptr;
    DetachSound = nullptr;
    ReattachSound = nullptr;

    State = EDrawerState::Attached;
    OwnerGrinder = nullptr;
    CurrentOffset = 0.f;
    GrabHandOffsetLocal = FVector::ZeroVector;
    MountRef = nullptr;
}

void AGrinderDrawerActor::BeginPlay()
{
    Super::BeginPlay();

    // UGrabComponent::BeginPlay 会强制 PhysicsActor，重新刷回 QueryOnly + Overlap
    if (DrawerMesh)
    {
        DrawerMesh->SetSimulatePhysics(false);
        DrawerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        DrawerMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }

    if (GrabComp)
    {
        GrabComp->OnGrabbed.AddDynamic(this, &AGrinderDrawerActor::HandleGrabbed);
        GrabComp->OnDropped.AddDynamic(this, &AGrinderDrawerActor::HandleDropped);
    }
}

void AGrinderDrawerActor::AttachToGrinder(ACoffeeGrinderActor* InOwner, USceneComponent* MountComp)
{
    if (!InOwner || !MountComp)
    {
        return;
    }
    OwnerGrinder = InOwner;
    MountRef = MountComp;

    // 挂到挂点，重置为合上状态
    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
    AttachRule.bWeldSimulatedBodies = false;
    AttachToComponent(MountComp, AttachRule);

    if (DrawerMesh)
    {
        DrawerMesh->SetSimulatePhysics(false);
        DrawerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        DrawerMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }

    State = EDrawerState::Attached;
    CurrentOffset = 0.f;
    SetActorRelativeLocation(FVector::ZeroVector);
    SetActorRelativeRotation(FRotator::ZeroRotator);
}

void AGrinderDrawerActor::ReattachToGrinder()
{
    if (!OwnerGrinder || !MountRef)
    {
        return;
    }

    // 关物理 → QueryOnly + Overlap → Attach 回挂点
    if (DrawerMesh)
    {
        DrawerMesh->SetSimulatePhysics(false);
        DrawerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        DrawerMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }
    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
    AttachRule.bWeldSimulatedBodies = false;
    AttachToComponent(MountRef, AttachRule);

    State = EDrawerState::Attached;
    CurrentOffset = 0.f;
    SetActorRelativeLocation(FVector::ZeroVector);
    SetActorRelativeRotation(FRotator::ZeroRotator);

    if (ReattachSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, ReattachSound, GetActorLocation());
    }
}

void AGrinderDrawerActor::DetachFromGrinder(UMotionControllerComponent* MotionController)
{
    if (!MotionController)
    {
        return;
    }

    // 从主体上脱离，保持世界位姿；随后 Attach 到手柄
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::KeepWorldTransform;
    AttachRule.bWeldSimulatedBodies = true;
    AttachToComponent(MotionController, AttachRule);

    if (DrawerMesh)
    {
        DrawerMesh->SetSimulatePhysics(false);
        // 拔出后恢复正常碰撞（可撞环境）
        DrawerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    }

    State = EDrawerState::Detached;

    // 反馈
    if (DetachHaptic)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            PC->PlayHapticEffect(DetachHaptic, GrabComp ? GrabComp->GetHeldByHand() : EControllerHand::AnyHand);
        }
    }
    if (DetachSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, DetachSound, GetActorLocation());
    }
}

void AGrinderDrawerActor::HandleGrabbed()
{
    if (!GrabComp)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    if (State == EDrawerState::Attached)
    {
        // 抓住抽屉但还挂在主体上：记录初始手偏移，Tick 里用手位移驱动滑动
        if (MountRef)
        {
            const FTransform MountXform = MountRef->GetComponentTransform();
            const FVector HandLocal = MountXform.InverseTransformPosition(MC->GetComponentLocation());
            // 基准 = 手抓瞬间"应该对应"的 CurrentOffset 位置；这样抓抽屉的时候不会瞬间跳动
            GrabHandOffsetLocal = HandLocal - FVector(0.f, CurrentOffset, 0.f);
        }
    }
    else
    {
        // Detached / Free：抽屉是独立物体，抓到就 Attach 到手柄（保持世界位姿）
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::KeepWorldTransform;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(MC, AttachRule);

        if (DrawerMesh)
        {
            DrawerMesh->SetSimulatePhysics(false);
            DrawerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
        }
        State = EDrawerState::Detached;
    }
}

void AGrinderDrawerActor::HandleDropped()
{
    if (State == EDrawerState::Attached)
    {
        // 挂着松手：什么都不做，抽屉停在当前滑动位置
        return;
    }

    // Detached / Free 松手：先脱离手柄，再判断是否吸回挂点
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    if (!OwnerGrinder || !MountRef)
    {
        if (DrawerMesh)
        {
            DrawerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            DrawerMesh->SetSimulatePhysics(true);
        }
        State = EDrawerState::Free;
        return;
    }

    const FVector MountWS = MountRef->GetComponentLocation();
    const float Dist = FVector::Dist(GetActorLocation(), MountWS);

    if (Dist <= ReattachSnapDistance)
    {
        // 靠近挂点 → 自动吸回
        ReattachToGrinder();
    }
    else
    {
        // 远离挂点 → 作为独立物体掉落
        if (DrawerMesh)
        {
            DrawerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            DrawerMesh->SetSimulatePhysics(true);
        }
        State = EDrawerState::Free;
    }
}

void AGrinderDrawerActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 只有 Attached 状态下才需要"手驱动滑动"逻辑
    if (State != EDrawerState::Attached)
    {
        return;
    }
    if (!GrabComp || !GrabComp->IsHeld() || !MountRef || !OwnerGrinder)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    // ---- 1. 把手柄位置转到挂点局部空间 ----
    const FTransform MountXform = MountRef->GetComponentTransform();
    const FVector HandLocal = MountXform.InverseTransformPosition(MC->GetComponentLocation());
    const FVector RelativeHand = HandLocal - GrabHandOffsetLocal;

    // ---- 2. 侧向偏出容差检测 ----
    if (MaxSideOffset > 0.f)
    {
        if (FMath::Abs(RelativeHand.X) > MaxSideOffset || FMath::Abs(RelativeHand.Z) > MaxSideOffset)
        {
            GrabComp->TryRelease();
            return;
        }
    }

    // ---- 3. 目标偏移（沿 +Y 分量，允许拉超过 PullOutDistance 以便触发 Detach） ----
    //       禁止负方向（推入到主体内部）
    const float RawTarget = RelativeHand.Y;
    const float ClampedTarget = FMath::Max(0.f, RawTarget);

    // ---- 4. 平滑插值 ----
    CurrentOffset = FMath::FInterpTo(CurrentOffset, ClampedTarget, DeltaTime, SlideInterpSpeed);

    // ---- 5. 判定：超过 PullOutDistance → 拔出到手上 ----
    if (CurrentOffset >= PullOutDistance)
    {
        DetachFromGrinder(MC);
        return;
    }

    // ---- 6. 应用滑动位移 ----
    SetActorRelativeLocation(FVector(0.f, CurrentOffset, 0.f));
    SetActorRelativeRotation(FRotator::ZeroRotator);
}

