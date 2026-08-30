// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LiquidVolumeWidget.generated.h"

class UTextBlock;

/**
 * ULiquidVolumeWidget
 * 液体容量显示 Widget：显示 "当前mL / 最大mL"。
 *
 * 使用方式：
 *   - 在编辑器里创建蓝图子类 WBP_LiquidVolume（继承本类）；
 *   - 在蓝图中放一个 UTextBlock，命名为 "VolumeText"（会通过 BindWidget 自动绑定）；
 *   - 由 ACupActor 的 UWidgetComponent 承载并显示。
 */
UCLASS(BlueprintType, Blueprintable)
class CYBERCAFE_API ULiquidVolumeWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /**
     * 刷新显示的容量。
     * @param CurrentML 当前液体体积（mL）
     * @param MaxML     容器最大容量（mL）
     */
    UFUNCTION(BlueprintCallable, Category = "Liquid|UI")
    void UpdateVolume(float CurrentML, float MaxML);

protected:
    /**
     * 绑定到蓝图子类中名为 "VolumeText" 的 UTextBlock。
     * 允许为空（美术还没做蓝图时不至于崩）。
     */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> VolumeText;
};
