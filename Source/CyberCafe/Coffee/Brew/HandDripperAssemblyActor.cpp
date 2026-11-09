#include "Coffee/Brew/HandDripperAssemblyActor.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GrabComponent.h"
#include "UObject/ConstructorHelpers.h"

AHandDripperAssemblyActor::AHandDripperAssemblyActor()
{
    StandLeftLeg = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandLeftLeg"));
    StandRightLeg = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandRightLeg"));
    StandLeftShelf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandLeftShelf"));
    StandRightShelf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandRightShelf"));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMeshComponent* StandMeshes[] = { StandLeftLeg, StandRightLeg, StandLeftShelf, StandRightShelf };
    for (UStaticMeshComponent* Mesh : StandMeshes)
    {
        Mesh->SetupAttachment(ContainerMesh);
        if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetSimulatePhysics(false);
        Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Mesh->SetCollisionObjectType(ECC_PhysicsBody);
    }

    // 以原滤杯 pivot 为根：原支架根比它低 15 cm，不添加封闭杯通道的代理盒。
    StandLeftLeg->SetRelativeTransform(FTransform(FQuat::Identity,
        FVector(-5.2, 0.0, -7.8), FVector(0.01, 0.07, 0.144)));
    StandRightLeg->SetRelativeTransform(FTransform(FQuat::Identity,
        FVector(5.2, 0.0, -7.8), FVector(0.01, 0.07, 0.144)));
    StandLeftShelf->SetRelativeTransform(FTransform(FQuat::Identity,
        FVector(-5.2, 0.0, -0.5), FVector(0.015, 0.1, 0.01)));
    StandRightShelf->SetRelativeTransform(FTransform(FQuat::Identity,
        FVector(5.2, 0.0, -0.5), FVector(0.015, 0.1, 0.01)));
    StandLeftShelf->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    ApplyAssemblyGrabSettings();
}

void AHandDripperAssemblyActor::ApplyAssemblyGrabSettings()
{
    bCanPour = false;
    TInlineComponentArray<USceneComponent*> SceneComponents(this);
    for (USceneComponent* Component : SceneComponents)
    {
        if (!IsValid(Component) || Component->GetOwner() != this) continue;
        Component->SetMobility(EComponentMobility::Movable);
        if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
            Primitive->SetSimulatePhysics(false);
    }

    if (IsValid(GrabComp) && GrabComp->GetOwner() == this)
    {
        GrabComp->GrabType = EGrabType::Free;
        GrabComp->PhysicsMode = EGrabPhysicsMode::Kinematic;
        GrabComp->bSimulateOnDrop = false;
        GrabComp->bCaptureHand = false;
        GrabComp->bHideHandWhileHeld = false;
        GrabComp->bAutoActivate = true;
    }
}

void AHandDripperAssemblyActor::BeginPlay()
{
    Super::BeginPlay();
    if (!IsValid(this) || IsBroken()) return;

    // 基类完成组件 BeginPlay 后再关闭可能由旧 BP 覆盖的模拟，并清掉松手模拟缓存。
    ApplyAssemblyGrabSettings();
    if (IsValid(GrabComp) && GrabComp->GetOwner() == this
        && GrabComp->GetAttachParent() == GetRootComponent())
    {
        GrabComp->Activate(true);
    }
}
