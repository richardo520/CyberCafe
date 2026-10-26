// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/TutorialNPC.h"
#include "Tutorial/UI/TutorialHintWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"

ATutorialNPC::ATutorialNPC()
{
    // 本 NPC 不需要帧更新；未来做眼神追踪再按需打开
    PrimaryActorTick.bCanEverTick = false;

    // Capsule 碰撞策略：
    //   - 对 WorldStatic/WorldDynamic：Block，保证 Character 能站在地面上（不穿地）
    //   - 对 Pawn/PhysicsBody：Ignore，避免挡住 VR 玩家 / 被玩家抓的物体撞飞
    // 不能用 NoCollision —— 那样 FindFloor 失败，Character 会直接自由落体穿透地板。
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);  // 只查询，不参与物理模拟
        Capsule->SetCollisionObjectType(ECC_Pawn);
        Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
        Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    }

    // CharacterMovement 保持默认 Walking：这样引擎会每帧 FindFloor 把 Capsule 贴到地面上。
    // 不要把 MovementMode 设为 MOVE_None —— 那样连"站在地上"的落地校正也没了。
    // 想让 NPC 完全不动，只需 AIController 不发移动指令即可（我们本来就不发）。

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
