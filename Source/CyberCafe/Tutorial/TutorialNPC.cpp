// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/TutorialNPC.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"

ATutorialNPC::ATutorialNPC()
{
    // 本 NPC 不需要每帧 Tick（无气泡 Billboard、无自定义运动）
    // 后续真要加"眼神朝向玩家"时再在蓝图或子类里 bCanEverTick=true 并实现
    PrimaryActorTick.bCanEverTick = false;

    // Capsule 碰撞策略：
    //   - 对 WorldStatic/WorldDynamic：Block，保证 Character 能站在地面上（不穿地）
    //   - 对 Pawn/PhysicsBody：Ignore，避免挡住 VR 玩家 / 被玩家抓的物体撞飞
    // 不能用 NoCollision —— 那样 FindFloor 失败，Character 会直接自由落体穿透地板。
    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Capsule->SetCollisionObjectType(ECC_Pawn);
        Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
        Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    }

    // Mesh 默认关碰撞：避免身体挡住 VR 玩家视线 / 物体
    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        MeshComp->SetCollisionProfileName(TEXT("NoCollision"));
    }

    // 头部挂载点：挂在 CapsuleComponent 下（不受 Mesh 自身 Yaw=-90 旋转影响）
    HeadAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("HeadAnchor"));
    HeadAnchor->SetupAttachment(GetCapsuleComponent());
    // Capsule 中心在角色腰部，+100 大约到头顶上方一点；蓝图里可微调
    HeadAnchor->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
}