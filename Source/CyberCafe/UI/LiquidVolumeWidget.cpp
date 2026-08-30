// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/LiquidVolumeWidget.h"
#include "Components/TextBlock.h"

void ULiquidVolumeWidget::UpdateVolume(float CurrentML, float MaxML)
{
    if (!VolumeText)
    {
        return;
    }

    // 固定整数显示，例："120 mL / 200 mL"
    const FString Text = FString::Printf(TEXT("%.0f mL / %.0f mL"), CurrentML, MaxML);
    VolumeText->SetText(FText::FromString(Text));
}
