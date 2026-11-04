// Fill out your copyright notice in the Description page of Project Settings.

#include "Serving/ServingSpotActor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
// @trace RC-03 RC-05
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

    if (!bAcceptSubmissions || (bHasBeenServed && bOneShot) || bCommitting) return;
    if (!CandidateCup.IsValid() && DetectBox)
    {
        TArray<AActor*> Overlapping;
        DetectBox->GetOverlappingActors(Overlapping, ALiquidContainerActor::StaticClass());
        for (AActor* A : Overlapping)
            if (ALiquidContainerActor* C = Cast<ALiquidContainerActor>(A); IsCupAcceptable(C) && IsInServingArea(C))
            { CandidateCup = C; ObservedRevision = -1; StableElapsed = 0; break; }
    }
    ALiquidContainerActor* Cup = CandidateCup.Get();
    if (!Cup)
    {
        return;
    }

    if (Cup->IsBroken() || !IsInServingArea(Cup)) { CandidateCup.Reset(); StableElapsed = 0; ObservedRevision = -1; return; }
    // Picking up re-arms the submission; never force a release to serve a cup.
    if (Cup->IsHeld())
    { StableElapsed = 0; ObservedRevision = -1; if (LastAttemptCup.Get() == Cup) LastAttemptRevision = -1; return; }
    if (!ActiveRecipe) { TrySubmitCup(Cup); return; }
    const int32 Revision = Cup->GetLiquidRevision();
    if (LastAttemptCup.Get() == Cup && LastAttemptRevision == Revision) return;
    if (ObservedRevision != Revision || Cup->bIsPouring)
    { ObservedRevision = Revision; StableElapsed = 0; return; }
    StableElapsed += FMath::Max(0.f, DeltaSeconds);
    if (StableElapsed >= FMath::Max(0.f, StabilitySeconds)) TrySubmitCup(Cup);
}

bool AServingSpotActor::IsCupAcceptable(ALiquidContainerActor* Cup) const
{
    if (!IsValid(Cup) || Cup->IsBroken())
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
    if (CandidateCup.Get() != Cup) { StableElapsed = 0; ObservedRevision = -1; }
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
        // Multiple components may overlap. Do not cancel while another body remains.
        if (DetectBox && DetectBox->IsOverlappingActor(Cup)) return;
        CandidateCup.Reset(); StableElapsed = 0; ObservedRevision = -1;
        if (LastAttemptCup.Get() == Cup) { LastAttemptCup.Reset(); LastAttemptRevision = -1; }
    }
}

bool AServingSpotActor::IsInServingArea(const ALiquidContainerActor* Cup) const
{
    if (!IsValid(Cup) || !DetectBox) return false;
    const FVector Local = DetectBox->GetComponentTransform().InverseTransformPosition(Cup->GetActorLocation());
    const FVector Extent = DetectBox->GetUnscaledBoxExtent();
    return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void AServingSpotActor::SetActiveRecipe(UDrinkRecipe* Recipe)
{
    if (bCommitting) return;
    ++OrderSerial; ActiveRecipe = Recipe;
    LastAttemptCup.Reset(); LastAttemptRevision = -1; ObservedRevision = -1; StableElapsed = 0;
    if (!bHasBeenServed) LastValidation = FDrinkValidationResult();
}

void AServingSpotActor::SetSubmissionEnabled(bool bEnabled)
{
    if (bCommitting) return;
    bAcceptSubmissions = bEnabled;
    ++OrderSerial; StableElapsed = 0; ObservedRevision = -1;
}

bool AServingSpotActor::HasAcceptedCup(const ALiquidContainerActor* Cup, FName RecipeId) const
{
    return bHasBeenServed && ServedCup.Get() == Cup && LastValidation.bAccepted && LastValidation.RecipeId == RecipeId;
}

bool AServingSpotActor::TrySubmitCup(ALiquidContainerActor* Cup)
{
    if (!bAcceptSubmissions || bCommitting || (bHasBeenServed && bOneShot) || !IsCupAcceptable(Cup) || !IsInServingArea(Cup) || Cup->IsHeld() || Cup->IsLiquidFrozen() || Cup->bIsPouring) return false;
    TGuardValue<bool> Guard(bCommitting, true);
    if (!ActiveRecipe) { PerformServe(Cup); return bHasBeenServed && ServedCup.Get() == Cup; }
    const uint32 Serial = OrderSerial;
    UDrinkRecipe* Recipe = ActiveRecipe;
    const FDrinkValidationResult Result = Recipe->Evaluate(Cup);
    const bool bNewAttempt = LastAttemptCup.Get() != Cup || LastAttemptRevision != Result.LiquidRevision;
    LastAttemptCup = Cup; LastAttemptRevision = Result.LiquidRevision;
    LastValidation = Result;
    if (!Result.bAccepted)
    {
        if (bNewAttempt) OnDrinkValidated.Broadcast(this, Cup, Result);
        return false;
    }
    // No asynchronous bool or old timer may lock a changed cup/changed order.
    if (Serial != OrderSerial || Recipe != ActiveRecipe || !bAcceptSubmissions || Cup->IsBroken() || Cup->IsHeld() || !IsInServingArea(Cup) || Cup->GetLiquidRevision() != Result.LiquidRevision) return false;
    AcceptedOrderSerial = Serial;
    Cup->FreezeLiquidForServing();
    PerformServe(Cup);
    return bHasBeenServed && ServedCup.Get() == Cup;
}

void AServingSpotActor::PerformServe(ALiquidContainerActor* Cup)
{
    if (!IsValid(Cup) || Cup->IsBroken())
    {
        return;
    }

    if (Cup->IsHeld()) return;
    // Never take a cup away from a player's hand, even during an explicit submission.

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
            GrabComp->GrabType = EGrabType::None;
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

    // Result is committed before either callback; only accepted submissions emit success.
    if (ActiveRecipe && LastValidation.bAccepted) OnDrinkValidated.Broadcast(this, Cup, LastValidation);
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
    if (bCommitting) return;
    AcceptedOrderSerial = 0;
    bHasBeenServed = false;
    ServedCup.Reset();
    CandidateCup.Reset(); LastAttemptCup.Reset(); LastAttemptRevision = -1;
    LastValidation = FDrinkValidationResult(); ObservedRevision = -1; StableElapsed = 0; ++OrderSerial;
}
