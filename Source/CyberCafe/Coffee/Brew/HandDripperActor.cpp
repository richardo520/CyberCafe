// @trace CF-02 CF-03 CF-04
#include "Coffee/Brew/HandDripperActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Character/VRPawn.h"
#include "MotionControllerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GrabComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr double EmptyToleranceML = 1.e-6;
    constexpr float BatchCapacityGrams = 20.f;
    constexpr float MaximumYieldMLPerGram = 16.f;
    const FName WaterId(TEXT("Water"));
    const FName CoffeeId(TEXT("Coffee"));
    const FLinearColor CoffeeColor(0.12f, 0.035f, 0.012f, 1.f);
}

AHandDripperActor::AHandDripperActor()
{
    bCanPour = false;
    bAcceptLiquidFromOthers = true;
    FillAmount = 0.f;
    MaxVolumeML = 120.f;
    InitialIngredientId = WaterId;
    LiquidColor = FLinearColor(0.85f, 0.92f, 1.f, 1.f);
    PourEntryRadius = 4.f;
    bUseRibbonTrajectory = false;
    PourInitialSpeedCms = 20.f;
    ContainerMesh->SetSimulatePhysics(false);
    GrabComp->GrabType = EGrabType::Free;
    GrabComp->PhysicsMode = EGrabPhysicsMode::Kinematic;
    GrabComp->bSimulateOnDrop = false;
    PourEntryPoint->SetRelativeLocation(FVector(0, 0, 6.5));
    PourTargetPoint->SetRelativeLocation(FVector::ZeroVector);
    PourFX->SetRelativeLocation(OutletLocal);

    PaperMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PaperMesh"));
    PaperMesh->SetupAttachment(ContainerMesh);
    PaperMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PaperMesh->SetGenerateOverlapEvents(false);

    GroundBed = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundBed"));
    GroundBed->SetupAttachment(ContainerMesh);
    GroundBed->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundBed->SetGenerateOverlapEvents(false);
    GroundBed->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    DropVisual=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DropVisual"));DropVisual->SetupAttachment(ContainerMesh);DropVisual->SetStaticMesh(Sphere.Object);DropVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);DropVisual->SetCastShadow(false);
    WaterSurface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterSurface"));WaterSurface->SetupAttachment(ContainerMesh);WaterSurface->SetStaticMesh(Cylinder.Object);WaterSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);WaterSurface->SetVisibility(false);WaterSurface->SetCastShadow(false);
    PaperButton=CreateDefaultSubobject<UBoxComponent>(TEXT("PaperButton"));PaperButton->SetupAttachment(ContainerMesh);PaperButton->SetRelativeLocation(FVector(0,-7,3));PaperButton->SetBoxExtent(FVector(2.5,1.5,1));PaperButton->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PaperButtonLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("PaperButtonLabel"));PaperButtonLabel->SetupAttachment(ContainerMesh);PaperButtonLabel->SetRelativeLocation(FVector(0,-7,3));PaperButtonLabel->SetRelativeRotation(FRotator(0,-90,0));PaperButtonLabel->SetHorizontalAlignment(EHTA_Center);PaperButtonLabel->SetWorldSize(.7f);PaperButtonLabel->SetText(FText::FromString(TEXT("NEW FILTER")));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> PaperAsset(
        TEXT("/Game/coffee_grinder_scene/CoffeeBean/FilterSet/SM_ConeFilterPaper.SM_ConeFilterPaper"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> GroundsAsset(
        TEXT("/Game/coffee_grinder_scene/CoffeeBean/GroundsV02/SM_Grounds_Filter_V02.SM_Grounds_Filter_V02"));
    if (PaperAsset.Succeeded()) PaperMesh->SetStaticMesh(PaperAsset.Object);
    if (GroundsAsset.Succeeded())
    {
        GroundBed->SetStaticMesh(GroundsAsset.Object);
        const FBoxSphereBounds Bounds = GroundsAsset.Object->GetBounds();
        const double Radius = FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
        if (Radius > UE_SMALL_NUMBER && Bounds.BoxExtent.Z > UE_SMALL_NUMBER)
        {
            const FVector Scale(1.65 / Radius, 1.65 / Radius, 0.45 / Bounds.BoxExtent.Z);
            GroundBed->SetRelativeScale3D(Scale);
            GroundBed->SetRelativeLocation(FVector(0, 0, 3.6) - Bounds.Origin * Scale);
        }
    }

    StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
    StatusText->SetupAttachment(ContainerMesh);
    StatusText->SetRelativeLocation(FVector(0, -6, 8));
    StatusText->SetRelativeRotation(FRotator(0, -90, 0));
    StatusText->SetHorizontalAlignment(EHTA_Center);
    StatusText->SetWorldSize(1.2f);
    StatusText->SetTextRenderColor(FColor(235, 220, 190));
    StatusText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StatusText->SetText(FText::FromString(TEXT("EMPTY / ADD GROUNDS")));
    InitialGroundLocation = GroundBed->GetRelativeLocation();
    InitialGroundScale = GroundBed->GetRelativeScale3D();
}

void AHandDripperActor::BeginPlay()
{
    // 先订阅，基类初始化后的实际入液及出液通知均只读取库存。
    OnLiquidChanged.AddUniqueDynamic(this, &AHandDripperActor::OnStorageChanged);
    Super::BeginPlay();
    if (GroundBed)
    {
        InitialGroundLocation = GroundBed->GetRelativeLocation();
        InitialGroundScale = GroundBed->GetRelativeScale3D();
        for (int32 Index = 0; Index < GroundBed->GetNumMaterials(); ++Index)
            if (GroundBed->GetMaterial(Index)) GroundBed->CreateAndSetMaterialInstanceDynamic(Index);
    }
    OnStorageChanged(FillAmount, LiquidColor);
    UpdateBrewVisuals();
}

void AHandDripperActor::Tick(float Dt)
{
    if (!FMath::IsFinite(Dt) || Dt < 0.f) return;
    // 禁止基类倾倒路径，但保留波动及接液冷却维护。
    bCanPour = false;
    Super::Tick(Dt);
    AdvanceBrew(Dt);
    bool Touch=false;
    for(TActorIterator<AVRPawn> It(GetWorld());It;++It){if(!It->IsLocallyControlled())continue;
        for(int H=0;H<2;++H){auto* Grip=H==0?It->MotionControllerLeftGrip.Get():It->MotionControllerRightGrip.Get();auto* Held=H==0?It->HeldComponentLeft.Get():It->HeldComponentRight.Get();if(!Grip||!Grip->IsTracked()||(Held&&Held->IsHeld()))continue;auto* Hand=H==0?It->HandLeft.Get():It->HandRight.Get();const FName Bone=H==0?TEXT("index_03_l"):TEXT("index_03_r");const FVector P=Hand&&Hand->DoesSocketExist(Bone)?Hand->GetSocketLocation(Bone):Grip->GetComponentLocation();FVector L=PaperButton->GetComponentTransform().InverseTransformPosition(P);FVector E=PaperButton->GetUnscaledBoxExtent();Touch|=FMath::Abs(L.X)<E.X&&FMath::Abs(L.Y)<E.Y&&FMath::Abs(L.Z)<E.Z;}}
    if(!Touch){PaperHold=0;bPaperTouchLatched=false;}else if(!bPaperTouchLatched){PaperHold+=FMath::Min(Dt,.1f);if(PaperHold>=1){bPaperTouchLatched=true;ReplacePaper();}}
}
bool AHandDripperActor::ReplacePaper()
{
    if(IsBroken()||IsLiquidFrozen()||IsHeld()||GetPreciseVolumeML()>1.e-6)return false;
    auto* S=GetWorld()->GetSubsystem<ULiquidTransferSubsystem>();if(S&&S->GetUnsettledFlightHorizon()>0)return false;
    DiscardedGroundsGrams+=GroundsGrams;GroundsGrams=0;RemainingCoffeeML=0;CoffeeProducedML=0;DrippedML=0;Wetness=0;BloomElapsed=0;bWaterStarted=false;BrewState=ECoffeeBrewState::Empty;++BrewGeneration;UpdateBrewVisuals();return true;
}

bool AHandDripperActor::ResetBrewForWorkbench()
{
    const double VolumeML = GetPreciseVolumeML();
    if (!IsValid(this) || IsBroken() || IsLiquidFrozen()
        || !IsValid(GrabComp) || GrabComp->GetOwner() != this
        || GrabComp->IsHeld() || GrabComp->IsPulled()
        || !FMath::IsFinite(VolumeML) || VolumeML < 0.0 || VolumeML > EmptyToleranceML)
    {
        return false;
    }

    // 排液及源在途包由管理器结算；其他器具的在途包不阻塞此恢复。
    DiscardedGroundsGrams += GroundsGrams;
    GroundsGrams = 0.f;
    RemainingCoffeeML = 0.f;
    BloomElapsed = 0.f;
    Wetness = 0.f;
    BrewState = ECoffeeBrewState::Empty;
    bWaterStarted = false;
    ++BrewGeneration;
    CoffeeProducedAtBatchStartML = CoffeeProducedML;
    StopPouring();
    if (IsValid(DropVisual)) { DropVisual->ClearInstances(); }
    DropAge = 0.f;
    PaperHold = 0.f;
    bPaperTouchLatched = false;
    SoundCooldown = 0.f;
    if (IsValid(WaterSurface)) { WaterSurface->SetVisibility(false); }
    UpdateBrewVisuals();
    return true;
}

bool AHandDripperActor::CanReceiveGrounds() const
{
    const float Capacity = FMath::IsFinite(MaxGroundsGrams)
        ? FMath::Clamp(MaxGroundsGrams, 0.f, BatchCapacityGrams) : 0.f;
    return !IsBroken() && !IsLiquidFrozen() && !bWaterStarted
        && (BrewState == ECoffeeBrewState::Empty || BrewState == ECoffeeBrewState::Dry)
        && GetPreciseVolumeML() <= EmptyToleranceML && GroundsGrams < Capacity;
}

float AHandDripperActor::AddGrounds(float Grams, const FVector& WorldPoint)
{
    if (!FMath::IsFinite(Grams) || Grams <= 0.f || WorldPoint.ContainsNaN()
        || !CanReceiveGrounds() || !PourEntryPoint
        || !FMath::IsFinite(PourEntryRadius) || PourEntryRadius <= 0.f) return 0.f;

    // API 自身校验开口平面，不依赖调用方已经做过相交测试。
    const FTransform EntryTransform = PourEntryPoint->GetComponentTransform();
    if (EntryTransform.ContainsNaN()) return 0.f;
    const FVector LocalPoint = EntryTransform.InverseTransformPositionNoScale(WorldPoint);
    if (LocalPoint.ContainsNaN() || FMath::Abs(LocalPoint.Z) > 2.0
        || LocalPoint.SizeSquared2D() > FMath::Square(static_cast<double>(PourEntryRadius))) return 0.f;

    const float Capacity = FMath::Clamp(MaxGroundsGrams, 0.f, BatchCapacityGrams);
    const float NewGrounds = FMath::Min(Capacity, GroundsGrams + FMath::Min(Grams, Capacity - GroundsGrams));
    const float Accepted = NewGrounds - GroundsGrams;
    if (Accepted <= 0.f) return 0.f;
    const float Yield = FMath::IsFinite(CoffeeYieldMLPerGram)
        ? FMath::Clamp(CoffeeYieldMLPerGram, 0.f, MaximumYieldMLPerGram) : 0.f;
    if (GroundsGrams == 0.f)
    {
        ++BrewGeneration;
        CoffeeProducedAtBatchStartML = CoffeeProducedML;
    }
    GroundsGrams = NewGrounds;
    RemainingCoffeeML = FMath::Min(GroundsGrams * MaximumYieldMLPerGram,
        RemainingCoffeeML + Accepted * Yield);
    BrewState = ECoffeeBrewState::Dry;
    UpdateBrewVisuals();
    return Accepted;
}

void AHandDripperActor::OnStorageChanged(float /*Fill*/, FLinearColor /*Color*/)
{
    if (IsBroken() || IsLiquidFrozen()) return;
    const double Volume = GetPreciseVolumeML();
    const double Water = GetIngredientVolumeML(WaterId);
    if(Water>EmptyToleranceML)Wetness=FMath::Max(Wetness,static_cast<float>(FMath::Clamp(Water/FMath::Max(1.f,GroundsGrams*2.f),0.0,1.0)));

    if (!bWaterStarted && Water > EmptyToleranceML)
    {
        bWaterStarted = true;
        BloomElapsed = 0.f;
        BrewState = GroundsGrams > 0.f ? ECoffeeBrewState::Blooming : ECoffeeBrewState::Brewing;
    }
    if (Volume <= EmptyToleranceML)
    {
        // 一个滤纸批次，不因滴空而清粉、恢复预算或允许重新加粉。
        BrewState = bWaterStarted ? ECoffeeBrewState::Spent
            : (GroundsGrams > 0.f ? ECoffeeBrewState::Dry : ECoffeeBrewState::Empty);
    }
    else if (BrewState != ECoffeeBrewState::Blooming)
    {
        // Spent 可再接水并使用本批剩余预算，但不重复闷蒸。
        BrewState = ECoffeeBrewState::Brewing;
    }
}

void AHandDripperActor::PrepareTransferredLiquid(FLiquidComposition& Portion, FLinearColor& Color)
{
    if (IsBroken() || IsLiquidFrozen()) return;
    const double Total = Portion.Total();
    if (!FMath::IsFinite(Total) || Total <= 0.0) return;
    DrippedML += Total;

    const double Water = Portion.Get(WaterId);
    const double Budget = FMath::IsFinite(RemainingCoffeeML) ? FMath::Max(0.f, RemainingCoffeeML) : 0.0;
    // 双精度累计值另行约束整批上限，避免 float 余额舍入产生无限尾量。
    const double BatchRemaining = FMath::Max(0.0,
        static_cast<double>(GroundsGrams) * MaximumYieldMLPerGram
            - FMath::Max(0.0, CoffeeProducedML - CoffeeProducedAtBatchStartML));
    const double Converted = FMath::IsFinite(Water)
        ? FMath::Min(FMath::Max(0.0, Water), FMath::Min(Budget, BatchRemaining)) : 0.0;
    if (Converted > 0.0)
    {
        const double WaterLeft = Water - Converted;
        if (WaterLeft > 0.0) Portion.Ingredients.FindChecked(WaterId) = WaterLeft;
        else Portion.Ingredients.Remove(WaterId);
        Portion.Ingredients.FindOrAdd(CoffeeId) += Converted;
        RemainingCoffeeML = static_cast<float>(FMath::Max(0.0,
            FMath::Min(Budget - Converted, BatchRemaining - Converted)));
        CoffeeProducedML += Converted;
        Color = FMath::Lerp(Color, CoffeeColor, static_cast<float>(Converted / Total));
    }
    // 仅改变出液包的 Water 标签；Milk/Wine 等毫升数不动，也不回注库存。
    if (PourFX) PourFX->SetNiagaraVariableLinearColor(TEXT("User.Color"), Color);
}

UMaterialInterface* AHandDripperActor::GetTransferredMaterial(const FLiquidComposition& Portion) const
{
    if(Portion.Get(CoffeeId)>1.e-6&&CoffeeLiquidMaterial)return CoffeeLiquidMaterial.Get();
    return WaterLiquidMaterial?WaterLiquidMaterial.Get():LiquidMaterialAsset.Get();
}
void AHandDripperActor::AdvanceBrew(float Dt)
{
    if (!FMath::IsFinite(Dt) || Dt < 0.f) return;
    SoundCooldown = FMath::Max(0.f, SoundCooldown - Dt);
    if (IsBroken() || IsLiquidFrozen())
    {
        StopPouring();
        return;
    }
    OnStorageChanged(FillAmount, LiquidColor);
    double DrainTime = Dt;
    double BloomTime = 0.0;
    if (BrewState == ECoffeeBrewState::Blooming)
    {
        const double Duration = FMath::IsFinite(BloomSeconds) ? FMath::Max(0.f, BloomSeconds) : 8.0;
        BloomTime = FMath::Min(DrainTime, FMath::Max(0.0, Duration - BloomElapsed));
        BloomElapsed = static_cast<float>(FMath::Min(Duration, BloomElapsed + BloomTime));
        DrainTime -= BloomTime;
        if (BloomElapsed >= Duration) BrewState = ECoffeeBrewState::Brewing;
        else DrainTime = 0.0;
    }

    // 闷蒸按完整传入时间推进；长帧滴流最多积分两秒，分片出生于各片中点。
    DrainTime = FMath::Min(DrainTime, 2.0);
    const double Rate = FMath::IsFinite(DrainRateMLPerSecond) ? FMath::Max(0.f, DrainRateMLPerSecond) : 0.0;
    ULiquidTransferSubsystem* Transfer = GetWorld() ? GetWorld()->GetSubsystem<ULiquidTransferSubsystem>() : nullptr;
    double Emitted = 0.0;
    if(DropVisual)DropVisual->ClearInstances();
    if (Transfer && DrainTime > 0.0 && Rate > 0.0 && BrewState == ECoffeeBrewState::Brewing
        && !IsHeld() && FVector::DotProduct(GetActorUpVector(), FVector::UpVector) >= 0.4)
    {
        const int32 Slices = FMath::Clamp(FMath::CeilToInt(DrainTime * 60.0), 1, 120);
        const double SliceTime = DrainTime / Slices;
        for (int32 Index = 0; Index < Slices; ++Index)
        {
            // EmitVolume 通知可触发外部抓取、冻结或销毁，下一片必须重新检查。
            if (!IsValid(this) || IsBroken() || IsLiquidFrozen() || IsHeld()
                || BrewState != ECoffeeBrewState::Brewing
                || FVector::DotProduct(GetActorUpVector(), FVector::UpVector) < 0.4) break;
            const double Wanted = FMath::Min(GetPreciseVolumeML(), Rate * SliceTime);
            if (Wanted <= 0.0) break;
            const FVector Outlet = GetActorTransform().TransformPosition(OutletLocal);
            const double Actual = Transfer->EmitVolume(this, Wanted, Outlet,
                FVector(0, 0, -20), FVector(0, 0, -980), BloomTime + (Index + 0.5) * SliceTime);
            Emitted += Actual;
            if (Actual <= 0.0) break;
        }
    }
    if (!IsValid(this)) return;
    if(Emitted>0&&DropVisual){DropAge+=Dt;for(int I=0;I<6;++I){const float T=FMath::Frac(DropAge*2.5f+I/6.f);const FVector Pos=GetActorTransform().TransformPosition(OutletLocal)+FVector(0,0,-T*12.f);DropVisual->AddInstance(FTransform(FQuat::Identity,Pos,FVector(.0025,.0025,.0045)),true);}}
    if(WaterSurface){const float SurfaceZ=3.1f+2.6f*FMath::Clamp(FillAmount,0.f,1.f);const float Radius=SurfaceZ/6.5f*4.f-.35f;WaterSurface->SetRelativeLocation(FVector(0,0,SurfaceZ));WaterSurface->SetRelativeScale3D(FVector(Radius/50,Radius/50,.0006));WaterSurface->SetVisibility(GetPreciseVolumeML()>1.e-6);}
    OnStorageChanged(FillAmount, LiquidColor);
    if (IsBroken() || IsLiquidFrozen())
    {
        StopPouring();
        return;
    }
    if (Emitted > 0.0 && DripSound && SoundCooldown <= 0.f)
    {
        UGameplayStatics::PlaySoundAtLocation(this, DripSound,
            GetActorTransform().TransformPosition(OutletLocal), 0.35f);
        SoundCooldown = 0.3f;
    }
    // 可选原生滴流资产；不套用轨迹不匹配的 P_Ribbon，不由 FX 结算毫升。
    if (PourFX)
    {
        const bool bUsableFX = PourFX->GetAsset() && PourFX->GetAsset()->GetFName() != TEXT("P_Ribbon");
        if (Emitted > 0.0 && bUsableFX)
        {
            PourFX->SetWorldLocationAndRotation(GetActorTransform().TransformPosition(OutletLocal),
                FVector(0, 0, -1).Rotation());
            if (!PourFX->IsActive()) PourFX->Activate(false);
        }
        else if (PourFX->IsActive()) PourFX->Deactivate();
    }
    UpdateBrewVisuals();
}

void AHandDripperActor::UpdateBrewVisuals()
{
    if (GroundBed)
    {
        GroundBed->SetVisibility(GroundsGrams > 0.f && !IsBroken());
        const float Fraction = FMath::Clamp(GroundsGrams / BatchCapacityGrams, 0.f, 1.f);
        const float XYFactor = 0.35f + 0.65f * FMath::Sqrt(Fraction);
        const float ZFactor = 0.15f + 0.85f * Fraction;
        GroundBed->SetRelativeScale3D(InitialGroundScale * FVector(XYFactor, XYFactor, ZFactor));
        if (const UStaticMesh* Mesh = GroundBed->GetStaticMesh())
        {
            const FBoxSphereBounds Bounds = Mesh->GetBounds();
            const FVector BottomShift(0, 0,
                (Bounds.Origin.Z - Bounds.BoxExtent.Z) * InitialGroundScale.Z * (1.f - ZFactor));
            GroundBed->SetRelativeLocation(InitialGroundLocation
                + GroundBed->GetRelativeRotation().RotateVector(BottomShift));
        }
        for (int32 Index = 0; Index < GroundBed->GetNumMaterials(); ++Index)
            if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(GroundBed->GetMaterial(Index)))
                MID->SetScalarParameterValue(TEXT("Wetness"), Wetness);
    }
    if (!StatusText) return;
    FString Label;
    switch (BrewState)
    {
    case ECoffeeBrewState::Empty:
        Label = TEXT("EMPTY / ADD GROUNDS");
        break;
    case ECoffeeBrewState::Dry:
        Label = FString::Printf(TEXT("%.1f g / POUR WATER"), GroundsGrams);
        break;
    case ECoffeeBrewState::Blooming:
    {
        const float Duration = FMath::IsFinite(BloomSeconds) ? FMath::Max(0.f, BloomSeconds) : 8.f;
        Label = FString::Printf(TEXT("BLOOM %.1f s"), FMath::Max(0.f, Duration - BloomElapsed));
        break;
    }
    case ECoffeeBrewState::Brewing:
        Label = FString::Printf(TEXT("DRIPPING %.1f mL"), GetCurrentVolumeML());
        break;
    case ECoffeeBrewState::Spent:
        Label = TEXT("SPENT FILTER");
        break;
    }
    StatusText->SetText(FText::FromString(Label));
}
