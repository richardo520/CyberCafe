// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LiquidContainerActor.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UGrabComponent;
class UNiagaraComponent;
class UNiagaraSystem;

/**
 * 液体变化事件
 * @param NewFillAmount 新的液面比例(0~1)
 * @param NewColor      当前液体颜色
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLiquidChangedSignature, float, NewFillAmount, FLinearColor, NewColor);

/**
 * FStr_Bottle
 * 酒瓶/酒杯的静态数据结构（对应 LiquidMaterials_VFXPack 中的 Str_Bottle）。
 * 用于数据表配置多种酒（外壳 Mesh + 液体 Mesh + 液体材质 + 容量 + 颜色 等）。
 */
USTRUCT(BlueprintType)
struct CYBERCAFE_API FStr_Bottle
{
    GENERATED_BODY()

    /** 液体显示名（如 "Red Wine"） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle")
    FName LiquidName = NAME_None;

    /** 容器外壳 Mesh（瓶身/杯身） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle")
    TObjectPtr<UStaticMesh> ContainerMesh = nullptr;

    /** 液体内芯 Mesh（酒瓶内部形状，写入 P_Liquid.User.Mesh） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle")
    TObjectPtr<UStaticMesh> LiquidMesh = nullptr;

    /** 液体材质（MI_Liquid_XX，写入 P_Liquid.User.Material） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle")
    TObjectPtr<UMaterialInterface> LiquidMaterial = nullptr;

    /** 液体内芯 Mesh 的包围盒大小（写入 P_Liquid.User.BottleSize，由美术手填） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle")
    FVector BottleSize = FVector(10.f, 10.f, 20.f);

    /** 初始液面 0~1 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InitialFill = 1.f;

    /** 最大容量 mL */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle", meta = (ClampMin = "0.0"))
    float MaxVolumeML = 750.f;

    /** 液体颜色（用于混色 & 出液流颜色） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Str_Bottle")
    FLinearColor LiquidColor = FLinearColor(0.35f, 0.05f, 0.08f, 1.f);
};

/**
 * ALiquidContainerActor
 * 液体容器基类：酒瓶(ABottleActor)与酒杯(ACupActor)的公共父类。
 *
 * 视觉表现层使用 LiquidMaterials_VFXPack 的 P_Liquid Niagara 系统，
 * 由该 NiagaraComponent 内部自己渲染液体网格。
 *
 * P_Liquid User 参数写入约定：
 *   - User.Mesh       (StaticMesh)   液体内芯模型
 *   - User.Material   (Material)     液体材质
 *   - User.BottleSize (Vector)       液体 Mesh 的包围盒尺寸
 *   - User.Fill       (Float 0~1)    当前液面
 *   - User.Opacity    (Float)        透明度
 *   - User.AddWaves   (Float)        波动强度
 *   - User.WavesScale (Float)        波动尺度
 *   - User.Viscosity  (Float)        粘稠度
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class CYBERCAFE_API ALiquidContainerActor : public AActor
{
    GENERATED_BODY()

public:
    ALiquidContainerActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 容器外壳(瓶身/杯身)，Root，模拟物理，用于抓取 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|Components")
    TObjectPtr<UStaticMeshComponent> ContainerMesh;

    /** 抓取组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    /** P_Liquid Niagara 组件：负责液体网格显示（替代原本自建的 LiquidMesh） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|Components")
    TObjectPtr<UNiagaraComponent> LiquidFX;

    /**
     * 出液粒子组件（预挂在容器上）。
     * Niagara Asset（P_Ribbon）与 Transform 均由美术在蓝图里直接在组件 Details 面板配置；
     * C++ 只在 BeginPlay 预写 User.* 参数，Tick 中控制 Activate/Deactivate。
     * 瓶子、杯子等所有可倒液容器共用此组件。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UNiagaraComponent> PourFX;

    //=====================================================================
    // Niagara 模板 & 资产（LiquidMaterials_VFXPack）
    //=====================================================================

    /** P_Liquid Niagara System 模板资产（子类蓝图指定 /Game/LiquidMaterials_VFXPack/Effects/P_Liquid） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Assets")
    TObjectPtr<UNiagaraSystem> LiquidFXTemplate;

    /** 液体内芯 Mesh（写入 P_Liquid.User.Mesh） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Assets")
    TObjectPtr<UStaticMesh> LiquidMeshAsset;

    /** 液体材质（MI_Liquid_XX，写入 P_Liquid.User.Material） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Assets")
    TObjectPtr<UMaterialInterface> LiquidMaterialAsset;

    /**
     * 材质里"液体颜色"参数的名字（Vector Parameter）。
     * 默认 "Liquid_Color01"（对应 LiquidMaterials_VFXPack 的 M_Liquid 的 00_GLOBAL 组）。
     * BeginPlay 时会从当前 LiquidMaterialAsset 读取该参数值并写入 LiquidColor，供 PourFX/Splash 使用。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Assets")
    FName LiquidColorParamName;

    /** 液体内芯 Mesh 的包围盒大小（写入 P_Liquid.User.BottleSize，由美术手填） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Assets")
    FVector BottleSize;

    //=====================================================================
    // 倒液（Pour）配置——瓶子/杯子共用
    //=====================================================================

    /** 是否允许当前容器"作为倒出方"倾斜出液。默认 true；如某些容器（如封口罐）不希望倒出，可关闭。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour")
    bool bCanPour;

    /**
     * 是否允许其他容器倒液进入本容器（作为"接液方"）。
     * 瓶子建议 false（水不会倒回瓶子），杯子建议 true。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour")
    bool bAcceptLiquidFromOthers;

    /**
     * 触发倒液的倾斜角度阈值(度)。
     * 容器局部 +Z 与世界 +Z 的夹角超过此值即出液。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour", meta = (ClampMin = "0.0", ClampMax = "180.0"))
    float PourAngleThreshold;

    /** 出液速率(mL/秒) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour", meta = (ClampMin = "0.0"))
    float PourRatePerSecond;

    /** 传入 P_Ribbon.User.FlowStrength 的水流强度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour", meta = (ClampMin = "0.0"))
    float FlowStrength;

    /** 从瓶口/杯口沿世界 -Z 方向的最大检测距离(cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour", meta = (ClampMin = "1.0"))
    float PourTraceDistance;

    /**
     * 检测用球体半径(cm)。
     * 由于 P_Ribbon 水流有弧度而 Trace 是直线，用一个"胖射线"(SphereTrace)去兜住水流落点范围。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour", meta = (ClampMin = "0.1"))
    float PourTraceRadius;

    /**
     * Trace 起点沿 PourFX 局部 +X 方向的前推距离(cm)，用于补偿水流的初速度水平位移。
     * 若发现水流是往前抛出去的、检测点却停留在正下方 → 调大此值让 Trace 也往前挪。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour")
    float PourTraceForwardOffset;

    /** 是否在编辑器中绘制倒液射线（调试用；打包版本自动关闭） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour|Debug")
    bool bDebugDrawTrace;

    //=====================================================================
    // 倒液（Pour）—— Niagara 资产 & FX 开关
    //=====================================================================

    /** 水花粒子 Niagara System 模板（P_Splash）——命中接液容器时将在命中点弹出 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour|FX")
    TObjectPtr<UNiagaraSystem> SplashEffectTemplate;

    /** 是否启用命中容器时的水花效果（P_Splash）。默认 true。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour|FX")
    bool bEnableSplash;

    /** 是否启用 P_Ribbon 内部的地面湿迹 Decal */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour|FX")
    bool bEnableDecal;

    /** 是否禁用 P_Ribbon 内部的小水花 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour|FX")
    bool bNoSplashes;

    /** 传入 P_Ribbon.User.NoList（默认 true，禁用它内部的 Bottle List） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Pour|FX")
    bool bNoList;

    //=====================================================================
    // 液体属性(编辑器可调 / 蓝图可读写)
    //=====================================================================

    /** 当前液面比例 0~1 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liquid", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FillAmount;

    /** 容器最大容量(毫升) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid", meta = (ClampMin = "0.0"))
    float MaxVolumeML;

    /** 当前液体颜色(RGB) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid")
    FLinearColor LiquidColor;

    //=====================================================================
    // P_Liquid 表现参数
    //=====================================================================

    /** 液体透明度（写入 P_Liquid.User.Opacity） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|FX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float LiquidOpacity;

    /** 液面波动幅度（写入 P_Liquid.User.AddWaves） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|FX", meta = (ClampMin = "0.0"))
    float AddWaves;

    /** 液面波动尺度（写入 P_Liquid.User.WavesScale） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|FX", meta = (ClampMin = "0.0"))
    float WavesScale;

    /** 液体粘稠度（写入 P_Liquid.User.Viscosity） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|FX", meta = (ClampMin = "0.0"))
    float Viscosity;

    //=====================================================================
    // 液面动态波动（根据容器运动强度实时调节 AddWaves）
    //=====================================================================

    /**
     * 是否启用动态波动：
     *   - true  ：静止时液面平静，运动/抓握时液面晃动（推荐）
     *   - false ：AddWaves 始终使用 IdleAddWaves 常量
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Waves")
    bool bDynamicWaves;

    /** 静止时的基础波动强度（0 = 完全平静） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Waves", meta = (ClampMin = "0.0", EditCondition = "bDynamicWaves"))
    float IdleAddWaves;

    /** 运动时能达到的最大波动强度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Waves", meta = (ClampMin = "0.0", EditCondition = "bDynamicWaves"))
    float MaxAddWaves;

    /** 线速度达到此值时波动达到最大值（cm/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Waves", meta = (ClampMin = "1.0", EditCondition = "bDynamicWaves"))
    float LinearVelocityRefCms;

    /** 角速度达到此值时波动达到最大值（度/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Waves", meta = (ClampMin = "1.0", EditCondition = "bDynamicWaves"))
    float AngularVelocityRefDegs;

    /** 波动上升平滑（值越大追随越快，1/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Waves", meta = (ClampMin = "0.1", EditCondition = "bDynamicWaves"))
    float WavesRiseSpeed;

    /** 波动衰减速度（值越大静下越快，1/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Waves", meta = (ClampMin = "0.1", EditCondition = "bDynamicWaves"))
    float WavesDecaySpeed;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 当前是否处于倒液状态 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Liquid|Runtime")
    bool bIsPouring;

    //=====================================================================
    // 事件
    //=====================================================================

    /** 液体变化时广播(增/减/换色) */
    UPROPERTY(BlueprintAssignable, Category = "Liquid|Events")
    FOnLiquidChangedSignature OnLiquidChanged;

    //=====================================================================
    // API
    //=====================================================================

    /**
     * 向容器加入液体。
     * 会按体积加权混合颜色：NewColor = (OldColor * OldML + InColor * AcceptedML) / TotalML
     *
     * @param DeltaML  希望加入的毫升数(>0)
     * @param InColor  加入液体的颜色
     * @return         实际接收的毫升数(受剩余容量限制)
     */
    UFUNCTION(BlueprintCallable, Category = "Liquid")
    float AddLiquid(float DeltaML, FLinearColor InColor);

    /**
     * 从容器消耗液体。
     *
     * @param DeltaML  希望消耗的毫升数(>0)
     * @return         实际消耗的毫升数(受剩余量限制)
     */
    UFUNCTION(BlueprintCallable, Category = "Liquid")
    float ConsumeLiquid(float DeltaML);

    /**
     * 将 FillAmount / 波动 / 粘稠度 等参数写入 P_Liquid 的 User 参数。
     * 注意：模式 A 下不改颜色（颜色由 Material 决定），
     *       但混色逻辑仍会更新 LiquidColor，供 P_Ribbon 出液流用。
     */
    UFUNCTION(BlueprintCallable, Category = "Liquid")
    void RefreshLiquidFX();

    /** 获取当前液体的体积(mL) */
    UFUNCTION(BlueprintPure, Category = "Liquid")
    float GetCurrentVolumeML() const { return FillAmount * MaxVolumeML; }

    /** 是否已被抓取(便于蓝图查询) */
    UFUNCTION(BlueprintPure, Category = "Liquid")
    bool IsHeld() const;

    /**
     * 每 Tick 调用：根据容器的线速度/角速度更新液面动态波动强度。
     * 由子类（如 ABottleActor）在自己的 Tick 中调用；基类默认不 Tick。
     */
    UFUNCTION(BlueprintCallable, Category = "Liquid|Waves")
    void UpdateDynamicWaves(float DeltaTime);

    /**
     * 运行时替换液体材质：更新 LiquidMaterialAsset 并同步给 P_Liquid.User.Material。
     * 用于"倒酒到杯子"时把瓶子的 MI 直接赋给杯子，让杯内液体立刻变成瓶内液体的外观。
     *
     * @param NewMaterial 新的液体材质（通常是 MI_Liquid_XX）
     * @param bReadColor  是否顺便从该材质读取 Liquid_Color01 并写入 LiquidColor
     */
    UFUNCTION(BlueprintCallable, Category = "Liquid")
    void SetLiquidMaterialAsset(UMaterialInterface* NewMaterial, bool bReadColor = true);

    /**
     * 从当前 LiquidMaterialAsset 读取 LiquidColorParamName 对应的 Vector 参数值，写入 LiquidColor。
     * 适用于 MaterialInstance——若参数没找到则不修改 LiquidColor。
     * @return 是否成功读取到颜色
     */
    UFUNCTION(BlueprintCallable, Category = "Liquid")
    bool TryReadColorFromMaterial();

protected:
    /** 初始化 LiquidFX：设置 P_Liquid 的 User.Mesh / User.Material / User.BottleSize，并首次同步一次表现参数 */
    void InitLiquidFX();

    /** 初始化 PourFX：写入 P_Ribbon 的 User.* 参数并默认关闭 */
    void InitPourFX();

    /** 获取出液口的世界变换 — 直接使用 PourFX 的当前 Transform（由美术在蓝图里配置） */
    UFUNCTION(BlueprintPure, Category = "Liquid|Pour")
    FTransform GetPourWorldTransform() const;

    /** 计算当前容器与世界+Z的夹角(度)：0=直立，180=完全倒置 */
    UFUNCTION(BlueprintPure, Category = "Liquid|Pour")
    float GetTiltAngleDegrees() const;

    /** 启动倒液 VFX / 状态 */
    virtual void StartPouring();

    /** 停止倒液 VFX / 状态 */
    virtual void StopPouring();

    /** 每 Tick 执行的倒液实际逻辑(FX 位置更新 + SphereTrace 判定 + 加/扣液) */
    virtual void UpdatePouring(float DeltaTime);

    /** 当前平滑后的动态波动强度（每 Tick 更新，写入 P_Liquid.User.AddWaves） */
    UPROPERTY(Transient)
    float CurrentDynamicWaves;

    /** 上一帧容器位置，用于非物理情况下估算线速度 */
    UPROPERTY(Transient)
    FVector PrevLocation;

    /** 上一帧容器旋转，用于非物理情况下估算角速度 */
    UPROPERTY(Transient)
    FQuat PrevRotation;
};
