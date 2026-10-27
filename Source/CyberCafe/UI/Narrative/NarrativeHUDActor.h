// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NarrativeHUDActor.generated.h"

class UWidgetComponent;
class UNarrativeWidget;

/**
 * ANarrativeHUDActor
 *
 * 场景中的"叙事告示牌"宿主 Actor。
 *
 * 工作方式：
 *   - 持有一个 World Space 的 UWidgetComponent，Widget 类是 UNarrativeWidget 的蓝图子类
 *   - 位置：**完全由关卡中的 Actor Transform 决定**（可以直接拖进关卡，摆到桌子上方）
 *   - 朝向：
 *       * Yaw / Roll 保持关卡中摆放时的朝向（不会扭过来追玩家）
 *       * Pitch 根据玩家摄像机位置做平滑偏转（玩家坐下 → 面板低头，站起来 → 面板正向）
 *   - 通过 GetNarrativeWidget() 把 UI 暴露给 NarrativeSubsystem 操作
 *
 * 使用方式：
 *   1) 在关卡中直接放置一个 BP_NarrativeHUD（基于本类的蓝图），放到合适位置
 *   2) UNarrativeSubsystem 会自动发现并复用场景里现有的实例
 *   3) 没有放置时，Subsystem 会在原点 Spawn 一个（兜底，不建议依赖）
 */
UCLASS()
class CYBERCAFE_API ANarrativeHUDActor : public AActor
{
    GENERATED_BODY()

public:
    ANarrativeHUDActor();

    virtual void Tick(float DeltaSeconds) override;

    /** 显示 / 隐藏面板 */
    void ShowPanel();
    void HidePanel();

    /** 拿到实例化的 UI Widget（C++ 基类）；Subsystem 用它调 SetText 等 */
    UNarrativeWidget* GetNarrativeWidget() const;

    /** 由 Subsystem 在 Spawn 后设置：Widget 的蓝图子类 */
    void SetWidgetClass(TSubclassOf<UNarrativeWidget> InClass);

protected:
    virtual void BeginPlay() override;

    /** Widget 组件：World Space 显示 UI */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Narrative|HUD")
    TObjectPtr<UWidgetComponent> PanelWidgetComp;

    /** 是否让面板朝向玩家（只偏 Pitch，Yaw 固定）。关闭则纯静态朝向 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD")
    bool bFaceCamera = true;

    /** Pitch 偏转最大角度（度），限制面板不要翘得太夸张 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD",
        meta = (ClampMin = "0.0", ClampMax = "89.0"))
    float MaxPitchDegrees = 60.f;

    /** 旋转平滑插值速率（越大越贴紧玩家视角，越小越慵懒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD",
        meta = (ClampMin = "0.0"))
    float RotationInterpSpeed = 4.f;

    /** 面板宽高（像素），决定 WidgetComponent 的 DrawSize */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD")
    FVector2D DrawSize = FVector2D(800.f, 240.f);

    /** 面板在世界里的物理尺寸比例（1 = 原尺寸，Scale 面板到合适大小） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD",
        meta = (ClampMin = "0.01"))
    float WorldScale = 0.2f;

private:
    /** 根据玩家摄像机计算目标 Pitch（Yaw / Roll 用关卡初始值） */
    FRotator ComputeTargetRotation() const;

    /** 关卡中摆放时的初始 Yaw / Roll（BeginPlay 缓存，Pitch 由玩家视角动态计算） */
    float InitialYaw = 0.f;
    float InitialRoll = 0.f;
};
