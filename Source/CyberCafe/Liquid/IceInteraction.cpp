// @trace IC-01 IC-02 IC-03 IC-04
#include "Liquid/IceInteraction.h"
#include "Liquid/CupActor.h"
#include "GrabComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    bool IsFiniteVector(const FVector& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    bool IsUsableTransform(const FTransform& Transform)
    {
        const FVector Scale = Transform.GetScale3D().GetAbs();
        return !Transform.ContainsNaN() && IsFiniteVector(Scale)
            && Scale.GetMin() > KINDA_SMALL_NUMBER;
    }

    FVector SafeVelocity(const FVector& Velocity, float Maximum = 800.f)
    {
        return IsFiniteVector(Velocity) ? Velocity.GetClampedToMaxSize(Maximum) : FVector::ZeroVector;
    }

    FVector SampleVelocity(const FVector& Previous, const FVector& Current, float Dt)
    {
        if (!FMath::IsFinite(Dt) || Dt < 0.0001f || !IsFiniteVector(Previous) || !IsFiniteVector(Current))
        {
            return FVector::ZeroVector;
        }
        const FVector Velocity = (Current - Previous) / Dt;
        // 传送或追踪跳变不能转化成虚假的投掷速度。
        if (!IsFiniteVector(Velocity) || Velocity.SizeSquared() > FMath::Square(5000.0))
        {
            return FVector::ZeroVector;
        }
        return SafeVelocity(Velocity);
    }

    void EnableFreeCollision(UStaticMeshComponent* Mesh)
    {
        if (!Mesh) return;
        Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Mesh->SetCollisionObjectType(ECC_PhysicsBody);
        Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
        Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
        Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
        Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        Mesh->SetGenerateOverlapEvents(false);
    }

    bool GetCupGeometry(const ACupActor* Cup, float InteriorRadius, float InteriorDepth,
        float IceRadius, FTransform& Entry, float& Radius, float& Depth, float& CubeRadius)
    {
        if (!IsValid(Cup) || !IsValid(Cup->PourEntryPoint.Get())
            || !FMath::IsFinite(InteriorRadius) || !FMath::IsFinite(InteriorDepth)
            || !FMath::IsFinite(Cup->PourEntryRadius) || !FMath::IsFinite(IceRadius)
            || InteriorRadius <= 0.f || InteriorDepth <= 0.f || Cup->PourEntryRadius <= 0.f || IceRadius <= 0.f)
        {
            return false;
        }
        Entry = Cup->PourEntryPoint->GetComponentTransform();
        if (!IsUsableTransform(Entry)) return false;
        const FVector Scale = Entry.GetScale3D().GetAbs();
        Radius = FMath::Min(InteriorRadius, Cup->PourEntryRadius);
        Depth = InteriorDepth;
        // 入口坐标随组件缩放，冰的碰撞球仍使用厘米；非等比缩放保守取横向较小轴。
        CubeRadius = IceRadius / FMath::Min(Scale.X, Scale.Y);
        return FMath::IsFinite(CubeRadius) && Radius > CubeRadius
            && Depth > IceRadius / Scale.Z * 2.f;
    }

    bool CrossesCupMouth(const UCupIceComponent* Component, const AIceCubeActor* Ice,
        const FVector& Previous, const FVector& Current, float& CrossingTime)
    {
        if (!IsValid(Component) || !IsValid(Ice) || !IsFiniteVector(Previous)
            || !IsFiniteVector(Current) || Previous.Equals(Current, UE_SMALL_NUMBER)) return false;
        FTransform Entry;
        float Radius = 0.f, Depth = 0.f, CubeRadius = 0.f;
        if (!GetCupGeometry(Component->GetCup(), Component->InteriorRadiusCM,
            Component->InteriorDepthCM, Ice->RadiusCM, Entry, Radius, Depth, CubeRadius)) return false;
        const FVector Old = Entry.InverseTransformPosition(Previous);
        const FVector New = Entry.InverseTransformPosition(Current);
        if (!IsFiniteVector(Old) || !IsFiniteVector(New) || Old.Z <= 0.0 || New.Z > 0.0) return false;
        const double Denominator = Old.Z - New.Z;
        if (!FMath::IsFinite(Denominator) || Denominator <= UE_SMALL_NUMBER) return false;
        CrossingTime = static_cast<float>(Old.Z / Denominator);
        if (!FMath::IsFinite(CrossingTime) || CrossingTime < 0.f || CrossingTime > 1.f) return false;
        const FVector Crossing = FMath::Lerp(Old, New, CrossingTime);
        const double Clearance = Radius - CubeRadius;
        // 整个冰球须能通过真实入口；当前点在外壁之外也不能吸入杯中。
        return Crossing.SizeSquared2D() <= Clearance * Clearance
            && New.SizeSquared2D() <= Clearance * Clearance;
    }

    void RebuildBucketPile(AIceBucketActor* Bucket)
    {
        if (!IsValid(Bucket) || !IsValid(Bucket->IcePile.Get())) return;
        UInstancedStaticMeshComponent* Pile = Bucket->IcePile.Get();
        Pile->SetSimulatePhysics(false);
        Pile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Pile->SetGenerateOverlapEvents(false);
        Pile->ClearInstances();
        Pile->SetStaticMesh(Bucket->IceMesh.Get());
        if (!IsValid(Bucket->IceMesh.Get())) return;
        const FBoxSphereBounds Bounds = Bucket->IceMesh->GetBounds();
        const double Diameter = (Bounds.BoxExtent * 2.0).GetMax();
        if (!FMath::IsFinite(Diameter) || Diameter <= UE_SMALL_NUMBER || !IsFiniteVector(Bounds.Origin)) return;
        const double Scale = 2.4 / Diameter;
        for (int32 Index = 0; Index < 25; ++Index)
        {
            const int32 Row = Index / 5;
            const int32 Column = Index % 5;
            const FVector Center((Column - 2) * 2.55, (Row - 2) * 2.55,
                10.0 + ((Row + Column) % 3) * 1.6);
            const FQuat Rotation = FRotator((Index % 3 - 1) * 9.f,
                Index * 37.f, (Index % 5 - 2) * 7.f).Quaternion();
            const FVector Origin = Center - Rotation.RotateVector(Bounds.Origin * Scale);
            Pile->AddInstance(FTransform(Rotation, Origin, FVector(Scale)), false);
        }
    }
}

bool UIceWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE || Type == EWorldType::GamePreview;
}

bool UIceWorldSubsystem::CanAllocate(int32 Count)
{
    MaxLiveIce = FMath::Clamp(MaxLiveIce, 1, 64);
    Live.RemoveAll([](const TWeakObjectPtr<AIceCubeActor>& Item)
    {
        const AIceCubeActor* Ice = Item.Get();
        return !IsValid(Ice) || Ice->IsActorBeingDestroyed();
    });
    return Count >= 0 && Count <= MaxLiveIce && Live.Num() <= MaxLiveIce - Count;
}

void UIceWorldSubsystem::RegisterIce(AIceCubeActor* Ice)
{
    if (!IsValid(Ice) || Ice->IsActorBeingDestroyed()) return;
    if (Live.ContainsByPredicate([Ice](const TWeakObjectPtr<AIceCubeActor>& Item)
        { return Item.Get() == Ice; })) return;
    if (!CanAllocate(1))
    {
        // 只拒绝新生成对象；绝不为腾名额销毁已有杯内冰。
        Ice->Destroy();
        return;
    }
    Live.Add(TWeakObjectPtr<AIceCubeActor>(Ice));
}

void UIceWorldSubsystem::ForgetIce(AIceCubeActor* Ice)
{
    Live.RemoveAll([Ice](const TWeakObjectPtr<AIceCubeActor>& Item)
    {
        const AIceCubeActor* Existing = Item.Get();
        return !IsValid(Existing) || Existing == Ice || Existing->IsActorBeingDestroyed();
    });
}

int32 UIceWorldSubsystem::GetLiveCount() const
{
    int32 Count = 0;
    for (const TWeakObjectPtr<AIceCubeActor>& Item : Live)
    {
        const AIceCubeActor* Ice = Item.Get();
        if (IsValid(Ice) && !Ice->IsActorBeingDestroyed()) ++Count;
    }
    return Count;
}

UCupIceComponent::UCupIceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCupIceComponent::BeginPlay()
{
    Super::BeginPlay();
    if (const ACupActor* Cup = GetCup())
    {
        const FVector Position = Cup->GetActorLocation();
        PreviousCupPosition = Position;
    }
    Age = 0.f;
    SoundCooldown = 0.f;
}

ACupActor* UCupIceComponent::GetCup() const
{
    return Cast<ACupActor>(GetOwner());
}

int32 UCupIceComponent::GetPreparationRevision() const
{
    // 查询前同步失效成员；只有实际移除才推进版本，不随查询或 Tick 空转。
    const_cast<UCupIceComponent*>(this)->RemoveIce(nullptr);
    return PreparationRevision;
}

int32 UCupIceComponent::GetIceCount() const
{
    const_cast<UCupIceComponent*>(this)->RemoveIce(nullptr);
    int32 Count = 0;
    for (const TObjectPtr<AIceCubeActor>& Item : IceCubes)
    {
        const AIceCubeActor* Ice = Item.Get();
        if (IsValid(Ice) && !Ice->IsActorBeingDestroyed()
            && Ice->State == EIceState::InCup && Ice->CupIce.Get() == this) ++Count;
    }
    return Count;
}

bool UCupIceComponent::TryAccept(AIceCubeActor* Ice, const FVector& Previous, const FVector& Current)
{
    ACupActor* Cup = GetCup();
    UWorld* World = GetWorld();
    if (!World || !IsValid(Cup) || Cup->IsActorBeingDestroyed() || Cup->IsBroken() || Cup->IsLiquidFrozen()
        || !IsValid(Cup->ContainerMesh.Get()) || !IsValid(Ice) || Ice->IsActorBeingDestroyed()
        || Ice->GetWorld() != World || Ice->State != EIceState::Free || !IsValid(Ice->Mesh.Get())
        || GetIceCount() >= FMath::Clamp(MaxCupIce, 0, 6)) return false;
    float CrossingTime = 0.f;
    if (!CrossesCupMouth(this, Ice, Previous, Current, CrossingTime)) return false;
    FTransform Entry;
    float Radius = 0.f, Depth = 0.f, CubeRadius = 0.f;
    if (!GetCupGeometry(Cup, InteriorRadiusCM, InteriorDepthCM, Ice->RadiusCM,
        Entry, Radius, Depth, CubeRadius)) return false;
    FVector LocalPosition = Entry.InverseTransformPosition(Current);
    const float VerticalRadius = Ice->RadiusCM / Entry.GetScale3D().GetAbs().Z;
    LocalPosition.Z = FMath::Clamp(LocalPosition.Z, static_cast<double>(-Depth + VerticalRadius), 0.0);
    FVector Velocity = Ice->Mesh->IsSimulatingPhysics()
        ? Ice->Mesh->GetPhysicsLinearVelocity() : Ice->LocalVelocity;
    Velocity -= SafeVelocity(Cup->ContainerMesh->GetComponentVelocity());
    Ice->LocalVelocity = SafeVelocity(Entry.InverseTransformVectorNoScale(Velocity), 120.f);
    Ice->Mesh->SetSimulatePhysics(false);
    Ice->Mesh->SetEnableGravity(false);
    Ice->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ice->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    Ice->CupIce = this;
    Ice->LastCupSource=Cup;
    Ice->Scoop = nullptr;
    Ice->State = EIceState::InCup;
    Ice->SetOwner(nullptr);
    Ice->SetLifeSpan(0.f);
    Ice->AttachToComponent(Cup->ContainerMesh.Get(), FAttachmentTransformRules::KeepWorldTransform);
    Ice->SetActorLocation(Entry.TransformPosition(LocalPosition), false, nullptr, ETeleportType::TeleportPhysics);
    const int32 PreviousCount = IceCubes.Num();
    IceCubes.AddUnique(Ice);
    if (IceCubes.Num() != PreviousCount) ++PreparationRevision;
    if (IsValid(ClinkSound.Get()) && SoundCooldown <= 0.f)
    {
        UGameplayStatics::PlaySoundAtLocation(this, ClinkSound.Get(), Current, 0.65f);
        SoundCooldown = 0.15f;
    }
    return true;
}

void UCupIceComponent::RemoveIce(AIceCubeActor* Ice)
{
    const int32 RemovedCount = IceCubes.RemoveAll([this, Ice](const TObjectPtr<AIceCubeActor>& Item)
    {
        const AIceCubeActor* Existing = Item.Get();
        return !IsValid(Existing) || Existing == Ice || Existing->IsActorBeingDestroyed()
            || Existing->State != EIceState::InCup || Existing->CupIce.Get() != this;
    });
    if (RemovedCount > 0) ++PreparationRevision;
}

void UCupIceComponent::ReleaseAll()
{
    ACupActor* Cup = GetCup();
    FVector CupVelocity = FVector::ZeroVector;
    FTransform Entry = FTransform::Identity;
    if (Cup)
    {
        if (IsValid(Cup->ContainerMesh.Get()))
            CupVelocity = SafeVelocity(Cup->ContainerMesh->GetComponentVelocity());
        if (IsValid(Cup->PourEntryPoint.Get()))
        {
            const FTransform Candidate = Cup->PourEntryPoint->GetComponentTransform();
            if (IsUsableTransform(Candidate)) Entry = Candidate;
        }
    }
    const TArray<TObjectPtr<AIceCubeActor>> Releasing = MoveTemp(IceCubes);
    IceCubes.Reset();
    // 先记成员变化，再进入释放回调；空杯重复释放和回调里的 RemoveIce 不重复计数。
    if (!Releasing.IsEmpty()) ++PreparationRevision;
    for (const TObjectPtr<AIceCubeActor>& Item : Releasing)
    {
        AIceCubeActor* Ice = Item.Get();
        if (IsValid(Ice) && !Ice->IsActorBeingDestroyed()
            && Ice->State == EIceState::InCup && Ice->CupIce.Get() == this)
        {
            Ice->ReleaseToWorld(CupVelocity + Entry.TransformVectorNoScale(SafeVelocity(Ice->LocalVelocity, 120.f)));
        }
    }
}

void UCupIceComponent::TickComponent(float Dt, ELevelTick Tick, FActorComponentTickFunction* Fn)
{
    Super::TickComponent(Dt, Tick, Fn);
    ACupActor* Cup = GetCup();
    if (!IsValid(Cup) || Cup->IsActorBeingDestroyed() || Cup->IsBroken()
        || !IsValid(Cup->ContainerMesh.Get()) || !IsValid(Cup->PourEntryPoint.Get()))
    {
        ReleaseAll();
        return;
    }
    RemoveIce(nullptr);
    const FVector CupPosition = Cup->GetActorLocation();
    const FVector CupVelocity = SampleVelocity(PreviousCupPosition, CupPosition, Dt);
    PreviousCupPosition = CupPosition;
    if (!FMath::IsFinite(Dt) || Dt <= 0.f) return;
    const float StepTime = FMath::Min(Dt, 0.1f);
    Age = FMath::Fmod(Age + StepTime, 3600.f);
    SoundCooldown = FMath::Max(0.f, SoundCooldown - Dt);
    // 已提交的杯保持现有摆放，杯体运动也不能触发滑出或重新模拟。
    if (Cup->IsLiquidFrozen())
    {
        for (const TObjectPtr<AIceCubeActor>& Item : IceCubes)
            if (AIceCubeActor* Ice = Item.Get()) Ice->LocalVelocity = FVector::ZeroVector;
        return;
    }
    const FTransform Entry = Cup->PourEntryPoint->GetComponentTransform();
    if (!IsUsableTransform(Entry))
    {
        ReleaseAll();
        return;
    }
    const FVector LocalCupVelocity = Entry.InverseTransformVectorNoScale(CupVelocity);
    const FVector LocalGravity = Entry.InverseTransformVectorNoScale(FVector(0.f, 0.f, -980.f));
    const double UpCos = FVector::DotProduct(Entry.GetUnitAxis(EAxis::Z), FVector::UpVector);
    const bool bMayEscape = UpCos < FMath::Cos(FMath::DegreesToRadians(65.f));
    const float Fill = FMath::IsFinite(Cup->FillAmount) ? FMath::Clamp(Cup->FillAmount, 0.f, 1.f) : 0.f;
    const bool bHasLiquid = Cup->GetCurrentVolumeML() > KINDA_SMALL_NUMBER && Fill > KINDA_SMALL_NUMBER;
    const TArray<TObjectPtr<AIceCubeActor>> Updating = IceCubes;
    for (int32 Index = 0; Index < Updating.Num(); ++Index)
    {
        AIceCubeActor* Ice = Updating[Index].Get();
        if (!IsValid(Ice) || Ice->IsActorBeingDestroyed()
            || Ice->State != EIceState::InCup || Ice->CupIce.Get() != this) continue;
        FTransform CheckedEntry;
        float Radius = 0.f, Depth = 0.f, CubeRadius = 0.f;
        if (!GetCupGeometry(Cup, InteriorRadiusCM, InteriorDepthCM, Ice->RadiusCM,
            CheckedEntry, Radius, Depth, CubeRadius))
        {
            Ice->ReleaseToWorld(CupVelocity);
            continue;
        }
        const float VerticalRadius = Ice->RadiusCM / Entry.GetScale3D().GetAbs().Z;
        const float Floor = -Depth + VerticalRadius;
        const float Upper = -VerticalRadius * 0.25f;
        const float Surface = (Fill - 1.f) * Depth;
        const float BaseTargetZ = bHasLiquid
            ? FMath::Clamp(Surface - 0.3f * VerticalRadius, Floor, Upper) : Floor;
        const double Limit = Radius - CubeRadius;
        const double Phase = static_cast<double>(Ice->GetUniqueID() % 97) * 0.37;
        const double Angle = Index * 2.399963229728653;
        const double Ring = Updating.Num() > 1 ? Limit * 0.6 : 0.0;
        FVector Target(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, BaseTargetZ);
        if (bHasLiquid)
        {
            Target.X += FMath::Sin(Age * 1.7 + Phase) * 0.04 * CubeRadius;
            Target.Y += FMath::Cos(Age * 1.3 + Phase) * 0.04 * CubeRadius;
            Target.Z = FMath::Clamp(Target.Z + FMath::Sin(Age * 2.0 + Phase) * 0.06 * VerticalRadius,
                static_cast<double>(Floor), static_cast<double>(Upper));
        }
        FVector Position = Entry.InverseTransformPosition(Ice->GetActorLocation());
        if (!IsFiniteVector(Position)) Position = Target;
        FVector Velocity = SafeVelocity(Ice->LocalVelocity, 120.f);
        const double RelativeSpeed = Velocity.Size();
        bool bContact = false;
        bool bEscape = false;
        const int32 Steps = FMath::Clamp(FMath::CeilToInt(StepTime / (1.f / 60.f)), 1, 6);
        const float H = StepTime / Steps;
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            FVector Acceleration;
            if (bMayEscape)
            {
                Acceleration = LocalGravity - Velocity * 0.8;
                // The cup wall still contains the ice. Tilt guides it along the
                // wall toward the open rim, never directly through the side.
                if(Position.Size2D()>Limit*.8)Acceleration.Z+=FMath::Max(0.0,-LocalGravity.Z)+240.0;
            }
            else
            {
                Acceleration = (Target - Position) * 45.0 - Velocity * 10.0;
                Acceleration.X += LocalGravity.X * 0.15;
                Acceleration.Y += LocalGravity.Y * 0.15;
            }
            Acceleration.X -= LocalCupVelocity.X * 3.0;
            Acceleration.Y -= LocalCupVelocity.Y * 3.0;
            Velocity = SafeVelocity(Velocity + Acceleration * H, 120.f);
            Position += Velocity * H;
            const double Distance = Position.Size2D();
            if (bMayEscape && Position.Z > -VerticalRadius * 0.25)
            {
                bEscape = true;
                break;
            }
            if (Distance > Limit)
            {
                const FVector Normal(Position.X / Distance, Position.Y / Distance, 0.0);
                Position.X = Normal.X * Limit;
                Position.Y = Normal.Y * Limit;
                const double OutwardSpeed = FVector::DotProduct(Velocity, Normal);
                if (OutwardSpeed > 0.0) Velocity -= Normal * (OutwardSpeed * 1.25);
                bContact = true;
            }
            if (Position.Z < Floor)
            {
                Position.Z = Floor;
                if (Velocity.Z < 0.0) Velocity.Z *= -0.15;
                bContact = true;
            }
            if (!bMayEscape && Position.Z > Upper)
            {
                Position.Z = Upper;
                if (Velocity.Z > 0.0) Velocity.Z *= -0.15;
                bContact = true;
            }
        }
        if (!IsFiniteVector(Position)) continue;
        Ice->LocalVelocity = Velocity;
        const FVector WorldPosition = Entry.TransformPosition(Position);
        if (!IsFiniteVector(WorldPosition)) continue;
        Ice->SetActorLocation(WorldPosition, false, nullptr, ETeleportType::TeleportPhysics);
        if (bEscape)
        {
            Ice->ReleaseToWorld(CupVelocity + Entry.TransformVectorNoScale(Velocity));
            continue;
        }
        if (bContact && RelativeSpeed >= 20.0 && SoundCooldown <= 0.f && IsValid(ClinkSound.Get()))
        {
            UGameplayStatics::PlaySoundAtLocation(this, ClinkSound.Get(), WorldPosition, 0.4f);
            SoundCooldown = 0.15f;
        }
    }
}

void UCupIceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseAll();
    Super::EndPlay(Reason);
}

AIceCubeActor::AIceCubeActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IceMesh"));
    SetRootComponent(Mesh.Get());
    Mesh->SetMobility(EComponentMobility::Movable);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BasicCube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (BasicCube.Succeeded())
    {
        Mesh->SetStaticMesh(BasicCube.Object);
        Mesh->SetRelativeScale3D(FVector(0.024f));
    }
    EnableFreeCollision(Mesh.Get());
    Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    Mesh->SetEnableGravity(true);
}

void AIceCubeActor::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();
    if (World)
    {
        if (UIceWorldSubsystem* System = World->GetSubsystem<UIceWorldSubsystem>())
        {
            System->RegisterIce(this);
            if (IsActorBeingDestroyed()) return;
        }
    }
    FreeAge = 0.f;
    PreviousPosition = GetActorLocation();
    bPreviousReady = IsFiniteVector(PreviousPosition);
    if (State == EIceState::Free && IsValid(Mesh.Get()))
    {
        EnableFreeCollision(Mesh.Get());
        Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        Mesh->IgnoreActorWhenMoving(this, true);
        Mesh->SetEnableGravity(true);
        if (Mesh->GetStaticMesh()) Mesh->SetSimulatePhysics(true);
    }
}

void AIceCubeActor::PlaceInScoop(AIceScoopActor* OwnerScoop, FVector LocalPosition)
{
    if (!IsValid(OwnerScoop) || !IsValid(OwnerScoop->Mesh.Get()) || !IsValid(Mesh.Get())
        || !IsFiniteVector(LocalPosition) || IsActorBeingDestroyed()) return;
    if (IsValid(CupIce.Get())) CupIce->RemoveIce(this);
    CupIce = nullptr;
    Scoop = OwnerScoop;
    LastScoopSource=OwnerScoop;
    State = EIceState::InScoop;
    FreeAge = 0.f;
    LocalVelocity = FVector::ZeroVector;
    SetLifeSpan(0.f);
    Mesh->SetSimulatePhysics(false);
    Mesh->SetEnableGravity(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    AttachToComponent(OwnerScoop->Mesh.Get(), FAttachmentTransformRules::KeepRelativeTransform);
    SetActorRelativeLocation(LocalPosition, false, nullptr, ETeleportType::TeleportPhysics);
    SetActorRelativeRotation(FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
    PreviousPosition = GetActorLocation();
    bPreviousReady = IsFiniteVector(PreviousPosition);
}

void AIceCubeActor::ReleaseToWorld(FVector Velocity)
{
    if (IsActorBeingDestroyed()) return;
    UCupIceComponent* PreviousCup = CupIce.Get();
    CupIce = nullptr;
    Scoop = nullptr;
    State = EIceState::Free;
    if (IsValid(PreviousCup)) PreviousCup->RemoveIce(this);
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    SetOwner(nullptr);
    SetLifeSpan(0.f);
    FreeAge = 0.f;
    LocalVelocity = SafeVelocity(Velocity, 890.f);
    // 从实际释放点开始下一段运动，禁止把铲内位置到杯口的传送当作穿口。
    PreviousPosition = GetActorLocation();
    bPreviousReady = IsFiniteVector(PreviousPosition);
    if (IsValid(Mesh.Get()))
    {
        EnableFreeCollision(Mesh.Get());
        Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        Mesh->IgnoreActorWhenMoving(this, true);
        Mesh->SetEnableGravity(true);
        if (Mesh->GetStaticMesh()) Mesh->SetSimulatePhysics(true);
        if (Mesh->IsSimulatingPhysics())
        {
            Mesh->SetPhysicsLinearVelocity(LocalVelocity);
            Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            Mesh->WakeAllRigidBodies();
        }
    }
}

void AIceCubeActor::Tick(float Dt)
{
    Super::Tick(Dt);
    if (State != EIceState::Free)
    {
        if (State == EIceState::InCup && (!IsValid(CupIce.Get()) || !IsValid(CupIce->GetCup())))
            ReleaseToWorld(FVector::ZeroVector);
        else if (State == EIceState::InScoop && (!IsValid(Scoop.Get()) || Scoop->IsActorBeingDestroyed()))
            ReleaseToWorld(FVector::ZeroVector);
        return;
    }
    const FVector Current = GetActorLocation();
    const FVector Previous = PreviousPosition;
    const bool bCanTrace = bPreviousReady && IsFiniteVector(Previous) && IsFiniteVector(Current)
        && FMath::IsFinite(Dt) && Dt >= 0.0001f;
    PreviousPosition = Current;
    bPreviousReady = IsFiniteVector(Current);
    if (FMath::IsFinite(Dt) && Dt > 0.f) FreeAge += Dt;
    if (UWorld* World = GetWorld(); World && bCanTrace)
    {
        const FVector MotionVelocity = (Current - Previous) / FMath::Max(Dt, 0.0001f);
        // 只检测实际世界运动；追踪跳变不跨杯吸附。无物理测试也可手动移动自由冰。
        if (IsFiniteVector(MotionVelocity) && MotionVelocity.SizeSquared() <= FMath::Square(5000.0))
        {
            if (!IsValid(Mesh.Get()) || !Mesh->IsSimulatingPhysics()) LocalVelocity = SafeVelocity(MotionVelocity);
            UCupIceComponent* FirstTarget = nullptr;
            float FirstTime = 2.f;
            uint32 FirstId = MAX_uint32;
            for (TActorIterator<ACupActor> It(World); It; ++It)
            {
                ACupActor* Cup = *It;
                if (!IsValid(Cup) || Cup->IsActorBeingDestroyed()) continue;
                UCupIceComponent* Component = Cup->FindComponentByClass<UCupIceComponent>();
                float Time = 0.f;
                if (CrossesCupMouth(Component, this, Previous, Current, Time)
                    && (Time < FirstTime || (Time == FirstTime && Cup->GetUniqueID() < FirstId)))
                {
                    FirstTarget = Component;
                    FirstTime = Time;
                    FirstId = Cup->GetUniqueID();
                }
            }
            // 最先穿过的真实入口是唯一目标；满杯或冻结杯拒绝后不改投后面的杯子。
            if (FirstTarget && FirstTarget->TryAccept(this, Previous, Current)) return;
        }
    }
    if (FMath::IsFinite(FreeLifetimeSeconds) && FreeLifetimeSeconds > 0.f && FreeAge >= FreeLifetimeSeconds)
        Destroy();
}

void AIceCubeActor::EndPlay(const EEndPlayReason::Type Reason)
{
    UCupIceComponent* PreviousCup = CupIce.Get();
    CupIce = nullptr;
    Scoop = nullptr;
    if (IsValid(PreviousCup)) PreviousCup->RemoveIce(this);
    if (UWorld* World = GetWorld())
        if (UIceWorldSubsystem* System = World->GetSubsystem<UIceWorldSubsystem>()) System->ForgetIce(this);
    Super::EndPlay(Reason);
}

AIceScoopActor::AIceScoopActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScoopMesh"));
    SetRootComponent(Mesh.Get());
    Mesh->SetMobility(EComponentMobility::Movable);
    EnableFreeCollision(Mesh.Get());
    Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Grab = CreateDefaultSubobject<UGrabComponent>(TEXT("Grab"));
    Grab->SetupAttachment(Mesh.Get());
    Grab->GrabType = EGrabType::Free;
    Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
    Grab->bAllowRemoteGrab = true;
    IceClass = AIceCubeActor::StaticClass();
}

void AIceScoopActor::BeginPlay()
{
    Super::BeginPlay();
    if (IsValid(Mesh.Get()))
    {
        // 蓝图此时已经赋好网格，不能在构造函数读取其资产或物理配置。
        EnableFreeCollision(Mesh.Get());
        Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        Mesh->SetEnableGravity(true);
        if (Mesh->GetStaticMesh()) Mesh->SetSimulatePhysics(true);
    }
    if (IsValid(Grab.Get()))
    {
        Grab->GrabType = EGrabType::Free;
        Grab->PhysicsMode = EGrabPhysicsMode::Kinematic;
        Grab->bSimulateOnDrop = true;
    }
    if (!IceClass) IceClass = AIceCubeActor::StaticClass();
    const FVector Position = IsValid(Mesh.Get()) ? Mesh->GetComponentLocation() : GetActorLocation();
    PreviousPosition = Position;
    FrameVelocity = FVector::ZeroVector;
}

int32 AIceScoopActor::GetLoadedCount() const
{
    int32 Count = 0;
    for (const TObjectPtr<AIceCubeActor>& Item : Loaded)
    {
        const AIceCubeActor* Ice = Item.Get();
        if (IsValid(Ice) && !Ice->IsActorBeingDestroyed()
            && Ice->State == EIceState::InScoop && Ice->Scoop.Get() == this) ++Count;
    }
    return Count;
}

bool AIceScoopActor::TryLoadFromBucket()
{
    Loaded.RemoveAll([this](const TObjectPtr<AIceCubeActor>& Item)
    {
        const AIceCubeActor* Ice = Item.Get();
        return !IsValid(Ice) || Ice->IsActorBeingDestroyed()
            || Ice->State != EIceState::InScoop || Ice->Scoop.Get() != this;
    });
    UWorld* World = GetWorld();
    if (!World || IsActorBeingDestroyed() || GetLoadedCount() != 0 || !bBucketArmed
        || !IsValid(Bucket.Get()) || Bucket->IsActorBeingDestroyed() || !IsValid(Mesh.Get()) || !IceClass)
        return false;
    const FTransform ScoopTransform = Mesh->GetComponentTransform();
    if (!IsUsableTransform(ScoopTransform) || !IsFiniteVector(BowlCenter)
        || FVector::DotProduct(ScoopTransform.GetUnitAxis(EAxis::Z), FVector::UpVector) <= 0.6
        || !Bucket->ContainsBowl(ScoopTransform.TransformPosition(BowlCenter))) return false;
    UIceWorldSubsystem* System = World->GetSubsystem<UIceWorldSubsystem>();
    const int32 Count = FMath::Clamp(IcePerScoop, 1, 3);
    if (!System || !System->CanAllocate(Count)) return false;
    TArray<TObjectPtr<AIceCubeActor>> Spawned;
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // 球槽上三块独立冰，根网格使用资产自己的缩放，不把铲的根缩放写入冰资产。
    const FVector Positions[] = { FVector(8.5, -1.4, -1.45), FVector(11.0, 1.4, -1.45), FVector(13.0, -1.4, -1.45) };
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FVector WorldPosition = ScoopTransform.TransformPosition(Positions[Index]);
        AIceCubeActor* Ice = World->SpawnActor<AIceCubeActor>(IceClass, WorldPosition,
            ScoopTransform.Rotator(), Parameters);
        if (!IsValid(Ice) || Ice->IsActorBeingDestroyed())
        {
            for (const TObjectPtr<AIceCubeActor>& Item : Spawned)
                if (AIceCubeActor* Partial = Item.Get())
                    if (!Partial->IsActorBeingDestroyed()) Partial->Destroy();
            return false;
        }
        Spawned.Add(Ice);
        Ice->PlaceInScoop(this, Positions[Index]);
        if (Ice->State != EIceState::InScoop || Ice->Scoop.Get() != this)
        {
            for (const TObjectPtr<AIceCubeActor>& Item : Spawned)
                if (AIceCubeActor* Partial = Item.Get())
                    if (!Partial->IsActorBeingDestroyed()) Partial->Destroy();
            return false;
        }
    }
    Loaded = MoveTemp(Spawned);
    bBucketArmed = false;
    ReleaseCountdown = 0.f;
    if (IsValid(ScoopSound.Get()))
        UGameplayStatics::PlaySoundAtLocation(this, ScoopSound.Get(), ScoopTransform.TransformPosition(BowlCenter), 0.65f);
    if (IsValid(Grab.Get()) && Grab->IsHeld() && IsValid(Grab->OnGrabHapticEffect.Get()))
    {
        const EControllerHand Hand = Grab->GetHeldByHand();
        if (Hand == EControllerHand::Left || Hand == EControllerHand::Right)
            if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
                PC->PlayHapticEffect(Grab->OnGrabHapticEffect.Get(), Hand, 0.3f, false);
    }
    return true;
}

bool AIceScoopActor::ReleaseOne()
{
    Loaded.RemoveAll([this](const TObjectPtr<AIceCubeActor>& Item)
    {
        const AIceCubeActor* Ice = Item.Get();
        return !IsValid(Ice) || Ice->IsActorBeingDestroyed()
            || Ice->State != EIceState::InScoop || Ice->Scoop.Get() != this;
    });
    if (Loaded.IsEmpty() || !IsValid(Mesh.Get()) || !IsFiniteVector(ReleaseLip)) return false;
    const FTransform Transform = Mesh->GetComponentTransform();
    if (!IsUsableTransform(Transform)) return false;
    const FVector Lip = Transform.TransformPosition(ReleaseLip);
    if (!IsFiniteVector(Lip)) return false;
    AIceCubeActor* Ice = Loaded[0].Get();
    // 先解除铲的数组所有权，再让冰解除指针并进入自由态。
    Loaded.RemoveAt(0, 1, EAllowShrinking::No);
    Ice->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    Ice->SetActorLocation(Lip, false, nullptr, ETeleportType::TeleportPhysics);
    Ice->ReleaseToWorld(SafeVelocity(FrameVelocity) + GetActorForwardVector() * 90.f);
    return true;
}

// @trace RS-03
bool AIceScoopActor::ResetForWorkbench(FTransform SavedPose)
{
    if (IsActorBeingDestroyed() || !IsValid(Mesh.Get()) || !IsUsableTransform(SavedPose)
        || (IsValid(Grab.Get()) && (Grab->IsHeld() || Grab->IsPulled()))) return false;
    TStrongObjectPtr<AIceScoopActor> Self(this);
    const TArray<TObjectPtr<AIceCubeActor>> Disposing = MoveTemp(Loaded);
    Loaded.Reset();
    // 只销毁仍归本铲所有的装载冰，不能释放成新的散冰，也不能误删已入杯的冰。
    for (const TObjectPtr<AIceCubeActor>& Item : Disposing)
    {
        AIceCubeActor* Ice = Item.Get();
        if (IsValid(Ice) && !Ice->IsActorBeingDestroyed()
            && Ice->State == EIceState::InScoop && Ice->Scoop.Get() == this) Ice->Destroy();
    }
    if (!IsValid(this) || IsActorBeingDestroyed() || !IsValid(Mesh.Get())) return false;
    Mesh->SetSimulatePhysics(false);
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    SetActorTransform(SavedPose, false, nullptr, ETeleportType::TeleportPhysics);
    Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    Mesh->SetEnableGravity(true);
    if (Mesh->GetStaticMesh()) Mesh->SetSimulatePhysics(true);
    Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    bBucketArmed = false;
    ReleaseCountdown = 0.f;
    PreviousPosition = Mesh->GetComponentLocation();
    FrameVelocity = FVector::ZeroVector;
    return true;
}

void AIceScoopActor::Tick(float Dt)
{
    Super::Tick(Dt);
    Loaded.RemoveAll([this](const TObjectPtr<AIceCubeActor>& Item)
    {
        const AIceCubeActor* Ice = Item.Get();
        return !IsValid(Ice) || Ice->IsActorBeingDestroyed()
            || Ice->State != EIceState::InScoop || Ice->Scoop.Get() != this;
    });
    if (!IsValid(Mesh.Get())) return;
    const FVector Position = Mesh->GetComponentLocation();
    FrameVelocity = SampleVelocity(PreviousPosition, Position, Dt);
    PreviousPosition = Position;
    const FTransform Transform = Mesh->GetComponentTransform();
    if (!IsUsableTransform(Transform) || !IsFiniteVector(BowlCenter)) return;
    if (!bBucketArmed && IsValid(Bucket.Get())
        && !Bucket->ContainsBowl(Transform.TransformPosition(BowlCenter), 2.f)) bBucketArmed = true;
    if (!FMath::IsFinite(Dt) || Dt <= 0.f) return;
    if (GetLoadedCount() == 0)
    {
        ReleaseCountdown = 0.f;
        TryLoadFromBucket();
        return;
    }
    const float Tilt = FMath::IsFinite(PourTiltDegrees) ? FMath::Clamp(PourTiltDegrees, 0.f, 180.f) : 55.f;
    const double UpCos = FVector::DotProduct(GetActorUpVector(), FVector::UpVector);
    const double TipDown = FVector::DotProduct(GetActorForwardVector(), FVector::DownVector);
    if (UpCos < FMath::Cos(FMath::DegreesToRadians(Tilt)) && TipDown > 0.45)
    {
        ReleaseCountdown -= Dt;
        if (ReleaseCountdown <= 0.f && ReleaseOne())
            ReleaseCountdown = FMath::IsFinite(ReleaseIntervalSeconds)
                ? FMath::Max(0.01f, ReleaseIntervalSeconds) : 0.16f;
    }
    else
    {
        ReleaseCountdown = 0.f;
    }
}

void AIceScoopActor::EndPlay(const EEndPlayReason::Type Reason)
{
    const TArray<TObjectPtr<AIceCubeActor>> Releasing = MoveTemp(Loaded);
    Loaded.Reset();
    for (const TObjectPtr<AIceCubeActor>& Item : Releasing)
    {
        AIceCubeActor* Ice = Item.Get();
        if (IsValid(Ice) && !Ice->IsActorBeingDestroyed()
            && Ice->State == EIceState::InScoop && Ice->Scoop.Get() == this)
            Ice->ReleaseToWorld(FrameVelocity);
    }
    Super::EndPlay(Reason);
}

AIceBucketActor::AIceBucketActor()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BucketMesh"));
    SetRootComponent(Mesh.Get());
    Mesh->SetMobility(EComponentMobility::Static);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    IcePile = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("IcePile"));
    IcePile->SetupAttachment(Mesh.Get());
    IcePile->SetMobility(EComponentMobility::Static);
    IcePile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    IcePile->SetGenerateOverlapEvents(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BasicCube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (BasicCube.Succeeded()) IceMesh = BasicCube.Object;
}

void AIceBucketActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildBucketPile(this);
}
void AIceBucketActor::BeginPlay()
{
    Super::BeginPlay();
    RebuildBucketPile(this);
}

bool AIceBucketActor::ContainsBowl(const FVector& Point, float Margin) const
{
    if (!IsValid(Mesh.Get()) || !IsFiniteVector(Point) || !FMath::IsFinite(Margin)
        || !FMath::IsFinite(LoadRadiusCM) || !FMath::IsFinite(LoadMinZCM) || !FMath::IsFinite(LoadMaxZCM)
        || LoadRadiusCM <= 0.f || LoadMinZCM > LoadMaxZCM) return false;
    const FTransform Transform = Mesh->GetComponentTransform();
    if (!IsUsableTransform(Transform)) return false;
    const FVector Local = Transform.InverseTransformPosition(Point);
    if (!IsFiniteVector(Local)) return false;
    const double Padding = FMath::Max(0.f, Margin);
    const double Radius = LoadRadiusCM + Padding;
    return Local.SizeSquared2D() <= Radius * Radius
        && Local.Z >= LoadMinZCM - Padding && Local.Z <= LoadMaxZCM + Padding;
}

AIceStationActor::AIceStationActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StationRoot")));
}

void AIceStationActor::BeginPlay()
{
    Super::BeginPlay();
    if (IsValid(Scoop.Get()))
    {
        if (IsValid(Bucket.Get())) Scoop->Bucket = Bucket;
        if (!Scoop->IceClass) Scoop->IceClass = AIceCubeActor::StaticClass();
        if (IsValid(Bucket.Get()))
        {
            // 读取已完成蓝图赋值的类默认对象，确保桶里的展示冰与实际生成冰同源。
            const AIceCubeActor* Defaults = Scoop->IceClass.GetDefaultObject();
            if (Defaults && IsValid(Defaults->Mesh.Get()) && IsValid(Defaults->Mesh->GetStaticMesh()))
            {
                Bucket->IceMesh = Defaults->Mesh->GetStaticMesh();
                // 与各 Actor 的 BeginPlay 顺序无关；展示堆不参与活冰计数或库存。
                RebuildBucketPile(Bucket.Get());
            }
        }
    }
    if (!IsValid(Cup.Get()) || Cup->IsActorBeingDestroyed() || Cup->GetWorld() != GetWorld()) return;
    UCupIceComponent* Component = Cup->FindComponentByClass<UCupIceComponent>();
    const bool bNewComponent = Component == nullptr;
    if (bNewComponent)
    {
        Component = NewObject<UCupIceComponent>(Cup.Get(), UCupIceComponent::StaticClass(), NAME_None, RF_Transient);
        if (!Component) return;
        Cup->AddInstanceComponent(Component);
    }
    // A station is an explicit per-instance geometry override, including legacy
    // practice cups. Unbound menu cups keep their authored per-class cavity.
    Component->InteriorRadiusCM = CupInteriorRadiusCM;
    Component->InteriorDepthCM = CupInteriorDepthCM;
    Component->ClinkSound = ClinkSound;
    if (!Component->IsRegistered()) Component->RegisterComponent();
    // 只给显式绑定的已有杯新增表现组件，不写液量、配方、容量、入口或倒液状态。
}
