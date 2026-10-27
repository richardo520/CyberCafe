// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "TutorialDirector.generated.h"

class ATutorialNPC;
class UTutorialStep;
class UNarrativeWidget;

/**
 * 整个教学流程完成时广播（可供成就 / 关卡事件接）。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTutorialFinishedSignature);

/**
 * ATutorialDirector
 *
 * 教学流程导演：按顺序推进 StepClasses 列表里的所有 UTutorialStep。
 *
 * 工作流程：
 *   1) BeginPlay：
 *      - 为 StepClasses 中的每个类实例化一个 UTutorialStep
 *      - 把 NarrativeHUDWidgetClass 注册到 UNarrativeSubsystem
 *      - 如果 bAutoStart = true，立即启动第一个 Step
 *   2) 启动 Step：
 *      - 调用 Step->EnterStep(this)
 *      - 让 Narrative HUD 显示 Step->HintText
 *      - 订阅 Step->OnStepCompleted
 *   3) Step 完成：
 *      - 让 Narrative HUD 临时显示 Step->CompletedText（"做得好！"）
 *      - 延迟 CheerDuration 秒
 *      - 调用 Step->ExitStep + 推进 CurrentStepIndex
 *      - 启动下一个 Step（没有下一个则结束整体流程）
 *   4) 整体完成：
 *      - Narrative HUD 显示 FinishedText 并淡出
 *      - Broadcast OnTutorialFinished
 */
UCLASS()
class CYBERCAFE_API ATutorialDirector : public AActor
{
    GENERATED_BODY()

public:
    ATutorialDirector();

    //~ Begin AActor
    virtual void BeginPlay() override;
    //~ End AActor

    /** 手动启动教学（bAutoStart = false 时用） */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|Director")
    void StartTutorial();

    /** 立即结束整个教学（可供"跳过"按钮） */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|Director")
    void SkipTutorial();

    /** 整个教学完成时广播 */
    UPROPERTY(BlueprintAssignable, Category = "Tutorial|Director")
    FOnTutorialFinishedSignature OnTutorialFinished;

    /**
     * NPC 引用：关卡里拖进来；
     * 当前只用于语义上的"吧台对面有个人"摆件 + 未来动画 / 眼神朝向。
     * 不再负责显示文字。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Director")
    TObjectPtr<ATutorialNPC> NPCRef;

    /**
     * 教学步骤序列：按顺序执行。
     * 使用 Instanced 让每个 Step 可以在 Details 面板里展开编辑（RequiredActor 等参数）。
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Tutorial|Director")
    TArray<TObjectPtr<UTutorialStep>> Steps;

    /** 是否在 BeginPlay 自动启动 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Director")
    bool bAutoStart = true;

    /** Step 完成后"做得好"气泡显示时长（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Director",
        meta = (ClampMin = "0.0"))
    float CheerDuration = 1.5f;

    /** 全部步骤完成后显示的最终文本 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Director",
        meta = (MultiLine = true))
    FText FinishedText;

    /**
     * 整个教学结束后，FinishedText 显示多久后自动隐藏（秒）。
     * <= 0 则一直显示直到外部手动调 NarrativeSubsystem::HideText。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Director",
        meta = (ClampMin = "0.0"))
    float FinishedTextAutoHideSeconds = 3.f;

    /**
     * 教学使用的叙事 HUD Widget 蓝图类。
     * BeginPlay 时会注入 UNarrativeSubsystem。
     * 关卡里只要有一个 Director，这里配好 WBP_NarrativeHUD 即可。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Director")
    TSubclassOf<UNarrativeWidget> NarrativeHUDWidgetClass;

protected:
    /** 启动指定索引的 Step */
    void StartStepAt(int32 Index);

    /** Step 完成回调（Step 的 OnStepCompleted 触发时调） */
    UFUNCTION()
    void HandleStepCompleted(UTutorialStep* CompletedStep);

    /** 延迟器：从 CompletedText 过渡到下一步 */
    void AdvanceAfterCheer();

    /** 整个教学结束 */
    void FinishTutorial();

    /** 向 NarrativeSubsystem 显示一段文本（内部封装 null 检查） */
    void ShowNarrative(const FText& InText, float AutoHideSeconds = 0.f);

    /** 当前正在进行的 Step 索引（-1 表示未开始 / 已结束） */
    UPROPERTY(Transient)
    int32 CurrentStepIndex = -1;

    /** CheerDuration 延时句柄 */
    FTimerHandle CheerTimerHandle;
};
