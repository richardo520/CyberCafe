// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "MotionControllerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Sound/SoundBase.h"

AGrinderDrawerActor::AGrinderDrawerActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;

    // 抽屉 Mesh 作为 Root。默认 Attached 状态下关物理；Detach 后再开
    DrawerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DrawerMesh"));
    SetRootComponent(DrawerMesh);
    DrawerMesh->SetSimulatePhysics(false);
    DrawerMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DrawerMesh->SetCollisionResponseToAllChannels(ECR_Overlap);

    // 抽屉内部的咖啡粉堆（蓝图里指定 StaticMesh，拖到抽屉内底）
    GroundPileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundPileMesh"));
    GroundPileMesh->SetupAttachment(DrawerMesh);
    GroundPileMesh->SetSimulatePhysics(false);
    GroundPileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundPileMesh->SetVisibility(false);
    PowderStream=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PowderStream"));
    PowderStream->SetupAttachment(DrawerMesh);PowderStream->SetCollisionEnabled(ECollisionEnabled::NoCollision);PowderStream->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Grain(TEXT("/Engine/BasicShapes/Sphere.Sphere"));PowderStream->SetStaticMesh(Grain.Object);

    GroundPourPoint = CreateDefaultSubobject<USceneComponent>(TEXT("GroundPourPoint"));
    GroundPourPoint->SetupAttachment(DrawerMesh);
    GroundPourPoint->SetRelativeLocation(FVector(0.f, 6.f, 2.f));

    // Custom 抓取：由本类手动维护滑动 / 拔出 / 落地
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(DrawerMesh);
    GrabComp->GrabType = EGrabType::Custom;
    GrabComp->GrabPriority = 1;
    // 抽屉默认使用"原地控制 + 隐藏手部 Mesh"：
    //   - 挂在主体上时：GrabComponent 不生效原地控制（我们自己在 Tick 里用滑轨约束驱动位置），
    //     仅隐藏手部 Mesh；
    //   - 拔出到独立状态后：DetachFromGrinder 里调用 BeginGrabInPlace，抽屉不吸附到手柄，
    //     悬在拔出位置由手柄增量驱动。
    GrabComp->bGrabInPlace = true;
    GrabComp->bHideHandWhileHeld = true;
    // 不在构造函数里禁远程抓取；挂在主体上时由 AttachToGrinder / ReattachToGrinder 关闭，
    // 拔出后（DetachFromGrinder）恢复为 true，自由抽屉就能像普通物体一样被远程抓。

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

    DrawerCapacityGrams = 60.f;   // 抽屉能接约 60g 粉，够磨两仓豆
    CurrentGroundGrams  = 0.f;
    GroundPileInitialScale = FVector::OneVector;
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

    SanitizeGroundProperties();
    // 缓存完整基准；平移模式不压缩网格，抓取组件在本 Actor 查询之前更新位姿。
    if (GrabComp) { AddTickPrerequisiteComponent(GrabComp); }
    if (GroundPileMesh)
    {
        GroundPileInitialScale = GroundPileMesh->GetRelativeScale3D();
        GroundPileInitialLocation = GroundPileMesh->GetRelativeLocation();
    }
    UpdateGroundPileVisual();
}

void AGrinderDrawerActor::AttachToGrinder(ACoffeeGrinderActor* InOwner, USceneComponent* MountComp)
{
    if (!InOwner || !MountComp)
    {
        return;
    }
    if (OwnerGrinder && OwnerGrinder != InOwner)
    {
        OwnerGrinder->OnGrindProgress.RemoveDynamic(this, &AGrinderDrawerActor::OnGrinderGrindProgress);
    }
    OwnerGrinder = InOwner;
    MountRef = MountComp;
    if (DrawerMesh) { DrawerMesh->SetSimulatePhysics(false); }

    // 将本抽屉的 OnGrinderGrindProgress 回调绑定到主体的 OnGrindProgress，
    // 这样每次主体 ProcessGrinding 产出粉后，抽屉能自动搬粉并刷新可视化。
    // AddUniqueDynamic 避免重复绑定（包括后续 ReattachToGrinder 的情况）
    InOwner->OnGrindProgress.AddUniqueDynamic(this, &AGrinderDrawerActor::OnGrinderGrindProgress);

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

    // 挂在主体上时禁止远程抓取/高亮（DetachFromGrinder 会重新开启）
    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = false;
        // 防御性：若之前处于原地控制模式，在重新挂回主体后关闭，避免 GrabComp Tick 继续驱动 Owner Actor
        GrabComp->EndGrabInPlace();
    }

    // 恢复与主体的正常碰撞（初始挂上时保险起见清一次）
    SetIgnoreCollisionWithGrinder(false);
    OnGrinderGrindProgress(OwnerGrinder->GetCurrentBeanGrams(), OwnerGrinder->GetCurrentGroundGrams());
}

bool AGrinderDrawerActor::ResetForWorkbench(ACoffeeGrinderActor* InOwner, USceneComponent* MountComp)
{
    if (!IsValid(this) || !IsValid(InOwner) || !IsValid(MountComp)
        || GetWorld() != InOwner->GetWorld() || MountComp->GetOwner() != InOwner
        || MountComp != InOwner->DrawerMountPoint || OwnerGrinder != InOwner
        || !IsValid(DrawerMesh) || DrawerMesh->GetOwner() != this || GetRootComponent() != DrawerMesh
        || !IsValid(GrabComp) || GrabComp->GetOwner() != this
        || GrabComp->IsHeld() || GrabComp->IsPulled()
        || !InOwner->CanResetForWorkbench() || InOwner->GetCurrentGroundGrams() != 0.f)
    {
        return false;
    }

    DiscardedGroundGrams += CurrentGroundGrams;
    CurrentGroundGrams = 0.f;
    LastPouredGroundGrams = 0.f;
    LastAcceptedGroundGrams = 0.f;
    LastGroundPourOrigin = FVector::ZeroVector;
    LastGroundPourEnd = FVector::ZeroVector;
    if (IsValid(PowderStream)) { PowderStream->ClearInstances(); }
    PowderVisualAge = 0.f;
    PourSoundCooldown = 0.f;
    GrabHandOffsetLocal = FVector::ZeroVector;
    if (DrawerMesh->IsSimulatingPhysics())
    {
        DrawerMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
        DrawerMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    }
    // 主体留存已是零，原挂接回调不会把待丢弃的粉搬回空抽屉。
    AttachToGrinder(InOwner, MountComp);
    UpdateGroundPileVisual();
    return true;
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

    // 自动吸回主体：同样关闭远程抓取/高亮
    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = false;
        // 防御性：若 Reattach 发生时抽屉还在原地控制模式，关闭标志避免 GrabComp Tick 继续驱动
        GrabComp->EndGrabInPlace();
    }

    // 恢复与主体之间的正常碰撞（拔出期间通过 SetIgnoreCollisionWithGrinder(true) 忽略过）
    SetIgnoreCollisionWithGrinder(false);
    OnGrinderGrindProgress(OwnerGrinder->GetCurrentBeanGrams(), OwnerGrinder->GetCurrentGroundGrams());
}

void AGrinderDrawerActor::DetachFromGrinder(UMotionControllerComponent* MotionController)
{
    if (!MotionController)
    {
        return;
    }

    // 从主体上脱离，保持世界位姿
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    if (DrawerMesh)
    {
        DrawerMesh->SetSimulatePhysics(false);
        // 拔出后恢复正常碰撞（可撞环境）
        DrawerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    }

    if (GrabComp && GrabComp->bGrabInPlace)
    {
        // 原地控制：不 Attach 到手柄，交给 GrabComponent 驱动 Owner Actor 位姿
        GrabComp->BeginGrabInPlace();
    }
    else
    {
        // 默认：Attach 到手柄
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::KeepWorldTransform;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(MotionController, AttachRule);
    }

    State = EDrawerState::Detached;

    // 拔出后恢复为普通可交互物体：允许远程抓取/高亮
    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = true;
    }

    // ★ 让抽屉与主体之间互相忽略碰撞：
    //   - 目的：主体在 PhysicsHandle 抓取模式下仍保持实体碰撞档案（PhysicsActor），
    //     若不忽略，抽屉的 PhysicsActor 会被主体外壳挡在外面，无法"塞回去"到 ReattachSnapDistance 内。
    //   - 忽略仅限"抽屉 ↔ 主体"，抽屉与桌面、地面、其他 Actor 的碰撞保持正常。
    //   - ReattachToGrinder / AttachToGrinder 里会恢复。
    SetIgnoreCollisionWithGrinder(true);

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
        // Detached / Free：抽屉是独立物体，抓到后根据模式选 Attach 到手 或 原地控制
        if (GrabComp->bGrabInPlace)
        {
            if (DrawerMesh)
            {
                DrawerMesh->SetSimulatePhysics(false);
                DrawerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            }
            GrabComp->BeginGrabInPlace();
        }
        else
        {
            FAttachmentTransformRules AttachRule = FAttachmentTransformRules::KeepWorldTransform;
            AttachRule.bWeldSimulatedBodies = true;
            AttachToComponent(MC, AttachRule);

            if (DrawerMesh)
            {
                DrawerMesh->SetSimulatePhysics(false);
                DrawerMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            }
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

    // 记录松手瞬间手柄位置（用于"手离挂点足够近就吸回"的兜底判定）——
    // 必须在 DetachFromActor 之前拿，因为 Detach 后 GetHoldingController 仍可用，
    // 但为了防御性，先取值再断开。
    FVector HandWS = FVector::ZeroVector;
    bool bHasHand = false;
    if (GrabComp)
    {
        if (UMotionControllerComponent* MC = GrabComp->GetHoldingController())
        {
            HandWS = MC->GetComponentLocation();
            bHasHand = true;
        }
    }

    // Detached / Free 松手：先脱离手柄，再判断是否吸回挂点
    // 注：若处于"原地控制"模式，抽屉并未 Attach 到手柄，DetachFromActor 也安全（无作用）。
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
    const float DrawerDist = FVector::Dist(GetActorLocation(), MountWS);
    const float HandDist   = bHasHand ? FVector::Dist(HandWS, MountWS) : TNumericLimits<float>::Max();

    // 双阈值判定：抽屉本体靠近 或 玩家的手靠近，任一命中即吸回。
    // 这样即使主体处于 PhysicsHandle 模式导致抽屉被外壳弹开、无法真正贴到挂点，
    // 只要玩家的意图是"把抽屉放回去"（手已经伸到抽屉槽内），一样能触发 Reattach。
    const bool bShouldReattach =
        (DrawerDist <= ReattachSnapDistance) ||
        (HandDist   <= ReattachSnapDistance);

    if (bShouldReattach)
    {
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

void AGrinderDrawerActor::SetIgnoreCollisionWithGrinder(bool bIgnore)
{
    if (!OwnerGrinder)
    {
        return;
    }

    UPrimitiveComponent* DrawerPrim = DrawerMesh;
    UPrimitiveComponent* BodyPrim = Cast<UPrimitiveComponent>(OwnerGrinder->GetRootComponent());
    if (!DrawerPrim || !BodyPrim)
    {
        return;
    }

    // 双向登记：让抽屉/主体的物理与 Sweep 解算都跳过对方
    DrawerPrim->IgnoreActorWhenMoving(OwnerGrinder, bIgnore);
    BodyPrim->IgnoreActorWhenMoving(this, bIgnore);
}

void AGrinderDrawerActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    SanitizeGroundProperties();
    LastPouredGroundGrams = 0.f;
    LastAcceptedGroundGrams = 0.f;
    if(PowderStream)PowderStream->ClearInstances();
    // 绝不自动触发自由落地/无人持有抽屉的倒粉。
    if (State == EDrawerState::Detached && GrabComp && GrabComp->IsHeld())
    {
        PourGrounds(DeltaTime);
        UpdatePowderStream(DeltaTime);
        PourSoundCooldown=FMath::Max(0.f,PourSoundCooldown-DeltaTime);
        if(LastPouredGroundGrams>0&&GroundPourSound&&PourSoundCooldown<=0){UGameplayStatics::PlaySoundAtLocation(this,GroundPourSound,LastGroundPourOrigin,.5f);PourSoundCooldown=.4f;}
        return;
    }
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

//=====================================================================
// 粉 抽取 & 可视化
//=====================================================================

void AGrinderDrawerActor::SanitizeGroundProperties()
{
    DrawerCapacityGrams = FMath::IsFinite(DrawerCapacityGrams) ? FMath::Max(DrawerCapacityGrams, 0.f) : 0.f;
    CurrentGroundGrams = FMath::IsFinite(CurrentGroundGrams) ? FMath::Max(CurrentGroundGrams, 0.f) : 0.f;
    GroundPourRateGramsPerSecond = FMath::IsFinite(GroundPourRateGramsPerSecond) ? FMath::Max(GroundPourRateGramsPerSecond, 0.f) : 0.f;
    PourTiltDegrees = FMath::IsFinite(PourTiltDegrees) ? FMath::Clamp(PourTiltDegrees, 0.f, 89.f) : 45.f;
    GroundPourTraceDistanceCM = FMath::IsFinite(GroundPourTraceDistanceCM) ? FMath::Clamp(GroundPourTraceDistanceCM, 20.f, 40.f) : 40.f;
    GroundPileDepthCM = FMath::IsFinite(GroundPileDepthCM) ? FMath::Max(GroundPileDepthCM, 0.f) : 1.8f;
    checkSlow(CurrentGroundGrams >= 0.f && DrawerCapacityGrams >= 0.f);
}

void AGrinderDrawerActor::OnGrinderGrindProgress(float /*BeanGramsLeft*/, float /*GroundGramsInGrinder*/)
{
    SanitizeGroundProperties();
    // 直接读取真实留存，Attach/Reattach 也调用；满屉不 Drain，不再次广播。
    if (State != EDrawerState::Attached || !OwnerGrinder) { return; }
    const float Space = FMath::Max(DrawerCapacityGrams - CurrentGroundGrams, 0.f);
    if (Space <= 0.f) { return; }
    const float Drawn = OwnerGrinder->DrainGround(Space);
    if (Drawn > 0.f)
    {
        // 不用显示/容量 clamp 丢掉已从主体扣除的质量。
        CurrentGroundGrams += Drawn;
        UpdateGroundPileVisual();
    }
}

float AGrinderDrawerActor::AddGrounds(float G)
{
    SanitizeGroundProperties();
    if (!FMath::IsFinite(G) || G <= 0.f) { return 0.f; }
    const float Space = FMath::Max(DrawerCapacityGrams - CurrentGroundGrams, 0.f);
    const float Before = CurrentGroundGrams;
    CurrentGroundGrams += FMath::Min(G, Space);
    UpdateGroundPileVisual();
    return CurrentGroundGrams - Before;
}

float AGrinderDrawerActor::RemoveGrounds(float G)
{
    SanitizeGroundProperties();
    if (!FMath::IsFinite(G) || G <= 0.f) { return 0.f; }
    const float Before = CurrentGroundGrams;
    CurrentGroundGrams = FMath::Max(0.f, CurrentGroundGrams - FMath::Min(G, CurrentGroundGrams));
    UpdateGroundPileVisual();
    return Before - CurrentGroundGrams;
}

float AGrinderDrawerActor::PourGrounds(float Dt)
{
    LastPouredGroundGrams = 0.f;
    LastAcceptedGroundGrams = 0.f;
    SanitizeGroundProperties();
    UWorld* World = GetWorld();
    if (!World || !DrawerMesh || !GroundPourPoint || !FMath::IsFinite(Dt) || Dt <= 0.f || CurrentGroundGrams <= 0.f)
    {
        return 0.f;
    }
    const FVector Down = -FVector::UpVector;
    const float UpDot = static_cast<float>(FVector::DotProduct(DrawerMesh->GetUpVector(), FVector::UpVector));
    // UE 的 ForwardVector 是 +X，这里的口沿方向明确取网格局部 +Y。
    const FVector TipAxis = DrawerMesh->GetComponentQuat().RotateVector(FVector::YAxisVector);
    const float TipDown = static_cast<float>(FVector::DotProduct(TipAxis, Down));
    const float Threshold = FMath::Cos(FMath::DegreesToRadians(PourTiltDegrees));
    if (UpDot >= Threshold || TipDown <= 0.3f) { return 0.f; }
    const FVector Origin = GroundPourPoint->GetComponentLocation();
    const FVector End = Origin + Down * GroundPourTraceDistanceCM;
    if (Origin.ContainsNaN() || End.ContainsNaN()) { return 0.f; }
    const double Want = static_cast<double>(GroundPourRateGramsPerSecond) * Dt * FMath::Clamp(TipDown, 0.f, 1.f);
    const float Requested = static_cast<float>(FMath::Min(Want, static_cast<double>(CurrentGroundGrams)));
    if (Requested <= 0.f) { return 0.f; }

    FCollisionQueryParams Params(SCENE_QUERY_STAT(GrinderGroundPour), false, this);
    Params.AddIgnoredActor(GetOwner());
    if (OwnerGrinder) { Params.AddIgnoredActor(OwnerGrinder); }
    for (TActorIterator<APawn> It(World); It; ++It) { Params.AddIgnoredActor(*It); }
    // 本帧最早物理障碍先决定可达距离。不是先选一个最近 Actor 再隔空找入口。
    FHitResult PhysicalHit;
    const bool bBlocked = World->LineTraceSingleByChannel(PhysicalHit, Origin, End, ECC_Visibility, Params);
    const double ObstacleDistance = bBlocked ? PhysicalHit.Distance : GroundPourTraceDistanceCM;
    AHandDripperActor* Target = nullptr;
    FVector EntryHit = End;
    double NearestDistance = GroundPourTraceDistanceCM + 1.0;
    for (TActorIterator<AHandDripperActor> It(World); It; ++It)
    {
        AHandDripperActor* Dripper = *It;
        if (!IsValid(Dripper) || !Dripper->PourEntryPoint || !FMath::IsFinite(Dripper->PourEntryRadius) || Dripper->PourEntryRadius <= 0.f)
        {
            continue;
        }
        const FTransform EntryTransform = Dripper->PourEntryPoint->GetComponentTransform();
        const FVector Center = EntryTransform.GetLocation();
        const FVector Normal = Dripper->PourEntryPoint->GetUpVector();
        if (Center.ContainsNaN() || Normal.ContainsNaN() || EntryTransform.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER) { continue; }
        const double StartAbove = FVector::DotProduct(Origin - Center, Normal);
        const double Approach = FVector::DotProduct(Down, Normal);
        if (StartAbove <= 0.0 || Approach >= -UE_SMALL_NUMBER) { continue; }
        const double Distance = -StartAbove / Approach;
        if (Distance < 0.0 || Distance > GroundPourTraceDistanceCM || Distance >= NearestDistance) { continue; }
        const FVector Hit = Origin + Down * Distance;
        const FVector LocalHit = EntryTransform.InverseTransformPosition(Hit);
        const double Radius = Dripper->PourEntryRadius;
        if (LocalHit.ContainsNaN() || LocalHit.X * LocalHit.X + LocalHit.Y * LocalHit.Y > Radius * Radius) { continue; }
        if (bBlocked && ObstacleDistance + 0.01 < Distance) { continue; }
        // 即使最近入口拒收，也不穿透它改投第二个滤杯。
        Target = Dripper;
        EntryHit = Hit;
        NearestDistance = Distance;
    }
    LastGroundPourOrigin = Origin;
    LastGroundPourEnd = Target ? EntryHit : (bBlocked ? PhysicalHit.ImpactPoint : End);
    const float Dropped = RemoveGrounds(Requested);
    float Accepted = 0.f;
    if (Target && Dropped > 0.f && Target->CanReceiveGrounds())
    {
        const float Result = Target->AddGrounds(Dropped, EntryHit);
        Accepted = FMath::IsFinite(Result) ? FMath::Clamp(Result, 0.f, Dropped) : 0.f;
    }
    SpilledGroundGrams += static_cast<double>(Dropped) - Accepted;
    LastPouredGroundGrams = Dropped;
    LastAcceptedGroundGrams = Accepted;
    return Dropped;
}

void AGrinderDrawerActor::UpdatePowderStream(float Dt)
{
    if(!PowderStream)return;PowderStream->ClearInstances();
    if(LastPouredGroundGrams<=0||!FMath::IsFinite(Dt))return;
    PowderVisualAge+=FMath::Max(0.f,Dt);
    for(int I=0;I<24;++I){const float T=FMath::Frac(PowderVisualAge*2.3f+I/24.f);FVector P=FMath::Lerp(LastGroundPourOrigin,LastGroundPourEnd,T);P.X+=FMath::Sin(I*2.1f)*.35;P.Y+=FMath::Cos(I*2.7f)*.35;PowderStream->AddInstance(FTransform(FQuat::Identity,P,FVector(.0017)),true);}
}
void AGrinderDrawerActor::UpdateGroundPileVisual()
{
    if (!GroundPileMesh) { return; }
    const float Ratio = DrawerCapacityGrams > 0.f ? FMath::Clamp(CurrentGroundGrams / DrawerCapacityGrams, 0.f, 1.f) : 0.f;
    FVector Location = GroundPileInitialLocation;
    FVector Scale = GroundPileInitialScale;
    if (bTranslateGroundPile) { Location.Z -= GroundPileDepthCM * (1.f - Ratio); }
    else { Scale.Z *= Ratio; }
    GroundPileMesh->SetRelativeLocation(Location);
    GroundPileMesh->SetRelativeScale3D(Scale);
    GroundPileMesh->SetVisibility(CurrentGroundGrams > 0.f);
}

