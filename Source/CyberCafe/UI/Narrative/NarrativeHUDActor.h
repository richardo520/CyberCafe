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
 * 玩家身前的"叙事面板"宿主 Actor。
 *
 * 工作方式：
 *   - 持有一个 World Space 的 UWidgetComponent，Widget 类是 UNarrativeWidget 的蓝图子类
 *   - 每帧跟随玩家摄像机：位置贴到相机前方 FollowDistance，水平转身跟你，俯仰保持水平
 *     （避免抬头 / 低头时面板跟着飞）
 *   - 通过 GetNarrativeWidget() 把 UI 暴露给 NarrativeSubsystem 操作
 *
 * 一般不要直接创建该 Actor，而是通过 UNarrativeSubsystem 的 API。
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

    /** 跟随玩家摄像机前方的距离（厘米）。默认 200 = 2m */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD",
        meta = (ClampMin = "50.0", ClampMax = "500.0"))
    float FollowDistance = 200.f;

    /** 相对视线中心的垂直偏移（厘米），负值 = 面板位于视线下方 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD")
    float VerticalOffset = -30.f;

    /** 位置平滑插值速率（越大越贴紧） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD",
        meta = (ClampMin = "0.0"))
    float LocationInterpSpeed = 8.f;

    /** 旋转平滑插值速率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD",
        meta = (ClampMin = "0.0"))
    float RotationInterpSpeed = 8.f;

    /** 跟随策略：true 则忽略摄像机俯仰（Pitch），面板保持水平 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD")
    bool bIgnoreCameraPitch = true;

    /** 面板宽高（像素），决定 WidgetComponent 的 DrawSize */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD")
    FVector2D DrawSize = FVector2D(800.f, 240.f);

    /** 面板在世界里的物理尺寸比例（1 = 原尺寸，Scale 面板到合适大小） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Narrative|HUD",
        meta = (ClampMin = "0.01"))
    float WorldScale = 0.3f;

private:
    /** 计算目标位置 + 旋转 */
    void ComputeFollowTarget(FVector& OutLocation, FRotator& OutRotation) const;

    /** 第一帧用于避免插值从原点开始 */
    bool bFirstTickSnapped = false;
};
