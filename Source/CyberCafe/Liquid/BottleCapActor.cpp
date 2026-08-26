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
    CapMesh->SetCollisionProfileName(TEXT("PhysicsActor"));

    // 抓取组件：Custom 模式，不由 GrabComponent 自动 Attach 到手上，扭转达到阈值后由本类主动 Attach
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(CapMesh);
    GrabComp->GrabType = EGrabType::Custom;
    // 抓盖子优先于抓瓶身（避免玩家想抓盖子时抓到了瓶子）
    GrabComp->GrabPriority = 1;

    // 默认参数
    DetachTwistAngle     = 45.f;   // 拧 45° 拔出
    ReattachSnapDistance = 8.f;    // 8cm 内自动吸回
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

    // 关物理并 Snap 到瓶口 Socket
    if (CapMesh)
    {
        CapMesh->SetSimulatePhysics(false);
    }
    UStaticMeshComponent* BottleMesh = InBottle->ContainerMesh;
    if (BottleMesh)
    {
        AttachToComponent(BottleMesh,
                          FAttachmentTransformRules::SnapToTargetNotIncludingScale,
                          CapSocketName);
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

    // 关物理，重新 Snap 回瓶口 Socket
    if (CapMesh)
    {
        CapMesh->SetSimulatePhysics(false);
    }
    UStaticMeshComponent* BottleMesh = OwnerBottle->ContainerMesh;
    if (BottleMesh)
    {
        AttachToComponent(BottleMesh,
                          FAttachmentTransformRules::SnapToTargetNotIncludingScale,
                          CapSocketName);
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

    // 1) 从瓶子上脱离并 Attach 到手柄（保持世界 Transform，避免瞬移）
    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::KeepWorldTransform;
    AttachRule.bWeldSimulatedBodies = true;
    AttachToComponent(MotionController, AttachRule);

    if (CapMesh)
    {
        CapMesh->SetSimulatePhysics(false);
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
    if (OwnerBottle)
    {
        OwnerBottle->OnCapDetached();
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
            CapMesh->SetSimulatePhysics(true);
        }
        return;
    }

    UStaticMeshComponent* BottleMesh = OwnerBottle->ContainerMesh;
    if (!BottleMesh)
    {
        if (CapMesh)
        {
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
        // 远离瓶口——盖子掉落
        if (CapMesh)
        {
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

    // 手柄前向投影到瓶口局部 XY 平面（垂直于瓶口 +Z）
    const FVector SocketUp   = SocketXform.GetUnitAxis(EAxis::Z);
    const FVector SocketX    = SocketXform.GetUnitAxis(EAxis::X);
    const FVector SocketY    = SocketXform.GetUnitAxis(EAxis::Y);
    const FVector HandFwdWS  = MC->GetForwardVector();

    // 把手柄前向投影到瓶口 XY 平面上
    const FVector HandFwdOnPlane = FVector::VectorPlaneProject(HandFwdWS, SocketUp).GetSafeNormal();
    if (HandFwdOnPlane.IsNearlyZero())
    {
        return 0.f;
    }

    // 计算投影向量在 Socket 局部坐标系下的 Yaw
    const float LocalX = FVector::DotProduct(HandFwdOnPlane, SocketX);
    const float LocalY = FVector::DotProduct(HandFwdOnPlane, SocketY);
    const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(LocalY, LocalX));
    return AngleDeg;
}
