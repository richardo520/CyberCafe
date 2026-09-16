// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Bean/BeanJarLidActor.h"
#include "Coffee/Bean/BeanJarActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MotionControllerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Sound/SoundBase.h"

ABeanJarLidActor::ABeanJarLidActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 盖子外壳 Mesh 作为 Root
    LidMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LidMesh"));
    SetRootComponent(LidMesh);
    LidMesh->SetSimulatePhysics(false);
    LidMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    LidMesh->SetCollisionResponseToAllChannels(ECR_Overlap);

    // Custom Grab：由本类主动决定"何时真正 Attach 到手上"
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(LidMesh);
    GrabComp->GrabType = EGrabType::Custom;
    GrabComp->GrabPriority = 1;

    DetachPullDistance   = 3.f;
    ReattachSnapDistance = 15.f;
    DetachHaptic         = nullptr;
    DetachSound          = nullptr;
    ReattachSound        = nullptr;

    bIsAttached            = true;
    OwnerJar               = nullptr;
    LidSocketName          = NAME_None;
    bGrabbedButNotDetached = false;
    bGrabInPlace           = false;
    bDetachedInPlace       = false;
    GrabbedRelativeToHand  = FTransform::Identity;
}

void ABeanJarLidActor::BeginPlay()
{
    Super::BeginPlay();

    // UGrabComponent::BeginPlay 会把父组件碰撞档案改成 PhysicsActor，
    // 我们希望盖在罐口时可被 SphereTrace 命中但不撞飞罐身 → 刷回 QueryOnly + Overlap
    if (LidMesh)
    {
        LidMesh->SetSimulatePhysics(false);
        LidMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        LidMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }

    if (GrabComp)
    {
        GrabComp->OnGrabbed.AddDynamic(this, &ABeanJarLidActor::HandleGrabbed);
        GrabComp->OnDropped.AddDynamic(this, &ABeanJarLidActor::HandleDropped);
    }
}

void ABeanJarLidActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 原地控制：盖已拔下但未 Attach 到手柄 → 每帧把手柄位姿应用到盖子上
    if (bDetachedInPlace && !bIsAttached && GrabComp && GrabComp->IsHeld())
    {
        if (UMotionControllerComponent* HandMC = GrabComp->GetHoldingController())
        {
            const FTransform HandXform(HandMC->GetComponentQuat(), HandMC->GetComponentLocation());
            const FTransform TargetWorld = GrabbedRelativeToHand * HandXform;
            SetActorLocationAndRotation(TargetWorld.GetLocation(), TargetWorld.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
        }
    }

    // 只有"抓住但还没拔下"状态需要 Tick 里做距离检测
    if (!bGrabbedButNotDetached || !GrabComp || !OwnerJar)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    // 用罐子的 JarMesh + LidSocketName 得到罐口位置（避免在头文件里前置声明罐子的成员）
    // 拿 Actor 的世界位置也可作为近似，但 Socket 精度更好
    UStaticMeshComponent* JarMesh = OwnerJar->GetJarMesh();
    if (!JarMesh)
    {
        return;
    }
    const FVector SocketWS = JarMesh->GetSocketLocation(LidSocketName);
    const float HandToSocketDist = FVector::Dist(MC->GetComponentLocation(), SocketWS);
    if (HandToSocketDist >= DetachPullDistance)
    {
        DetachFromJar(MC);
    }
}

//=====================================================================
// 对外接口
//=====================================================================

void ABeanJarLidActor::AttachToJar(ABeanJarActor* InJar, FName InSocketName)
{
    if (!InJar)
    {
        return;
    }
    OwnerJar      = InJar;
    LidSocketName = InSocketName;

    if (LidMesh)
    {
        LidMesh->SetSimulatePhysics(false);
        LidMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        LidMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }

    UStaticMeshComponent* JarMesh = InJar->GetJarMesh();
    if (JarMesh)
    {
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(JarMesh, AttachRule, LidSocketName);
    }

    bIsAttached            = true;
    bGrabbedButNotDetached = false;

    // 盖在罐口时禁止远程抓取
    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = false;
    }
}

void ABeanJarLidActor::ReattachToJar()
{
    if (!OwnerJar)
    {
        return;
    }

    if (LidMesh)
    {
        LidMesh->SetSimulatePhysics(false);
        LidMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        LidMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
    }

    UStaticMeshComponent* JarMesh = OwnerJar->GetJarMesh();
    if (JarMesh)
    {
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(JarMesh, AttachRule, LidSocketName);
    }

    bIsAttached            = true;
    bGrabbedButNotDetached = false;

    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = false;
    }

    if (ReattachSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, ReattachSound, GetActorLocation());
    }
}

void ABeanJarLidActor::DetachFromJar(UMotionControllerComponent* MotionController)
{
    if (!MotionController || !bGrabbedButNotDetached)
    {
        return;
    }

    // 先从罐上脱离（保持世界位姿）
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    if (LidMesh)
    {
        LidMesh->SetSimulatePhysics(false);
        // 拔下后恢复正常碰撞档案，可以撞桌面/地板
        LidMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    }

    if (bGrabInPlace)
    {
        // 原地控制：不 Attach 到手柄，记住"盖相对手"当前变换，Tick 里驱动盖子位姿
        const FTransform HandXform(MotionController->GetComponentQuat(), MotionController->GetComponentLocation());
        const FTransform LidXform  = GetActorTransform();
        GrabbedRelativeToHand = LidXform.GetRelativeTransform(HandXform);
        bDetachedInPlace = true;
    }
    else
    {
        // 默认：Attach 到手柄
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
        AttachRule.bWeldSimulatedBodies = true;
        AttachToComponent(MotionController, AttachRule);
        bDetachedInPlace = false;
    }

    bIsAttached            = false;
    bGrabbedButNotDetached = false;

    // 拔下后允许远程抓取（跟瓶盖一致）
    if (GrabComp)
    {
        GrabComp->bAllowRemoteGrab = true;
    }

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

//=====================================================================
// 回调
//=====================================================================

void ABeanJarLidActor::HandleGrabbed()
{
    if (!GrabComp || !OwnerJar)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    if (bIsAttached)
    {
        bGrabbedButNotDetached = true;
    }
    else
    {
        // 已经不在罐口——根据模式选 Attach 到手 或 原地控制
        if (bGrabInPlace)
        {
            DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
            if (LidMesh)
            {
                LidMesh->SetSimulatePhysics(false);
            }
            const FTransform HandXform(MC->GetComponentQuat(), MC->GetComponentLocation());
            const FTransform LidXform  = GetActorTransform();
            GrabbedRelativeToHand = LidXform.GetRelativeTransform(HandXform);
            bDetachedInPlace = true;
        }
        else
        {
            // 直接 Attach 到手
            FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
            AttachRule.bWeldSimulatedBodies = true;
            AttachToComponent(MC, AttachRule);
            if (LidMesh)
            {
                LidMesh->SetSimulatePhysics(false);
            }
            bDetachedInPlace = false;
        }
        bGrabbedButNotDetached = false;
    }
}

void ABeanJarLidActor::HandleDropped()
{
    bGrabbedButNotDetached = false;
    // 松手后不再追随手柄，重置原地控制标志
    bDetachedInPlace = false;

    if (!OwnerJar)
    {
        if (LidMesh)
        {
            LidMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            LidMesh->SetSimulatePhysics(true);
        }
        return;
    }

    UStaticMeshComponent* JarMesh = OwnerJar->GetJarMesh();
    if (!JarMesh)
    {
        if (LidMesh)
        {
            LidMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            LidMesh->SetSimulatePhysics(true);
        }
        return;
    }

    const FVector SocketWS = JarMesh->GetSocketLocation(LidSocketName);
    const float Dist = FVector::Dist(GetActorLocation(), SocketWS);
    if (Dist <= ReattachSnapDistance)
    {
        ReattachToJar();
    }
    else
    {
        // 随便丢：盖子作为独立物体掉落
        if (LidMesh)
        {
            LidMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
            LidMesh->SetSimulatePhysics(true);
        }
    }
}
