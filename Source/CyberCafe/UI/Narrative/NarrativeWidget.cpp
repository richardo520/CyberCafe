// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Narrative/NarrativeWidget.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UNarrativeWidget::SetText(const FText& InText)
{
    // 立即显示：停掉打字机、把全文一次写入
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TypewriterTimer);
    }
    FullText = InText.ToString();
    VisibleCharCount = FullText.Len();
    ApplyBodyText(InText);
}

void UNarrativeWidget::SetSpeaker(const FText& InSpeaker)
{
    if (Text_Speaker)
    {
        Text_Speaker->SetText(InSpeaker);
        // 空字符串时隐藏整条说话人栏，视觉更干净
        Text_Speaker->SetVisibility(
            InSpeaker.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    }
}

void UNarrativeWidget::PlayTypewriter(const FText& InText, float CharsPerSec)
{
    FullText = InText.ToString();

    // 速度 <=0：等价于立即显示，省掉定时器开销
    if (CharsPerSec <= 0.f || FullText.IsEmpty())
    {
        SetText(InText);
        return;
    }

    TypewriterInterval = 1.f / CharsPerSec;
    VisibleCharCount = 0;

    UWorld* World = GetWorld();
    if (!World)
    {
        SetText(InText);
        return;
    }

    World->GetTimerManager().ClearTimer(TypewriterTimer);
    // 从空字符串开始，第一帧由定时器推进
    ApplyBodyText(FText::GetEmpty());
    World->GetTimerManager().SetTimer(TypewriterTimer, this,
        &UNarrativeWidget::TickTypewriter, TypewriterInterval, true);
}

void UNarrativeWidget::SkipTypewriter()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TypewriterTimer);
    }
    VisibleCharCount = FullText.Len();
    ApplyBodyText(FText::FromString(FullText));
}

void UNarrativeWidget::NativeDestruct()
{
    // Widget 销毁时清掉定时器，避免悬挂回调
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TypewriterTimer);
    }
    Super::NativeDestruct();
}

void UNarrativeWidget::ApplyBodyText(const FText& InText)
{
    if (Text_Body)
    {
        Text_Body->SetText(InText);
    }
    ReceiveTextUpdated(InText);
}

void UNarrativeWidget::TickTypewriter()
{
    VisibleCharCount = FMath::Min(VisibleCharCount + 1, FullText.Len());

    const FString Shown = FullText.Left(VisibleCharCount);
    ApplyBodyText(FText::FromString(Shown));

    if (VisibleCharCount >= FullText.Len())
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(TypewriterTimer);
        }
    }
}
