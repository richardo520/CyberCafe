#include "Coffee/Brew/DripperAssemblyEditorLibrary.h"
#include "Coffee/Brew/HandDripperAssemblyActor.h"

#if WITH_EDITOR
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GrabComponent.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace DripperAssemblyEditorPrivate
{
    const FName FilterName(TEXT("HandDripper_Practice"));
    const FName StandName(TEXT("HandDripperStand_Practice"));
    const FName StagedName(TEXT("HandDripperAssembly_Staged"));
    constexpr double TransformTolerance = 1.e-4;

    struct FSceneSnapshot
    {
        USceneComponent* Source = nullptr;
        FTransform WorldTransform;
        FTransform RelativeTransform;
        USceneComponent* Parent = nullptr;
        FName Socket;
        UStaticMesh* Mesh = nullptr;
        TArray<UMaterialInterface*> Materials;
    };

    FSceneSnapshot Snapshot(USceneComponent* Source)
    {
        FSceneSnapshot Result;
        Result.Source = Source;
        Result.WorldTransform = Source->GetComponentTransform();
        Result.RelativeTransform = Source->GetRelativeTransform();
        Result.Parent = Source->GetAttachParent();
        Result.Socket = Source->GetAttachSocketName();
        if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Source))
        {
            Result.Mesh = Mesh->GetStaticMesh();
            for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
                Result.Materials.Add(Mesh->GetMaterial(Index));
        }
        return Result;
    }

    void RestoreMesh(const FSceneSnapshot& Saved, UStaticMeshComponent* Target)
    {
        Target->SetStaticMesh(Saved.Mesh);
        Target->EmptyOverrideMaterials();
        for (int32 Index = 0; Index < Saved.Materials.Num(); ++Index)
            Target->SetMaterial(Index, Saved.Materials[Index]);
    }

    bool MatchesSnapshot(const FSceneSnapshot& Saved, USceneComponent* Target)
    {
        if (!IsValid(Target) || !Target->GetComponentTransform().Equals(Saved.WorldTransform, TransformTolerance))
            return false;
        if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Target))
        {
            if (Mesh->GetStaticMesh() != Saved.Mesh || Mesh->GetNumMaterials() != Saved.Materials.Num())
                return false;
            for (int32 Index = 0; Index < Saved.Materials.Num(); ++Index)
                if (Mesh->GetMaterial(Index) != Saved.Materials[Index]) return false;
        }
        return true;
    }

    bool ValidTransform(const FTransform& Transform)
    {
        return !Transform.ContainsNaN() && Transform.GetScale3D().GetAbs().GetMin() > UE_SMALL_NUMBER;
    }

    UActorComponent* FindComponent(AActor* Actor, FName Name)
    {
        for (UActorComponent* Component : Actor->GetComponents())
            if (IsValid(Component) && Component->GetOwner() == Actor && Component->GetFName() == Name)
                return Component;
        return nullptr;
    }

    bool HasSingleRootGrab(AHandDripperActor* Actor)
    {
        TInlineComponentArray<UGrabComponent*> Grabs(Actor);
        return Grabs.Num() == 1 && IsValid(Actor->ContainerMesh) && IsValid(Actor->GrabComp)
            && Actor->ContainerMesh->GetOwner() == Actor && Actor->GrabComp->GetOwner() == Actor
            && Actor->GetRootComponent() == Actor->ContainerMesh
            && Grabs[0] == Actor->GrabComp && Actor->GrabComp->GetAttachParent() == Actor->ContainerMesh;
    }

    // 每次仅序列化指定对象，子对象由世界枚举逐一处理，不递归进入原对象或共享资产。
    class FWorldReferenceArchive final : public FArchiveReplaceObjectRef<UObject>
    {
    public:
        using FArchiveReplaceObjectRef<UObject>::operator<<;

        FWorldReferenceArchive(UObject* Object, const TMap<UObject*, UObject*>& Remap)
            : FArchiveReplaceObjectRef<UObject>(Object, Remap,
                EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef
                | EArchiveReplaceObjectFlags::DelayStart)
        {
            SerializeSearchObject();
        }

        virtual FArchive& operator<<(UObject*& Object) override
        {
            // Level 的 Actor 表由 Spawn/EditorDestroyActor 维护，不能将两个旧槽都改成新 Actor。
            if (SearchObject->IsA<ULevel>() && Object && Object->IsA<AActor>()) return *this;
            return FArchiveReplaceObjectRef<UObject>::operator<<(Object);
        }

    protected:
        virtual void SerializeObject(UObject* Object) override
        {
            if (Object == SearchObject) FArchiveReplaceObjectRefBase::SerializeObject(Object);
        }
    };

    bool IsReplacementScope(UObject* Object, UWorld* World, AActor* OldFilter, AActor* OldStand)
    {
        return IsValid(Object) && (Object == World || Object->IsIn(World))
            && !Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
            && !Object->IsA<UClass>() && Object != OldFilter && Object != OldStand
            && !Object->IsIn(OldFilter) && !Object->IsIn(OldStand);
    }
}
#endif

bool UDripperAssemblyEditorLibrary::InstallAssembly(UWorld* World, TSubclassOf<AHandDripperAssemblyActor> NewClass)
{
#if WITH_EDITOR
    using namespace DripperAssemblyEditorPrivate;
    if (!IsInGameThread() || !IsValid(World) || World->WorldType != EWorldType::Editor
        || World->IsGameWorld() || World->HasBegunPlay() || !NewClass
        || !NewClass->IsChildOf(AHandDripperAssemblyActor::StaticClass())
        || NewClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        UE_LOG(LogTemp, Error, TEXT("组合安装失败：须提供未 BeginPlay 的编辑器世界和有效组合类。"));
        return false;
    }

    AHandDripperActor* OldFilter = nullptr;
    AActor* OldStand = nullptr;
    int32 NamedActorCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetFName() == FilterName)
        {
            ++NamedActorCount;
            OldFilter = Cast<AHandDripperActor>(*It);
        }
        else if (It->GetFName() == StandName)
        {
            ++NamedActorCount;
            OldStand = *It;
        }
    }
    if (NamedActorCount != 2 || !IsValid(OldFilter) || !IsValid(OldStand)
        || OldFilter->IsA<AHandDripperAssemblyActor>() || OldStand->GetClass() != AActor::StaticClass()
        || OldFilter->GetLevel() != OldStand->GetLevel() || !HasSingleRootGrab(OldFilter)
        || OldFilter->IsBroken() || OldFilter->GrabComp->IsHeld() || OldFilter->GrabComp->IsPulled()
        || OldFilter->GetRootComponent()->GetAttachParent() || !IsValid(OldStand->GetRootComponent()))
    {
        UE_LOG(LogTemp, Error, TEXT("组合安装失败：精确名称、原对象类型或根抓取结构不符。"));
        return false;
    }
    TArray<AActor*> AttachedActors;
    OldFilter->GetAttachedActors(AttachedActors);
    OldStand->GetAttachedActors(AttachedActors, false);
    if (!AttachedActors.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("组合安装失败：原器具附着了其他 Actor，不能携带接液杯。"));
        return false;
    }

    const FTransform OldTransform = OldFilter->GetActorTransform();
    const FString OriginalFilterLabel = OldFilter->GetActorLabel();
    if (!ValidTransform(OldTransform)) return false;
    ULevel* Level = OldFilter->GetLevel();
    TInlineComponentArray<UActorComponent*> OldComponents(OldFilter);
    TInlineComponentArray<UStaticMeshComponent*> StandMeshes(OldStand);
    if (StandMeshes.Num() != 4 || OldStand->GetComponents().Num() != 5) return false;

    TArray<FSceneSnapshot> FilterScenes;
    for (UActorComponent* Component : OldComponents)
        if (USceneComponent* Scene = Cast<USceneComponent>(Component))
        {
            if (!ValidTransform(Scene->GetComponentTransform())) return false;
            FilterScenes.Add(Snapshot(Scene));
        }

    FSceneSnapshot StandScenes[4];
    for (int32 Index = 0; Index < 4; ++Index)
    {
        UStaticMeshComponent* Source = Cast<UStaticMeshComponent>(FindComponent(OldStand,
            FName(*FString::Printf(TEXT("StaticMeshComponent_%d"), Index))));
        if (!IsValid(Source) || !Source->GetStaticMesh() || Source->GetAttachParent() != OldStand->GetRootComponent()
            || !ValidTransform(Source->GetComponentTransform())) return false;
        StandScenes[Index] = Snapshot(Source);
    }

    FActorSpawnParameters Spawn;
    Spawn.Name = StagedName;
    Spawn.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ReturnNull;
    Spawn.OverrideLevel = Level;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Spawn.bDeferConstruction = true;
    Spawn.ObjectFlags = RF_Transactional;
    AHandDripperAssemblyActor* NewActor = World->SpawnActor<AHandDripperAssemblyActor>(NewClass.Get(), OldTransform, Spawn);
    if (!IsValid(NewActor)) return false;

    const auto AbortStaged = [World, NewActor](const TCHAR* Reason)
    {
        UE_LOG(LogTemp, Error, TEXT("组合安装失败：%s；原器具保留。"), Reason);
        if (IsValid(NewActor) && !World->EditorDestroyActor(NewActor, true))
            UE_LOG(LogTemp, Error, TEXT("暂存组合清理失败，请检查 HandDripperAssembly_Staged。"));
        return false;
    };

    UStaticMeshComponent* NewRoot = NewActor->ContainerMesh;
    UGrabComponent* NewGrab = NewActor->GrabComp;
    UStaticMeshComponent* NewStand[4] = {
        NewActor->StandLeftLeg, NewActor->StandRightLeg, NewActor->StandLeftShelf, NewActor->StandRightShelf
    };
    TMap<UObject*, UObject*> Remap;
    Remap.Add(OldFilter, NewActor);
    Remap.Add(OldStand, NewActor);
    Remap.Add(OldStand->GetRootComponent(), NewRoot);
    for (int32 Index = 0; Index < 4; ++Index)
        Remap.Add(StandScenes[Index].Source, NewStand[Index]);
    for (UActorComponent* Source : OldComponents)
        if (UActorComponent* Target = FindComponent(NewActor, Source->GetFName()))
            Remap.Add(Source, Target);

    // Deferred Spawn 仍可能先注册原生组件；引擎属性复制要求新 Actor 的组件全部未注册。
    NewActor->UnregisterAllComponents();
    UEngine::FCopyPropertiesForUnrelatedObjectsParams Copy;
    Copy.bDoDelta = false;
    Copy.bPreserveRootComponent = true;
    Copy.bPerformDuplication = true;
    Copy.bReplaceObjectClassReferences = false;
    Copy.bNotifyObjectReplacement = false;
    Copy.bReplaceInternalReferenceUponRead = true;
    Copy.OptionalReplacementMappings = &Remap;
    UEngine::CopyPropertiesForUnrelatedObjects(OldFilter, NewActor, Copy);
    if (NewActor->ContainerMesh != NewRoot || NewActor->GrabComp != NewGrab || !HasSingleRootGrab(NewActor))
        return AbortStaged(TEXT("继承组件未正确复制到新实例"));

    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (!IsValid(NewStand[Index]) || NewStand[Index]->GetOwner() != NewActor)
            return AbortStaged(TEXT("组合支架默认子组件缺失"));
        UEngine::CopyPropertiesForUnrelatedObjects(StandScenes[Index].Source, NewStand[Index], Copy);
        NewStand[Index]->SetupAttachment(NewRoot);
        // UE 变换乘法约定：子组件世界变换相对于旧滤杯世界变换，保留网格 pivot。
        NewStand[Index]->SetRelativeTransform(StandScenes[Index].WorldTransform.GetRelativeTransform(OldTransform));
    }
    NewActor->SetActorTransform(OldTransform, false, nullptr, ETeleportType::TeleportPhysics);
    NewActor->ApplyAssemblyGrabSettings();
    NewGrab->Activate(true);
    NewActor->FinishSpawning(OldTransform);
    if (!IsValid(NewActor) || NewActor->HasActorBegunPlay() || World->HasBegunPlay()
        || !HasSingleRootGrab(NewActor) || NewActor->ContainerMesh != NewRoot || NewActor->GrabComp != NewGrab)
        return AbortStaged(TEXT("构造后根结构或编辑器世界状态改变"));

    // FinishSpawning 可能运行蓝图构造脚本；按当前源实例再次恢复视觉，而非读取旧 CDO。
    for (UActorComponent* Source : OldComponents)
    {
        UActorComponent* Target = FindComponent(NewActor, Source->GetFName());
        if (!Target || Target->GetClass() != Source->GetClass())
            return AbortStaged(TEXT("构造后缺少同名继承组件"));
        Remap.Add(Source, Target);
    }
    NewActor->SetActorTransform(OldTransform, false, nullptr, ETeleportType::TeleportPhysics);
    for (const FSceneSnapshot& Saved : FilterScenes)
    {
        USceneComponent* Target = Cast<USceneComponent>(Remap.FindRef(Saved.Source));
        if (!Target) return AbortStaged(TEXT("继承场景组件映射失败"));
        if (Saved.Source != OldFilter->GetRootComponent())
        {
            USceneComponent* Parent = Cast<USceneComponent>(Remap.FindRef(Saved.Parent));
            if (!Parent || !Target->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform, Saved.Socket))
                return AbortStaged(TEXT("继承组件附着恢复失败"));
            Target->SetRelativeTransform(Saved.RelativeTransform);
        }
        if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Target)) RestoreMesh(Saved, Mesh);
    }
    for (int32 Index = 0; Index < 4; ++Index)
    {
        UStaticMeshComponent* Target = NewStand[Index];
        UStaticMeshComponent* Source = CastChecked<UStaticMeshComponent>(StandScenes[Index].Source);
        if (Target->GetOwner() != NewActor
            || !Target->AttachToComponent(NewRoot, FAttachmentTransformRules::KeepRelativeTransform))
            return AbortStaged(TEXT("支架直接子组件附着失败"));
        RestoreMesh(StandScenes[Index], Target);
        Target->SetRelativeTransform(StandScenes[Index].WorldTransform.GetRelativeTransform(OldTransform));
        const bool bNoCollision = Source->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
        Target->SetCollisionProfileName(bNoCollision ? Source->GetCollisionProfileName() : FName(TEXT("PhysicsActor")));
        Target->SetCollisionResponseToChannels(Source->GetCollisionResponseToChannels());
        Target->SetCollisionObjectType(ECC_PhysicsBody);
        Target->SetCollisionEnabled(bNoCollision ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        Target->SetGenerateOverlapEvents(Source->GetGenerateOverlapEvents());
    }
    NewActor->ApplyAssemblyGrabSettings();
    NewRoot->SetCollisionProfileName(TEXT("PhysicsActor"));
    NewRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    NewRoot->SetCollisionObjectType(ECC_PhysicsBody);
    NewGrab->Activate(true);

    if (!NewActor->GetActorTransform().Equals(OldTransform, TransformTolerance)
        || !NewGrab->IsActive() || !NewGrab->bAutoActivate || NewActor->bCanPour
        || NewGrab->GrabType != EGrabType::Free || NewGrab->PhysicsMode != EGrabPhysicsMode::Kinematic
        || NewGrab->bSimulateOnDrop || NewGrab->bCaptureHand || NewGrab->bHideHandWhileHeld
        || NewActor->FillAmount != OldFilter->FillAmount || NewActor->MaxVolumeML != OldFilter->MaxVolumeML
        || NewActor->BloomSeconds != OldFilter->BloomSeconds
        || NewActor->DrainRateMLPerSecond != OldFilter->DrainRateMLPerSecond
        || NewActor->CoffeeYieldMLPerGram != OldFilter->CoffeeYieldMLPerGram
        || NewActor->DrippedML != OldFilter->DrippedML || NewActor->CoffeeProducedML != OldFilter->CoffeeProducedML)
        return AbortStaged(TEXT("组合配置或冲煮参数校验失败"));
    for (const FSceneSnapshot& Saved : FilterScenes)
        if (!MatchesSnapshot(Saved, Cast<USceneComponent>(Remap.FindRef(Saved.Source))))
            return AbortStaged(TEXT("滤杯网格、材质或世界变换未保真"));
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const UStaticMeshComponent* Source = CastChecked<UStaticMeshComponent>(StandScenes[Index].Source);
        const ECollisionEnabled::Type ExpectedCollision = Source->GetCollisionEnabled() == ECollisionEnabled::NoCollision
            ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;
        if (!MatchesSnapshot(StandScenes[Index], NewStand[Index]) || NewStand[Index]->GetAttachParent() != NewRoot
            || NewStand[Index]->GetCollisionEnabled() != ExpectedCollision
            || NewStand[Index]->GetCollisionObjectType() != ECC_PhysicsBody)
            return AbortStaged(TEXT("支架网格、材质、碰撞或世界变换未保真"));
    }
    TInlineComponentArray<USceneComponent*> NewScenes(NewActor);
    for (USceneComponent* Scene : NewScenes)
        if (Scene->Mobility != EComponentMobility::Movable || Scene->IsAnySimulatingPhysics())
            return AbortStaged(TEXT("组合仍有静态或模拟物理组件"));

    // 安装只映射关卡实例；Copy 的可选映射可能含内部条目，不传播类、CDO 或共享资产映射。
    TMap<UObject*, UObject*> WorldRemap;
    for (const TPair<UObject*, UObject*>& Pair : Remap)
        if (IsValid(Pair.Key) && IsValid(Pair.Value)
            && (Pair.Key == OldFilter || Pair.Key == OldStand || Pair.Key->IsIn(OldFilter) || Pair.Key->IsIn(OldStand))
            && (Pair.Value == NewActor || Pair.Value->IsIn(NewActor)))
            WorldRemap.Add(Pair.Key, Pair.Value);
    TMap<UObject*, UObject*> ProbeMap;
    for (const TPair<UObject*, UObject*>& Pair : WorldRemap) ProbeMap.Add(Pair.Key, Pair.Key);
    TArray<UObject*> OldObjects;
    GetObjectsWithOuter(OldFilter, OldObjects, true);
    GetObjectsWithOuter(OldStand, OldObjects, true);
    TMap<UObject*, UObject*> UnmappedProbeMap;
    for (UObject* Object : OldObjects)
        if (IsValid(Object) && !WorldRemap.Contains(Object)) UnmappedProbeMap.Add(Object, Object);
    TArray<UObject*> WorldObjects;
    GetObjectsWithOuter(World, WorldObjects, true);
    WorldObjects.Add(World);
    TArray<UObject*> ReferencingObjects;
    for (UObject* Object : WorldObjects)
        if (IsReplacementScope(Object, World, OldFilter, OldStand))
        {
            FWorldReferenceArchive UnmappedProbe(Object, UnmappedProbeMap);
            if (UnmappedProbe.IsError() || UnmappedProbe.GetCount() > 0)
                return AbortStaged(TEXT("关卡仍引用无对应新对象的原子对象"));
            FWorldReferenceArchive Probe(Object, ProbeMap);
            if (Probe.IsError()) return AbortStaged(TEXT("关卡引用预检失败"));
            if (Probe.GetCount() > 0) ReferencingObjects.Add(Object);
        }

    // 先在同一个 Level 内交换名字，保留关卡层级；软引用将写成最终名称，而非暂存名称。
    const FName RetiredName = MakeUniqueObjectName(Level, OldFilter->GetClass(), TEXT("HandDripper_Replaced"));
    const ERenameFlags RenameFlags = REN_DontCreateRedirectors;
    if (!OldFilter->Rename(*RetiredName.ToString(), Level, RenameFlags | REN_Test)
        || !OldFilter->Rename(*RetiredName.ToString(), Level, RenameFlags))
        return AbortStaged(TEXT("无法暂存原滤杯名称"));
    if (!NewActor->Rename(*FilterName.ToString(), Level, RenameFlags | REN_Test)
        || !NewActor->Rename(*FilterName.ToString(), Level, RenameFlags))
    {
        OldFilter->Rename(*FilterName.ToString(), Level, RenameFlags);
        return AbortStaged(TEXT("无法恢复滤杯原始名称"));
    }

    bool bRemapSucceeded = true;
    for (UObject* Object : ReferencingObjects)
        if (IsReplacementScope(Object, World, OldFilter, OldStand))
        {
            Object->Modify();
            FWorldReferenceArchive Replace(Object, WorldRemap);
            bRemapSucceeded &= !Replace.IsError();
            if (Replace.GetCount() > 0) Object->MarkPackageDirty();
        }
    for (UObject* Object : WorldObjects)
        if (IsReplacementScope(Object, World, OldFilter, OldStand))
        {
            FWorldReferenceArchive Remaining(Object, ProbeMap);
            bRemapSucceeded &= !Remaining.IsError() && Remaining.GetCount() == 0;
        }
    if (!bRemapSucceeded)
    {
        // 引用迁移已有部分写入，不能清理新组合；未销毁原对象，也不自动保存。
        UE_LOG(LogTemp, Error, TEXT("组合引用迁移未完成：原器具和新组合均保留，请检查或重载未保存关卡。"));
        return false;
    }

    // 破坏性提交放最后。若编辑器回调导致销毁失败，保留新组合，避免丢失已迁移的引用。
    const bool bDestroyedStand = World->EditorDestroyActor(OldStand, true);
    const bool bDestroyedFilter = World->EditorDestroyActor(OldFilter, true);
    NewActor->SetActorLabel(OriginalFilterLabel);
    NewActor->MarkPackageDirty();
    Level->MarkPackageDirty();
    World->MarkPackageDirty();
    if (!bDestroyedStand || !bDestroyedFilter)
    {
        UE_LOG(LogTemp, Error, TEXT("组合安装的末段销毁未全部成功；新组合保留，请检查原对象残留。未保存关卡。"));
        return false;
    }
    UE_LOG(LogTemp, Display, TEXT("组合安装完成：%s；唯一根 GrabComp 为 Free/Kinematic。未保存关卡或蓝图。"),
        *NewActor->GetClass()->GetPathName());
    return true;
#else
    return false;
#endif
}
