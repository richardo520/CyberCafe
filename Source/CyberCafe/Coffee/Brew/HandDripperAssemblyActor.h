#pragma once

#include "CoreMinimal.h"
#include "Coffee/Brew/HandDripperActor.h"
#include "HandDripperAssemblyActor.generated.h"

class UStaticMeshComponent;

/** 滤杯与四件支架共用原 ContainerMesh 根和唯一的 GrabComp。 */
UCLASS(Blueprintable)
class CYBERCAFE_API AHandDripperAssemblyActor : public AHandDripperActor
{
    GENERATED_BODY()

public:
    AHandDripperAssemblyActor();
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Coffee|Assembly")
    TObjectPtr<UStaticMeshComponent> StandLeftLeg;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Coffee|Assembly")
    TObjectPtr<UStaticMeshComponent> StandRightLeg;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Coffee|Assembly")
    TObjectPtr<UStaticMeshComponent> StandLeftShelf;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Coffee|Assembly")
    TObjectPtr<UStaticMeshComponent> StandRightShelf;

    // 安装复制后重新应用组合抓取约束，不改冲煮参数、库存或网格碰撞形状。
    void ApplyAssemblyGrabSettings();
};
