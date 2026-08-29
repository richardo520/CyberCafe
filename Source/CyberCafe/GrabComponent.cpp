// Fill out your copyright notice in the Description page of Project Settings.

#include "GrabComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MotionControllerComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
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

    // ConstraintDrive 模式：若物体被环境卡住、手柄拉得太远，自动断开约束，避免弹簧无限拉伸
    if (bIsHeld && GrabConstraint && MotionControllerRef && BreakDistance > 0.f)
    {
        if (UPrimitiveComponent* Prim = GetOwnerPrimitive())
        {
            const float DistSq = FVector::DistSquared(
                Prim->GetComponentLocation(),
                MotionControllerRef->GetComponentLocation());
            if (DistSq > BreakDistance * BreakDistance)
            {
                // 主动释放（会走 TryRelease 里拆约束 + 派发 OnDropped）
                TryRelease();
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
    // ConstraintDrive 模式仅对 Free / Snap 生效；SnapInPlace / Custom 保持原有行为
    const bool bUseConstraint =
        (PhysicsMode == EGrabPhysicsMode::ConstraintDrive) &&
        (GrabType == EGrabType::Free || GrabType == EGrabType::Snap);

    switch (GrabType)
    {
    case EGrabType::Free:
    {
        if (bUseConstraint)
        {
            // 保持物理开启，不 Attach，仅创建约束驱动物体追随手柄
            SetPrimitiveCompPhysics(true);
            SetupGrabConstraint(MotionController);
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
        if (bUseConstraint)
        {
            // ConstraintDrive + Snap：不做强制 Teleport（避免瞬移造成物体与手/环境穿透而被 Chaos 弹飞），
            // 直接建约束 + 用 SetConstraintReferenceFrame 将"目标相对位姿"设为GrabComponent自身的相对变换，
            // 约束驱动会用弹簧将物体平滑地"吸"到吸附位姿。
            SetPrimitiveCompPhysics(true);
            SetupGrabConstraint(MotionController);
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

    // 先拆除 ConstraintDrive 模式下的物理约束（如果有）
    // 注意：必须在 TrySimulateOnDrop 之前拆，否则 Detach 时会被约束盉住。
    const bool bWasConstraintDrive = (GrabConstraint != nullptr);
    if (bWasConstraintDrive)
    {
        TeardownGrabConstraint();
    }

    switch (GrabType)
    {
    case EGrabType::Free:
    case EGrabType::Snap:
    {
        if (bWasConstraintDrive)
        {
            // ConstraintDrive 模式下本就开着物理，无需 TrySimulateOnDrop 重新开物理
            // 但需要恢复 CCD 等临时修改的属性
            if (UPrimitiveComponent* Prim = GetOwnerPrimitive())
            {
                if (bUseCCDWhileHeld)
                {
                    Prim->GetBodyInstance()->bUseCCD = bCachedUseCCD;
                }
            }
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
// ConstraintDrive：物理约束驱动抓取（可与环境碰撞）
//===========================================================================

void UGrabComponent::SetupGrabConstraint(UMotionControllerComponent* MotionController)
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

    // 抓取前若有残留约束，先清理，避免重复创建
    if (GrabConstraint)
    {
        TeardownGrabConstraint();
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

    // 抓取瞬间先清零物体旧速度，避免旧道力参与弹簧驱动叠加导致弹飞
    GrabbedPrim->SetSimulatePhysics(true);
    GrabbedPrim->SetPhysicsLinearVelocity(FVector::ZeroVector, false);
    GrabbedPrim->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
    GrabbedPrim->WakeAllRigidBodies();

    // 拓展使用者：抓取期间让被抓物体 与 VRPawn 不产生物理接触（只使用 MoveIgnoreActors，不影响它与环境、其他物体的碰撞）
    if (AActor* PawnOwner = MotionController->GetOwner())
    {
        GrabbedPrim->IgnoreActorWhenMoving(PawnOwner, true);
    }

    // 动态创建 PhysicsConstraint，附着到 MotionController 上（约束的世界位姿 = 手柄当前位姿）
    GrabConstraint = NewObject<UPhysicsConstraintComponent>(this, NAME_None, RF_Transient);
    if (GrabConstraint == nullptr)
    {
        return;
    }

    GrabConstraint->SetupAttachment(MotionController);
    GrabConstraint->RegisterComponent();
    // 把约束放到当前手柄位置，作为"目标锚点"
    GrabConstraint->SetWorldLocationAndRotation(
        MotionController->GetComponentLocation(),
        MotionController->GetComponentQuat());

    // ---- 关键配置：允许所有轴的自由度（不锁死），完全靠 Drive 拉过去 ----
    GrabConstraint->SetLinearXLimit(LCM_Free, 0.f);
    GrabConstraint->SetLinearYLimit(LCM_Free, 0.f);
    GrabConstraint->SetLinearZLimit(LCM_Free, 0.f);
    GrabConstraint->SetAngularSwing1Limit(ACM_Free, 0.f);
    GrabConstraint->SetAngularSwing2Limit(ACM_Free, 0.f);
    GrabConstraint->SetAngularTwistLimit(ACM_Free, 0.f);

    // 约束两端之间不禁用碰撞（但由于一端为 nullptr，实际也不会产生硬碰撞）
    GrabConstraint->SetDisableCollision(false);

    // ---- 位置驱动 (Linear Drive) ----
    GrabConstraint->SetLinearDriveParams(LinearStiffness, LinearDamping, LinearMaxForce);
    GrabConstraint->SetLinearPositionDrive(true, true, true);
    GrabConstraint->SetLinearVelocityDrive(true, true, true);

    // ---- 姿态驱动 (Angular Drive)：使用 SLERP 一次性驱动全部三轴，效果最稳定 ----
    GrabConstraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
    GrabConstraint->SetAngularDriveParams(AngularStiffness, AngularDamping, AngularMaxTorque);
    GrabConstraint->SetAngularOrientationDrive(true, true);
    GrabConstraint->SetAngularVelocityDrive(true, true);

    // ---- 建立约束 ----
    // 关键：一端传 nullptr（等价于"锚在世界"），但约束组件本身 attach 在 MotionController 下，
    // Chaos 会把约束自身的世界位姿作为"固定锺定的目标位姿"。
    // 这样避免了把 UMotionControllerComponent 当作 Kinematic Body 参与解算（它的 BodyInstance 未必与手柄追踪同步，会引入伪速度导致弹飞）。
    GrabConstraint->SetConstrainedComponents(
        nullptr,     NAME_None,   // 一端：世界（约束自身 attach 到 MotionController 会跟随手柄）
        GrabbedPrim, NAME_None);  // 一端：被抓物体

    // ---- 设定目标 rest 位姿 ----
    // Frame1 (世界锚点端)：约束自身已位于手柄处，默认就是手柄世界变换 → Identity 即可
    // Frame2 (物体端)：需要告诉约束"物体上的哪个点应该被拉到手柄处"
    // Free 模式：用拓取当下相对手柄的偏移（保持当前相对手柄的姿态）
    // Snap 模式：用 GrabComponent 自身相对于 Owner 的 Transform（将物体上的"抓握点"拉到手柄）
    const bool bIsSnap = (GrabType == EGrabType::Snap);

    // Frame1 用 Identity（世界坐标下约束位置，已经在 MotionController 上）
    GrabConstraint->SetConstraintReferenceFrame(EConstraintFrame::Frame1, FTransform::Identity);

    if (bIsSnap)
    {
        // Snap：Frame2 = GrabComponent 相对 Owner 的本地变换（描述"物体上的吸附锚点"）
        const FTransform LocalGrab = GetRelativeTransform();
        GrabConstraint->SetConstraintReferenceFrame(EConstraintFrame::Frame2, LocalGrab);
    }
    else
    {
        // Free：Frame2 = 物体当前相对手柄的本地变换（保持当前相对手柄的相对位姿）
        // = 物体世界变换 乘 手柄世界变换的逆矩阵
        const FTransform ObjWorld = GrabbedPrim->GetComponentTransform();
        const FTransform CtrlWorld(MotionController->GetComponentQuat(), MotionController->GetComponentLocation());
        const FTransform RelToCtrl = ObjWorld.GetRelativeTransform(CtrlWorld);
        // Frame2 描述的是"物体上哪个本地点对应 Frame1"，需要取逆
        GrabConstraint->SetConstraintReferenceFrame(EConstraintFrame::Frame2, RelToCtrl.Inverse());
    }
}

void UGrabComponent::TeardownGrabConstraint()
{
    if (GrabConstraint)
    {
        // 释放时同时取消 IgnoreActors（避免机位后玩家踢/推自己丢到地上的物体时仍被忽略）
        if (UPrimitiveComponent* GrabbedPrim = GetOwnerPrimitive())
        {
            if (MotionControllerRef)
            {
                if (AActor* PawnOwner = MotionControllerRef->GetOwner())
                {
                    GrabbedPrim->IgnoreActorWhenMoving(PawnOwner, false);
                }
            }
        }

        GrabConstraint->BreakConstraint();
        GrabConstraint->DestroyComponent();
        GrabConstraint = nullptr;
    }
}