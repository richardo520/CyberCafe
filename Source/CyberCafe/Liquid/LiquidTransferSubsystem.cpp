// @trace LQ-02 LQ-03 LQ-04 LQ-05
#include "Liquid/LiquidTransferSubsystem.h"
#include "Liquid/LiquidContainerActor.h"
#include "Liquid/LiquidTransferMath.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GrabComponent.h"
#include "MotionControllerComponent.h"
#include "CollisionQueryParams.h"
#include "Tutorial/CyberCafeEventSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarLiquidDebug(TEXT("cybercafe.Liquid.Debug"),0,TEXT("Draw logical liquid flight and the conservation ledger."));
#endif

bool ULiquidTransferSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{return Type==EWorldType::Game||Type==EWorldType::PIE||Type==EWorldType::GamePreview;}
TStatId ULiquidTransferSubsystem::GetStatId() const
{RETURN_QUICK_DECLARE_CYCLE_STAT(ULiquidTransferSubsystem,STATGROUP_Tickables);}
void ULiquidTransferSubsystem::Tick(float DeltaTime)
{
    Simulate(DeltaTime);
#if !UE_BUILD_SHIPPING
    if(CVarLiquidDebug.GetValueOnGameThread()>0&&GEngine)
    {
        const FLiquidBalance B=GetBalance();
        GEngine->AddOnScreenDebugMessage(0xC0FFE,0.f,FColor::Cyan,FString::Printf(TEXT("Liquid: containers %.3f | flight %.3f | spill %.3f | removed %.3f | balance error %.6f mL | packets %d"),B.ContainerML,B.InFlightML,B.SpilledML,B.RemovedML,B.ErrorML,B.PacketCount));
    }
#endif
}
void ULiquidTransferSubsystem::Deinitialize()
{Packets.Empty();Containers.Empty();PreviousMouths.Empty();Super::Deinitialize();}

void ULiquidTransferSubsystem::RegisterContainer(ALiquidContainerActor* A,double ML)
{
    if(!A||Containers.Contains(A))return;
    Containers.Add(A);
    if(A->bInitialVolumeIsReplacement)InjectedML+=ML;else InitialML+=ML;
    if(A->PourEntryPoint)PreviousMouths.Add(A,A->PourEntryPoint->GetComponentTransform());
}
void ULiquidTransferSubsystem::UnregisterContainer(ALiquidContainerActor* A,double ML)
{
    if(Containers.Remove(A)>0){RemovedML+=ML;PreviousMouths.Remove(A);}
    // The source's already emitted packets survive its destruction.
}
bool ULiquidTransferSubsystem::PrepareWorkbenchRestore(const TArray<ALiquidContainerActor*>& ContainersToRestore)
{
    if(bSimulating)return false;
    check(IsInGameThread());
    const UWorld* World=GetWorld();
    if(!World)return false;

    TSet<TWeakObjectPtr<ALiquidContainerActor>> Sources;
    struct FRestoreSource
    {
        TWeakObjectPtr<ALiquidContainerActor> Actor;
        TWeakObjectPtr<UNiagaraComponent> PourFX;
    };
    TArray<FRestoreSource> RestoreSources;
    for(ALiquidContainerActor* A:ContainersToRestore)
    {
        if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=World||A->IsLiquidFrozen())return false;
        const TWeakObjectPtr<ALiquidContainerActor> Key(A);
        if(Sources.Contains(Key))continue;
        Sources.Add(Key);
        RestoreSources.Add({Key,A->PourFX.Get()});
    }

    // 全量校验后先提交；FX 组件回调期间拒绝 Simulate/恢复重入。
    TGuardValue<bool> Guard(bSimulating,true);
    for(const FRestoreSource& Source:RestoreSources)
    {
        ALiquidContainerActor* A=Source.Actor.Get();
        check(A);
        A->bCanPour=false;
        A->bAcceptLiquidFromOthers=false;
        A->bIsPouring=false;
    }
    for(FLiquidPacket& P:Packets)
    {
        // 按弱指针身份匹配，不把失效 Source.Get()==nullptr 当作匹配。
        if(!Sources.Contains(P.Source))continue;
        SpilledML+=P.VolumeML;
        P.VolumeML=0;
    }
    Packets.RemoveAll([&Sources](const FLiquidPacket& P){return Sources.Contains(P.Source);});

    // 所有液体状态已提交；限定基类调用，避开子类 virtual 事件。
    // StopPouring/Deactivate 仍可能触发组件回调，之后只重新解析快照。
    for(const FRestoreSource& Source:RestoreSources)
    {
        if(ALiquidContainerActor* A=Source.Actor.Get())A->ALiquidContainerActor::StopPouring();
        if(UNiagaraComponent* FX=Source.PourFX.Get())FX->DeactivateImmediate();
    }
    return true;
}
FLiquidBalance ULiquidTransferSubsystem::GetBalance() const
{
    FLiquidBalance B;B.InitialML=InitialML;B.InjectedML=InjectedML;B.RemovedML=RemovedML;B.SpilledML=SpilledML;
    for(const auto& Weak:Containers)if(const ALiquidContainerActor* A=Weak.Get())B.ContainerML+=A->GetPreciseVolumeML();
    for(const FLiquidPacket& P:Packets)B.InFlightML+=P.VolumeML;
    B.PacketCount=Packets.Num();B.ErrorML=B.InitialML+B.InjectedML-B.RemovedML-B.ContainerML-B.InFlightML-B.SpilledML;return B;
}
double ULiquidTransferSubsystem::GetUnsettledFlightHorizon() const
{
    double Horizon=0;
    for(const FLiquidPacket& P:Packets)if(P.VolumeML>0)Horizon=FMath::Max(Horizon,FMath::Max(0.0,P.BirthDelay)+FMath::Max(0.0,3.0-P.Age)+.02);
    return Horizon;
}
double ULiquidTransferSubsystem::EmitVolume(ALiquidContainerActor* Source,double ML,const FVector& Position,const FVector& Velocity,const FVector& Gravity,double BirthDelay,const FTransform* RibbonTransform)
{
    if(!IsValid(Source)||!FMath::IsFinite(ML)||ML<=0||Position.ContainsNaN()||Velocity.ContainsNaN()||Gravity.ContainsNaN())return 0;
    Source->InitializeLiquidState();
    const FLinearColor Color=Source->LiquidColor;
    UMaterialInterface* Material=Source->LiquidMaterialAsset;
    if (Source->IsLiquidFrozen() || Source->IsBroken()) return 0;
    FLiquidComposition Composition;
    const double Actual=Source->DebitVolumeInternal(ML, &Composition);
    if(Actual<=0)return 0;
    if(Packets.Num()>=MaxPackets)
    {
        SpilledML+=Actual;Source->NotifyLiquidState();return Actual;
    }
    FLiquidPacket P;P.Id=NextPacketId++;P.Source=Source;P.VolumeML=Actual;P.Position=Position;P.Origin=Position;P.Velocity=Velocity;P.Gravity=Gravity;P.Color=Color;P.Material=Material;P.BirthDelay=FMath::Max(0.0,BirthDelay);
    if(RibbonTransform)
    {
        P.bRibbonCurve=true;P.RibbonAxis=RibbonTransform->GetUnitAxis(EAxis::X);
        P.RibbonVelocity=Source->PourRibbonVelocity;P.RibbonOffset=Source->PourRibbonOffset;
        P.Position=P.Origin+CyberCafeLiquid::RibbonDisplacement(0,P.RibbonAxis,P.RibbonVelocity,P.RibbonOffset);
    }
    P.Composition = Composition;
    P.IgnoreActors.Add(Source);
    TArray<AActor*> Ignore;Source->GetPourTraceIgnoreActors(Ignore);
    if(Source->GetInstigator())Ignore.Add(Source->GetInstigator());
    if(Source->GrabComp && Source->GrabComp->GetHoldingController())
        Ignore.Add(Source->GrabComp->GetHoldingController()->GetOwner());
    for(AActor* A:Ignore)if(IsValid(A))P.IgnoreActors.AddUnique(A);
    Packets.Add(MoveTemp(P));
    // Both debit and in-flight credit are committed before external subscribers run.
    Source->NotifyLiquidState();return Actual;
}
void ULiquidTransferSubsystem::EmitPour(ALiquidContainerActor* Source,double Dt,const FTransform& TM)
{
    if(!IsValid(Source)||!Source->bCanPour||!FMath::IsFinite(Dt)||Dt<=0)return;
    const bool bWasPouring=Source->bIsPouring;
    const double Rate=FMath::IsFinite(Source->PourRatePerSecond)?FMath::Max(0.f,Source->PourRatePerSecond):0;
    if(Rate<=0)return;
    // Volume follows time, not particle count. Midpoint births prevent a long frame
    // from treating all newly emitted liquid as if it left at the frame start.
    const int32 N=FMath::Clamp(FMath::CeilToInt(Dt/StepSeconds),1,1024);
    const double Slice=Dt/N;
    for(int32 I=0;I<N&&IsValid(Source);++I)
    {
        // NotifyLiquidState may close the cap, disable pouring, stop the stream
        // or destroy the source. Recheck before every subsequent slice.
        if(!Source->bCanPour||(bWasPouring&&!Source->bIsPouring)||Source->GetPreciseVolumeML()<=0)break;
        const FVector Velocity=-TM.GetUnitAxis(EAxis::X)*Source->PourInitialSpeedCms;
        const bool bRibbon=Source->bUseRibbonTrajectory&&Source->PourFX&&Source->PourFX->GetAsset()&&Source->PourFX->GetAsset()->GetFName()==TEXT("P_Ribbon");
        EmitVolume(Source,Rate*Slice,TM.GetLocation(),Velocity,Source->PourGravityCms2,(I+.5)*Slice,bRibbon?&TM:nullptr);
    }
}
void ULiquidTransferSubsystem::Simulate(double Dt)
{
    if(bSimulating||!FMath::IsFinite(Dt)||Dt<=0||!GetWorld())return;
    TGuardValue<bool> Guard(bSimulating,true);
    struct FMouth{TWeakObjectPtr<ALiquidContainerActor> Actor;FTransform Start,End;double Radius;};
    TArray<FMouth> Mouths;
    for(const auto& Weak:Containers)
    {
        ALiquidContainerActor* A=Weak.Get();
        if(!A||!A->PourEntryPoint)continue;
        const FTransform Current=A->PourEntryPoint->GetComponentTransform();
        const FTransform* Previous=PreviousMouths.Find(Weak);
        if(A->bAcceptLiquidFromOthers&&!A->IsLiquidFrozen()&&!A->IsBroken()&&A->RestoreReceiveCooldown<=0&&A->PourEntryRadius>0)Mouths.Add({A,Previous?*Previous:Current,Current,A->PourEntryRadius});
        PreviousMouths.Add(Weak,Current);
    }
    struct FAccepted{TWeakObjectPtr<ALiquidContainerActor> Source,Target;double ML;};
    TArray<FAccepted> AcceptedEvents;
    TSet<TWeakObjectPtr<ALiquidContainerActor>> Changed,ResetMID;
    double Elapsed=0;
    while(Elapsed<Dt-1.e-12)
    {
        const double Step=FMath::Min(StepSeconds,Dt-Elapsed);
        for(FLiquidPacket& P:Packets)
        {
            if(P.VolumeML<=0)continue;
            double H=Step,StartOffset=0;
            if(P.BirthDelay>0)
            {
                const double Delay=FMath::Min(P.BirthDelay,H);P.BirthDelay-=Delay;H-=Delay;StartOffset=Delay;
                if(H<=1.e-12)continue;
            }
            const FVector A=P.Position;
            const FVector B=P.bRibbonCurve
                ? P.Origin+CyberCafeLiquid::RibbonDisplacement(P.Age+H,P.RibbonAxis,P.RibbonVelocity,P.RibbonOffset)
                : A+P.Velocity*H+.5*P.Gravity*H*H;
            const double FrameA=(Elapsed+StartOffset)/Dt,FrameB=(Elapsed+Step)/Dt;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(LiquidFlight),false);
            for(const auto& W:P.IgnoreActors)if(AActor* Actor=W.Get())Params.AddIgnoredActor(Actor);
            FHitResult Hit;
            const bool bBlocked=GetWorld()->LineTraceSingleByChannel(Hit,A,B,ECC_Visibility,Params);
            double BestTime=bBlocked?static_cast<double>(Hit.Time):2.0;
            ALiquidContainerActor* Target=nullptr;
            for(const FMouth& Mouth:Mouths)
            {
                ALiquidContainerActor* Candidate=Mouth.Actor.Get();
                if(!Candidate||Candidate==P.Source.Get())continue;
                double T=0;
                if(CyberCafeLiquid::CrossMouth(A,B,CyberCafeLiquid::Interpolate(Mouth.Start,Mouth.End,FrameA),CyberCafeLiquid::Interpolate(Mouth.Start,Mouth.End,FrameB),Mouth.Radius,T)&&T<=BestTime+1.e-6)
                {BestTime=T;Target=Candidate;}
            }
#if ENABLE_DRAW_DEBUG
            bool bDraw=false;
#if !UE_BUILD_SHIPPING
            bDraw=CVarLiquidDebug.GetValueOnGameThread()>0;
#endif
            if(ALiquidContainerActor* Source=P.Source.Get();bDraw||(Source&&Source->bDebugDrawTrace))
            {
                DrawDebugLine(GetWorld(),A,B,Target?FColor::Green:(bBlocked?FColor::Red:FColor::Cyan),false,.15f,0,.3f);
                if(Target)DrawDebugPoint(GetWorld(),FMath::Lerp(A,B,BestTime),6,FColor::Green,false,.3f);
            }
#endif
            if(Target)
            {
                const bool bEmpty=Target->GetPreciseVolumeML()<=0;
                const double Received=Target->CreditVolumeInternal(P.VolumeML,P.Color,&P.Composition);
                SpilledML+=P.VolumeML-Received;
                if(Received>0)
                {
                    if(P.Material){Target->LiquidMaterialAsset=P.Material;if(bEmpty)ResetMID.Add(Target);}
                    Changed.Add(Target);AcceptedEvents.Add({P.Source,Target,Received});
                }
                P.VolumeML=0;
            }
            else if(bBlocked){SpilledML+=P.VolumeML;P.VolumeML=0;}
            else
            {
                P.Position=B;P.Velocity+=P.Gravity*H;P.Age+=H;
                if(P.Age>=3.0||FVector::DistSquared(P.Position,P.Origin)>2000.0*2000.0)
                {SpilledML+=P.VolumeML;P.VolumeML=0;}
            }
        }
        Elapsed+=Step;
    }
    Packets.RemoveAll([](const FLiquidPacket& P){return P.VolumeML<=0;});
    // Notify only after all packets and ledger entries have committed. Callbacks
    // can destroy containers or emit more packets without invalidating iteration.
    for(const auto& Weak:Changed)if(ALiquidContainerActor* A=Weak.Get())
    {if(ResetMID.Contains(Weak))A->EnsureLiquidMID(true);A->NotifyLiquidState();}
    if(UCyberCafeEventSubsystem* Events=UCyberCafeEventSubsystem::Get(GetWorld()))
    {
        for(const FAccepted& E:AcceptedEvents)if(ALiquidContainerActor* Target=E.Target.Get())
            Events->OnLiquidPoured.Broadcast(E.Source.Get(),Target,static_cast<float>(E.ML));
    }
}
