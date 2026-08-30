// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/LiquidVolumeWidget.h"
#include "Components/TextBlock.h"

void ULiquidVolumeWidget::UpdateVolume(float CurrentML, float MaxML)
{
    if (!VolumeText)
    {
        return;
    }

    // 用配置的 Format 格式化。默认 "%.0f / %.0f mL" → "120 / 200 mL"
    const FString Text = FString::Printf(*VolumeFormat, CurrentML, MaxML);
    VolumeText->SetText(FText::FromString(Text));
}
