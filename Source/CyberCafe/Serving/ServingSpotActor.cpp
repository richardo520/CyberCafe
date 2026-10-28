// Fill out your copyright notice in the Description page of Project Settings.

#include "Serving/ServingSpotActor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Liquid/LiquidContainerActor.h"
#include "GrabComponent.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "Engine/World.h"

AServingSpotActor::AServingSpotActor()
{
    // 需要 Tick 检查"玩家松手"条件
    PrimaryActorTick.bCanEverTick = true;

    // 根组件：空的 SceneComponent，用于定位"杯子吸附后的最终 Transform"
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    // 指示 Mesh（可选）
    IndicatorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IndicatorMesh"));
    IndicatorMesh->SetupAttachment(SceneRoot);
    IndicatorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    IndicatorMesh->SetGenerateOverlapEvents(false);

    // 检测 Box：QueryOnly + Overlap 响应物理 Actor
    DetectBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DetectBox"));
    DetectBox->SetupAttachment(SceneRoot);
    DetectBox->SetBoxExtent(FVector(15.f, 15.f, 20.f)); // 默认 30×30×40 cm，覆盖一个杯子
    DetectBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DetectBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    DetectBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
    DetectBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
    DetectBox->SetGenerateOverlapEvents(true);
}

void AServingSpotActor::BeginPlay()
{
    Super::BeginPlay();

    if (DetectBox)
    {
        DetectBox->OnComponentBeginOverlap.AddDynamic(this, &AServingSpotActor::HandleDetectBeginOverlap);
        DetectBox->OnComponentEndOverlap.AddDynamic(this, &AServingSpotActor::HandleDetectEndOverlap);
    }
}

void AServingSpotActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bHasBeenServed && bOneShot)
    {
        return;
    }

    ALiquidContainerActor* Cup = CandidateCup.Get();
    if (!Cup)
    {
        return;
    }

    // 候选杯子还在手上 → 等玩家松手
    if (Cup->IsHeld())
    {
        return;
    }

    // 松手了：执行吸附
    PerformServe(Cup);
}

bool AServingSpotActor::IsCupAcceptable(ALiquidContainerActor* Cup) const
{
    if (!Cup)
    {
        return false;
    }

    // 类型过滤：若配置了 AcceptedCupClass，必须是其子类
    if (AcceptedCupClass && !Cup->IsA(AcceptedCupClass))
    {
        return false;
    }

    // 已经被别的上菜点服务过？不该发生（杯子被锁死后无法移动），但稳妥起见判一下
    if (Cup->IsAttachedTo(this))
    {
        return false;
    }

    return true;
}

void AServingSpotActor::HandleDetectBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/,
    AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
    int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
    if (bHasBeenServed && bOneShot)
    {
        return;
    }

    ALiquidContainerActor* Cup = Cast<ALiquidContainerActor>(OtherActor);
    if (!IsCupAcceptable(Cup))
    {
        return;
    }

    // 已经有一个候选且不是当前这只 → 以最后进入的为准（避免卡住）
    CandidateCup = Cup;
}

void AServingSpotActor::HandleDetectEndOverlap(UPrimitiveComponent* /*OverlappedComp*/,
    AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
    int32 /*OtherBodyIndex*/)
{
    ALiquidContainerActor* Cup = Cast<ALiquidContainerActor>(OtherActor);
    if (!Cup)
    {
        return;
    }

    // 只清除"自己就是当前候选"的情况，避免误清
    if (CandidateCup.Get() == Cup)
    {
        CandidateCup.Reset();
    }
}

void AServingSpotActor::PerformServe(ALiquidContainerActor* Cup)
{
    if (!Cup)
    {
        return;
    }

    // 1) 防御性：如果杯子还在被抓着（极端边界：IsHeld 返回假但 GrabComp 内部状态未同步），
    //    强制释放一下
    if (UGrabComponent* GrabComp = Cup->FindComponentByClass<UGrabComponent>())
    {
        if (GrabComp->IsHeld())
        {
            GrabComp->TryRelease();
        }
    }

    // 2) 关闭杯身物理 + 碰撞，防止吸附后被玩家再次抓起或被别的物体撞飞
    if (bLockCupAfterServing)
    {
        if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Cup->GetRootComponent()))
        {
            Root->SetSimulatePhysics(false);
            Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }

        // 禁止远程/近距抓取（防御：即便没关碰撞也抓不到）
        if (UGrabComponent* GrabComp = Cup->FindComponentByClass<UGrabComponent>())
        {
            GrabComp->bAllowRemoteGrab = false;
            GrabComp->SetActive(false);
        }
    }

    // 3) 瞬移吸附：Attach 到 SceneRoot 并对齐世界 Transform
    //    SnapToTarget 规则会把杯子的位置/朝向直接 = SceneRoot 的位置/朝向
    const FAttachmentTransformRules AttachRule(EAttachmentRule::SnapToTarget,
        EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, /*bWeld=*/false);
    Cup->AttachToComponent(SceneRoot, AttachRule);

    // 4) 更新状态
    bHasBeenServed = true;
    ServedCup = Cup;
    CandidateCup.Reset();

    // 5) 广播全局事件
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnDrinkServed.Broadcast(this, Cup);
    }

    UE_LOG(LogTemp, Log, TEXT("[ServingSpot] Served cup %s on spot %s"),
        *Cup->GetName(), *GetName());
}

void AServingSpotActor::ResetServing()
{
    bHasBeenServed = false;
    ServedCup.Reset();
    CandidateCup.Reset();
}
