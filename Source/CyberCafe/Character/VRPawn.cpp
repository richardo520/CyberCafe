// Fill out your copyright notice in the Description page of Project Settings.

#include "VRPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "MotionControllerComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "VRNotificationsComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

#include "NavigationSystem.h"
#include "NavigationData.h"

#include "Blueprint/UserWidget.h"
#include "GrabComponent.h"
#include "VRFunctionLibrary.h"

//=====================================================================
// 构造函数：搭建蓝图 SimpleConstructionScript 中的组件层级
//=====================================================================

AVRPawn::AVRPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    // Root
    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
    SetRootComponent(Root);

    // VROrigin：Pawn根下的偏移点，供相机和手柄挂载
    VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
    VROrigin->SetupAttachment(Root);

    // 相机
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(VROrigin);
    Camera->bLockToHmd = true;

    // HMD可视化Mesh（编辑器可见，Owner不可见）
    HeadMountedDisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMountedDisplayMesh"));
    HeadMountedDisplayMesh->SetupAttachment(Camera);
    HeadMountedDisplayMesh->SetOwnerNoSee(true);
    HeadMountedDisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // 左右手 Grip MotionController
    MotionControllerLeftGrip = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerLeftGrip"));
    MotionControllerLeftGrip->SetupAttachment(VROrigin);
    MotionControllerLeftGrip->MotionSource = FName(TEXT("LeftGrip"));

    MotionControllerRightGrip = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerRightGrip"));
    MotionControllerRightGrip->SetupAttachment(VROrigin);
    MotionControllerRightGrip->MotionSource = FName(TEXT("RightGrip"));

    // 左右手 Aim MotionController（UI/瞄准）
    MotionControllerLeftAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerLeftAim"));
    MotionControllerLeftAim->SetupAttachment(VROrigin);
    MotionControllerLeftAim->MotionSource = FName(TEXT("LeftAim"));

    MotionControllerRightAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerRightAim"));
    MotionControllerRightAim->SetupAttachment(VROrigin);
    MotionControllerRightAim->MotionSource = FName(TEXT("RightAim"));

    // 骨骼手
    HandLeft = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HandLeft"));
    HandLeft->SetupAttachment(MotionControllerLeftGrip);
    HandLeft->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    HandRight = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HandRight"));
    HandRight->SetupAttachment(MotionControllerRightGrip);
    HandRight->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Widget交互
    WidgetInteractionLeft = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("WidgetInteractionLeft"));
    WidgetInteractionLeft->SetupAttachment(MotionControllerLeftAim);

    WidgetInteractionRight = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("WidgetInteractionRight"));
    WidgetInteractionRight->SetupAttachment(MotionControllerRightAim);

    // Niagara Teleport Trace
    TeleportTraceNiagaraSystem = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TeleportTraceNiagaraSystem"));
    TeleportTraceNiagaraSystem->SetupAttachment(VROrigin);
    TeleportTraceNiagaraSystem->bAutoActivate = false;

    // VR系统通知
    VRNotifications = CreateDefaultSubobject<UVRNotificationsComponent>(TEXT("VRNotifications"));

    // 配置默认值（与官方VR模板一致）
    SnapTurnDegrees = 45.f;
    GrabRadiusFromGripPosition = 6.f;
    LocalTeleportLaunchSpeed = 900.f;
    LocalTeleportProjectileRadius = 3.6f;
    LocalNavMeshCellHeight = 8.f;
    TeleportProjectPointToNavigationQueryExtent = FVector(100.f, 100.f, 100.f);

    // 运行时状态默认值
    bTeleportTraceActive = false;
    bValidTeleportLocation = false;
    ProjectedTeleportLocation = FVector::ZeroVector;
    bActiveMenuHandRight = false;
    bTurnConsumed = false;

    PoseAlphaGrasp = 0.f;
    PoseAlphaIndexCurl = 0.f;
    PoseAlphaPoint = 0.f;
    PoseAlphaThumbUp = 0.f;
}

//=====================================================================
// BeginPlay：加载IMC、设置Tracking Origin、决定HMD是否隐藏
//=====================================================================

void AVRPawn::BeginPlay()
{
    Super::BeginPlay();

    // 将Tracking Origin设为Stage（VR模板默认，用于行走式体验）
    // 注：UE5.4+ 移除了 Floor 成员，改为 Stage(行走)/LocalFloor(站立)/Local(坐姿)/View/CustomOpenXR
    UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Stage);

    // HMD已启用时不显示HMD可视化Mesh
    if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled() && HeadMountedDisplayMesh)
    {
        HeadMountedDisplayMesh->SetVisibility(false);
    }

    UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), TEXT("xr.SecondaryScreenPercentage.HMDRenderTarget 100"), nullptr);

    // 挂上 Enhanced Input Mapping Context
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
            }
            if (HandsMappingContext)
            {
                Subsystem->AddMappingContext(HandsMappingContext, 0);
            }
        }
    }
}

//=====================================================================
// Tick：更新目标抓取、传送轨迹、拉拽状态
//=====================================================================

void AVRPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 更新左右手可抓取目标
    UpdateTargetGrabComponent(MotionControllerLeftGrip, false);
    UpdateTargetGrabComponent(MotionControllerRightGrip, true);

    // 更新拉拽状态
    if (PulledGrabComponentLeft)
    {
        UpdatePulledObject(PulledGrabComponentLeft, MotionControllerLeftGrip, DeltaTime);
    }
    if (PulledGrabComponentRight)
    {
        UpdatePulledObject(PulledGrabComponentRight, MotionControllerRightGrip, DeltaTime);
    }

    // 传送轨迹
    if (bTeleportTraceActive)
    {
        TeleportTrace();
    }
}

//=====================================================================
// SetupPlayerInputComponent：绑定Enhanced Input回调
//=====================================================================

void AVRPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (EIC == nullptr)
    {
        return;
    }

    // 移动 / 转向
    if (IA_Move)    EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AVRPawn::OnMove);
    if (IA_Turn)
    {
        EIC->BindAction(IA_Turn, ETriggerEvent::Started,   this, &AVRPawn::OnTurnStarted);
        EIC->BindAction(IA_Turn, ETriggerEvent::Triggered, this, &AVRPawn::OnTurnTriggered);
        EIC->BindAction(IA_Turn, ETriggerEvent::Completed, this, &AVRPawn::OnTurnCompleted);
    }

    // 抓取
    if (IA_Grab_Left_Pressed)   EIC->BindAction(IA_Grab_Left_Pressed,   ETriggerEvent::Triggered, this, &AVRPawn::OnGrabLeftPressed);
    if (IA_Grab_Left_Released)  EIC->BindAction(IA_Grab_Left_Released,  ETriggerEvent::Triggered, this, &AVRPawn::OnGrabLeftReleased);
    if (IA_Grab_Right_Pressed)  EIC->BindAction(IA_Grab_Right_Pressed,  ETriggerEvent::Triggered, this, &AVRPawn::OnGrabRightPressed);
    if (IA_Grab_Right_Released) EIC->BindAction(IA_Grab_Right_Released, ETriggerEvent::Triggered, this, &AVRPawn::OnGrabRightReleased);

    // 菜单
    if (IA_Menu_Toggle_Left)  EIC->BindAction(IA_Menu_Toggle_Left,  ETriggerEvent::Triggered, this, &AVRPawn::OnMenuToggleLeft);
    if (IA_Menu_Toggle_Right) EIC->BindAction(IA_Menu_Toggle_Right, ETriggerEvent::Triggered, this, &AVRPawn::OnMenuToggleRight);

    // 手部动画
    if (IA_Hand_Grasp_Left)      EIC->BindAction(IA_Hand_Grasp_Left,      ETriggerEvent::Triggered, this, &AVRPawn::OnHandGraspLeft);
    if (IA_Hand_Grasp_Right)     EIC->BindAction(IA_Hand_Grasp_Right,     ETriggerEvent::Triggered, this, &AVRPawn::OnHandGraspRight);
    if (IA_Hand_IndexCurl_Left)  EIC->BindAction(IA_Hand_IndexCurl_Left,  ETriggerEvent::Triggered, this, &AVRPawn::OnHandIndexCurlLeft);
    if (IA_Hand_IndexCurl_Right) EIC->BindAction(IA_Hand_IndexCurl_Right, ETriggerEvent::Triggered, this, &AVRPawn::OnHandIndexCurlRight);
    if (IA_Hand_Point_Left)      EIC->BindAction(IA_Hand_Point_Left,      ETriggerEvent::Triggered, this, &AVRPawn::OnHandPointLeft);
    if (IA_Hand_Point_Right)     EIC->BindAction(IA_Hand_Point_Right,     ETriggerEvent::Triggered, this, &AVRPawn::OnHandPointRight);
    if (IA_Hand_ThumbUp_Left)    EIC->BindAction(IA_Hand_ThumbUp_Left,    ETriggerEvent::Triggered, this, &AVRPawn::OnHandThumbUpLeft);
    if (IA_Hand_ThumbUp_Right)   EIC->BindAction(IA_Hand_ThumbUp_Right,   ETriggerEvent::Triggered, this, &AVRPawn::OnHandThumbUpRight);
}

//=====================================================================
// Enhanced Input 回调实现
//=====================================================================

void AVRPawn::OnMove(const FInputActionValue& Value)
{
    // 传送模式：按下摇杆前推激活轨迹，松开尝试传送
    const float AxisY = Value.Get<FVector2D>().Y;

    if (AxisY > 0.7f && !bTeleportTraceActive)
    {
        StartTeleportTrace();
    }
    else if (AxisY < 0.5f && bTeleportTraceActive)
    {
        EndTeleportTrace();
    }
}

void AVRPawn::OnTurnStarted(const FInputActionValue& Value)
{
    bTurnConsumed = false;
}

void AVRPawn::OnTurnTriggered(const FInputActionValue& Value)
{
    if (bTurnConsumed)
    {
        return;
    }

    const float AxisX = Value.Get<FVector2D>().X;
    if (FMath::Abs(AxisX) > 0.7f)
    {
        SnapTurn(AxisX > 0.f);
        bTurnConsumed = true;
    }
}

void AVRPawn::OnTurnCompleted(const FInputActionValue& Value)
{
    bTurnConsumed = false;
}

void AVRPawn::OnGrabLeftPressed(const FInputActionValue& Value)   { TryGrabLeft(); }
void AVRPawn::OnGrabLeftReleased(const FInputActionValue& Value)
{
    if (HeldComponentLeft)
    {
        HeldComponentLeft->TryRelease();
        HeldComponentLeft = nullptr;
    }
}
void AVRPawn::OnGrabRightPressed(const FInputActionValue& Value)  { TryGrabRight(); }
void AVRPawn::OnGrabRightReleased(const FInputActionValue& Value)
{
    if (HeldComponentRight)
    {
        HeldComponentRight->TryRelease();
        HeldComponentRight = nullptr;
    }
}

void AVRPawn::OnMenuToggleLeft(const FInputActionValue& Value)   { ToggleMenu(false); }
void AVRPawn::OnMenuToggleRight(const FInputActionValue& Value)  { ToggleMenu(true);  }

void AVRPawn::OnHandGraspLeft(const FInputActionValue& Value)      { PoseAlphaGrasp     = Value.Get<float>(); }
void AVRPawn::OnHandGraspRight(const FInputActionValue& Value)     { PoseAlphaGrasp     = Value.Get<float>(); }
void AVRPawn::OnHandIndexCurlLeft(const FInputActionValue& Value)  { PoseAlphaIndexCurl = Value.Get<float>(); }
void AVRPawn::OnHandIndexCurlRight(const FInputActionValue& Value) { PoseAlphaIndexCurl = Value.Get<float>(); }
void AVRPawn::OnHandPointLeft(const FInputActionValue& Value)      { PoseAlphaPoint     = Value.Get<float>(); }
void AVRPawn::OnHandPointRight(const FInputActionValue& Value)     { PoseAlphaPoint     = Value.Get<float>(); }
void AVRPawn::OnHandThumbUpLeft(const FInputActionValue& Value)    { PoseAlphaThumbUp   = Value.Get<float>(); }
void AVRPawn::OnHandThumbUpRight(const FInputActionValue& Value)   { PoseAlphaThumbUp   = Value.Get<float>(); }

//=====================================================================
// 抓取
//=====================================================================

bool AVRPawn::TryGrabLeft()
{
    UGrabComponent* Target = TargetGrabComponentLeft;
    if (Target == nullptr)
    {
        return false;
    }

    if (Target->TryGrab(MotionControllerLeftGrip))
    {
        HeldComponentLeft = Target;
        HideUnhideHand(false, true);
        return true;
    }
    return false;
}

bool AVRPawn::TryGrabRight()
{
    UGrabComponent* Target = TargetGrabComponentRight;
    if (Target == nullptr)
    {
        return false;
    }

    if (Target->TryGrab(MotionControllerRightGrip))
    {
        HeldComponentRight = Target;
        HideUnhideHand(true, true);
        return true;
    }
    return false;
}

UGrabComponent* AVRPawn::GetGrabComponentNearMotionController(UMotionControllerComponent* MotionController) const
{
    if (MotionController == nullptr)
    {
        return nullptr;
    }

    // 以Grip位置为球心进行球体扫描
    const FVector Start = MotionController->GetComponentLocation();
    const FVector End = Start;

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(const_cast<AVRPawn*>(this));

    TArray<FHitResult> Hits;
    UKismetSystemLibrary::SphereTraceMultiForObjects(
        this, Start, End, GrabRadiusFromGripPosition,
        { UEngineTypes::ConvertToObjectType(ECC_WorldDynamic), UEngineTypes::ConvertToObjectType(ECC_PhysicsBody) },
        false, IgnoreActors, EDrawDebugTrace::None, Hits, true);

    // 收集命中的所有Actor上的GrabComponent，从中选优先级最高者
    TArray<UGrabComponent*> Candidates;
    for (const FHitResult& H : Hits)
    {
        if (AActor* HitActor = H.GetActor())
        {
            TArray<UGrabComponent*> Grabs;
            HitActor->GetComponents<UGrabComponent>(Grabs);
            for (UGrabComponent* G : Grabs)
            {
                if (G && !G->IsHeld())
                {
                    Candidates.AddUnique(G);
                }
            }
        }
    }

    UGrabComponent* Best = nullptr;
    bool bCanTarget = false;
    UVRFunctionLibrary::FindTopPrioGrabComponent(Candidates, Best, bCanTarget);
    return Best;
}

UGrabComponent* AVRPawn::GetGrabComponentUnderAim(UMotionControllerComponent* MotionControllerAim) const
{
    FHitResult Hit;
    if (!TraceAim(MotionControllerAim, Hit))
    {
        return nullptr;
    }

    if (AActor* HitActor = Hit.GetActor())
    {
        TArray<UGrabComponent*> Grabs;
        HitActor->GetComponents<UGrabComponent>(Grabs);
        UGrabComponent* Best = nullptr;
        bool bCanTarget = false;
        UVRFunctionLibrary::FindTopPrioGrabComponent(Grabs, Best, bCanTarget);
        return Best;
    }
    return nullptr;
}

bool AVRPawn::UpdatePulledObject(UGrabComponent* InGrabComponent, UMotionControllerComponent* InMotionController, float DeltaTime)
{
    if (InGrabComponent == nullptr || InMotionController == nullptr)
    {
        return false;
    }

    // 简化：当拉拽距离足够接近手时，转为正式抓取
    const FVector HandLoc = InMotionController->GetComponentLocation();
    const FVector ObjLoc = InGrabComponent->GetComponentLocation();
    const float Dist = FVector::Dist(HandLoc, ObjLoc);
    if (Dist <= GrabRadiusFromGripPosition * 2.f)
    {
        InGrabComponent->StopPull();
        return true;
    }
    return false;
}

void AVRPawn::UpdateTargetGrabComponent(UMotionControllerComponent* InMotionController, bool bRightHand)
{
    if (InMotionController == nullptr)
    {
        return;
    }

    // 优先在Grip半径内查找，其次再从Aim射线中查找
    UGrabComponent* Candidate = GetGrabComponentNearMotionController(InMotionController);
    if (Candidate == nullptr)
    {
        UMotionControllerComponent* AimMC = bRightHand ? MotionControllerRightAim : MotionControllerLeftAim;
        Candidate = GetGrabComponentUnderAim(AimMC);
    }

    // 注意：TargetGrabComponentLeft/Right 是 TObjectPtr<UGrabComponent>，
    // 不能绑定到 UGrabComponent*& 上，需要用 TObjectPtr<UGrabComponent>& 引用
    TObjectPtr<UGrabComponent>& CurrentTarget = bRightHand ? TargetGrabComponentRight : TargetGrabComponentLeft;
    if (Candidate != CurrentTarget)
    {
        // 取消旧目标高亮
        if (CurrentTarget)
        {
            MarkForGrab(CurrentTarget, false);
        }
        // 高亮新目标
        if (Candidate)
        {
            MarkForGrab(Candidate, true);
        }
        CurrentTarget = Candidate;
        UpdatePotentialTarget(Candidate, bRightHand);
    }
}

void AVRPawn::UpdatePotentialTarget(UGrabComponent* PotentialGrabComponent, bool bRightHand)
{
    // 默认实现空：可在蓝图子类覆写以刷新UI或做触觉提示
}

void AVRPawn::MarkForGrab(UGrabComponent* InGrabComponent, bool bCanBeGrab)
{
    if (InGrabComponent == nullptr)
    {
        return;
    }

    // 默认通过CustomDepth描边高亮所属Actor的Root Primitive
    if (AActor* OwnerActor = InGrabComponent->GetOwner())
    {
        if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
        {
            Prim->SetRenderCustomDepth(bCanBeGrab);
        }
    }
}

//=====================================================================
// 传送
//=====================================================================

void AVRPawn::StartTeleportTrace()
{
    bTeleportTraceActive = true;
    bValidTeleportLocation = false;

    if (TeleportTraceNiagaraSystem)
    {
        TeleportTraceNiagaraSystem->Activate(true);
    }

    // 生成传送可视化Actor
    if (TeleportVisualizerClass && TeleportVisualizerReference == nullptr)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        TeleportVisualizerReference = GetWorld()->SpawnActor<AActor>(
            TeleportVisualizerClass, FTransform::Identity, Params);
    }
}

void AVRPawn::EndTeleportTrace()
{
    bTeleportTraceActive = false;

    if (TeleportTraceNiagaraSystem)
    {
        TeleportTraceNiagaraSystem->Deactivate();
    }

    if (TeleportVisualizerReference)
    {
        TeleportVisualizerReference->Destroy();
        TeleportVisualizerReference = nullptr;
    }

    if (bValidTeleportLocation)
    {
        TryTeleport();
    }
    bValidTeleportLocation = false;
}

void AVRPawn::TeleportTrace()
{
    if (MotionControllerLeftAim == nullptr)
    {
        return;
    }

    // 用左手Aim作为传送弹道发射端（可按需换成右手）
    const FVector StartPos = MotionControllerLeftAim->GetComponentLocation();
    const FVector LaunchVelocity = MotionControllerLeftAim->GetForwardVector() * LocalTeleportLaunchSpeed;

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(this);

    FPredictProjectilePathParams PredictParams(
        LocalTeleportProjectileRadius, StartPos, LaunchVelocity, 2.f,
        ECC_WorldStatic, this);
    PredictParams.ActorsToIgnore = IgnoreActors;
    PredictParams.bTraceComplex = false;
    PredictParams.DrawDebugType = EDrawDebugTrace::None;

    FPredictProjectilePathResult PredictResult;
    const bool bHit = UGameplayStatics::PredictProjectilePath(this, PredictParams, PredictResult);

    // 收集路径点供Niagara使用
    TeleportTracePathPositions.Reset();
    for (const FPredictProjectilePathPointData& P : PredictResult.PathData)
    {
        TeleportTracePathPositions.Add(P.Location);
    }

    // 判断传送位置有效性
    FVector Projected;
    bValidTeleportLocation = bHit && IsValidTeleportLocation(PredictResult.HitResult, Projected);
    if (bValidTeleportLocation)
    {
        ProjectedTeleportLocation = Projected;
        if (TeleportVisualizerReference)
        {
            TeleportVisualizerReference->SetActorLocation(ProjectedTeleportLocation);
        }
    }

    // 将路径点数组喂给Niagara VFX
    if (TeleportTraceNiagaraSystem)
    {
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
            TeleportTraceNiagaraSystem, TEXT("PathPositions"), TeleportTracePathPositions);
    }
}

bool AVRPawn::TryTeleport()
{
    if (!bValidTeleportLocation)
    {
        return false;
    }

    // 以HMD相对Pawn的水平偏移做补偿，让传送后头部对齐目标点
    const FVector CameraLoc = Camera ? Camera->GetComponentLocation() : GetActorLocation();
    const FVector PawnLoc = GetActorLocation();
    const FVector Offset(CameraLoc.X - PawnLoc.X, CameraLoc.Y - PawnLoc.Y, 0.f);

    const FVector NewLocation = ProjectedTeleportLocation - Offset;
    return TeleportTo(NewLocation, GetActorRotation(), false, false);
}

bool AVRPawn::IsValidTeleportLocation(const FHitResult& Hit, FVector& OutProjectedLocation) const
{
    OutProjectedLocation = Hit.Location;

    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
    if (NavSys == nullptr)
    {
        return false;
    }

    FNavLocation NavLoc;
    const bool bProjected = NavSys->ProjectPointToNavigation(
        Hit.Location, NavLoc, TeleportProjectPointToNavigationQueryExtent);
    if (bProjected)
    {
        OutProjectedLocation = NavLoc.Location;
        // 补偿NavMesh cell高度
        OutProjectedLocation.Z -= LocalNavMeshCellHeight;
        return true;
    }
    return false;
}

//=====================================================================
// 其他
//=====================================================================

void AVRPawn::SnapTurn(bool bRightTurn)
{
    const float DeltaYaw = bRightTurn ? SnapTurnDegrees : -SnapTurnDegrees;

    // 围绕相机而非Actor根做旋转，避免Pawn位置漂移
    if (Camera)
    {
        const FVector CameraLoc = Camera->GetComponentLocation();
        const FVector PawnLoc = GetActorLocation();
        const FVector Offset = CameraLoc - PawnLoc;

        const FRotator Delta(0.f, DeltaYaw, 0.f);
        const FVector RotatedOffset = Delta.RotateVector(Offset);
        const FVector NewLoc = CameraLoc - RotatedOffset;

        AddActorWorldRotation(Delta);
        SetActorLocation(NewLoc);
    }
    else
    {
        AddActorWorldRotation(FRotator(0.f, DeltaYaw, 0.f));
    }
}

void AVRPawn::ToggleMenu(bool bRightHand)
{
    if (MenuReference)
    {
        CloseMenu();
        return;
    }

    if (MenuClass == nullptr)
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC == nullptr)
    {
        return;
    }

    MenuReference = CreateWidget<UUserWidget>(PC, MenuClass);
    if (MenuReference)
    {
        bActiveMenuHandRight = bRightHand;
        MenuReference->AddToViewport();
    }
}

void AVRPawn::CloseMenu()
{
    if (MenuReference)
    {
        MenuReference->RemoveFromParent();
        MenuReference = nullptr;
    }
}

void AVRPawn::HideUnhideHand(bool bRightHand, bool bHide)
{
    if (USkeletalMeshComponent* HandMesh = bRightHand ? HandRight : HandLeft)
    {
        HandMesh->SetVisibility(!bHide, true);
    }
}

bool AVRPawn::TraceAim(UMotionControllerComponent* MotionControllerAim, FHitResult& OutHit) const
{
    if (MotionControllerAim == nullptr)
    {
        return false;
    }

    const FVector Start = MotionControllerAim->GetComponentLocation();
    const FVector End = Start + MotionControllerAim->GetForwardVector() * 500.f;

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(const_cast<AVRPawn*>(this));

    return UKismetSystemLibrary::SphereTraceSingleForObjects(
        this, Start, End, 3.f,
        { UEngineTypes::ConvertToObjectType(ECC_WorldDynamic),
          UEngineTypes::ConvertToObjectType(ECC_PhysicsBody) },
        false, IgnoreActors, EDrawDebugTrace::None, OutHit, true);
}
