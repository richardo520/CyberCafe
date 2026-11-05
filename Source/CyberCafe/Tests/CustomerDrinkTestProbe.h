#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CustomerDrinkTestProbe.generated.h"

class UCustomerDrinkComponent;
class ALiquidContainerActor;
class AServingSpotActor;
class ATutorialNPC;
class UTutorialStep;

enum class ECustomerDrinkProbeAction : uint8
{
    None,
    Cancel,
    DestroyCup,
    DestroyNPC,
    ChangeOrder
};

// 独立反射头与 .cpp，供 UHT 生成动态委托接收器；不放入关卡、不保存资产。
UCLASS(Transient, NotBlueprintable)
class ACustomerDrinkTestProbe : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY() TWeakObjectPtr<UCustomerDrinkComponent> Component;
    UPROPERTY() TWeakObjectPtr<ALiquidContainerActor> Cup;
    UPROPERTY() TWeakObjectPtr<AServingSpotActor> Spot;
    UPROPERTY() TWeakObjectPtr<ATutorialNPC> NPC;
    UPROPERTY() TWeakObjectPtr<ALiquidContainerActor> EndedCup;
    UPROPERTY() TWeakObjectPtr<ALiquidContainerActor> ServedCup;
    UPROPERTY() TWeakObjectPtr<AServingSpotActor> ServedSpot;
    UPROPERTY() TWeakObjectPtr<UTutorialStep> CompletedStep;

    ECustomerDrinkProbeAction Action = ECustomerDrinkProbeAction::None;
    int32 ServedCount = 0;
    int32 EndedCount = 0;
    int32 LiquidChangedCount = 0;
    int32 StepCompletedCount = 0;
    double EndedConsumedML = 0;
    double LiquidConsumedML = 0;
    double LiquidRemainingML = 0;
    double LiquidRemovedML = 0;
    bool bEndedCompleted = false;
    bool bCommittedBeforeLiquidCallback = false;
    bool bAttemptRestartOnEnded = false;
    bool bRestartAccepted = false;
    bool bInactiveAtEnded = false;

    UFUNCTION() void HandleEnded(ALiquidContainerActor* InCup, bool bCompleted, double ConsumedML);
    UFUNCTION() void HandleLiquidChanged(float Fill, FLinearColor Color);
    UFUNCTION() void HandleServed(AServingSpotActor* InSpot, ALiquidContainerActor* InCup);
    UFUNCTION() void HandleStepCompleted(UTutorialStep* InStep);
};
