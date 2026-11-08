#pragma once

#include "CoreMinimal.h"
#include "Liquid/BottleActor.h"
#include "DirectBeanJarActor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class ACoffeeGrinderActor;

/** 跨工作台替换周期的克数账本；粒子数量和液体毫升不参与账本。 */
USTRUCT(BlueprintType)
struct CYBERCAFE_API FBeanJarLedgerSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Beans|Ledger")
    double InitialGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "Beans|Ledger")
    double InjectedGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "Beans|Ledger")
    double AcceptedGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "Beans|Ledger")
    double SpilledGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "Beans|Ledger")
    double DiscardedGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "Beans|Ledger")
    double CurrentGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "Beans|Ledger")
    double TotalPouredGrams = 0.0;
};

/** 继承瓶体只为兼容抓取、GlassBreakable 与恢复盖；豆库存始终仅以 g 计。 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ADirectBeanJarActor : public ABottleActor
{
    GENERATED_BODY()

public:
    ADirectBeanJarActor();
    virtual bool SupportsLiquidInventory() const override { return false; }
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Destroyed() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beans|Components")
    TObjectPtr<UStaticMeshComponent> BeanPileMesh;
    /** 可在新蓝图上指定小颗单豆网格；最多 24 个纯视觉实例，无独立物理体。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beans|Components")
    TObjectPtr<UInstancedStaticMeshComponent> BeanStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Supply", meta = (ClampMin = "0.0"))
    float BeanCapacityGrams = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Supply", meta = (ClampMin = "0.0"))
    float InitialBeanGrams = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Pour", meta = (ClampMin = "0.0"))
    float BeanPourRateGramsPerSecond = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Pour", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float PourBeanTiltDegrees = 65.f;
    /** 世界厘米，不随模型缩放改变射程。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Pour", meta = (ClampMin = "0.0"))
    float MaxBeanTravelCM = 40.f;
    /** ContainerMesh 的局部坐标，随新蓝图网格尺寸和缩放配置。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Pour")
    FVector MouthLocal = FVector(0.f, 0.f, 19.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Pour", meta = (ClampMin = "0.0"))
    float OpeningRadiusCM = 3.2f;
    /** 磨豆机 BeanEntryPoint 平面上的世界厘米半径，不乘入口组件 Scale。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Pour", meta = (ClampMin = "0.0"))
    float BeanEntryRadiusCM = 3.5f;
    /** Intake contact region above BeanEntryPoint for the existing coarse grinder collision. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beans|Pour", meta = (ClampMin = "0.0"))
    float HopperIntakeHeightCM = 20.f;

    UPROPERTY(BlueprintReadOnly, Transient, Category = "Beans|Ledger")
    double CurrentBeanGrams = 0.0;
    /** 实际首次初始库存，独立于后续编辑器配置变化。 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Beans|Ledger")
    double InitialStockGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Beans|Ledger")
    double RestoredBeanGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Beans|Ledger")
    double TotalPouredGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Beans|Ledger")
    double TotalAcceptedGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Beans|Ledger")
    double SpilledBeanGrams = 0.0;
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Beans|Ledger")
    double DiscardedBeanGrams = 0.0;

    /** BeginPlay 或显式调用均只初始化一次。 */
    UFUNCTION(BlueprintCallable, Category = "Beans")
    void InitializeBeans();
    UFUNCTION(BlueprintPure, Category = "Beans")
    double GetBeanGrams() const;
    /** 测试可不持罐调用，但仍必须满足真实姿态、开盖、未破碎/冻结/恢复状态。 */
    UFUNCTION(BlueprintCallable, Category = "Beans")
    float PourBeans(float DeltaTime);
    UFUNCTION(BlueprintPure, Category = "Beans|Ledger")
    double GetBeanBalanceError() const;
    /** 管理器必须在销毁旧 Jar 前捕获，之后不再依赖旧 Actor 的生命周期。 */
    UFUNCTION(BlueprintPure, Category = "Beans|Ledger")
    FBeanJarLedgerSnapshot GetBeanLedger() const;
    /** 新 Jar BeginPlay 后调用一次：弃置旧 Current，Injected 增加实际容量内的新供给。
     *  覆盖新 Actor 默认初始化账本，不再计入默认的 150g；非法账本/重复调用返回 false。
     */
    UFUNCTION(BlueprintCallable, Category = "Beans|Workbench")
    bool ApplyRestoredBeanLedger(const FBeanJarLedgerSnapshot& Snapshot, double NewSupplyGrams);
    /** 恢复调用期可显式阻止豆发射，和工作台公共护栏同时生效。 */
    UFUNCTION(BlueprintCallable, Category = "Beans|Workbench")
    void SetBeanRestoreBlocked(bool bBlocked);

protected:
    /** 不进入基类液体发射路径。 */
    virtual void StartPouring() override;
    virtual void UpdatePouring(float DeltaTime) override;

private:
    UFUNCTION()
    void HandleJarBroken(ALiquidContainerActor* Container);
    bool CanPourBeans() const;
    FVector GetBeanMouthWorld() const;
    ACoffeeGrinderActor* FindBeanReceiver(const FVector& Start, const FVector& End, FVector& Impact) const;
    void DisableLiquidPresentation();
    void UpdateBeanPileVisual();
    void UpdateBeanStream(const FVector& Start, const FVector& End, float DeltaTime);
    void ClearBeanStream();
    void SettleRemainingBeans();

    UPROPERTY(Transient)
    FVector BeanPileInitialLocation = FVector::ZeroVector;
    UPROPERTY(Transient)
    FVector BeanPileInitialScale = FVector::OneVector;
    bool bBeansInitialized = false;
    bool bBeanVisualInitialized = false;
    bool bRestoredLedgerApplied = false;
    bool bBeanRestoreBlocked = false;
    bool bBeanTransferInProgress = false;
    double BeanStreamPhase = 0.0;
};
