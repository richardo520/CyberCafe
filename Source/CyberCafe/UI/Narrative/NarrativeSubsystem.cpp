// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Narrative/NarrativeSubsystem.h"
#include "UI/Narrative/NarrativeHUDActor.h"
#include "UI/Narrative/NarrativeWidget.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UNarrativeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UNarrativeSubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AutoHideTimer);
    }

    // Actor 本身随 World 销毁，这里只清指针
    HUDActor.Reset();
    HUDWidgetClass = nullptr;

    Super::Deinitialize();
}

void UNarrativeSubsystem::SetHUDWidgetClass(TSubclassOf<UNarrativeWidget> InClass)
{
    HUDWidgetClass = InClass;

    // 如果 HUDActor 已经存在，立即替换它的 WidgetClass
    if (HUDActor.IsValid() && InClass)
    {
        HUDActor->SetWidgetClass(InClass);
    }
}

void UNarrativeSubsystem::EnsureHUDSpawned()
{
    if (HUDActor.IsValid())
    {
        return;
    }
    if (!HUDWidgetClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NarrativeSubsystem] HUDWidgetClass is not set. Call SetHUDWidgetClass(WBP_NarrativeHUD) first."));
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
#if WITH_EDITOR
    Params.ObjectFlags |= RF_Transient; // 不参与保存
#endif

    ANarrativeHUDActor* Spawned = World->SpawnActor<ANarrativeHUDActor>(
        ANarrativeHUDActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);

    if (!Spawned)
    {
        UE_LOG(LogTemp, Error, TEXT("[NarrativeSubsystem] Failed to spawn ANarrativeHUDActor"));
        return;
    }

    Spawned->SetWidgetClass(HUDWidgetClass);
    HUDActor = Spawned;
}

void UNarrativeSubsystem::ShowText(const FText& InText, float AutoHideSeconds)
{
    EnsureHUDSpawned();
    if (!HUDActor.IsValid())
    {
        return;
    }

    HUDActor->ShowPanel();

    if (UNarrativeWidget* W = HUDActor->GetNarrativeWidget())
    {
        W->SetText(InText);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NarrativeSubsystem] NarrativeWidget not ready; WBP must inherit UNarrativeWidget"));
    }

    // 处理 AutoHide
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AutoHideTimer);
        if (AutoHideSeconds > 0.f)
        {
            World->GetTimerManager().SetTimer(AutoHideTimer, this,
                &UNarrativeSubsystem::HandleAutoHide, AutoHideSeconds, false);
        }
    }
}

void UNarrativeSubsystem::PlayTypewriter(const FText& InText, float CharsPerSec, float AutoHideSeconds)
{
    EnsureHUDSpawned();
    if (!HUDActor.IsValid())
    {
        return;
    }

    HUDActor->ShowPanel();

    if (UNarrativeWidget* W = HUDActor->GetNarrativeWidget())
    {
        W->PlayTypewriter(InText, CharsPerSec);
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AutoHideTimer);
        if (AutoHideSeconds > 0.f)
        {
            World->GetTimerManager().SetTimer(AutoHideTimer, this,
                &UNarrativeSubsystem::HandleAutoHide, AutoHideSeconds, false);
        }
    }
}

void UNarrativeSubsystem::SetSpeaker(const FText& InSpeaker)
{
    EnsureHUDSpawned();
    if (!HUDActor.IsValid())
    {
        return;
    }
    if (UNarrativeWidget* W = HUDActor->GetNarrativeWidget())
    {
        W->SetSpeaker(InSpeaker);
    }
}

void UNarrativeSubsystem::HideText()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AutoHideTimer);
    }
    if (HUDActor.IsValid())
    {
        HUDActor->HidePanel();
    }
}

bool UNarrativeSubsystem::IsVisible() const
{
    if (!HUDActor.IsValid())
    {
        return false;
    }
    // Actor 隐藏即视为不可见
    return !HUDActor->IsHidden() && HUDActor->GetNarrativeWidget() != nullptr;
}

void UNarrativeSubsystem::HandleAutoHide()
{
    HideText();
}
