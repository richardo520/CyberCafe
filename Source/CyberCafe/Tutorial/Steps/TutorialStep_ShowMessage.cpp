// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/Steps/TutorialStep_ShowMessage.h"
#include "Tutorial/TutorialDirector.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"

void UTutorialStep_ShowMessage::EnterStep(ATutorialDirector* InDirector)
{
    Super::EnterStep(InDirector);

    UWorld* World = GetWorld();
    if (!World)
    {
        // 没有 World，直接完成，避免 Step 永远卡住
        MarkCompleted();
        return;
    }

    // 1) 定时器：Duration 秒后自动完成
    if (Duration > 0.f)
    {
        World->GetTimerManager().SetTimer(DurationTimer, this,
            &UTutorialStep_ShowMessage::HandleDurationElapsed, Duration, false);
    }

    // 2) 按键跳过：订阅 PlayerController 的 AnyKey 事件
    if (bAllowSkipByInput)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            if (UInputComponent* IC = PC->InputComponent)
            {
                // bConsume=false：不消费事件，不影响其它系统（抓取等）接收输入
                FInputKeyBinding& Binding = IC->BindKey(EKeys::AnyKey, IE_Pressed, this,
                    &UTutorialStep_ShowMessage::HandleAnyKeyPressed);
                Binding.bConsumeInput = false;
            }
        }
    }

    // 若 Duration <= 0 且不允许跳过，Step 会卡死：给个日志提醒配置问题
    if (Duration <= 0.f && !bAllowSkipByInput)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[TutorialStep_ShowMessage] Duration<=0 and bAllowSkipByInput=false; step will never complete!"));
    }
}

void UTutorialStep_ShowMessage::ExitStep()
{
    // 清掉定时器（无论是因超时完成还是按键跳过）
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DurationTimer);
    }

    // 解绑 AnyKey：遍历 PlayerController 的 InputComponent 找到属于本对象的 KeyBinding 移除
    if (bAllowSkipByInput)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            if (UInputComponent* IC = PC->InputComponent)
            {
                for (int32 i = IC->KeyBindings.Num() - 1; i >= 0; --i)
                {
                    const FInputKeyBinding& B = IC->KeyBindings[i];
                    if (B.KeyDelegate.IsBoundToObject(this))
                    {
                        IC->KeyBindings.RemoveAt(i);
                    }
                }
            }
        }
    }

    Super::ExitStep();
}

void UTutorialStep_ShowMessage::HandleDurationElapsed()
{
    if (!IsCompleted())
    {
        MarkCompleted();
    }
}

void UTutorialStep_ShowMessage::HandleAnyKeyPressed(FKey Key)
{
    // 过滤连续轴输入（摇杆 / 扳机轴值），避免手柄漂移误触；
    // 真正的按下事件都是离散按键，不会被过滤。
    if (Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D())
    {
        return;
    }

    if (!IsCompleted())
    {
        MarkCompleted();
    }
}
