// Fill out your copyright notice in the Description page of Project Settings.

#include "Coffee/Bean/CoffeeScoopActor.h"
#include "Coffee/Bean/BeanJarActor.h"
#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "GrabComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"

ACoffeeScoopActor::ACoffeeScoopActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 勺子 Mesh：Root，模拟物理
    ScoopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScoopMesh"));
    SetRootComponent(ScoopMesh);
    ScoopMesh->SetSimulatePhysics(true);
    ScoopMesh->SetCollisionProfileName(TEXT("PhysicsActor"));

    // Snap 抓取：Snap 到勺柄握把（HandSocket 由蓝图配置）
    GrabComp = CreateDefaultSubobject<UGrabComponent>(TEXT("GrabComp"));
    GrabComp->SetupAttachment(ScoopMesh);
    GrabComp->GrabType = EGrabType::Snap;
    GrabComp->GrabPriority = 0;

    // 勺兜采样点（蓝图里拖到勺兜正中央 & 高度略高于勺底）
    BowlPoint = CreateDefaultSubobject<USceneComponent>(TEXT("BowlPoint"));
    BowlPoint->SetupAttachment(ScoopMesh);

    // 勺兜 Overlap 球：只查询，不产生物理
    BowlSphere = CreateDefaultSubobject<USphereComponent>(TEXT("BowlSphere"));
    BowlSphere->SetupAttachment(BowlPoint);
    BowlSphere->InitSphereRadius(3.f);
    BowlSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BowlSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
    BowlSphere->SetGenerateOverlapEvents(true);

    // 勺兜内豆堆可视化（蓝图里指派 StaticMesh，拖到勺兜内合适位置）
    ScoopBeanMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScoopBeanMesh"));
    ScoopBeanMesh->SetupAttachment(BowlPoint);
    ScoopBeanMesh->SetSimulatePhysics(false);
    ScoopBeanMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ScoopBeanMesh->SetVisibility(false);

    ScoopCapacityGrams        = 5.f;
    ScoopFillRateGramsPerSec  = 20.f;
    PourRateGramsPerSec       = 30.f;
    UprightDot                = 0.5f;
    PourDot                   = -0.1f;
    ScoopDepthBelowLidCm      = 0.f;
    PourAlignRadiusCm         = 6.f;

    ScoopLoadGrams          = 0.f;
    ScoopBeanInitialScale   = FVector::OneVector;
}

void ACoffeeScoopActor::BeginPlay()
{
    Super::BeginPlay();

    if (ScoopBeanMesh)
    {
        ScoopBeanInitialScale = ScoopBeanMesh->GetRelativeScale3D();
    }
    UpdateScoopBeanVisual();
}

bool ACoffeeScoopActor::IsHeld() const
{
    return GrabComp ? GrabComp->IsHeld() : false;
}

void ACoffeeScoopActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 只有被握住时才可能舀 / 倒
    if (!IsHeld())
    {
        return;
    }

    // "勺兜朝上"→尝试舀豆；"勺兜朝下"→尝试倒豆；中间状态什么也不做
    const float UpZ = GetActorUpVector().Z;
    if (UpZ >= UprightDot)
    {
        TickScoopFromJar(DeltaTime);
    }
    else if (UpZ <= PourDot)
    {
        TickPourToGrinder(DeltaTime);
    }
}

//=====================================================================
// 舀豆
//=====================================================================

void ACoffeeScoopActor::TickScoopFromJar(float DeltaTime)
{
    // 勺满就不再舀
    if (ScoopLoadGrams >= ScoopCapacityGrams - KINDA_SMALL_NUMBER)
    {
        return;
    }

    ABeanJarActor* Jar = FindOverlappingOpenJar();
    if (!Jar)
    {
        return;
    }

    // 可选的"伸进罐口深度"校验：BowlPoint 相对罐口 Socket 局部 Z 应低于 -ScoopDepthBelowLidCm
    if (ScoopDepthBelowLidCm > 0.f && BowlPoint)
    {
        if (UStaticMeshComponent* JarMesh = Jar->GetJarMesh())
        {
            const FTransform LidXform = JarMesh->GetSocketTransform(Jar->LidSocketName, RTS_World);
            const FVector BowlLocalInLid = LidXform.InverseTransformPosition(BowlPoint->GetComponentLocation());
            // 局部 -Z 方向为"罐内"（Socket 通常朝罐外/罐上），这里取绝对深度
            if (-BowlLocalInLid.Z < ScoopDepthBelowLidCm)
            {
                return;
            }
        }
    }

    const float Want   = ScoopFillRateGramsPerSec * DeltaTime;
    const float Space  = ScoopCapacityGrams - ScoopLoadGrams;
    const float Try    = FMath::Min(Want, Space);
    const float Actual = Jar->TryScoopBeans(Try);
    if (Actual > 0.f)
    {
        ScoopLoadGrams = FMath::Min(ScoopLoadGrams + Actual, ScoopCapacityGrams);
        UpdateScoopBeanVisual();
    }
}

ABeanJarActor* ACoffeeScoopActor::FindOverlappingOpenJar() const
{
    if (!BowlSphere)
    {
        return nullptr;
    }
    TArray<AActor*> OverlappingActors;
    BowlSphere->GetOverlappingActors(OverlappingActors, ABeanJarActor::StaticClass());
    for (AActor* Actor : OverlappingActors)
    {
        ABeanJarActor* Jar = Cast<ABeanJarActor>(Actor);
        if (Jar && Jar->IsOpen() && Jar->CurrentBeanGrams > KINDA_SMALL_NUMBER)
        {
            return Jar;
        }
    }
    return nullptr;
}

//=====================================================================
// 倒豆
//=====================================================================

void ACoffeeScoopActor::TickPourToGrinder(float DeltaTime)
{
    if (ScoopLoadGrams <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float Want = PourRateGramsPerSec * DeltaTime;
    const float Try  = FMath::Min(Want, ScoopLoadGrams);

    ACoffeeGrinderActor* Grinder = FindAlignedGrinder();
    if (Grinder)
    {
        // 对准了：塞进研磨器顶仓（超容量部分会被 Grinder 拒收，剩余留在勺子里）
        const float Added = Grinder->TryAddBeans(Try);
        ScoopLoadGrams -= Added;
    }
    else
    {
        // 没对准：豆子被"洒"掉（不生成物理豆颗粒，简化处理）
        ScoopLoadGrams -= Try;
    }

    ScoopLoadGrams = FMath::Max(ScoopLoadGrams, 0.f);
    UpdateScoopBeanVisual();
}

ACoffeeGrinderActor* ACoffeeScoopActor::FindAlignedGrinder() const
{
    if (!BowlPoint)
    {
        return nullptr;
    }

    const FVector BowlWS = BowlPoint->GetComponentLocation();

    // 简化：遍历世界里所有 ACoffeeGrinderActor（VR 咖啡厅场景里数量有限），
    // 找 BeanEntryPoint 在 BowlPoint 下方且水平距离最近的一个。
    ACoffeeGrinderActor* Best = nullptr;
    float BestScore = TNumericLimits<float>::Max();

    for (TActorIterator<ACoffeeGrinderActor> It(GetWorld()); It; ++It)
    {
        ACoffeeGrinderActor* Grinder = *It;
        if (!Grinder || !Grinder->BeanEntryPoint)
        {
            continue;
        }
        const FTransform EntryXform = Grinder->BeanEntryPoint->GetComponentTransform();
        // BowlPoint 转到 Entry 局部空间
        const FVector Local = EntryXform.InverseTransformPosition(BowlWS);
        // 期望 BowlPoint 在 Entry 上方（Local.Z > 0）
        if (Local.Z <= 0.f)
        {
            continue;
        }
        const float Horiz = FVector2D(Local.X, Local.Y).Size();
        if (Horiz <= PourAlignRadiusCm && Horiz < BestScore)
        {
            BestScore = Horiz;
            Best = Grinder;
        }
    }
    return Best;
}

//=====================================================================
// 可视化
//=====================================================================

void ACoffeeScoopActor::UpdateScoopBeanVisual()
{
    if (!ScoopBeanMesh)
    {
        return;
    }
    const float Ratio = (ScoopCapacityGrams > KINDA_SMALL_NUMBER)
        ? FMath::Clamp(ScoopLoadGrams / ScoopCapacityGrams, 0.f, 1.f)
        : 0.f;
    if (Ratio <= 0.001f)
    {
        ScoopBeanMesh->SetVisibility(false);
        return;
    }
    ScoopBeanMesh->SetVisibility(true);
    FVector NewScale = ScoopBeanInitialScale;
    NewScale.Z = ScoopBeanInitialScale.Z * Ratio;
    ScoopBeanMesh->SetRelativeScale3D(NewScale);
}
