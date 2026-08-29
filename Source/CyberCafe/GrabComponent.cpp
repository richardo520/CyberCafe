// Fill out your copyright notice in the Description page of Project Settings.

#include "GrabComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MotionControllerComponent.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Animation/AnimInstance.h"

UGrabComponent::UGrabComponent()
{
    // 启用Tick：仅在Held期间用于采样手柄的位置/旋转，估算释放瞬间的线/角速度
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

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

    // 仅在被持有且有有效手柄时采样：每帧 Push 一个样本到环形缓冲，
    // 供 TryRelease 计算"峰值 + 加权平均"的投掷速度。
    if (bIsHeld && MotionControllerRef)
    {
        const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

        FThrowSample Sample;
        Sample.Time     = Now;
        Sample.Location = MotionControllerRef->GetComponentLocation();
        Sample.Rotation = MotionControllerRef->GetComponentQuat();
        ThrowSamples.Add(Sample);

        // 修剪：只保留最近 ThrowSampleWindow 秒内的样本
        const double MinTime = Now - static_cast<double>(ThrowSampleWindow);
        int32 RemoveCount = 0;
        while (RemoveCount < ThrowSamples.Num() && ThrowSamples[RemoveCount].Time < MinTime)
        {
            ++RemoveCount;
        }
        // 至少2个样本（哪怕在窗口之外），保证释放时能算出速度
        if (RemoveCount > 0 && ThrowSamples.Num() - RemoveCount >= 2)
        {
            ThrowSamples.RemoveAt(0, RemoveCount, EAllowShrinking::No);
        }
    }

    // PhysicsHandle 模式：
    //   1) 每帧将 PhysicsHandle 的目标位姿更新为"手柄世界变换 * 抓取瞬间的相对变换"
    //      → 保持抓取时的相对位姿，手柄移动/旋转时物体被弹簧拉着跟
    //   2) 若物体被环境卡住、手柄拉得太远，自动释放，避免弹簧无限拉伸
    if (bIsHeld && GrabPhysicsHandle && MotionControllerRef)
    {
        // Step 1: 更新目标位姿
        const FTransform CtrlWorld(MotionControllerRef->GetComponentQuat(), MotionControllerRef->GetComponentLocation());
        const FTransform TargetWorld = HeldRelativeToController * CtrlWorld;
        GrabPhysicsHandle->SetTargetLocationAndRotation(TargetWorld.GetLocation(), TargetWorld.Rotator());

        // Step 2: 距离 break 检查
        if (BreakDistance > 0.f)
        {
            if (UPrimitiveComponent* Prim = GetOwnerPrimitive())
            {
                const float DistSq = FVector::DistSquared(
                    Prim->GetComponentLocation(),
                    MotionControllerRef->GetComponentLocation());
                if (DistSq > BreakDistance * BreakDistance)
                {
                    // 主动释放（会走 TryRelease 里拆 PhysicsHandle + 派发 OnDropped）
                    TryRelease();
                }
            }
        }
    }
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

            // 打开投掷采样：清空环形缓冲，从下一帧 Tick 开始记录
            ThrowSamples.Reset();
            SetComponentTickEnabled(true);

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
    // PhysicsHandle 模式仅对 Free / Snap 生效；SnapInPlace / Custom 保持原有行为
    const bool bUsePhysHandle =
        (PhysicsMode == EGrabPhysicsMode::PhysicsHandle) &&
        (GrabType == EGrabType::Free || GrabType == EGrabType::Snap);

    // === 关键：Pull 阶段 Kinematic 位置差会被 Chaos 记为初速度，切到 Dynamic 会"甩"出去 ===
    // 所以对所有走 PhysicsHandle 分支的情况，抓取前先强制把物体 Teleport 到当前位置（清除位置差历史）
    if (bUsePhysHandle)
    {
        if (UPrimitiveComponent* Prim = GetOwnerPrimitive())
        {
            const FVector CurLoc = Prim->GetComponentLocation();
            const FQuat   CurRot = Prim->GetComponentQuat();
            Prim->SetWorldLocationAndRotation(CurLoc, CurRot, false, nullptr, ETeleportType::TeleportPhysics);
        }
    }

    switch (GrabType)
    {
    case EGrabType::Free:
    {
        if (bUsePhysHandle)
        {
            // 保持物理开启，不 Attach，仅创建 PhysicsHandle 驱动物体追随手柄
            SetupPhysicsHandle(MotionController);
            bIsHeld = true;
        }
        else
        {
            // 保持相对手部当前偏移，直接使用KeepWorld规则附着
            SetPrimitiveCompPhysics(false);
            AttachParentToMotionController(MotionController);
            bIsHeld = true;
        }
        break;
    }
    case EGrabType::Snap:
    {
        if (bUsePhysHandle)
        {
            // PhysicsHandle + Snap：先创建 PhysicsHandle，然后将 HeldRelativeToController 覆盖为
            // "GrabComponent 相对 Owner 的本地变换的逆"，这样弹簧会把物体上的抓握锚点吸到手柄。
            SetupPhysicsHandle(MotionController);
            // 覆盖为 Snap 目标：物体相对手柄的目标变换 = GrabComponent 本地变换的逆
            // 推导：希望 Grab世界 == Ctrl世界，即 Obj世界 * GrabLocal == Ctrl世界，所以 Obj世界 == Ctrl世界 * GrabLocal⁻¹
            // 转为相对：Obj世界 * Ctrl世界⁻¹ = GrabLocal⁻¹
            HeldRelativeToController = GetRelativeTransform().Inverse();
            bIsHeld = true;
        }
        else
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
        }
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
    // 释放前，先把当前的手柄位姿作为"最后一帧"补录到采样缓冲，
    // 这样即使 Tick 与 TryRelease 之间还有半帧偏差也能覆盖到。
    // 注意：只补录不清空，真正的速度计算在下面。
    const double NowTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (MotionControllerRef)
    {
        FThrowSample Sample;
        Sample.Time     = NowTime;
        Sample.Location = MotionControllerRef->GetComponentLocation();
        Sample.Rotation = MotionControllerRef->GetComponentQuat();
        ThrowSamples.Add(Sample);
    }

    // 拷贝一份采样出来，等下切物理之后再用（先切物理再Set速度）
    TArray<FThrowSample> SamplesForThrow = ThrowSamples;

    // 先拆除 PhysicsHandle 模式下的抓取（如果有）
    // 注意：必须在 TrySimulateOnDrop 之前拆，否则 Detach 时会与 PhysicsHandle 产生冲突。
    const bool bWasPhysHandle = (GrabPhysicsHandle != nullptr);
    if (bWasPhysHandle)
    {
        TeardownPhysicsHandle();
    }

    switch (GrabType)
    {
    case EGrabType::Free:
    case EGrabType::Snap:
    {
        if (bWasPhysHandle)
        {
            // PhysicsHandle 模式下本就开着物理，无需 TrySimulateOnDrop 重新开物理
            // CCD 与重力开关已在 TeardownPhysicsHandle 里恢复
            bIsHeld = false;
        }
        else
        {
            TrySimulateOnDrop();
            bIsHeld = false;
        }
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

    // === 投掷手感：完整方案（时间窗口 + 峰值过滤 + 加权平均） ===
    if (SamplesForThrow.Num() >= 2)
    {
        if (UPrimitiveComponent* Prim = GetOwnerPrimitive())
        {
            if (Prim->IsSimulatingPhysics())
            {
                FVector LinearVel = FVector::ZeroVector;
                FVector AngularVel = FVector::ZeroVector;
                if (ComputeThrowVelocities(SamplesForThrow, LinearVel, AngularVel))
                {
                    Prim->SetPhysicsLinearVelocity(LinearVel * ThrowVelocityScale, false);
                    Prim->SetPhysicsAngularVelocityInRadians(AngularVel * ThrowAngularVelocityScale, false);
                }
            }
        }
    }

    // 关闭采样
    SetComponentTickEnabled(false);
    ThrowSamples.Reset();

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

//===========================================================================
// 投掷速度计算：时间窗口 + 峰值过滤 + 加权平均
//===========================================================================

bool UGrabComponent::ComputeThrowVelocities(const TArray<FThrowSample>& Samples, FVector& OutLinearVel, FVector& OutAngularVel) const
{
    OutLinearVel  = FVector::ZeroVector;
    OutAngularVel = FVector::ZeroVector;

    const int32 N = Samples.Num();
    if (N < 2)
    {
        return false;
    }

    // Step 1: 相邻两两差分，得到 N-1 段瞬时速度
    struct FSegment
    {
        double  MidTime;      // 段中点时间（用于峰值定位）
        FVector LinearVel;    // 段线速度
        FVector AngularVel;   // 段角速度（rad/s）
        float   Speed;        // 线速度模长（用于峰值检测）
    };

    TArray<FSegment> Segments;
    Segments.Reserve(N - 1);

    for (int32 i = 1; i < N; ++i)
    {
        const FThrowSample& A = Samples[i - 1];
        const FThrowSample& B = Samples[i];

        const double Dt = B.Time - A.Time;
        if (Dt <= (double)SMALL_NUMBER)
        {
            continue;
        }
        const float DtF = static_cast<float>(Dt);

        FSegment Seg;
        Seg.MidTime   = 0.5 * (A.Time + B.Time);
        Seg.LinearVel = (B.Location - A.Location) / DtF;

        // 角速度：Delta 四元数 -> AxisAngle -> 归一到 [-PI, PI] -> /dt
        FQuat DeltaQuat = B.Rotation * A.Rotation.Inverse();
        DeltaQuat.EnforceShortestArcWith(FQuat::Identity); // 强制走最短弧，避免绕远路
        FVector Axis; float Angle;
        DeltaQuat.ToAxisAndAngle(Axis, Angle);
        if (Angle > PI)
        {
            Angle -= 2.f * PI;
        }
        Seg.AngularVel = Axis * (Angle / DtF);
        Seg.Speed      = Seg.LinearVel.Size();

        Segments.Add(Seg);
    }

    if (Segments.Num() == 0)
    {
        return false;
    }

    // Step 2: 峰值检测 —— 找到速度模长最大的段作为"投掷意图峰值"
    int32 PeakIdx = 0;
    if (bUsePeakDetection)
    {
        for (int32 i = 1; i < Segments.Num(); ++i)
        {
            if (Segments[i].Speed > Segments[PeakIdx].Speed)
            {
                PeakIdx = i;
            }
        }
    }
    else
    {
        // 不启用峰值检测时，把"峰值"设为最后一段，等价于对整个窗口做加权平均
        PeakIdx = Segments.Num() - 1;
    }

    const double PeakTime = Segments[PeakIdx].MidTime;

    // Step 3: 加权平均
    // 只考虑 [PeakTime - Radius, PeakTime + Radius] 内的段（默认丢弃松手瞬间的减速尾巴）；
    // 权重 = 1 - |t - PeakTime| / Radius，越靠近峰值越大。
    // 若关闭峰值检测，则对所有段做以"最新时间"为峰值的线性衰减加权。
    const double Radius = bUsePeakDetection
        ? static_cast<double>(ThrowPeakWindowRadius)
        : static_cast<double>(ThrowSampleWindow);

    FVector SumLinear  = FVector::ZeroVector;
    FVector SumAngular = FVector::ZeroVector;
    double  SumWeight  = 0.0;

    for (int32 i = 0; i < Segments.Num(); ++i)
    {
        const double Dist = FMath::Abs(Segments[i].MidTime - PeakTime);
        if (Dist > Radius)
        {
            continue; // 峰值窗口外，丢弃（这也顺带过滤了末端反向减速的样本）
        }

        const double Weight = 1.0 - (Dist / Radius); // [0, 1]
        SumLinear  += Segments[i].LinearVel  * Weight;
        SumAngular += Segments[i].AngularVel * Weight;
        SumWeight  += Weight;
    }

    if (SumWeight <= (double)SMALL_NUMBER)
    {
        // Fallback：直接用峰值段
        OutLinearVel  = Segments[PeakIdx].LinearVel;
        OutAngularVel = Segments[PeakIdx].AngularVel;
        return true;
    }

    OutLinearVel  = SumLinear  / SumWeight;
    OutAngularVel = SumAngular / SumWeight;
    return true;
}

//===========================================================================
// PhysicsHandle：使用引擎内置 UPhysicsHandleComponent 实现可碰撞抓取
//===========================================================================

void UGrabComponent::SetupPhysicsHandle(UMotionControllerComponent* MotionController)
{
    if (MotionController == nullptr)
    {
        return;
    }

    UPrimitiveComponent* GrabbedPrim = GetOwnerPrimitive();
    if (GrabbedPrim == nullptr)
    {
        return;
    }

    // 抓取前若有残留 PhysicsHandle，先清理
    if (GrabPhysicsHandle)
    {
        TeardownPhysicsHandle();
    }

    // ---- Step 1: 先开物理，再清速度，再唤醒（顺序敏感）----
    // SetSimulatePhysics(true) 从 Kinematic 切到 Dynamic 时，Chaos 会用最近记录的位置差推算初速度。
    // Pull 阶段每帧 SetActorLocation(TeleportPhysics) 快速拖动物体，此位置差被记录后会变成伪初速度。
    // 所以必须在 SetSimulatePhysics 之后立即 Set 速度为 0，覆盖这个残留。
    GrabbedPrim->SetSimulatePhysics(true);
    GrabbedPrim->SetPhysicsLinearVelocity(FVector::ZeroVector, false);
    GrabbedPrim->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
    GrabbedPrim->WakeAllRigidBodies();

    // 【诊断日志】输出物体质量，方便调参时判断"不跟手"是否因为重量过大
    UE_LOG(LogTemp, Log, TEXT("[Grab] Grabbed '%s' Mass=%.2fkg  LinStiff=%.1f LinDamp=%.1f AngStiff=%.1f AngDamp=%.1f  DisableGravity=%d"),
        *GrabbedPrim->GetName(),
        GrabbedPrim->GetMass(),
        PhysicsHandleLinearStiffness, PhysicsHandleLinearDamping,
        PhysicsHandleAngularStiffness, PhysicsHandleAngularDamping,
        bDisableGravityWhileHeld ? 1 : 0);

    // 按需禁用重力（避免弹簧驱动被重力拖住，重物也能轻盈跟手）
    bCachedEnableGravity = GrabbedPrim->IsGravityEnabled();
    if (bDisableGravityWhileHeld)
    {
        GrabbedPrim->SetEnableGravity(false);
    }

    // 缓存并按需开启 CCD（快速挥动时防止穿墙）
    if (FBodyInstance* Body = GrabbedPrim->GetBodyInstance())
    {
        bCachedUseCCD = Body->bUseCCD;
        if (bUseCCDWhileHeld)
        {
            Body->bUseCCD = true;
        }
    }

    // 抓取期间让被抓物体 与 VRPawn 不产生物理接触（只使用 MoveIgnoreActors，不影响它与环境、其他物体的碰撞）
    if (AActor* PawnOwner = MotionController->GetOwner())
    {
        GrabbedPrim->IgnoreActorWhenMoving(PawnOwner, true);
    }

    // ---- Step 2: 记录抓取瞬间物体相对手柄的位姿（Free 默认； Snap 会在 PerformGrab 中覆盖）----
    // Tick 中每帧计算 TargetWorld = HeldRelativeToController * CtrlWorld，作为 PhysicsHandle 的目标
    {
        const FTransform ObjWorld = GrabbedPrim->GetComponentTransform();
        const FTransform CtrlWorld(MotionController->GetComponentQuat(), MotionController->GetComponentLocation());
        HeldRelativeToController = ObjWorld.GetRelativeTransform(CtrlWorld);
    }

    // ---- Step 3: 动态创建 PhysicsHandleComponent 并配参 ----
    GrabPhysicsHandle = NewObject<UPhysicsHandleComponent>(this, NAME_None, RF_Transient);
    if (GrabPhysicsHandle == nullptr)
    {
        return;
    }

    // 先配好参数再 Register（内部 CreateJointHandle 时会读取这些值）
    GrabPhysicsHandle->LinearStiffness  = PhysicsHandleLinearStiffness;
    GrabPhysicsHandle->LinearDamping    = PhysicsHandleLinearDamping;
    GrabPhysicsHandle->AngularStiffness = PhysicsHandleAngularStiffness;
    GrabPhysicsHandle->AngularDamping   = PhysicsHandleAngularDamping;
    GrabPhysicsHandle->InterpolationSpeed = PhysicsHandleInterpolationSpeed;
    // Grab 硬度上限：给一个很大的默认值，避免内部 Clamp
    GrabPhysicsHandle->bSoftLinearConstraint = true;
    GrabPhysicsHandle->bSoftAngularConstraint = true;

    GrabPhysicsHandle->RegisterComponent();

    // ---- Step 4: 开始抓取 ----
    // GrabComponentAtLocationWithRotation 会内部创建一个 Kinematic Actor 作为另一端的驱动锚点，
    // Tick 中调用 SetTargetLocationAndRotation 就会移动它，从而拉动被抓物体。
    // 将物体当前世界位姿作为初始抓取点 → rest state = 当前位姿，弹簧初始受力 = 0，不会弹飞。
    const FVector  ObjLoc = GrabbedPrim->GetComponentLocation();
    const FRotator ObjRot = GrabbedPrim->GetComponentRotation();
    GrabPhysicsHandle->GrabComponentAtLocationWithRotation(GrabbedPrim, NAME_None, ObjLoc, ObjRot);
}

void UGrabComponent::TeardownPhysicsHandle()
{
    if (GrabPhysicsHandle)
    {
        // 释放时同时恢复重力、CCD，取消 IgnoreActors
        if (UPrimitiveComponent* GrabbedPrim = GetOwnerPrimitive())
        {
            // 恢复重力开关（如果抓取时禁用了）
            if (bDisableGravityWhileHeld)
            {
                GrabbedPrim->SetEnableGravity(bCachedEnableGravity);
            }

            // 恢复 CCD 开关
            if (bUseCCDWhileHeld)
            {
                if (FBodyInstance* Body = GrabbedPrim->GetBodyInstance())
                {
                    Body->bUseCCD = bCachedUseCCD;
                }
            }

            if (MotionControllerRef)
            {
                if (AActor* PawnOwner = MotionControllerRef->GetOwner())
                {
                    GrabbedPrim->IgnoreActorWhenMoving(PawnOwner, false);
                }
            }
        }

        // 释放抓取并销毁组件
        GrabPhysicsHandle->ReleaseComponent();
        GrabPhysicsHandle->DestroyComponent();
        GrabPhysicsHandle = nullptr;
    }
}