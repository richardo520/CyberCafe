// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Liquid/LiquidContainerActor.h"
#include "CupActor.generated.h"

class UWidgetComponent;
class ULiquidVolumeWidget;

/**
 * ACupActor
 * 酒杯：可倒液容器的具体实现之一。
 *
 * 与酒瓶共用基类的倒液流程（倾斜出液、SphereTrace、加/扣液、水花）。
 * 本子类只做一件事：构造时给"杯子风格"的默认值——小容量、慢出液、更大倾角阈值，且默认接受被倒入。
 *
 * 额外功能：在杯口上方挂一个 3D UI（UWidgetComponent），
 *   实时显示 "当前mL / 最大mL"。只要杯里有液体就显示，空杯时自动隐藏。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ACupActor : public ALiquidContainerActor
{
    GENERATED_BODY()

public:
    ACupActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    /**
     * 编辑器内的构造脚本：
     * 用于在编辑器 Viewport 里直接预览 3D UI（不用进 PIE / 戴头显）。
     * 只在非游戏世界（编辑器场景 / 蓝图预览）中生效，PIE 与打包版本完全走 BeginPlay 那条路径。
     */
    virtual void OnConstruction(const FTransform& Transform) override;

    //=====================================================================
    // 编辑器预览
    //=====================================================================

    /**
     * 是否在编辑器 Viewport 中预览 3D UI（不影响运行时行为）。
     * 打开后：在关卡编辑器/蓝图预览里，UI 会强制显示，并按 EditorPreviewCurrentML / EditorPreviewMaxML 填一个假的数值，
     * 方便调整 VolumeWidgetComp 的相对 Transform / VolumeWidgetDrawSize 等。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|UI|EditorPreview")
    bool bEditorPreviewWidget = true;

    /** 编辑器预览时假装的当前液量（mL），只影响 Viewport 显示 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|UI|EditorPreview", meta = (EditCondition = "bEditorPreviewWidget"))
    float EditorPreviewCurrentML = 120.f;

    /** 编辑器预览时假装的最大容量（mL），只影响 Viewport 显示 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|UI|EditorPreview", meta = (EditCondition = "bEditorPreviewWidget"))
    float EditorPreviewMaxML = 200.f;

    //=====================================================================
    // 容量 UI
    //=====================================================================

    /** 头顶容量显示的 3D Widget 组件（World Space） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|UI")
    TObjectPtr<UWidgetComponent> VolumeWidgetComp;

    /**
     * 蓝图指定的 Widget 类（WBP_LiquidVolume）。
     * 必须继承自 ULiquidVolumeWidget。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|UI")
    TSubclassOf<ULiquidVolumeWidget> VolumeWidgetClass;

    /** Widget 面板在 World Space 下的绘制尺寸（像素） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|UI")
    FVector2D VolumeWidgetDrawSize = FVector2D(400.f, 80.f);

    /** 是否让 Widget 每帧朝向玩家相机（只保留 Yaw，避免歪脖子） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|UI")
    bool bFaceCamera = true;

protected:
    /** 响应基类的液体变化广播：刷新 UI 文本 + 空/非空切换可见性 */
    UFUNCTION()
    void HandleLiquidChanged(float NewFillAmount, FLinearColor NewColor);

    /** 根据 FillAmount 判断当前是否应该显示 UI */
    void RefreshVolumeUIVisibility();

    /** 把 CurrentML / MaxML 推到 Widget */
    void PushVolumeToWidget();

    /** 让 Widget 面向玩家相机 */
    void UpdateWidgetFacing();
};
