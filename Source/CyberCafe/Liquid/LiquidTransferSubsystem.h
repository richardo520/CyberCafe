// @trace LQ-02 LQ-03 LQ-04
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Liquid/LiquidComposition.h"
#include "LiquidTransferSubsystem.generated.h"
class ALiquidContainerActor;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct CYBERCAFE_API FLiquidBalance
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="Liquid") double InitialML=0;
    UPROPERTY(BlueprintReadOnly,Category="Liquid") double InjectedML=0;
    UPROPERTY(BlueprintReadOnly,Category="Liquid") double RemovedML=0;
    UPROPERTY(BlueprintReadOnly,Category="Liquid") double ContainerML=0;
    UPROPERTY(BlueprintReadOnly,Category="Liquid") double InFlightML=0;
    UPROPERTY(BlueprintReadOnly,Category="Liquid") double SpilledML=0;
    UPROPERTY(BlueprintReadOnly,Category="Liquid") double ErrorML=0;
    UPROPERTY(BlueprintReadOnly,Category="Liquid") int32 PacketCount=0;
};

USTRUCT()
struct FLiquidPacket
{
    GENERATED_BODY()
    uint64 Id=0;
    double VolumeML=0;
    double Age=0;
    double BirthDelay=0;
    FVector Position=FVector::ZeroVector;
    FVector Velocity=FVector::ZeroVector;
    FVector Gravity=FVector(0,0,-980);
    FVector Origin=FVector::ZeroVector;
    bool bRibbonCurve=false;
    FVector RibbonAxis=FVector::ForwardVector;
    double RibbonVelocity=20, RibbonOffset=3;
    FLinearColor Color=FLinearColor::White;
    UPROPERTY() FLiquidComposition Composition;
    UPROPERTY() TObjectPtr<UMaterialInterface> Material;
    UPROPERTY() TWeakObjectPtr<ALiquidContainerActor> Source;
    UPROPERTY() TArray<TWeakObjectPtr<AActor>> IgnoreActors;
};

/** World-local authority. Logical packets are independent of Niagara particle density. */
UCLASS()
class CYBERCAFE_API ULiquidTransferSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    void RegisterContainer(ALiquidContainerActor* Container,double InitialVolume);
    void UnregisterContainer(ALiquidContainerActor* Container,double RemainingVolume);
    // 仅限主游戏线程；预检不校验数组、不修改状态。
    bool CanPrepareWorkbenchRestore() const { return !bSimulating; }
    // false 表示未修改：正在结算、无世界，或任一目标无效/跨世界/冻结。
    // true 仅锁定旧容器并取消其来源包；manager 随后 Destroy，由 Unregister 记 Removed。
    // 不清余液、容量、成分；新容器须在初始化/BeginPlay 前标记 replacement。
    // 未取消来源仍在途；新杯接液隔离由 manager 覆盖剩余 BirthDelay + 3 秒模拟时间。
    bool PrepareWorkbenchRestore(const TArray<ALiquidContainerActor*>& ContainersToRestore);
    void RecordInjected(double ML){InjectedML+=ML;}
    void RecordRemoved(double ML){RemovedML+=ML;}
    void RecordSpilled(double ML){SpilledML+=ML;}
    double EmitVolume(ALiquidContainerActor* Source,double RequestedML,const FVector& Position,const FVector& Velocity,const FVector& Gravity,double BirthDelay=0,const FTransform* RibbonTransform=nullptr);
    double EmitPour(ALiquidContainerActor* Source,double DeltaTime,const FTransform& PourTransform);
    void Simulate(double DeltaTime);
    double GetUnsettledFlightHorizon() const;
    UFUNCTION(BlueprintPure,Category="Liquid|Debug") FLiquidBalance GetBalance() const;
private:
    UPROPERTY() TArray<FLiquidPacket> Packets;
    TArray<TWeakObjectPtr<ALiquidContainerActor>> Containers;
    TMap<TWeakObjectPtr<ALiquidContainerActor>,FTransform> PreviousMouths;
    double InitialML=0,InjectedML=0,RemovedML=0,SpilledML=0;
    uint64 NextPacketId=1;
    bool bSimulating=false;
    static constexpr double StepSeconds=1.0/120.0;
    static constexpr int32 MaxPackets=4096;
};
