// Fill out your copyright notice in the Description page of Project Settings.

#include "Serving/ServingSpotActor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
// @trace RC-03 RC-05
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "GrabComponent.h"
#include "Core/Events/CyberCafeEventSubsystem.h"
#include "Engine/World.h"

namespace
{
    int32 GetServingSpotCupIcePreparationRevision(const ALiquidContainerActor* Cup)
    {
        if (!IsValid(Cup)) return 0;
        const UCupIceComponent* Ice = Cup->FindComponentByClass<UCupIceComponent>();
        return Ice ? Ice->GetPreparationRevision() : 0;
    }
}

AServingSpotActor::AServingSpotActor()
{
    // 需要 Tick 检查"玩家松手"条件
    PrimaryActorTick.bCanEverTick = true;
    OrderSession=CreateDefaultSubobject<UServingOrderComponent>(TEXT("ServingOrder"));

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

    if (!bAcceptSubmissions || (bHasBeenServed && bOneShot) || OrderSession->bCommitting) return;
    if (!CandidateCup.IsValid() && DetectBox)
    {
        TArray<AActor*> Overlapping;
        DetectBox->GetOverlappingActors(Overlapping, ALiquidContainerActor::StaticClass());
        for (AActor* A : Overlapping)
            if (ALiquidContainerActor* C = Cast<ALiquidContainerActor>(A); IsCupAcceptable(C) && IsInServingArea(C))
            { CandidateCup = C; ObservedRevision = -1; ObservedIceRevision = -1; StableElapsed = 0; break; }
    }
    ALiquidContainerActor* Cup = CandidateCup.Get();
    if (!Cup)
    {
        return;
    }

    if (Cup->IsBroken() || !IsInServingArea(Cup)) { CandidateCup.Reset(); StableElapsed = 0; ObservedRevision = -1; ObservedIceRevision = -1; return; }
    // Picking up re-arms the submission; never force a release to serve a cup.
    if (Cup->IsHeld())
    {
        StableElapsed = 0; ObservedRevision = -1; ObservedIceRevision = -1;
        if (LastAttemptCup.Get() == Cup) { LastAttemptRevision = -1; LastAttemptIceRevision = -1; }
        return;
    }
    if (!ActiveRecipe) { TrySubmitCup(Cup); return; }
    const int32 Revision = Cup->GetLiquidRevision();
    const int32 IceRevision = GetServingSpotCupIcePreparationRevision(Cup);
    if (LastAttemptCup.Get() == Cup && LastAttemptRevision == Revision && LastAttemptIceRevision == IceRevision) return;
    if (ObservedRevision != Revision || ObservedIceRevision != IceRevision || Cup->bIsPouring)
    { ObservedRevision = Revision; ObservedIceRevision = IceRevision; StableElapsed = 0; return; }
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
    if (CandidateCup.Get() != Cup) { StableElapsed = 0; ObservedRevision = -1; ObservedIceRevision = -1; }
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
        CandidateCup.Reset(); StableElapsed = 0; ObservedRevision = -1; ObservedIceRevision = -1;
        if (LastAttemptCup.Get() == Cup) { LastAttemptCup.Reset(); LastAttemptRevision = -1; LastAttemptIceRevision = -1; }
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
    OrderSession->SetActiveRecipe(Recipe);
}

bool AServingSpotActor::TryClaimOrder(UObject* Requester)
{
    return OrderSession->TryClaimOrder(Requester);
}

void AServingSpotActor::ReleaseOrder(UObject* Requester)
{
    OrderSession->ReleaseOrder(Requester);
}

void AServingSpotActor::SetSubmissionEnabled(bool bEnabled)
{
    OrderSession->SetSubmissionEnabled(bEnabled);
}

bool AServingSpotActor::HasAcceptedCup(const ALiquidContainerActor* Cup, FName RecipeId) const
{
    return OrderSession->HasAcceptedCup(Cup, RecipeId);
}

bool AServingSpotActor::TrySubmitCup(ALiquidContainerActor* Cup)
{
    return OrderSession->TrySubmitCup(Cup);
}

void AServingSpotActor::PerformServe(ALiquidContainerActor* Cup)
{
    if (!IsValid(Cup) || Cup->IsBroken())
    {
        return;
    }

    if (Cup->IsHeld()) return;
    const TWeakObjectPtr<ALiquidContainerActor> ServingCup(Cup);
    // Never take a cup away from a player's hand, even during an explicit submission.

    // 2) 关闭杯身物理 + 碰撞，防止吸附后被玩家再次抓起或被别的物体撞飞
    if (bLockCupAfterServing)
    {
        if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Cup->GetRootComponent()))
        {
            const TWeakObjectPtr<UPrimitiveComponent> Body(Root);
            Root->SetSimulatePhysics(false);
            Cup = ServingCup.Get();
            Root = Body.Get();
            if (!IsValid(this) || !Cup || !Root || (OrderSession->bCommitting && OrderSession->CommittingCup != ServingCup)) return;
            Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        Cup = ServingCup.Get();
        if (!IsValid(this) || !Cup || (OrderSession->bCommitting && OrderSession->CommittingCup != ServingCup)) return;

        // 禁止远程/近距抓取（防御：即便没关碰撞也抓不到）
        if (UGrabComponent* GrabComp = Cup->FindComponentByClass<UGrabComponent>())
        {
            GrabComp->bAllowRemoteGrab = false;
            GrabComp->GrabType = EGrabType::None;
            GrabComp->SetActive(false);
        }
    }

    Cup = ServingCup.Get();
    if (!IsValid(this) || !Cup || Cup->IsBroken() || Cup->IsHeld() || (OrderSession->bCommitting && OrderSession->CommittingCup != ServingCup)) return;

    // 3) 瞬移吸附：Attach 到 SceneRoot 并对齐世界 Transform
    //    SnapToTarget 规则会把杯子的位置/朝向直接 = SceneRoot 的位置/朝向
    const FAttachmentTransformRules AttachRule(EAttachmentRule::SnapToTarget,
        EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, /*bWeld=*/false);
    Cup->AttachToComponent(SceneRoot, AttachRule);
    Cup = ServingCup.Get();
    if (!IsValid(this) || !Cup || Cup->IsBroken() || Cup->IsHeld() || (OrderSession->bCommitting && OrderSession->CommittingCup != ServingCup)) return;

    // 4) 更新状态
    bHasBeenServed = true;
    ServedCup = Cup;
    CandidateCup.Reset();

    // 回调使用结果快照；恢复/销毁回调后不可继续使用旧裸指针。
    const FDrinkValidationResult Validation = OrderSession->LastValidation;
    if (ActiveRecipe && Validation.bAccepted) OnDrinkValidated.Broadcast(this, Cup, Validation);
    Cup = ServingCup.Get();
    if (!IsValid(this) || !Cup) return;
    // 5) 广播全局事件
    if (UCyberCafeEventSubsystem* Sys = UCyberCafeEventSubsystem::Get(this))
    {
        Sys->OnDrinkServed.Broadcast(this, Cup);
    }

    Cup = ServingCup.Get();
    if (!IsValid(this) || !Cup) return;
    UE_LOG(LogTemp, Log, TEXT("[ServingSpot] Served cup %s on spot %s"),
        *Cup->GetName(), *GetName());
}

void AServingSpotActor::ForgetRestoredContainers(const TArray<ALiquidContainerActor*>& ContainersToRestore)
{
    check(IsInGameThread());
    TSet<TWeakObjectPtr<ALiquidContainerActor>> RestoredCups;
    for(ALiquidContainerActor* A:ContainersToRestore)
    {
        if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=GetWorld()||A->IsLiquidFrozen())continue;
        ACupActor* Cup=Cast<ACupActor>(A);
        if(!Cup)continue;
        const TWeakObjectPtr<ALiquidContainerActor> Key(Cup);
        if(Key!=ServedCup)RestoredCups.Add(Key);
    }
    // 按对象身份清缓存；无关候选的稳定时间、已验收结果与订单均保留。
    if(RestoredCups.Contains(CandidateCup))
    {
        CandidateCup.Reset();
        StableElapsed=0;
        ObservedRevision=-1;
        ObservedIceRevision=-1;
    }
    if(RestoredCups.Contains(LastAttemptCup))
    {
        LastAttemptCup.Reset();
        LastAttemptRevision=-1;
        LastAttemptIceRevision=-1;
        if(!OrderSession->LastValidation.bAccepted)OrderSession->LastValidation=FDrinkValidationResult();
    }
    // 即使在吸附/失败回调中恢复，也不得继续提交被忘记的旧杯。
    if(RestoredCups.Contains(OrderSession->CommittingCup))OrderSession->CommittingCup.Reset();
}

void AServingSpotActor::ResetServing()
{
    if (OrderSession->bCommitting) return;
    OrderSession->AcceptedOrderSerial = 0;
    bHasBeenServed = false;
    ServedCup.Reset();
    CandidateCup.Reset(); LastAttemptCup.Reset(); LastAttemptRevision = -1; LastAttemptIceRevision = -1;
    OrderSession->LastValidation = FDrinkValidationResult(); ObservedRevision = -1; ObservedIceRevision = -1; StableElapsed = 0; ++OrderSession->OrderSerial;
}
