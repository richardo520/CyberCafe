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

    /** 液体内芯 Mesh 的包围盒大小（写入 P_Liquid.User.BottleSize，由美术手填） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Liquid|Assets")
    FVector BottleSize;

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

protected:
    /** 初始化 LiquidFX：设置 P_Liquid 的 User.Mesh / User.Material / User.BottleSize，并首次同步一次表现参数 */
    void InitLiquidFX();
};
