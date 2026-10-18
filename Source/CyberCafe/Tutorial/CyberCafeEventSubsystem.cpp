// Fill out your copyright notice in the Description page of Project Settings.

#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UCyberCafeEventSubsystem* UCyberCafeEventSubsystem::Get(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return nullptr;
    }

    // GEngine->GetWorldFromContextObject 的轻量替代：直接走 UObject::GetWorld
    const UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return nullptr;
    }

    UGameInstance* GI = World->GetGameInstance();
    if (!GI)
    {
        return nullptr;
    }

    return GI->GetSubsystem<UCyberCafeEventSubsystem>();
}
