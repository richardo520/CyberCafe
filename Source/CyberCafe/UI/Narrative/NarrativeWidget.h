// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NarrativeWidget.generated.h"

class UTextBlock;
class UTimerManager;

/**
 * UNarrativeWidget
 *
 * 全局叙事 UI 的 C++ 基类（文字冒险风格的身前文本框）。
 *
 * 职责：
 *   - 对外暴露 SetText / SetSpeaker / PlayTypewriter 等接口
 *   - 自动绑定 WBP 里命名为 Text_Body、Text_Speaker 的 TextBlock
 *   - 具体视觉（底板、字体、动画）全部交给蓝图子类 WBP_NarrativeHUD 实现
 *
 * 使用方式：
 *   蓝图里继承本类（WBP_NarrativeHUD），在 Designer 里：
 *     1) 放一个 TextBlock 命名为 Text_Body，用于显示正文
 *     2) 可选：放一个 TextBlock 命名为 Text_Speaker，用于显示说话人（未设置时隐藏）
 *     3) 其余底板 / 边框 / 动画自行发挥
 */
UCLASS(BlueprintType, Blueprintable)
class CYBERCAFE_API UNarrativeWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 立即把正文设为 InText（覆盖打字机进度） */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetText(const FText& InText);

    /** 设置说话人名字（空字符串则隐藏说话人栏） */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetSpeaker(const FText& InSpeaker);

    /**
     * 以打字机效果播放文本。
     * @param InText        完整正文
     * @param CharsPerSec   每秒推进多少字；<=0 表示立即全部显示
     */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void PlayTypewriter(const FText& InText, float CharsPerSec);

    /** 强制把打字机推到结尾（显示完整文本） */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SkipTypewriter();

    /** 蓝图可实现：文本更新时的扩展钩子（播放动画 / 音效） */
    UFUNCTION(BlueprintImplementableEvent, Category = "Narrative",
        meta = (DisplayName = "On Text Updated"))
    void ReceiveTextUpdated(const FText& NewText);

protected:
    virtual void NativeDestruct() override;

    /** 正文 TextBlock（蓝图里命名为 Text_Body） */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
    TObjectPtr<UTextBlock> Text_Body;

    /** 说话人 TextBlock（可选，蓝图里命名为 Text_Speaker） */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Transient)
    TObjectPtr<UTextBlock> Text_Speaker;

private:
    /** 打字机定时器：每次推进一字 */
    FTimerHandle TypewriterTimer;

    /** 打字机完整目标文本（用于判断是否已播完） */
    FString FullText;

    /** 当前已显示字符数 */
    int32 VisibleCharCount = 0;

    /** 每字间隔（秒） */
    float TypewriterInterval = 0.f;

    /** 向正文 TextBlock 写字；若未绑定则只调蓝图钩子 */
    void ApplyBodyText(const FText& InText);

    /** 定时器回调：推进一字 */
    void TickTypewriter();
};
