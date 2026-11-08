#include "Coffee/Brew/HotWaterSteamComponent.h"
#include "Coffee/Brew/HandPourKettleActor.h"
#include "Liquid/LiquidTransferSubsystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Engine/World.h"

UHotWaterSteamComponent::UHotWaterSteamComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    bAutoActivate=false;
}

void UHotWaterSteamComponent::BeginPlay()
{
    Super::BeginPlay();
    SetComponentTickEnabled(false);
    if(UWorld* World=GetWorld())
        if(ULiquidTransferSubsystem* Subsystem=World->GetSubsystem<ULiquidTransferSubsystem>())
        {
            ImpactSubsystem=Subsystem;
            ImpactDelegateHandle=Subsystem->OnWaterImpactNative.AddUObject(this,&UHotWaterSteamComponent::HandleImpact);
        }
}

void UHotWaterSteamComponent::UnbindImpact()
{
    if(ULiquidTransferSubsystem* Subsystem=ImpactSubsystem.Get())
        Subsystem->OnWaterImpactNative.Remove(ImpactDelegateHandle);
    ImpactDelegateHandle.Reset();
    ImpactSubsystem.Reset();
}

void UHotWaterSteamComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    UnbindImpact();
    ReleaseSteam();
    Super::EndPlay(Reason);
}

void UHotWaterSteamComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    UnbindImpact();
    ReleaseSteam();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

UParticleSystemComponent* UHotWaterSteamComponent::GetSteamComponent() const
{
    return IsValid(SteamParticleComponent)?SteamParticleComponent.Get():nullptr;
}

void UHotWaterSteamComponent::HandleImpact(const FHotWaterImpact& Impact)
{
    AHandPourKettleActor* Kettle=Cast<AHandPourKettleActor>(GetOwner());
    if(!IsValid(Kettle)||Kettle->IsActorBeingDestroyed()||IsBeingDestroyed()||!HasBegunPlay()
        ||Impact.Source.Get()!=Kettle||!FMath::IsFinite(Impact.WaterML)||Impact.WaterML<=0)return;
    if(Kettle->bWorkbenchRestoreEmissionBlocked||Kettle->IsBroken()||Kettle->IsLiquidFrozen()
        ||!bEnabled||!IsValid(SteamSystem))
    {
        StopSteam(true);
        return;
    }
    if(Impact.Point.ContainsNaN())return;
    LastImpactPoint=Impact.Point;
    LastImpactWaterML=Impact.WaterML;
    LastImpactAge=0;
    // 计数表示请求门由关到开，不等同于粒子可见，也不按每个包累计。
    if(!bSteamRequested){bSteamRequested=true;++SteamStartCount;}
    SetComponentTickEnabled(true);
    RefreshSteam();
}

void UHotWaterSteamComponent::RefreshSteam()
{
    const TWeakObjectPtr<UHotWaterSteamComponent> Self(this);
    const TWeakObjectPtr<AHandPourKettleActor> Kettle(Cast<AHandPourKettleActor>(GetOwner()));
    const auto CanContinue=[Self,Kettle]()
    {
        const UHotWaterSteamComponent* Component=Self.Get();
        const AHandPourKettleActor* Owner=Kettle.Get();
        return Component&&!Component->IsBeingDestroyed()&&Component->HasBegunPlay()&&Component->bSteamRequested
            &&Component->bEnabled&&IsValid(Component->SteamSystem)&&Owner&&!Owner->IsActorBeingDestroyed()
            &&!Owner->bWorkbenchRestoreEmissionBlocked&&!Owner->IsBroken()&&!Owner->IsLiquidFrozen();
    };
    if(!CanContinue()){StopSteam(true);return;}
    if(!SteamParticleComponent)
    {
        SteamParticleComponent=NewObject<UParticleSystemComponent>(Kettle.Get());
        SteamParticleComponent->bAutoActivate=false;
        SteamParticleComponent->bAutoDestroy=false;
        SteamParticleComponent->bAutoManageAttachment=false;
        SteamParticleComponent->SetAbsolute(true,true,true);
        SteamParticleComponent->SetWorldLocationAndRotation(LastImpactPoint,FRotator::ZeroRotator);
        SteamParticleComponent->SetWorldScale3D(FVector(FMath::IsFinite(SteamScale)?FMath::Max(.001f,SteamScale):.18f));
        SteamParticleComponent->OnSystemFinished.AddDynamic(this,&UHotWaterSteamComponent::HandleSystemFinished);
        Kettle->AddInstanceComponent(SteamParticleComponent);
        SteamParticleComponent->RegisterComponent();
        if(!CanContinue())return;
    }
    UParticleSystemComponent* FX=GetSteamComponent();
    if(!FX){StopSteam(true);return;}
    const TWeakObjectPtr<UParticleSystemComponent> WeakFX(FX);
    if(FX->Template!=SteamSystem)
    {
        bSteamRunning=false;
        if(FX->Template)
        {
            FX->DeactivateSystem();
            if(!CanContinue()||!WeakFX.IsValid())return;
        }
        // 只引用配置的原 Cascade 模板，不修改或复制资源，不写 Niagara 参数。
        FX->SetTemplate(SteamSystem);
        if(!CanContinue()||!WeakFX.IsValid())return;
    }
    FX->SetWorldLocationAndRotation(LastImpactPoint,FRotator::ZeroRotator);
    FX->SetWorldScale3D(FVector(FMath::IsFinite(SteamScale)?FMath::Max(.001f,SteamScale):.18f));
    // 完成通知维护自有运行门；无 IsActive 轮询，更不会据此逐包创建组件。
    if(!bSteamRunning)
    {
        bSteamRunning=true;
        // Cascade 此参数是 bFlagAsJustAttached，并非 Niagara 的 bReset。
        FX->ActivateSystem(false);
    }
}

void UHotWaterSteamComponent::HandleSystemFinished(UParticleSystemComponent* FinishedComponent)
{
    if(FinishedComponent==SteamParticleComponent)bSteamRunning=false;
}

void UHotWaterSteamComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    const AHandPourKettleActor* Kettle=Cast<AHandPourKettleActor>(GetOwner());
    if(!IsValid(Kettle)||Kettle->IsActorBeingDestroyed()||Kettle->bWorkbenchRestoreEmissionBlocked
        ||Kettle->IsBroken()||Kettle->IsLiquidFrozen()||!bEnabled||!IsValid(SteamSystem))
    {
        StopSteam(true);
        return;
    }
    if(!bSteamRequested){SetComponentTickEnabled(false);return;}
    // 只累计世界传入的正模拟 DeltaTime；暂停/无效帧不消耗保留时间。
    if(!FMath::IsFinite(DeltaTime)||DeltaTime<=0)return;
    LastImpactAge+=DeltaTime;
    const double Hold=FMath::IsFinite(ImpactHoldSeconds)?FMath::Max(0.f,ImpactHoldSeconds):.18f;
    if(LastImpactAge>=Hold){StopSteam();return;}
    if(UParticleSystemComponent* FX=GetSteamComponent())
    {
        if(FX->Template!=SteamSystem)RefreshSteam();
        else FX->SetWorldScale3D(FVector(FMath::IsFinite(SteamScale)?FMath::Max(.001f,SteamScale):.18f));
    }
}

void UHotWaterSteamComponent::StopSteam(bool bImmediate)
{
    const bool bWasRequested=bSteamRequested||bSteamRunning;
    bSteamRequested=false;
    bSteamRunning=false;
    SetComponentTickEnabled(false);
    const TWeakObjectPtr<UParticleSystemComponent> FX(GetSteamComponent());
    if(UParticleSystemComponent* Component=FX.Get())
    {
        if(bWasRequested&&Component->GetWorld())Component->DeactivateSystem();
        if(bImmediate)
            if(UParticleSystemComponent* Remaining=FX.Get())Remaining->KillParticlesForced();
    }
}

void UHotWaterSteamComponent::ReleaseSteam()
{
    bSteamRequested=false;
    bSteamRunning=false;
    SetComponentTickEnabled(false);
    const TWeakObjectPtr<UParticleSystemComponent> FX(GetSteamComponent());
    // 先清所有权，避免停粒子回调销毁源时重复释放；生命周期外不销毁实例。
    SteamParticleComponent=nullptr;
    if(UParticleSystemComponent* Component=FX.Get())
    {
        Component->OnSystemFinished.RemoveDynamic(this,&UHotWaterSteamComponent::HandleSystemFinished);
        if(Component->GetWorld())Component->DeactivateSystem();
        if(UParticleSystemComponent* Remaining=FX.Get())
        {
            Remaining->KillParticlesForced();
            Remaining->DestroyComponent();
        }
    }
}
