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
    DetachPullDistance   = 3.f;    // 抓住后拉 3cm 即拔出
    ReattachSnapDistance = 15.f;   // 松手时若在此距离内自动吸回瓶口
    DetachHaptic         = nullptr;
    DetachSound          = nullptr;
    ReattachSound        = nullptr;

    bIsAttached              = true;
    OwnerBottle              = nullptr;
    CapSocketName            = NAME_None;
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

    // 仅在"已抓住但尚未拔下来"状态下检测"往外拉"距离，超过阈值就拔出。
    // 知道盖子已拔下后的一切盖回逻辑都在 HandleDropped 里完成。
    if (!bGrabbedButNotDetached || !GrabComp || !OwnerBottle || !OwnerBottle->ContainerMesh)
    {
        return;
    }

    UMotionControllerComponent* MC = GrabComp->GetHoldingController();
    if (!MC)
    {
        return;
    }

    UStaticMeshComponent* BottleMesh = OwnerBottle->ContainerMesh;
    const FVector SocketWS = BottleMesh->GetSocketLocation(CapSocketName);
    const float HandToSocketDist = FVector::Dist(MC->GetComponentLocation(), SocketWS);
    if (HandToSocketDist >= DetachPullDistance)
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
    //    盖子会固定在手柄原点上（盖子 Mesh 的 pivot 已在编辑器里调好）。
    FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
    AttachRule.bWeldSimulatedBodies = true;
    AttachToComponent(MotionController, AttachRule);

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
    // Custom 模式下 GrabComponent 不 Attach 也不关物理，我们在这里做"拔出前的准备"：
    // 只有在盖着状态下才进入"拉出拔盖"流程；若盖子已经掉下来后再次被抓，直接 Attach 到手
    if (!GrabComp || !OwnerBottle)
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
        // 进入"抓住但还没拔下"状态；Tick 里检测手柄距瓶口 Socket 的距离，
        // 一旦超过 DetachPullDistance，就在 DetachFromBottle 里正式 Attach 到手上。
        bGrabbedButNotDetached = true;
    }
    else
    {
        // 已经不在瓶口上——直接把盖子 Attach 到手（走一遍标准 Snap 逻辑）
        FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
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

// （旧 GetHandTwistAngleDegrees 已移除：现在用位移触发拔盖，不再检测扭转角度）
