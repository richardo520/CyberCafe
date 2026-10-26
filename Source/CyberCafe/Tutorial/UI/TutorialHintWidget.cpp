// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/UI/TutorialHintWidget.h"
#include "Components/TextBlock.h"

void UTutorialHintWidget::SetHintText(const FText& InText)
{
    // 优先用 C++ 自动绑定的 Text_Hint：只要蓝图里放了同名 TextBlock，这里就能直接写
    if (Text_Hint)
    {
        Text_Hint->SetText(InText);
        UE_LOG(LogTemp, Warning, TEXT("[HintWidget] SetHintText via BindWidget: '%s'"), *InText.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[HintWidget] Text_Hint is NOT bound! Please add a TextBlock named 'Text_Hint' in WBP_TutorialHint."));
    }

    // 蓝图事件照样触发，便于美术做额外动画 / 音效
    ReceiveSetHintText(InText);
}
