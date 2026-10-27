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

    // 缓存关卡摆放时的 Yaw / Roll，之后 Tick 里只会动 Pitch
    const FRotator InitRot = GetActorRotation();
    InitialYaw  = InitRot.Yaw;
    InitialRoll = InitRot.Roll;
}

void ANarrativeHUDActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 位置不动，只做朝向偏转
    if (!bFaceCamera)
    {
        return;
    }

    const FRotator Target  = ComputeTargetRotation();
    const FRotator Current = GetActorRotation();
    const FRotator NewRot  = FMath::RInterpTo(Current, Target, DeltaSeconds, RotationInterpSpeed);
    SetActorRotation(NewRot);
}

FRotator ANarrativeHUDActor::ComputeTargetRotation() const
{
    // 默认：保持关卡摆放的朝向（Pitch=0）
    FRotator Target(0.f, InitialYaw, InitialRoll);

    APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!CamMgr)
    {
        return Target;
    }

    const FVector CamLoc    = CamMgr->GetCameraLocation();
    const FVector SelfLoc   = GetActorLocation();

    // LookAt 得到的 Pitch 就是"面板看向相机"所需的俯仰
    // 由于 WidgetComponent 的"正面"是 +X，所以让 Actor 的 +X 指向相机即可
    // 但 Yaw 要锁死在初始值（不扭过去追玩家），所以这里只取 Pitch
    const FRotator LookAtRot = UKismetMathLibrary::FindLookAtRotation(SelfLoc, CamLoc);

    // 把 LookAt 的 Pitch 投影到"初始 Yaw 朝向"下的 Pitch：
    //   玩家在面板正前方上/下 → 需要正常 Pitch
    //   玩家在面板侧后方（Yaw 偏离大）→ Pitch 没有物理意义，这时应回归 0
    // 做法：计算 LookAt.Yaw 与 InitialYaw 的夹角，用它的 cos 作为 Pitch 的缩放系数。
    //   夹角 0°（玩家在正前方）→ cos=1，完整生效
    //   夹角 90°（玩家在正侧方）→ cos=0，Pitch 归零
    //   夹角 >90°（玩家在身后）→ cos<0，Pitch 反向，这里直接 Clamp 到 0 更安全
    const float YawDeltaDeg = FMath::FindDeltaAngleDegrees(InitialYaw, LookAtRot.Yaw);
    const float Weight = FMath::Max(FMath::Cos(FMath::DegreesToRadians(YawDeltaDeg)), 0.f);

    float Pitch = LookAtRot.Pitch * Weight;
    Pitch = FMath::Clamp(Pitch, -MaxPitchDegrees, MaxPitchDegrees);

    Target.Pitch = Pitch;
    return Target;
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
