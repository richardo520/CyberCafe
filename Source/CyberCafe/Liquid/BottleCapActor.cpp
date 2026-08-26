// Fill out your copyright notice in the Description page of Project Settings.

#include "Liquid/BottleCapActor.h"
#include "Liquid/BottleActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MotionControllerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Sound/SoundBase.h"

ABottleCapActor::ABottleCapActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 盖子外壳 Mesh 作为 Root
    CapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapMesh"));
    SetRootComponent(CapMesh);
    // 初始不模拟物理——盖在瓶口时跟随瓶子 Transform
    CapMesh->SetSimulatePhysics(false);
    // 盖在瓶口时的碰撞策略：
    //   - QueryOnly：不参与物理刚体计算（不会把瓶子弹飞）
    //   - AllChannels 设为 Overlap：VRPawn 的 SphereTrace 才能命中盖子从而找到它的 GrabComp
    // 拧下瓶盖后（DetachFromBottle）再切回 PhysicsActor 参与正常物理交互。
    CapMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CapMesh->SetCollisionResponseToAllChannels(ECR_Overlap);

    // 抓取组件：Custom 模式，不由 GrabComponent 自动 Attach 到手上，扭转达到阈值后由本类主动 Attach
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(CapMesh);
    GrabComp->GrabType = EGrabType::Custom;
    // 抓盖子优先于抓瓶身（避免玩家想抓盖子时抓到了瓶子）
    GrabComp->GrabPriority = 1;

    // 默认参数
    DetachTwistAngle     = 45.f;   // 拧 45° 拔出
    ReattachSnapDistance = 8.f;    // 8cm 内自动吸回
    HandGripOffset       = FTransform::Identity;  // 盖子的 pivot 与手柄原点重合
    DetachHaptic         = nullptr;
    DetachSound          = nullptr;
    ReattachSound        = nullptr;

    bIsAttached              = true;
    OwnerBottle              = nullptr;
    CapSocketName            = NAME_None;
    TwistBaselineDegrees     = 0.f;
    bGrabbedButNotDetached   = false;
}

void ABottleCapActor::BeginPlay()
{
    Super::BeginPlay();

    // 注意：UGrabComponent::BeginPlay 会把父组件的碰撞档案强制改成 PhysicsActor，
    // 我们希望盖子盖在瓶口时可被 SphereTrace 命中（用于抓取判定）
    // 但不与瓶身产生刚体碰撞（否则会把瓶子弹飞）。
    // 因此这里再刷回 QueryOnly + Overlap。
    if (CapMesh)
    {
        CapMesh->SetSimulatePhysics(false);
        CapMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        CapMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }

    if (GrabComp)
    {
        GrabComp->OnGrabbed.AddDynamic(this,  &ABottleCapActor::HandleGrabbed);
        GrabComp->OnDropped.AddDynamic(this,  &ABottleCapActor::HandleDropped);
    }
}

void ABottleCapActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 只在"已抓住但尚未拧下来"状态下检测扭转
    if (!bGrabbedButNotDetached || !GrabComp || !OwnerBottle)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    // 计算当前手柄相对瓶口 +Z 的旋转角度，与抓握瞬间的基准值做差得到累计扭转
    const float CurrentDeg = GetHandTwistAngleDegrees(MC);
    float DeltaDeg = CurrentDeg - TwistBaselineDegrees;
    // 归一化到 [-180, 180]
    DeltaDeg = FRotator::NormalizeAxis(DeltaDeg);

    if (FMath::Abs(DeltaDeg) >= DetachTwistAngle)
    {
        DetachFromBottle(MC);
    }
}

//=====================================================================
// 对外接口
//=====================================================================

void ABottleCapActor::AttachToBottle(ABottleActor* InBottle, FName InSocketName)
{
    if (!InBottle)
    {
        return;
    }
    OwnerBottle   = InBottle;
    CapSocketName = InSocketName;

    // 关物理 + 切到 QueryOnly + Overlap（可被抓取射线命中但不推动瓶身），Snap 到瓶口 Socket
    if (CapMesh)
    {
        CapMesh->SetSimulatePhysics(false);
        CapMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        CapMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }
    UStaticMeshComponent* BottleMesh = InBottle->ContainerMesh;
    if (BottleMesh)
    {
        // 关键：Weld 到瓶身刚体上，让盖子成为瓶身刚体的一部分（虽已 QueryOnly 但 Weld 依然是好习惯）
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(BottleMesh, AttachRule, CapSocketName);
    }

    bIsAttached            = true;
    bGrabbedButNotDetached = false;
}

void ABottleCapActor::ReattachToBottle()
{
    if (!OwnerBottle)
    {
        return;
    }

    // 关物理 + 切到 QueryOnly + Overlap，重新 Snap 回瓶口 Socket 并 Weld 到瓶身刚体
    if (CapMesh)
    {
        CapMesh->SetSimulatePhysics(false);
        CapMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        CapMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }
    UStaticMeshComponent* BottleMesh = OwnerBottle->ContainerMesh;
    if (BottleMesh)
    {
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(BottleMesh, AttachRule, CapSocketName);
    }

    bIsAttached            = true;
    bGrabbedButNotDetached = false;

    // 通知瓶子上锁
    OwnerBottle->OnCapAttached();

    // 音效
    if (ReattachSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, ReattachSound, GetActorLocation());
    }
}

void ABottleCapActor::DetachFromBottle(UMotionControllerComponent* MotionController)
{
    if (!MotionController || !bGrabbedButNotDetached)
    {
        return;
    }

    // 1) 从瓶子上脱离并 Attach 到手柄。
    //    使用 SnapToTargetNotIncludingScale：盖子的 Root 相对手柄的 transform 被强制置零，
    //    然后我们再应用 HandGripOffset 让盖子稳定停留在手掌里的期望位置。
    //    这样即使盖子 Mesh 资产 pivot 有偏移，玩家也能看到盖子在手里。
    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
    AttachRule.bWeldSimulatedBodies = true;
    AttachToComponent(MotionController, AttachRule);
    // 应用手柄 Offset
    SetActorRelativeTransform(HandGripOffset);

    if (CapMesh)
    {
        CapMesh->SetSimulatePhysics(false);
        // 拧下后恢复正常碰撞档案，允许盖子与世界物体（桌面、地面等）产生碰撞
        CapMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    }

    bIsAttached            = false;
    bGrabbedButNotDetached = false;

    // 2) 触觉 + 音效
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

    // 3) 通知瓶子解锁倒液
    UE_LOG(LogTemp, Log, TEXT("[BottleCap] DetachFromBottle done. OwnerBottle=%s, calling OnCapDetached..."),
           OwnerBottle ? *OwnerBottle->GetName() : TEXT("NULL"));
    if (OwnerBottle)
    {
        OwnerBottle->OnCapDetached();
        UE_LOG(LogTemp, Log, TEXT("[BottleCap] After OnCapDetached: bCanPour=%d, bIsCapped=%d"),
               OwnerBottle->bCanPour ? 1 : 0, OwnerBottle->bIsCapped ? 1 : 0);
    }
}

//=====================================================================
// 回调
//=====================================================================

void ABottleCapActor::HandleGrabbed()
{
    // Custom 模式下 GrabComponent 不 Attach 也不关物理，我们在这里做"扭转前的准备"：
    // 记录抓取起始时刻手柄相对瓶口的扭转基准角度，进入 Tick 检测阶段。
    if (!GrabComp || !OwnerBottle)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    // 只有在盖着状态下才进入"扭转拔出"流程；若盖子已经掉下来后再次被抓，直接 Attach 到手
    if (bIsAttached)
    {
        TwistBaselineDegrees   = GetHandTwistAngleDegrees(MC);
        bGrabbedButNotDetached = true;
    }
    else
    {
        // 已经不在瓶口上——直接把盖子 Attach 到手（走一遍标准 Snap 逻辑）
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::KeepWorldTransform;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(MC, AttachRule);
        if (CapMesh)
        {
            CapMesh->SetSimulatePhysics(false);
        }
        bGrabbedButNotDetached = false;
    }
}

void ABottleCapActor::HandleDropped()
{
    // 松手后有两种情况：
    //  A. 盖子在瓶口 Socket 附近 → 自动吸回瓶口
    //  B. 盖子远离瓶口 → 作为独立物体掉落（打开物理）
    //
    // 注意 GrabComponent::TryRelease 已经调用了 TrySimulateOnDrop，
    // 但那是在 bSimulateOnDrop 记录基础上执行的。由于 Custom 模式下 PerformGrab
    // 没有关物理，TrySimulateOnDrop 逻辑对我们影响可控；此处再显式接管一次以保证行为清晰。

    bGrabbedButNotDetached = false;

    if (!OwnerBottle)
    {
        // 没有归属瓶子——只能当独立物体落地
        if (CapMesh)
        {
            CapMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            CapMesh->SetSimulatePhysics(true);
        }
        return;
    }

    UStaticMeshComponent* BottleMesh = OwnerBottle->ContainerMesh;
    if (!BottleMesh)
    {
        if (CapMesh)
        {
            CapMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            CapMesh->SetSimulatePhysics(true);
        }
        return;
    }

    // 计算与瓶口 Socket 的世界距离
    const FVector SocketWS = BottleMesh->GetSocketLocation(CapSocketName);
    const float   Dist     = FVector::Dist(GetActorLocation(), SocketWS);

    if (Dist <= ReattachSnapDistance)
    {
        // 在阈值内——自动吸回瓶口
        ReattachToBottle();
    }
    else
    {
        // 远离瓶口——盖子作为独立物体掉落，恢复碰撞并开启物理
        if (CapMesh)
        {
            CapMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            CapMesh->SetSimulatePhysics(true);
        }
    }
}

//=====================================================================
// 工具
//=====================================================================

float ABottleCapActor::GetHandTwistAngleDegrees(UMotionControllerComponent* MC) const
{
    if (!MC || !OwnerBottle || !OwnerBottle->ContainerMesh)
    {
        return 0.f;
    }

    // 瓶口 Socket 的世界 Transform
    UStaticMeshComponent* BottleMesh = OwnerBottle->ContainerMesh;
    const FTransform SocketXform = BottleMesh->GetSocketTransform(CapSocketName, RTS_World);

    // 计算"手柄相对于瓶口 Socket 的局部旋转"
    //   拧瓶盖的物理动作是：手绕手柄自身 +X 轴（手指向方向）自转（Roll）；
    //   而这个 Roll 在瓶口 Socket 局部空间里，正好等价于"绕 Socket +Z 轴的旋转"（前提：
    //   玩家握瓶盖时手前向大致对齐瓶口轴向——这也是最自然的拧盖姿势）。
    //
    // 所以：把手柄的世界 Quat 转到 Socket 局部空间，然后提取绕 Socket +Z（局部 Z 轴）的
    // 旋转分量，就得到真正的"扭转角度"。
    const FQuat HandWS   = MC->GetComponentQuat();
    const FQuat SocketWS = SocketXform.GetRotation();
    const FQuat HandLocal = SocketWS.Inverse() * HandWS;

    // 提取"绕局部 Z 轴的 Swing/Twist 分解 Twist 分量"
    // 参考 Unreal 的 FQuat::ToSwingTwist：先把四元数的向量分量投影到 Twist 轴上，
    // 保留 W 与投影分量，再归一化即可得到"绕该轴的纯旋转四元数"。
    const FVector TwistAxis(0.f, 0.f, 1.f); // Socket 的局部 +Z
    FVector       QVec = FVector(HandLocal.X, HandLocal.Y, HandLocal.Z);
    const float   Proj = FVector::DotProduct(QVec, TwistAxis);
    FQuat         TwistQuat(TwistAxis.X * Proj, TwistAxis.Y * Proj, TwistAxis.Z * Proj, HandLocal.W);
    // 若接近零四元数（罕见）则直接返回 0
    if (TwistQuat.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        return 0.f;
    }
    TwistQuat.Normalize();

    // Quat → 角度（度）。SafeAcos 保证数值稳定。
    // 注意：ToAxisAndAngle 得到的角度总是正的，方向信息藏在 Axis 里。
    FVector Axis;
    float   AngleRad = 0.f;
    TwistQuat.ToAxisAndAngle(Axis, AngleRad);
    AngleRad = FMath::UnwindRadians(AngleRad);

    // 用 Axis 与 TwistAxis 的方向一致性给角度带上符号
    const float Sign = (FVector::DotProduct(Axis, TwistAxis) >= 0.f) ? 1.f : -1.f;
    return FMath::RadiansToDegrees(AngleRad) * Sign;
}
