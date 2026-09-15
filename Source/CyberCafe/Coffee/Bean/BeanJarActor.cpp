// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Bean/BeanJarActor.h"
#include "Coffee/Bean/BeanJarLidActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

ABeanJarActor::ABeanJarActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // 罐身 Mesh 作为 Root，模拟物理（跟瓶子风格一致，可被 Free 抓取）
    JarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JarMesh"));
    SetRootComponent(JarMesh);
    JarMesh->SetSimulatePhysics(true);
    JarMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
    JarMesh->SetNotifyRigidBodyCollision(true);

    // 抓取组件：Free
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(JarMesh);
    GrabComp->GrabType = EGrabType::Free;
    GrabComp->GrabPriority = 0;

    // 罐内豆堆（可见性 & 缩放由 UpdateBeanPileVisual 控制）
    BeanPileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeanPileMesh"));
    BeanPileMesh->SetupAttachment(JarMesh);
    BeanPileMesh->SetSimulatePhysics(false);
    BeanPileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BeanPileMesh->SetVisibility(true);

    LidClass               = nullptr;
    LidSocketName          = TEXT("LidSocket");
    TotalBeanCapacityGrams = 200.f;  // 一罐约 200g（足够磨很多次）
    InitialFillRatio       = 1.f;

    LidActor         = nullptr;
    CurrentBeanGrams = 0.f;
    BeanPileInitialScale = FVector::OneVector;
}

void ABeanJarActor::BeginPlay()
{
    Super::BeginPlay();

    // 缓存豆堆初始 Scale
    if (BeanPileMesh)
    {
        BeanPileInitialScale = BeanPileMesh->GetRelativeScale3D();
    }

    // 初始化豆量
    CurrentBeanGrams = TotalBeanCapacityGrams * FMath::Clamp(InitialFillRatio, 0.f, 1.f);
    UpdateBeanPileVisual();

    // Spawn 罐盖并 Attach 到 LidSocket
    if (LidClass && JarMesh)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            FActorSpawnParameters SpawnParams;
            SpawnParams.Owner = this;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            const FTransform SocketXform = JarMesh->GetSocketTransform(LidSocketName, RTS_World);
            LidActor = World->SpawnActor<ABeanJarLidActor>(LidClass, SocketXform, SpawnParams);
            if (LidActor)
            {
                LidActor->AttachToJar(this, LidSocketName);
            }
        }
    }
}

//=====================================================================
// API
//=====================================================================

bool ABeanJarActor::IsOpen() const
{
    return LidActor ? LidActor->IsOpen() : true;   // 没配置盖子视为常开
}

float ABeanJarActor::TryScoopBeans(float MaxGrams)
{
    if (MaxGrams <= 0.f || CurrentBeanGrams <= KINDA_SMALL_NUMBER)
    {
        return 0.f;
    }
    if (!IsOpen())
    {
        return 0.f;
    }
    const float Actual = FMath::Min(MaxGrams, CurrentBeanGrams);
    if (Actual <= KINDA_SMALL_NUMBER)
    {
        return 0.f;
    }
    CurrentBeanGrams -= Actual;
    UpdateBeanPileVisual();
    return Actual;
}

FVector ABeanJarActor::GetLidSocketWorldLocation() const
{
    if (JarMesh)
    {
        return JarMesh->GetSocketLocation(LidSocketName);
    }
    return GetActorLocation();
}

void ABeanJarActor::UpdateBeanPileVisual()
{
    if (!BeanPileMesh)
    {
        return;
    }
    const float Ratio = (TotalBeanCapacityGrams > KINDA_SMALL_NUMBER)
        ? FMath::Clamp(CurrentBeanGrams / TotalBeanCapacityGrams, 0.f, 1.f)
        : 0.f;
    if (Ratio <= 0.001f)
    {
        BeanPileMesh->SetVisibility(false);
        return;
    }
    BeanPileMesh->SetVisibility(true);
    FVector NewScale = BeanPileInitialScale;
    NewScale.Z = BeanPileInitialScale.Z * Ratio;
    BeanPileMesh->SetRelativeScale3D(NewScale);
}
