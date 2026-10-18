// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TutorialNPC.generated.h"

class UWidgetComponent;
class USceneComponent;
class UTutorialHintWidget;

/**
 * ATutorialNPC
 *
 * 教学 NPC：吧台对面站的那个角色。
 *
 * 为何用 Character：
 *   - 预留未来：动画蓝图、眼神看向玩家手部、走位、AI 感知
 *   - 现阶段只用它的 SkeletalMesh + 头顶 Widget 做"摆件 + 气泡"
 *
 * 构造层面：
 *   - 关闭 CharacterMovement 的物理 Tick（Pawn 本来就不动）
 *   - CapsuleComponent 碰撞关掉：避免挡住 VR 玩家 / 吃物理冲击
 *   - 顶上一个 HeadAnchor（USceneComponent）作为气泡挂载点
 *     由蓝图子类在编辑器里把它拖到骨骼头顶的大致位置
 *   - 一个 UWidgetComponent (HintBubble)：World Space、朝向摄像机
 *
 * 对外接口：
 *   - ShowHint(FText)：显示气泡 + 更新文本
 *   - HideHint()      ：隐藏气泡
 *
 * 使用方式：
 *   蓝图里继承本类（BP_TutorialNPC），在 Viewport 中：
 *     1. 给 Mesh 挂一个 SkeletalMesh（例如 SKM_Manny）+ AnimBlueprint
 *     2. 把 HeadAnchor 拖到头顶位置
 *     3. HintBubble 的 WidgetClass 指向 WBP_TutorialHint
 */
UCLASS()
class CYBERCAFE_API ATutorialNPC : public ACharacter
{
    GENERATED_BODY()

public:
    ATutorialNPC();

    /** 显示并更新气泡文本 */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|NPC")
    void ShowHint(const FText& InText);

    /** 隐藏气泡 */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|NPC")
    void HideHint();

protected:
    virtual void BeginPlay() override;

    /** 头顶挂载点：蓝图里拖到骨骼头部上方合适位置 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|NPC",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> HeadAnchor;

    /** 头顶气泡 Widget（World Space） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tutorial|NPC",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UWidgetComponent> HintBubble;

    /** 蓝图里指定的气泡 Widget 类（WBP_TutorialHint） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|NPC")
    TSubclassOf<UTutorialHintWidget> HintWidgetClass;

private:
    /** 内部：拿到当前 HintBubble 里实例化的 UserWidget 并转型 */
    UTutorialHintWidget* GetHintWidget() const;
};
