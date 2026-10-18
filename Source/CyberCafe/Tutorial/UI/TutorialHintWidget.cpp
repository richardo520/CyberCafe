// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/UI/TutorialHintWidget.h"

void UTutorialHintWidget::SetHintText(const FText& InText)
{
    // 转发到蓝图事件；蓝图子类里把 InText 写入 TextBlock 即可
    ReceiveSetHintText(InText);
}
