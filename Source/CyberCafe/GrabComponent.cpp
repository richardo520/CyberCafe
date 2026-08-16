// Fill out your copyright notice in the Description page of Project Settings.

#include "GrabComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MotionControllerComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Animation/AnimInstance.h"

UGrabComponent::UGrabComponent()
{
    // 该组件不需要每帧Tick，抓取状态维护通过附着/物理系统完成
    PrimaryComponentTick.bCanEverTick = false;

    // 默认配置
    GrabType = EGrabType::Free;
    HandSocket = NAME_None;
    HandAnimLayer = nullptr;
    bCaptureHand = false;
    OnGrabHapticEffect = nullptr;

    // 运行时状态
    bIsHeld = false;
    bIsPulled = false;
    bSimulateOnDrop = false;
    MotionControllerRef = nullptr;
    PrimaryGrabComponent = nullptr;
    PrimaryGrabRelativeRotation = FRotator::ZeroRotator;
    CachedHandLocationTransform = FTransform::Identity;
}

void UGrabComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetAttachParent()->IsAnySimulatingPhysics())
    {
        bSimulateOnDrop = true;
    }

    if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(GetAttachParent()))
    {
        PrimitiveComponent->SetCollisionProfileName("PhysicsActor",true);
    }
}

void UGrabComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

UPrimitiveComponent* UGrabComponent::GetOwnerPrimitive() const
{
    if (const AActor* OwnerActor = GetOwner())
    {
        // 优先使用RootComponent作为可交互目标
        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
        {
            return RootPrim;
        }
    }
    return nullptr;
}

//===========================================================================
// 抓取/释放核心
//===========================================================================

bool UGrabComponent::TryGrab(UMotionControllerComponent* MotionController)
{
    // 参数与状态校验
    if (MotionController == nullptr || GrabType == EGrabType::None)
    {
        return false;
    }

    // 先尝试释放当前抓取的物体
    
    if (!bIsHeld || TryRelease())
    {
        // 内部执行抓取动作
        PerformGrab(MotionController);

        if (bIsHeld)
        {
            StopPull();
            MotionControllerRef = MotionController;
            TryCaptureHandMesh();

            // 播放触觉反馈
            if (OnGrabHapticEffect)
            {
                if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
                {
                    const EControllerHand Hand = GetHeldByHand();
                    PC->PlayHapticEffect(OnGrabHapticEffect, Hand);
                }
            }
            OnGrabbed.Broadcast();
            return true;
        }
    }
    return false;
}

void UGrabComponent::PerformGrab(UMotionControllerComponent* MotionController)
{
    
    switch (GrabType)
    {
    case EGrabType::Free:
    {
        // 保持相对手部当前偏移，直接使用KeepWorld规则附着
        SetPrimitiveCompPhysics(false);
        AttachParentToMotionController(MotionController);
        bIsHeld = true;
        break;
    }
    case EGrabType::Snap:
    {
        // 吸附到手部原点，使用本GrabComponent自身的相对变换作为吸附偏移
        SetPrimitiveCompPhysics(false);
        AttachParentToMotionController(MotionController);
        bIsHeld = true;
        GetAttachParent()->SetRelativeRotation(GetRelativeRotation().GetInverse(), false, nullptr,ETeleportType::TeleportPhysics);
            
        FVector3d ComponentLocation = GetComponentLocation();
        FVector3d OwnerLocation = GetAttachParent()->GetComponentLocation();
        FVector3d MotionControllerLocation = MotionController->GetComponentLocation();
        FVector3d Offset = MotionControllerLocation - (ComponentLocation - OwnerLocation);
        GetAttachParent()->SetWorldLocation(Offset, false, nullptr,ETeleportType::TeleportPhysics);
        break;
    }
    case EGrabType::SnapInPlace:
    {
        // 保持在世界中的位置不变，仅登记为已被抓取（例如固定安装的武器）
        break;
    }
    case EGrabType::Custom:
    {
        // 交由外部逻辑处理，此处仅登记状态并广播事件
        bIsHeld = true;
        break;
    }
    default:
        break;
    }
}

bool UGrabComponent::TryRelease()
{
    switch (GrabType)
    {
    case EGrabType::Free:
    case EGrabType::Snap:
    {
        TrySimulateOnDrop();
        bIsHeld = false;
        break;
    }
    case EGrabType::SnapInPlace:
    case EGrabType::Custom:
    default:
        bIsHeld = false;
        break;
    }

    if (bIsHeld)
    {
        return false;
    }
    
    // 释放手部Mesh
    TryReleaseHandMesh();
    OnDropped.Broadcast();
    return true;
}

//===========================================================================
// 拉拽（Pull）
//===========================================================================

bool UGrabComponent::TryPull()
{
    if (GetOwner())
    {
        TArray<UGrabComponent*> GrabComponents;
        GetOwner()->GetComponents<UGrabComponent>(GrabComponents);
        for (UGrabComponent* GrabComponent : GrabComponents)
        {
            if (GrabComponent->bIsPulled)
            {
                return false;
            }
        }
    }

    bIsPulled = true;
    SetPrimitiveCompPhysics(false);
    return true;
}

void UGrabComponent::StopPull()
{
    if (bIsPulled)
    {
        bIsPulled = false;   
    }

    if (!bIsHeld)
    {
        TrySimulateOnDrop();
    }
}

//===========================================================================
// 手部Mesh接管
//===========================================================================

bool UGrabComponent::TryFindHandMeshOnController() 
{
    if (MotionControllerRef == nullptr)
    {
        return false;
    }

    // 遍历MotionController的子组件寻找SkeletalMeshComponent
    TArray<USceneComponent*> Children;
    MotionControllerRef->GetChildrenComponents(true, Children);
    for (USceneComponent* Child : Children)
    {
        if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Child))
        {
            HandMesh = SkelMesh;
            return true;
        }
    }
    return false;
}

bool UGrabComponent::TryCaptureHandMesh()
{
    if (!TryFindHandMeshOnController())
    {
        return false;
    }

    // 若配置了动画层，则链接（对应蓝图节点 LinkAnimClassLayers）
    if (HandAnimLayer)
    {
        HandMesh->LinkAnimClassLayers(HandAnimLayer);
    }

    // 若配置了HandSocket，则将手部Mesh附着到本组件所在Actor的对应Socket

    if (bCaptureHand)
    {
        CachedHandLocationTransform = HandMesh->GetRelativeTransform();
        FString AttachHandSocket = HandSocket.ToString();
        if (GetHeldByHand() == EControllerHand::Left)
        {
            AttachHandSocket = HandSocket.ToString() + "_Inverse";
        }
        
        HandMesh->AttachToComponent(GetAttachParent(), FAttachmentTransformRules::SnapToTargetIncludingScale, *AttachHandSocket);
    }

    return true;
}

void UGrabComponent::TryReleaseHandMesh()
{
    if (HandMesh)
    {
        if(IsValid(HandAnimLayer))
        {
            HandMesh->UnlinkAnimClassLayers(HandAnimLayer);
        }

        if (bCaptureHand)
        {
            HandMesh->AttachToComponent(MotionControllerRef, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
            HandMesh->SetRelativeTransform(CachedHandLocationTransform);
        }   
    }
}

//===========================================================================
// 附加 & 物理辅助
//===========================================================================

void UGrabComponent::AttachParentToMotionController(UMotionControllerComponent* MotionController)
{
    if (MotionController == nullptr)
    {
        return;
    }

    // 将所属Actor的Root（GetAttachParent即Root）附加到MotionController
    if (GetAttachParent())
    {
        FAttachmentTransformRules AttachmentRule = FAttachmentTransformRules::KeepWorldTransform;
        AttachmentRule.bWeldSimulatedBodies = true;
        bool bSuccess = GetAttachParent()->AttachToComponent(MotionController, AttachmentRule);
        UE_LOG(LogTemp, Warning, TEXT("Attached? Parent=%s, NewAttachParent=%s, SimPhysics=%d"),
        *GetAttachParent()->GetName(),
        GetAttachParent()->GetAttachParent() ? *GetAttachParent()->GetAttachParent()->GetName() : TEXT("NULL"),
        Cast<UPrimitiveComponent>(GetAttachParent()) ? Cast<UPrimitiveComponent>(GetAttachParent())->IsSimulatingPhysics() : -1);
    }
}

void UGrabComponent::SetPrimitiveCompPhysics(bool bSimulate)
{
    if (UPrimitiveComponent* OwnerPrim = GetOwnerPrimitive())
    {
        OwnerPrim->SetSimulatePhysics(bSimulate);
    }
}

void UGrabComponent::SetSimulatingParent(bool bSimulate)
{
    // 沿着附着链向上找到最顶层的Primitive组件并设置物理模拟
    USceneComponent* Cur = GetAttachParent();
    while (Cur)
    {
        if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Cur))
        {
            Prim->SetSimulatePhysics(bSimulate);
        }
        Cur = Cur->GetAttachParent();
    }
}

void UGrabComponent::TrySimulateOnDrop()
{
    if (bSimulateOnDrop)
    {
        SetPrimitiveCompPhysics(true);
    }
    else
    {
        if (GetAttachParent())
        {
            GetAttachParent()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        }
    }
}

//===========================================================================
// 查询
//===========================================================================

EControllerHand UGrabComponent::GetHeldByHand() const
{
    if (MotionControllerRef == nullptr)
    {
        return EControllerHand::AnyHand;
    }
    // UMotionControllerComponent::MotionSource 是FName，对应"Left"/"Right"/"LeftGrip"/"RightGrip"等
    const FName Source = MotionControllerRef->MotionSource;
    const FString SourceStr = Source.ToString();
    if (SourceStr.Contains(TEXT("Left")))
    {
        return EControllerHand::Left;
    }
    if (SourceStr.Contains(TEXT("Right")))
    {
        return EControllerHand::Right;
    }
    return EControllerHand::AnyHand;
}