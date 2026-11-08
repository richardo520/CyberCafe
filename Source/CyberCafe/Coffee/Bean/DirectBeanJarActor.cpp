#include "Coffee/Bean/DirectBeanJarActor.h"

#include "Coffee/Grinder/CoffeeGrinderActor.h"
#include "Liquid/BottleCapActor.h"
#include "Liquid/GlassBreakableComponent.h"
#include "GrabComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "NiagaraComponent.h"
#include <cmath>

namespace
{
    double BeanSupplyWithinCapacity(double Supply, float Capacity)
    {
        const double Limit = FMath::IsFinite(Capacity) ? FMath::Max(0.0, static_cast<double>(Capacity)) : 0.0;
        return FMath::IsFinite(Supply) ? FMath::Clamp(Supply, 0.0, Limit) : 0.0;
    }
}

ADirectBeanJarActor::ADirectBeanJarActor()
{
    PrimaryActorTick.bCanEverTick = true;
    MaxVolumeML = 1.f;
    FillAmount = 0.f;
    InitialIngredientId = TEXT("Unknown");
    bAcceptLiquidFromOthers = false;
    bCanPour = false;
    bEnablePourAudio = false;
    bDynamicWaves = false;

    BeanPileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeanPileMesh"));
    BeanPileMesh->SetupAttachment(RootComponent);
    BeanPileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BeanPileMesh->SetSimulatePhysics(false);
    BeanPileMesh->SetComponentTickEnabled(false);

    BeanStream = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BeanStream"));
    BeanStream->SetupAttachment(RootComponent);
    BeanStream->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BeanStream->SetSimulatePhysics(false);
    BeanStream->SetGenerateOverlapEvents(false);
    BeanStream->SetCastShadow(false);
    BeanStream->SetComponentTickEnabled(false);
    BeanStream->SetVisibility(false);

    // 网格、Metal/Coffee 材质和真实 CapSocket 均由新资产/蓝图配置，不改源资产。
    for (UNiagaraComponent* FX : {LiquidFX.Get(), PourFX.Get()})
    {
        if (!FX) continue;
        FX->SetAutoActivate(false);
        FX->SetVisibility(false);
        FX->SetHiddenInGame(true);
        FX->SetComponentTickEnabled(false);
    }
}

void ADirectBeanJarActor::BeginPlay()
{
    // 蓝图属性已就绪；父类以零毫升初始化并照常生成/归属盖子。
    MaxVolumeML = 1.f;
    FillAmount = 0.f;
    InitialIngredientId = TEXT("Unknown");
    bCanPour = false;
    bAcceptLiquidFromOthers = false;
    bEnablePourAudio = false;
    Super::BeginPlay();
    DisableLiquidPresentation();

    if (BeanPileMesh)
    {
        BeanPileInitialLocation = BeanPileMesh->GetRelativeLocation();
        BeanPileInitialScale = BeanPileMesh->GetRelativeScale3D();
        BeanPileMesh->SetSimulatePhysics(false);
        BeanPileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if (BeanStream)
    {
        BeanStream->SetSimulatePhysics(false);
        BeanStream->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        BeanStream->SetComponentTickEnabled(false);
    }
    bBeanVisualInitialized = true;
    InitializeBeans();
    if (UGlassBreakableComponent* Breakable = FindComponentByClass<UGlassBreakableComponent>())
    {
        Breakable->OnBroken.AddUniqueDynamic(this, &ADirectBeanJarActor::HandleJarBroken);
    }
    if (IsBroken()) HandleJarBroken(this);
    UpdateBeanPileVisual();
    ClearBeanStream();
}

void ADirectBeanJarActor::Tick(float DeltaTime)
{
    DisableLiquidPresentation();
    Super::Tick(DeltaTime);
    if (IsBroken()) HandleJarBroken(this);
    // 显式 PourBeans 可用于姿态测试；自动倒豆必须是真实持罐。
    if (!IsValid(this) || IsActorBeingDestroyed() || !GrabComp || !GrabComp->IsHeld())
    {
        ClearBeanStream();
        return;
    }
    PourBeans(DeltaTime);
}

void ADirectBeanJarActor::InitializeBeans()
{
    if (bBeansInitialized) return;
    BeanCapacityGrams = FMath::IsFinite(BeanCapacityGrams) ? FMath::Max(0.f, BeanCapacityGrams) : 0.f;
    InitialStockGrams = BeanSupplyWithinCapacity(InitialBeanGrams, BeanCapacityGrams);
    CurrentBeanGrams = InitialStockGrams;
    bBeansInitialized = true;
    UpdateBeanPileVisual();
}

double ADirectBeanJarActor::GetBeanGrams() const
{
    if (IsBroken()) return 0.0;
    return bBeansInitialized ? CurrentBeanGrams : BeanSupplyWithinCapacity(InitialBeanGrams, BeanCapacityGrams);
}

FBeanJarLedgerSnapshot ADirectBeanJarActor::GetBeanLedger() const
{
    FBeanJarLedgerSnapshot Result;
    Result.InitialGrams = bBeansInitialized ? InitialStockGrams : BeanSupplyWithinCapacity(InitialBeanGrams, BeanCapacityGrams);
    Result.InjectedGrams = RestoredBeanGrams;
    Result.AcceptedGrams = TotalAcceptedGrams;
    Result.SpilledGrams = SpilledBeanGrams;
    Result.DiscardedGrams = DiscardedBeanGrams;
    Result.CurrentGrams = bBeansInitialized ? CurrentBeanGrams : Result.InitialGrams;
    Result.TotalPouredGrams = TotalPouredGrams;
    // GlassBreakable 的 OnBroken 在解除盖/抓取之后广播；此前回调读取也不能把碎罐余豆当弃置。
    if (IsBroken())
    {
        Result.SpilledGrams += Result.CurrentGrams;
        Result.CurrentGrams = 0.0;
    }
    return Result;
}

double ADirectBeanJarActor::GetBeanBalanceError() const
{
    const FBeanJarLedgerSnapshot Ledger = GetBeanLedger();
    return Ledger.InitialGrams + Ledger.InjectedGrams - Ledger.CurrentGrams
        - Ledger.AcceptedGrams - Ledger.SpilledGrams - Ledger.DiscardedGrams;
}

bool ADirectBeanJarActor::ApplyRestoredBeanLedger(const FBeanJarLedgerSnapshot& Snapshot, double NewSupplyGrams)
{
    if (!IsValid(this) || IsActorBeingDestroyed() || IsBroken() || IsLiquidFrozen()
        || bRestoredLedgerApplied || bBeanTransferInProgress || !FMath::IsFinite(NewSupplyGrams) || NewSupplyGrams < 0.0)
    {
        return false;
    }
    for (double Value : {Snapshot.InitialGrams, Snapshot.InjectedGrams, Snapshot.AcceptedGrams,
        Snapshot.SpilledGrams, Snapshot.DiscardedGrams, Snapshot.CurrentGrams, Snapshot.TotalPouredGrams})
    {
        if (!FMath::IsFinite(Value) || Value < 0.0) return false;
    }
    const double Input = Snapshot.InitialGrams + Snapshot.InjectedGrams;
    const double Output = Snapshot.CurrentGrams + Snapshot.AcceptedGrams + Snapshot.SpilledGrams + Snapshot.DiscardedGrams;
    if (!FMath::IsFinite(Input) || !FMath::IsFinite(Output) || FMath::Abs(Input - Output) > 1.e-5) return false;
    const double Supply = BeanSupplyWithinCapacity(NewSupplyGrams, BeanCapacityGrams);
    const double Injected = Snapshot.InjectedGrams + Supply;
    const double Discarded = Snapshot.DiscardedGrams + Snapshot.CurrentGrams;
    if (!FMath::IsFinite(Injected) || !FMath::IsFinite(Discarded) || !FMath::IsFinite(Snapshot.InitialGrams + Injected)) return false;

    TGuardValue<bool> RestoreGuard(bBeanRestoreBlocked, true);
    StopFlowForWorkbenchRestore();
    DisableLiquidPresentation();
    ClearBeanStream();
    // 原生默认初始库存被覆盖，不与历史账本相加。
    InitialStockGrams = Snapshot.InitialGrams;
    RestoredBeanGrams = Injected;
    TotalAcceptedGrams = Snapshot.AcceptedGrams;
    SpilledBeanGrams = Snapshot.SpilledGrams;
    DiscardedBeanGrams = Discarded;
    TotalPouredGrams = Snapshot.TotalPouredGrams;
    CurrentBeanGrams = Supply;
    bBeansInitialized = true;
    bRestoredLedgerApplied = true;
    UpdateBeanPileVisual();
    return true;
}

void ADirectBeanJarActor::SetBeanRestoreBlocked(bool bBlocked)
{
    bBeanRestoreBlocked = bBlocked;
    if (bBlocked) ClearBeanStream();
}

bool ADirectBeanJarActor::CanPourBeans() const
{
    if (!IsValid(this) || IsActorBeingDestroyed() || IsBroken() || IsLiquidFrozen()
        || bWorkbenchRestoreEmissionBlocked || bBeanRestoreBlocked || bBeanTransferInProgress
        || !IsValid(ContainerMesh.Get()) || !IsValid(CapActor.Get()) || CapActor->IsActorBeingDestroyed()
        || CapActor->OwnerBottle != this || CapActor->bIsAttached || bIsCapped
        || !FMath::IsFinite(BeanPourRateGramsPerSecond) || BeanPourRateGramsPerSecond <= 0.f
        || !FMath::IsFinite(MaxBeanTravelCM) || MaxBeanTravelCM <= 0.f
        || !FMath::IsFinite(PourBeanTiltDegrees) || MouthLocal.ContainsNaN())
    {
        return false;
    }
    const FVector Outward = ContainerMesh->GetUpVector();
    const double UpDot = FVector::DotProduct(Outward, FVector::UpVector);
    const double Tilt = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(UpDot, -1.0, 1.0)));
    return FMath::IsFinite(Tilt) && Tilt + 1.e-4 >= FMath::Clamp(static_cast<double>(PourBeanTiltDegrees), 0.0, 180.0)
        && GetBeanGrams() > 0.0;
}

FVector ADirectBeanJarActor::GetBeanMouthWorld() const
{
    const FTransform Body = ContainerMesh->GetComponentTransform();
    const FVector Outward = Body.GetUnitAxis(EAxis::Z);
    const FVector RimDown = (FVector::DownVector - Outward * FVector::DotProduct(FVector::DownVector, Outward)).GetSafeNormal();
    const FVector Scale = Body.GetScale3D().GetAbs();
    const double Radius = FMath::IsFinite(OpeningRadiusCM) ? FMath::Max(0.f, OpeningRadiusCM) * FMath::Min(Scale.X, Scale.Y) : 0.0;
    return Body.TransformPosition(MouthLocal) + RimDown * Radius;
}

ACoffeeGrinderActor* ADirectBeanJarActor::FindBeanReceiver(const FVector& Start, const FVector& End, FVector& Impact) const
{
    UWorld* World = GetWorld();
    if (!World || !FMath::IsFinite(BeanEntryRadiusCM) || BeanEntryRadiusCM <= 0.f) return nullptr;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DirectBeanJarPour), false);
    Params.AddIgnoredActor(this);
    if (IsValid(CapActor.Get())) Params.AddIgnoredActor(CapActor.Get());
    for (TActorIterator<APawn> It(World); It; ++It) Params.AddIgnoredActor(*It);

    FHitResult Hit;
    const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    const double HitTime = bHit ? Hit.Time : 1.0;
    if (bHit) Impact = Hit.ImpactPoint;
    ACoffeeGrinderActor* Receiver = nullptr;
    double FirstTime = 2.0;
    const double RadiusSquared = static_cast<double>(BeanEntryRadiusCM) * BeanEntryRadiusCM;
    for (TActorIterator<ACoffeeGrinderActor> It(World); It; ++It)
    {
        ACoffeeGrinderActor* Grinder = *It;
        if (!IsValid(Grinder) || Grinder->IsActorBeingDestroyed() || !IsValid(Grinder->BeanEntryPoint.Get())
            || !IsValid(Grinder->BodyMesh.Get()) || FVector::DotProduct(Grinder->BodyMesh->GetUpVector(), FVector::UpVector) <= 0.4) continue;
        const FTransform Entry = Grinder->BeanEntryPoint->GetComponentTransform();
        if (Entry.ContainsNaN()) continue;
        const FVector Normal = Entry.GetUnitAxis(EAxis::Z);
        if (FVector::DotProduct(Normal, FVector::UpVector) <= 0.4) continue;
        const double Above = FVector::DotProduct(Start - Entry.GetLocation(), Normal);
        const double Below = FVector::DotProduct(End - Entry.GetLocation(), Normal);
        if (!FMath::IsFinite(Above) || !FMath::IsFinite(Below) || Above <= 0.0 || Below > 0.0) continue;
        const double Time = Above / (Above - Below);
        if (Time < 0.0 || Time > 1.0 || Time >= FirstTime) continue;
        double IntakeTime = Time;
        FVector IntakePoint = FMath::Lerp(Start, End, Time);
        if (Time > HitTime + 1.e-3)
        {
            // The saved grinder uses a solid upper-box collision over its visual hopper.
            // Accept only the first, upward-facing contact in this grinder's small intake column.
            // A foreign object, side wall or another grinder still blocks the stream.
            const FVector Contact = Entry.InverseTransformPositionNoScale(Hit.ImpactPoint);
            if (!bHit || Hit.GetActor() != Grinder || Hit.GetComponent() != Grinder->BodyMesh.Get()
                || FVector::DotProduct(Hit.ImpactNormal, Normal) < .5
                || !FMath::IsFinite(HopperIntakeHeightCM) || Contact.Z < 0 || Contact.Z > FMath::Max(0.f, HopperIntakeHeightCM)
                || Contact.X * Contact.X + Contact.Y * Contact.Y > RadiusSquared) continue;
            IntakeTime = HitTime;
            IntakePoint = Hit.ImpactPoint;
        }
        const FVector Point = FMath::Lerp(Start, End, Time);
        // 入口半径用世界厘米；剔除组件缩放，避免缩到 0.5 后误判开口只有半径的一半。
        const FVector Local = Entry.InverseTransformPositionNoScale(Point);
        if (Local.X * Local.X + Local.Y * Local.Y > RadiusSquared) continue;
        Receiver = Grinder;
        FirstTime = IntakeTime;
        Impact = IntakePoint;
    }
    return Receiver;
}

float ADirectBeanJarActor::PourBeans(float DeltaTime)
{
    DisableLiquidPresentation();
    if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f || !CanPourBeans())
    {
        ClearBeanStream();
        return 0.f;
    }
    InitializeBeans();
    const FVector Start = GetBeanMouthWorld();
    const FVector Direction = (ContainerMesh->GetUpVector() * 8.0 + FVector::DownVector * 20.0).GetSafeNormal();
    const FVector End = Start + Direction * MaxBeanTravelCM;
    if (Start.ContainsNaN() || End.ContainsNaN() || Direction.IsNearlyZero())
    {
        ClearBeanStream();
        return 0.f;
    }
    FVector Impact = End;
    ACoffeeGrinderActor* Receiver = FindBeanReceiver(Start, End, Impact);
    const double Out = FMath::Min(CurrentBeanGrams, static_cast<double>(BeanPourRateGramsPerSecond) * DeltaTime);
    if (Out <= 0.0) { ClearBeanStream(); return 0.f; }

    TGuardValue<bool> TransferGuard(bBeanTransferInProgress, true);
    // 先扣库存并把全部出豆登记为洒落；接纳后只移动分类，回调销毁源罐也不漏账。
    CurrentBeanGrams -= Out;
    TotalPouredGrams += Out;
    SpilledBeanGrams += Out;
    if (IsValid(Receiver) && !Receiver->IsActorBeingDestroyed())
    {
        const float Before = Receiver->GetCurrentBeanGrams();
        float Requested = 0.f;
        if (FMath::IsFinite(Before) && Before >= 0.f)
        {
            // 按接收方 float 库存的可表示增量向下取整，而非只对请求取整；
            // 否则 Before + Requested 的舍入仍可能创造超过已扣库存的克数。
            const double Upper = static_cast<double>(Before) + Out;
            float Target = static_cast<float>(Upper);
            if (static_cast<double>(Target) > Upper) Target = std::nextafter(Target, Before);
            Requested = FMath::Max(0.f, Target - Before);
            if (static_cast<double>(Requested) > Out) Requested = std::nextafter(Requested, 0.f);
            while (Requested > 0.f && static_cast<double>(Before + Requested) - Before > Out)
            {
                Requested = std::nextafter(Requested, 0.f);
            }
        }
        const float Received = Requested > 0.f ? Receiver->TryAddBeans(Requested) : 0.f;
        const double Accepted = FMath::IsFinite(Received) ? FMath::Clamp(static_cast<double>(Received), 0.0, Out) : 0.0;
        TotalAcceptedGrams += Accepted;
        SpilledBeanGrams -= Accepted;
    }
    if (!IsValid(this) || IsActorBeingDestroyed()) return static_cast<float>(Out);
    UpdateBeanPileVisual();
    if (IsBroken() || IsLiquidFrozen() || bWorkbenchRestoreEmissionBlocked || bBeanRestoreBlocked)
    {
        if (IsBroken()) HandleJarBroken(this);
        ClearBeanStream();
    }
    else UpdateBeanStream(Start, Impact, DeltaTime);
    return static_cast<float>(Out);
}

void ADirectBeanJarActor::UpdateBeanPileVisual()
{
    if (!bBeanVisualInitialized || !IsValid(BeanPileMesh.Get())) return;
    const bool bVisible = !IsBroken() && CurrentBeanGrams > 0.0 && BeanPileMesh->GetStaticMesh();
    BeanPileMesh->SetVisibility(bVisible);
    BeanPileMesh->SetHiddenInGame(!bVisible);
    if (!bVisible) return;
    const double Ratio = BeanCapacityGrams > 0.f && FMath::IsFinite(BeanCapacityGrams)
        ? FMath::Clamp(CurrentBeanGrams / BeanCapacityGrams, 0.0, 1.0) : 0.0;
    const double HeightRatio = FMath::Max(0.01, Ratio);
    FVector Scale = BeanPileInitialScale;
    Scale.Z *= HeightRatio;
    const FBox Bounds = BeanPileMesh->GetStaticMesh()->GetBoundingBox();
    const double Bottom = FMath::Min(Bounds.Min.Z * BeanPileInitialScale.Z, Bounds.Max.Z * BeanPileInitialScale.Z);
    const FVector BottomOffset = BeanPileMesh->GetRelativeRotation().RotateVector(FVector(0.0, 0.0, Bottom * (1.0 - HeightRatio)));
    BeanPileMesh->SetRelativeScale3D(Scale);
    // 围绕实际豆堆底面缩短高度；不把整块网格平移到玻璃罐底之外，不更换原材质。
    BeanPileMesh->SetRelativeLocation(BeanPileInitialLocation + BottomOffset);
}

void ADirectBeanJarActor::UpdateBeanStream(const FVector& Start, const FVector& End, float DeltaTime)
{
    if (!IsValid(BeanStream.Get()) || !BeanStream->GetStaticMesh()) { ClearBeanStream(); return; }
    constexpr int32 InstanceCount = 24;
    const double Length = FVector::Distance(Start, End);
    if (Length <= UE_SMALL_NUMBER) { ClearBeanStream(); return; }
    BeanStreamPhase = FMath::Fmod(BeanStreamPhase + DeltaTime * 100.0 / FMath::Max(Length, 1.0), 1.0);
    const FVector Along = (End - Start).GetSafeNormal();
    FVector Side = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
    if (Side.IsNearlyZero()) Side = FVector::RightVector;
    const FVector Other = FVector::CrossProduct(Along, Side).GetSafeNormal();
    while (BeanStream->GetInstanceCount() > InstanceCount) BeanStream->RemoveInstance(BeanStream->GetInstanceCount() - 1);
    for (int32 Index = 0; Index < InstanceCount; ++Index)
    {
        const double Alpha = FMath::Fmod(BeanStreamPhase + static_cast<double>(Index) / InstanceCount, 1.0);
        const double Scatter = FMath::Sin(Alpha * UE_PI) * 0.22;
        const FVector Position = FMath::Lerp(Start, End, Alpha)
            + Side * (FMath::Sin(Index * 2.399) * Scatter) + Other * (FMath::Cos(Index * 2.399) * Scatter);
        const FTransform Pose(FRotator(Index * 31.0 + Alpha * 180.0, Index * 47.0, Index * 19.0), Position, BeanStream->GetComponentScale());
        if (Index < BeanStream->GetInstanceCount()) BeanStream->UpdateInstanceTransform(Index, Pose, true, Index == InstanceCount - 1, true);
        else BeanStream->AddInstance(Pose, true);
    }
    BeanStream->SetVisibility(true);
    BeanStream->SetHiddenInGame(false);
}

void ADirectBeanJarActor::ClearBeanStream()
{
    BeanStreamPhase = 0.0;
    if (!IsValid(BeanStream.Get())) return;
    if (BeanStream->GetInstanceCount() > 0) BeanStream->ClearInstances();
    BeanStream->SetVisibility(false);
    BeanStream->SetHiddenInGame(true);
}

void ADirectBeanJarActor::DisableLiquidPresentation()
{
    bCanPour = false;
    bAcceptLiquidFromOthers = false;
    bEnablePourAudio = false;
    bDynamicWaves = false;
    MaxVolumeML = 1.f;
    FillAmount = 0.f;
    InitialIngredientId = TEXT("Unknown");
    if (bIsPouring) StopPouring();
    if (IsPourAudioRequested()) StopPourAudio(true);
    for (UNiagaraComponent* FX : {LiquidFX.Get(), PourFX.Get()})
    {
        if (!IsValid(FX)) continue;
        FX->SetAutoActivate(false);
        FX->DeactivateImmediate();
        FX->SetVisibility(false);
        FX->SetHiddenInGame(true);
        FX->SetComponentTickEnabled(false);
    }
}

void ADirectBeanJarActor::StartPouring()
{
    DisableLiquidPresentation();
}

void ADirectBeanJarActor::UpdatePouring(float DeltaTime)
{
    DisableLiquidPresentation();
}

void ADirectBeanJarActor::HandleJarBroken(ALiquidContainerActor* Container)
{
    if (Container != this) return;
    InitializeBeans();
    SpilledBeanGrams += CurrentBeanGrams;
    CurrentBeanGrams = 0.0;
    UpdateBeanPileVisual();
    ClearBeanStream();
}

void ADirectBeanJarActor::SettleRemainingBeans()
{
    InitializeBeans();
    if (IsBroken()) SpilledBeanGrams += CurrentBeanGrams;
    else DiscardedBeanGrams += CurrentBeanGrams;
    CurrentBeanGrams = 0.0;
    UpdateBeanPileVisual();
    ClearBeanStream();
}

void ADirectBeanJarActor::Destroyed()
{
    SettleRemainingBeans();
    Super::Destroyed();
}

void ADirectBeanJarActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SettleRemainingBeans();
    Super::EndPlay(EndPlayReason);
}
