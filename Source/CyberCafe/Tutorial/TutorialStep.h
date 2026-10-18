// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TutorialStep.generated.h"

class ATutorialDirector;
class UTutorialStep;

/**
 * 步骤完成事件：Step 自身在完成条件触发时 Broadcast。
 * Director 订阅这个事件，收到后推进到下一个 Step。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTutorialStepCompletedSignature,
    UTutorialStep*, CompletedStep);

/**
 * UTutorialStep
 *
 * 单个教学步骤的基类。每个具体步骤（抓取、远程抓取、倒液、递杯……）
 * 继承本类并重写 EnterStep / ExitStep 实现自己的"开始监听"与"停止监听"。
 *
 * 生命周期（由 ATutorialDirector 调度）：
 *   1) Director 创建实例 → 设置 Owner 为 Director
 *   2) Director 调用 EnterStep(Director)：Step 开始订阅完成条件
 *      —— 同时 Director 读取 HintText 让 NPC 显示气泡
 *   3) Step 自己判定完成 → Broadcast OnStepCompleted
 *   4) Director 调用 ExitStep：Step 解绑所有订阅
 *
 * 设计原则：
 *   - Step 本身不持有 UI、不操控 NPC；只负责"检测玩家做了什么"
 *   - UI 由 Director 用 Step 暴露的 HintText / CompletedText 驱动 NPC 显示
 */
UCLASS(Blueprintable, EditInlineNew, Abstract)
class CYBERCAFE_API UTutorialStep : public UObject
{
    GENERATED_BODY()

public:
    /** 本步骤进行中气泡显示的提示文字 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step",
        meta = (MultiLine = true))
    FText HintText;

    /** 本步骤完成后气泡显示的"做得好"文本（给 Director 临时切气泡用） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Step",
        meta = (MultiLine = true))
    FText CompletedText;

    /** 步骤完成事件：具体 Step 判定完成时广播 */
    UPROPERTY(BlueprintAssignable, Category = "Tutorial|Step")
    FOnTutorialStepCompletedSignature OnStepCompleted;

    /**
     * 进入步骤：开始订阅完成条件。
     * @param InDirector 调度本步骤的 Director（Step 可以通过它拿到 NPC / 关卡上下文）
     */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|Step")
    virtual void EnterStep(ATutorialDirector* InDirector);

    /**
     * 退出步骤：解绑所有订阅，清理临时资源。
     * Director 在推进到下一个 Step 之前调用。
     */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|Step")
    virtual void ExitStep();

    /** 当前步骤是否已经完成 */
    UFUNCTION(BlueprintPure, Category = "Tutorial|Step")
    bool IsCompleted() const { return bCompleted; }

    /** 调度本步骤的 Director */
    UFUNCTION(BlueprintPure, Category = "Tutorial|Step")
    ATutorialDirector* GetDirector() const { return Director; }

    //~ UObject interface
    virtual UWorld* GetWorld() const override;
    //~ End of UObject interface

protected:
    /**
     * 具体 Step 判定完成时调用：标记状态 + 广播事件。
     * 子类不要直接 Broadcast OnStepCompleted，走本函数更安全（防止重复触发）。
     */
    UFUNCTION(BlueprintCallable, Category = "Tutorial|Step")
    void MarkCompleted();

    /** 调度本步骤的 Director 引用（EnterStep 时设置） */
    UPROPERTY(Transient)
    TObjectPtr<ATutorialDirector> Director;

    /** 是否已完成（防止重复触发 OnStepCompleted） */
    UPROPERTY(Transient)
    bool bCompleted = false;
};
