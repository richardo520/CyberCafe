// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "NarrativeSubsystem.generated.h"

class ANarrativeHUDActor;
class UNarrativeWidget;

/**
 * UNarrativeSubsystem
 *
 * 全局叙事 UI 入口（WorldSubsystem，关卡切换会自动重建）。
 *
 * 用法：
 *   auto* N = GetWorld()->GetSubsystem<UNarrativeSubsystem>();
 *   N->ShowText(FText::FromString(TEXT("欢迎来上班")));
 *   N->ShowText(Text, 3.0f);              // 3 秒后自动隐藏
 *   N->PlayTypewriter(Text, 30.f);        // 打字机效果
 *   N->SetSpeaker(FText::FromString(TEXT("老板")));
 *   N->HideText();
 *
 * 实现：首次调用时会 Spawn 一个 ANarrativeHUDActor 并缓存。
 *
 * 面板蓝图类配置：
 *   有两种方式告诉 Subsystem 要用哪个 WBP：
 *     1) 项目设置 → 游戏逻辑模块启动前 BPCallable：调用 SetHUDWidgetClass
 *     2) 在你的 GameMode / Director BeginPlay 里调一次 SetHUDWidgetClass
 *   未设置时 Subsystem 不会 Spawn Actor，调用 ShowText 会被忽略并打警告。
 */
UCLASS()
class CYBERCAFE_API UNarrativeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //~ Begin USubsystem
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    //~ End USubsystem

    /**
     * 配置面板使用的 Widget 蓝图类（WBP_NarrativeHUD）。
     * 必须在首次 ShowText 之前调用；最推荐在 GameMode / Director BeginPlay 里调一次。
     */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void SetHUDWidgetClass(TSubclassOf<UNarrativeWidget> InClass);

    /**
     * 显示一段叙事文本（立即生效，覆盖当前内容）。
     * @param InText        要显示的文本
     * @param AutoHideSeconds 大于 0 时，经过该秒数后自动隐藏；<=0 则一直显示
     */
    UFUNCTION(BlueprintCallable, Category = "Narrative", meta = (AutoCreateRefTerm = "InText"))
    void ShowText(const FText& InText, float AutoHideSeconds = 0.f);

    /**
     * 打字机效果显示。
     * @param InText        完整文本
     * @param CharsPerSec   每秒推进多少字
     * @param AutoHideSeconds 从调用开始计时，>0 自动隐藏
     */
    UFUNCTION(BlueprintCallable, Category = "Narrative", meta = (AutoCreateRefTerm = "InText"))
    void PlayTypewriter(const FText& InText, float CharsPerSec = 30.f, float AutoHideSeconds = 0.f);

    /** 设置 / 清空说话人名字 */
    UFUNCTION(BlueprintCallable, Category = "Narrative", meta = (AutoCreateRefTerm = "InSpeaker"))
    void SetSpeaker(const FText& InSpeaker);

    /** 隐藏面板（面板 Actor 仍在，只是不可见） */
    UFUNCTION(BlueprintCallable, Category = "Narrative")
    void HideText();

    /** 查询当前是否可见 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Narrative")
    bool IsVisible() const;

    /** 返回底层 HUD Actor；一般不需要直接拿 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Narrative")
    ANarrativeHUDActor* GetHUDActor() const { return HUDActor.Get(); }

private:
    /** Spawn HUD Actor（若还没 Spawn 且已有 WidgetClass） */
    void EnsureHUDSpawned();

    /** 自动隐藏回调 */
    void HandleAutoHide();

    /** 场景中的 HUD Actor，首次使用时按需 Spawn */
    UPROPERTY(Transient)
    TWeakObjectPtr<ANarrativeHUDActor> HUDActor;

    /** 外部注入的 Widget 蓝图类 */
    UPROPERTY(Transient)
    TSubclassOf<UNarrativeWidget> HUDWidgetClass;

    /** AutoHide 定时器 */
    FTimerHandle AutoHideTimer;
};
