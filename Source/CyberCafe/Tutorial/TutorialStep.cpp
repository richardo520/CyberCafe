// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/TutorialStep.h"
#include "Tutorial/TutorialDirector.h"
#include "Engine/World.h"

void UTutorialStep::EnterStep(ATutorialDirector* InDirector)
{
    Director = InDirector;
    bCompleted = false;
    // 具体订阅由子类重写时在 Super::EnterStep 之后做
}

void UTutorialStep::ExitStep()
{
    // 具体解绑由子类重写时在 Super::ExitStep 之前做
    Director = nullptr;
}

void UTutorialStep::MarkCompleted()
{
    if (bCompleted)
    {
        return;
    }
    bCompleted = true;
    OnStepCompleted.Broadcast(this);
}

UWorld* UTutorialStep::GetWorld() const
{
    // UObject 默认没有 World；通过 Director 转发，便于子类里直接用 GetWorld() 查询
    // 注意：CDO / BP 编辑态下 Director 为空，这里返回 nullptr（Super 的默认行为）
    if (Director)
    {
        return Director->GetWorld();
    }
    return nullptr;
}
