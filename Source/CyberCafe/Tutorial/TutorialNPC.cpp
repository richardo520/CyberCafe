// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/TutorialNPC.h"
#include "Tutorial/UI/TutorialHintWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ATutorialNPC::ATutorialNPC()
{
    // 本 NPC 不需要帧更新；未来做眼神追踪再按需打开
    PrimaryActorTick.bCanEverTick = false;

    // CapsuleComponent 不参与碰撞：避免挡住 VR 玩家 / 被物体撞飞
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionProfileName(TEXT("NoCollision"));
    }

    // 关闭 CharacterMovement 的物理计算：此 NPC 当前是"摆件"，省性能
    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->SetComponentTickEnabled(false);
        Movement->MovementMode = MOVE_None;
    }

    // Mesh 默认关闭碰撞：避免身体挡住 VR 玩家视线 / 物体
    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        MeshComp->SetCollisionProfileName(TEXT("NoCollision"));
    }

    // 头顶挂载点：蓝图子类里拖到骨骼头部上方
    HeadAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("HeadAnchor"));
    HeadAnchor->SetupAttachment(GetMesh());
    // 给个默认偏移：胶囊顶部大约 1.7m 高（避免蓝图里忘了调完全是 0）
    HeadAnchor->SetRelativeLocation(FVector(0.f, 0.f, 180.f));

    // 气泡 Widget：挂在 HeadAnchor 下
    HintBubble = CreateDefaultSubobject<UWidgetComponent>(TEXT("HintBubble"));
    HintBubble->SetupAttachment(HeadAnchor);
    HintBubble->SetWidgetSpace(EWidgetSpace::World);
    HintBubble->SetDrawSize(FVector2D(400.f, 100.f));
    HintBubble->SetVisibility(false);  // 默认隐藏，等 ShowHint 时再开
    // Pivot 居中偏下，让气泡尖端对准 HeadAnchor 位置
    HintBubble->SetPivot(FVector2D(0.5f, 1.f));
}

void ATutorialNPC::BeginPlay()
{
    Super::BeginPlay();

    // 允许蓝图里在 Details 面板选 HintWidgetClass；若选了，就动态替换 WidgetComponent 的 WidgetClass
    // 这样蓝图子类也可以直接在 WidgetComponent 的 WidgetClass 里选，更灵活
    if (HintBubble && HintWidgetClass && HintBubble->GetWidgetClass() != HintWidgetClass)
    {
        HintBubble->SetWidgetClass(HintWidgetClass);
        HintBubble->InitWidget();
    }
}

void ATutorialNPC::ShowHint(const FText& InText)
{
    if (!HintBubble)
    {
        return;
    }

    HintBubble->SetVisibility(true);

    if (UTutorialHintWidget* Widget = GetHintWidget())
    {
        Widget->SetHintText(InText);
    }
}

void ATutorialNPC::HideHint()
{
    if (!HintBubble)
    {
        return;
    }
    HintBubble->SetVisibility(false);
}

UTutorialHintWidget* ATutorialNPC::GetHintWidget() const
{
    if (!HintBubble)
    {
        return nullptr;
    }
    return Cast<UTutorialHintWidget>(HintBubble->GetUserWidgetObject());
}
