// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LiquidContainerActor.generated.h"

class UStaticMeshComponent;
class UGrabComponent;
class UMaterialInstanceDynamic;

/**
 * 液体变化事件
 * @param NewFillAmount 新的液面比例(0~1)
 * @param NewColor      当前液体颜色
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLiquidChangedSignature, float, NewFillAmount, FLinearColor, NewColor);

/**
 * ALiquidContainerActor
 * 液体容器基类：酒瓶(ABottleActor)与酒杯(ACupActor)的公共父类。
 *
 * 提供：
 *   - 容器外壳 Mesh (ContainerMesh，物理模拟目标 & 抓取根)
 *   - 液体 Mesh    (LiquidMesh，附加在容器内部，禁止碰撞)
 *   - 抓取组件    (UGrabComponent)
 *   - 液体状态：FillAmount(0~1) / LiquidColor / MaxVolumeML
 *   - API：AddLiquid / ConsumeLiquid / RefreshLiquidMaterial
 *   - 事件：OnLiquidChanged(蓝图可绑)
 *
 * 液体材质约定的参数名(Scalar/Vector Parameter)：
 *   - Scalar : FillAmount     -- 液面比例 0~1
 *   - Scalar : Opacity        -- 液体透明度(默认 0.85)
 *   - Scalar : WaveAmplitude  -- 液面波动幅度(cm)
 *   - Scalar : WaveFrequency  -- 液面波动频率
 *   - Scalar : ContainerHeight -- 容器内高度(cm，运行时自动写入)
 *   - Vector : LiquidColor    -- 液体颜色(RGB)
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class CYBERCAFE_API ALiquidContainerActor : public AActor
{
    GENERATED_BODY()

public:
    ALiquidContainerActor();

    virtual void BeginPlay() override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 容器外壳(瓶身/杯身)，Root，模拟物理，用于抓取 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|Components")
    TObjectPtr<UStaticMeshComponent> ContainerMesh;

    /** 内部液体 Mesh(自定义模型：酒瓶/酒杯内部形状)，无碰撞 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|Components")
    TObjectPtr<UStaticMeshComponent> LiquidMesh;

    /** 抓取组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Liquid|Components")
    TObjectPtr<UGrabComponent> GrabComp;

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

    /** 液体材质透明度参数(默认写入 MID) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float LiquidOpacity;

    /** 液面波动幅度(cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Material", meta = (ClampMin = "0.0"))
    float WaveAmplitude;

    /** 液面波动频率 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Material", meta = (ClampMin = "0.0"))
    float WaveFrequency;

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

    /** 将 FillAmount / LiquidColor / 波动等参数写回 LiquidMID */
    UFUNCTION(BlueprintCallable, Category = "Liquid")
    void RefreshLiquidMaterial();

    /** 获取当前液体的体积(mL) */
    UFUNCTION(BlueprintPure, Category = "Liquid")
    float GetCurrentVolumeML() const { return FillAmount * MaxVolumeML; }

    /** 是否已被抓取(便于蓝图查询) */
    UFUNCTION(BlueprintPure, Category = "Liquid")
    bool IsHeld() const;

protected:
    /** LiquidMesh 用来创建的动态材质实例(每个 Slot 一个) */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> LiquidMIDs;

    /** LiquidMesh 局部包围盒的高度(cm)，用于材质裁剪 */
    UPROPERTY(Transient)
    float ContainerHeight;

    /** 初始化 LiquidMesh 的 MID 并写入初始参数 */
    void InitLiquidMaterials();
};
