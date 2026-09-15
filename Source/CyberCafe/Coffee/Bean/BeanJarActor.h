// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeanJarActor.generated.h"

class UStaticMeshComponent;
class UGrabComponent;
class ABeanJarLidActor;

/**
 * ABeanJarActor
 * 咖啡豆罐：一个装咖啡豆的玻璃罐子，可被 VR 手柄自由抓取（Free 模式）。
 *
 * 组件构成：
 *   - JarMesh    ：罐子主体（Root，模拟物理，包含一个"LidSocket"用于吸附罐盖）。
 *   - BeanPileMesh：罐内可见豆堆。随剩余豆量 (CurrentBeanGrams / TotalBeanCapacityGrams) 线性缩放 Z。
 *   - GrabComp   ：抓取组件（Free，允许玩家把罐子拿起）。
 *
 * BeginPlay 时按 LidClass 生成一个 ABeanJarLidActor 并 Attach 到 LidSocket。
 * 玩家把盖子拔起 → IsOpen() = true → 勺子伸进来时才能真正舀走豆子。
 */
UCLASS(Blueprintable, BlueprintType)
class CYBERCAFE_API ABeanJarActor : public AActor
{
    GENERATED_BODY()

public:
    ABeanJarActor();

    virtual void BeginPlay() override;

    //=====================================================================
    // 组件
    //=====================================================================

    /** 罐身 Mesh（Root，模拟物理，包含 LidSocket） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jar|Components")
    TObjectPtr<UStaticMeshComponent> JarMesh;

    /**
     * 罐内可见豆堆 Mesh（蓝图里指派 StaticMesh，拖到罐内合适高度）。
     * 随豆量线性缩放 Z（Ratio = CurrentBeanGrams / TotalBeanCapacityGrams）；
     * 无豆时自动隐藏。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jar|Components")
    TObjectPtr<UStaticMeshComponent> BeanPileMesh;

    /** 抓取组件（Free） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jar|Components")
    TObjectPtr<UGrabComponent> GrabComp;

    //=====================================================================
    // 配置
    //=====================================================================

    /** 罐盖 Actor 类（蓝图里指派为 BP_BeanJarLid 之类） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jar|Lid")
    TSubclassOf<ABeanJarLidActor> LidClass;

    /**
     * 罐口 Socket 名（在 JarMesh 的 StaticMesh 里定义，例如 "LidSocket"）。
     * 盖子会 Attach 到该 Socket；拔盖距离计算也基于该 Socket。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jar|Lid")
    FName LidSocketName;

    /** 罐子的总豆量上限 (g) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jar|Beans", meta = (ClampMin = "0.0"))
    float TotalBeanCapacityGrams;

    /** 初始豆量填充比例 (0~1)。默认 1.0 = 满罐 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jar|Beans", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InitialFillRatio;

    //=====================================================================
    // 运行时
    //=====================================================================

    /** 生成的罐盖实例 */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Jar|Runtime")
    TObjectPtr<ABeanJarLidActor> LidActor;

    /** 当前剩余豆量 (g) */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "Jar|Runtime")
    float CurrentBeanGrams;

    //=====================================================================
    // API
    //=====================================================================

    /** 便捷访问：罐身 Mesh（给盖子 / 勺子做距离与 Socket 查询） */
    UFUNCTION(BlueprintPure, Category = "Jar")
    UStaticMeshComponent* GetJarMesh() const { return JarMesh; }

    /** 罐是否已开盖（询问 LidActor） */
    UFUNCTION(BlueprintPure, Category = "Jar")
    bool IsOpen() const;

    /**
     * 尝试从罐子里舀出至多 MaxGrams 的豆。
     * 仅在开盖状态下有效。
     * @return 实际舀出的豆量 (g)。
     */
    UFUNCTION(BlueprintCallable, Category = "Jar")
    float TryScoopBeans(float MaxGrams);

    /** 便捷查询：罐口 Socket 世界位置（勺子做 Overlap 中心可参考） */
    UFUNCTION(BlueprintPure, Category = "Jar")
    FVector GetLidSocketWorldLocation() const;

protected:
    /** 缓存 BeanPileMesh 编辑器里的初始 Scale（供运行时按比例缩 Z） */
    UPROPERTY(Transient)
    FVector BeanPileInitialScale;

    /** 根据 CurrentBeanGrams / TotalBeanCapacityGrams 刷新 BeanPileMesh 的 Z 缩放与可见性 */
    void UpdateBeanPileVisual();
};
