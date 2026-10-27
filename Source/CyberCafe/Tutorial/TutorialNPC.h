// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TutorialNPC.generated.h"

class USceneComponent;

/**
 * ATutorialNPC
 *
 * 教学 NPC：吧台对面站的那个角色。
 *
 * 为何用 Character：
 *   - 预留未来：动画蓝图、眼神看向玩家手部、走位、AI 感知
 *   - 现阶段只做"场景摆件"：有一个站在吧台对面的身影
 *
 * 构造层面：
 *   - CapsuleComponent 开 QueryOnly 碰撞：Block WorldStatic/Dynamic 保证站地上，Ignore Pawn 不挡 VR 玩家
 *   - Mesh 关碰撞：不挡 VR 玩家 / 不吃物理冲击
 *   - 顶上 / 面前预留 HeadAnchor（USceneComponent）：未来挂眼神目标、注视锚点
 *
 * 文本交互：
 *   - 本类不负责显示文字（已重构到 UNarrativeSubsystem）
 *   - 需要显示文字时，Director 直接走 Subsystem，与 NPC 无耦合
 *
 * 使用方式：
 *   蓝图里继承本类（BP_TutorialNPC），在 Viewport 中：
 *     1. 给 Mesh 挂一个 SkeletalMesh（例如 SKM_Manny）+ AnimBlueprint
 *     2. 把 HeadAnchor 拖到头部位置（为将来眼神系统准备）
 */
UCLASS()
class CYBERCAFE_API ATutorialNPC : public ACharacter
{
    GENERATED_BODY()

public:
    ATutorialNPC();

protected:
    /** 头部挂载点：预留给未来的"眼神看向玩家手部"等 Look-At 系统使用 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|NPC",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> HeadAnchor;
};
