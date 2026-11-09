#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "MenuPointerComponent.generated.h"

class AVRPawn;
class AOrderHologramActor;
class UEnhancedPlayerInput;
class UInputAction;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMotionControllerComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FMenuPointerSample;
struct FMenuPointerInputSample;

/** 独立菜单指示器：只读手部状态，只有屏幕接受动作才消费原扳机事件。 */
UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent))
class CYBERCAFE_API UMenuPointerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMenuPointerComponent();

    void UpdatePointer(float DeltaTime);
    bool TryClickFromOriginalTrigger();

    UFUNCTION(BlueprintPure, Category = "VR|MenuPointer|Diagnostics")
    FString GetPointerStatus(EControllerHand Hand) const;

    UFUNCTION(BlueprintPure, Category = "VR|MenuPointer|Diagnostics")
    FString GetLastClickStatus() const { return LastClickStatus; }

    UFUNCTION(BlueprintPure, Category = "VR|MenuPointer|Diagnostics")
    int32 GetAcceptedClickCount() const { return AcceptedClickCount; }

    UFUNCTION(BlueprintPure, Category = "VR|MenuPointer|Diagnostics")
    int32 GetOriginalTriggerAttemptCount() const { return OriginalTriggerAttemptCount; }

    UFUNCTION(BlueprintPure, Category = "VR|MenuPointer|Diagnostics")
    UStaticMeshComponent* GetBeam(EControllerHand Hand) const;

    UFUNCTION(BlueprintPure, Category = "VR|MenuPointer|Diagnostics")
    UStaticMeshComponent* GetEndpoint(EControllerHand Hand) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|MenuPointer")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|MenuPointer", meta = (ClampMin = "1", ClampMax = "250"))
    float RayMaxDistance = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|MenuPointer", meta = (ClampMin = "0.01"))
    float BeamDiameterCM = .12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|MenuPointer", meta = (ClampMin = "0.01"))
    float EndpointDiameterCM = .7f;

    /** 可选 Unlit 材质：Tint × 4。不存在时不加载、不报缺失资源错误。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|MenuPointer")
    TSoftObjectPtr<UMaterialInterface> RayMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|MenuPointer")
    FName TintParameterName = TEXT("Tint");

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
    bool ReadOriginalTriggerInput(const AVRPawn* Pawn, FMenuPointerInputSample& InputSample) const;
    void RememberOriginalTriggerInput(const FMenuPointerInputSample& InputSample);
    bool ResolveOriginalTriggerHand(const AVRPawn* Pawn, const FMenuPointerInputSample& InputSample,
        bool& bRightHand, FString& Reason) const;
    void CachePointerStatus(int32 HandIndex, const FMenuPointerSample& Sample);
    bool SamplePointer(const AVRPawn* Pawn, bool bRightHand, FMenuPointerSample& Sample) const;
    UMotionControllerComponent* FindPointerOrigin(const AVRPawn* Pawn, bool bRightHand) const;
    void EnsureVisuals();
    void DrawPointer(int32 HandIndex, const FMenuPointerSample& Sample);
    void HidePointer(int32 HandIndex);
    void DestroyVisuals();

    // 引擎资源硬引用保证 Cook 收集；不引用旧射线、输入或蓝图资产。
    UPROPERTY()
    TObjectPtr<UStaticMesh> BeamMesh;

    UPROPERTY()
    TObjectPtr<UStaticMesh> EndpointMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> FallbackMaterial;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> Beams;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> Endpoints;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> PointerMaterials;

    UPROPERTY(Transient)
    FString StatusStrings[2];

    UPROPERTY(Transient)
    FString LastClickStatus;

    UPROPERTY(Transient)
    FString LastOriginalMappingKeys;

    UPROPERTY(Transient)
    int32 AcceptedClickCount = 0;

    UPROPERTY(Transient)
    int32 OriginalTriggerAttemptCount = 0;

    TWeakObjectPtr<const UEnhancedPlayerInput> PriorInput;
    TWeakObjectPtr<const UInputAction> PriorSkipAction;
    FString PriorMappingKeys;
    float HandPriorAxes[2] = {0.f, 0.f}; // 缓存各手阈值归一化值，不建立第二条输入事件链。
    bool bHasPriorAxes = false;
    bool bEndingPlay = false;
    bool bClickInProgress = false;

    TWeakObjectPtr<AOrderHologramActor> FlashScreens[2];
    float AcceptedFlashRemaining[2] = {0.f, 0.f};
};
