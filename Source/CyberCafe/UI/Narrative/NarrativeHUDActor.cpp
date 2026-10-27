// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Narrative/NarrativeHUDActor.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "Components/WidgetComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

ANarrativeHUDActor::ANarrativeHUDActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 一个空 Scene Root 更干净；WidgetComp 挂在其下
    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    PanelWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("PanelWidget"));
    PanelWidgetComp->SetupAttachment(Root);
    PanelWidgetComp->SetWidgetSpace(EWidgetSpace::World);
    PanelWidgetComp->SetDrawSize(DrawSize);
    // Pivot 居中：这样我们直接以 Actor 位置为面板中心
    PanelWidgetComp->SetPivot(FVector2D(0.5f, 0.5f));
    // 双面显示：避免算错朝向时直接变透明
    PanelWidgetComp->SetTwoSided(true);
    // 禁掉碰撞，纯视觉对象
    PanelWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PanelWidgetComp->SetGenerateOverlapEvents(false);
    // 初始隐藏：等 Subsystem 调 ShowPanel
    PanelWidgetComp->SetVisibility(false);
}

void ANarrativeHUDActor::BeginPlay()
{
    Super::BeginPlay();

    if (PanelWidgetComp)
    {
        // 应用蓝图可能修改过的 DrawSize / Scale
        PanelWidgetComp->SetDrawSize(DrawSize);
        PanelWidgetComp->SetRelativeScale3D(FVector(WorldScale));
        // 确保 UserWidget 实例已经创建
        PanelWidgetComp->InitWidget();
    }
}

void ANarrativeHUDActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    FVector TargetLoc;
    FRotator TargetRot;
    ComputeFollowTarget(TargetLoc, TargetRot);

    if (!bFirstTickSnapped)
    {
        // 首帧直接吸附，避免看到面板从原点飞过来
        SetActorLocationAndRotation(TargetLoc, TargetRot);
        bFirstTickSnapped = true;
        return;
    }

    const FVector  NewLoc = FMath::VInterpTo(GetActorLocation(), TargetLoc, DeltaSeconds, LocationInterpSpeed);
    const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaSeconds, RotationInterpSpeed);
    SetActorLocationAndRotation(NewLoc, NewRot);
}

void ANarrativeHUDActor::ComputeFollowTarget(FVector& OutLocation, FRotator& OutRotation) const
{
    APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!CamMgr)
    {
        OutLocation = GetActorLocation();
        OutRotation = GetActorRotation();
        return;
    }

    const FVector  CamLoc = CamMgr->GetCameraLocation();
    const FRotator CamRot = CamMgr->GetCameraRotation();

    // 跟随基准朝向：根据策略决定是否忽略 Pitch
    FRotator BaseRot = CamRot;
    if (bIgnoreCameraPitch)
    {
        BaseRot.Pitch = 0.f;
        BaseRot.Roll  = 0.f;
    }

    // 身前 FollowDistance 米处
    const FVector Forward = BaseRot.Vector();
    OutLocation = CamLoc + Forward * FollowDistance + FVector(0.f, 0.f, VerticalOffset);

    // 面板法线应指向摄像机 → 面板"正面"冲向玩家
    // 用 LookAt(From=面板, To=相机)，再在 Yaw 上 +180：
    // WidgetComponent 的"正面"是 +X 方向，所以直接用 LookAt 即可。
    OutRotation = UKismetMathLibrary::FindLookAtRotation(OutLocation, CamLoc);
}

void ANarrativeHUDActor::ShowPanel()
{
    if (PanelWidgetComp)
    {
        PanelWidgetComp->SetVisibility(true);
    }
}

void ANarrativeHUDActor::HidePanel()
{
    if (PanelWidgetComp)
    {
        PanelWidgetComp->SetVisibility(false);
    }
}

UNarrativeWidget* ANarrativeHUDActor::GetNarrativeWidget() const
{
    if (!PanelWidgetComp)
    {
        return nullptr;
    }
    return Cast<UNarrativeWidget>(PanelWidgetComp->GetUserWidgetObject());
}

void ANarrativeHUDActor::SetWidgetClass(TSubclassOf<UNarrativeWidget> InClass)
{
    if (!PanelWidgetComp || !InClass)
    {
        return;
    }
    PanelWidgetComp->SetWidgetClass(InClass);
    PanelWidgetComp->InitWidget();
}
