// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Coffee/Grinder/GrinderHandleActor.h"
#include "Coffee/Grinder/GrinderDrawerActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "NiagaraComponent.h"

ACoffeeGrinderActor::ACoffeeGrinderActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // 主体 Mesh 作为 Root，模拟物理，允许被抓取（跟 ABottleActor 一致）
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    SetRootComponent(BodyMesh);
    BodyMesh->SetSimulatePhysics(true);
    BodyMesh->SetCollisionProfileName(TEXT("PhysicsActor"));

    // 抓取组件：主体走 Free 模式，跟瓶子一致
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(BodyMesh);
    GrabComp->GrabType = EGrabType::Free;
    GrabComp->GrabPriority = 0;

    // 挂点：把手在顶端，抽屉在底部前侧，豆入口在顶端（Bean）
    HandleMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HandleMountPoint"));
    HandleMountPoint->SetupAttachment(BodyMesh);

    DrawerMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("DrawerMountPoint"));
    DrawerMountPoint->SetupAttachment(BodyMesh);

    BeanEntryPoint = CreateDefaultSubobject<USceneComponent>(TEXT("BeanEntryPoint"));
    BeanEntryPoint->SetupAttachment(BodyMesh);

    // FX / SFX：默认不激活，等运行时逻辑（后续接入）再开
    GrindFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("GrindFX"));
    GrindFX->SetupAttachment(BodyMesh);
    GrindFX->bAutoActivate = false;

    GrindSFX = CreateDefaultSubobject<UAudioComponent>(TEXT("GrindSFX"));
    GrindSFX->SetupAttachment(BodyMesh);
    GrindSFX->bAutoActivate = false;

    AccumulatedHandleAngleDeg = 0.f;
    HandleRef = nullptr;
    DrawerRef = nullptr;
}

void ACoffeeGrinderActor::BeginPlay()
{
    Super::BeginPlay();

    SpawnChildParts();
}

void ACoffeeGrinderActor::SpawnChildParts()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // ---- 把手 ----
    if (HandleClass && HandleMountPoint)
    {
        const FTransform MountXform = HandleMountPoint->GetComponentTransform();
        AGrinderHandleActor* Handle = World->SpawnActor<AGrinderHandleActor>(
            HandleClass, MountXform, SpawnParams);
        if (Handle)
        {
            Handle->AttachToGrinder(this, HandleMountPoint);
            HandleRef = Handle;
        }
    }

    // ---- 抽屉 ----
    if (DrawerClass && DrawerMountPoint)
    {
        const FTransform MountXform = DrawerMountPoint->GetComponentTransform();
        AGrinderDrawerActor* Drawer = World->SpawnActor<AGrinderDrawerActor>(
            DrawerClass, MountXform, SpawnParams);
        if (Drawer)
        {
            Drawer->AttachToGrinder(this, DrawerMountPoint);
            DrawerRef = Drawer;
        }
    }
}

void ACoffeeGrinderActor::OnHandleRotated(float DeltaAngleDeg)
{
    if (FMath::IsNearlyZero(DeltaAngleDeg))
    {
        return;
    }

    AccumulatedHandleAngleDeg += DeltaAngleDeg;

    // 豆 → 粉 转换等实际研磨逻辑：后续再接。这里先只广播事件方便蓝图 / 调试对接。
    OnHandleTurned.Broadcast(DeltaAngleDeg, AccumulatedHandleAngleDeg);
}

FTransform ACoffeeGrinderActor::GetHandleMountWorldTransform() const
{
    return HandleMountPoint ? HandleMountPoint->GetComponentTransform() : GetActorTransform();
}

FTransform ACoffeeGrinderActor::GetDrawerMountWorldTransform() const
{
    return DrawerMountPoint ? DrawerMountPoint->GetComponentTransform() : GetActorTransform();
}

bool ACoffeeGrinderActor::IsHeld() const
{
    return GrabComp ? GrabComp->IsHeld() : false;
}

