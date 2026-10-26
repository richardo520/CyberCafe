// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialHintWidget.generated.h"

class UTextBlock;

/**
 * UTutorialHintWidget
 *
 * 教学气泡 UI 的 C++ 基类。
 *
 * 本类只定义接口，具体视觉（气泡框 / 文本 / 箭头 / 动画）全部交给蓝图子类
 * (WBP_TutorialHint) 实现，便于美术在编辑器里直接调整。
 *
 * 使用方式：
 *   - 蓝图里继承本类，设计出气泡 UI
 *   - 在蓝图子类中重写 ReceiveSetHintText 事件，把参数文本写入 TextBlock
 *   - ATutorialNPC 持有一个 UWidgetComponent，WidgetClass 指向该 WBP
 *   - Director / NPC 通过本类的 SetHintText(FText) 更新气泡文字
 */
UCLASS(BlueprintType, Blueprintable)
class CYBERCAFE_API UTutorialHintWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /**
     * 设置气泡文字。
     * C++ 侧统一调用这个入口，内部触发蓝图事件 ReceiveSetHintText。
     */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
    void SetHintText(const FText& InText);

protected:
    /**
     * 自动绑定 WBP 里命名为 Text_Hint 的 TextBlock。
     * optional=true 表示蓝图里没有这个控件也不会编译失败，但缺失时 C++ 会用日志告警。
     * 推荐做法：直接在 WBP_TutorialHint 里放一个 TextBlock 并命名为 Text_Hint。
     */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
    TObjectPtr<UTextBlock> Text_Hint;

    /**
     * 蓝图可实现事件：收到新文本时的 UI 更新逻辑（可选）。
     * 如果绑定了 Text_Hint，C++ 会自动写字；本事件主要用于额外的美术扩展
     *（例如播放"冒出"动画、音效等）。
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Tutorial|Hint",
        meta = (DisplayName = "On Set Hint Text"))
    void ReceiveSetHintText(const FText& NewText);
};
