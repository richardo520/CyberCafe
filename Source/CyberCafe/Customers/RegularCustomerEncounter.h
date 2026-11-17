#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/MenuPointerTarget.h"
#include "Serving/DrinkRecipe.h"
#include "RegularCustomerEncounter.generated.h"

class ATutorialNPC;
class AServingSpotActor;
class UWidgetComponent;
class URegularCustomerMemorySubsystem;
class URegularCustomerWidget;
class UCyberCafeEventSubsystem;
class ALiquidContainerActor;

UENUM(BlueprintType)
enum class ERegularCustomerStage : uint8 { Greeting, TopicChoice, Preparing, Tasting, Completed, Interrupted, Unavailable };

/** An opt-in authored encounter, independent of the tutorial director. No model integration yet. */
UCLASS()
class CYBERCAFE_API ARegularCustomerEncounter : public AActor, public IMenuPointerTarget
{
    GENERATED_BODY()
public:
    ARegularCustomerEncounter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<ATutorialNPC> Customer;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<AServingSpotActor> ServingSpot;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<UDrinkRecipe> Recipe;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<AOrderHologramActor> OrderDisplay;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") FName CustomerId = TEXT("Regular_NightShift_01");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") FText CustomerName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Regular Customer") bool bAutoStart = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Regular Customer|Panel") bool bFacePlayer = true;
    bool UpdatePanelFacing(const FVector& ViewLocation);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Regular Customer") TObjectPtr<UWidgetComponent> Panel;
    UFUNCTION(BlueprintCallable, Category="Regular Customer") bool StartEncounter();
    UFUNCTION(BlueprintCallable, Category="Regular Customer") bool Choose(int32 Index);
    UFUNCTION(BlueprintCallable, Category="Regular Customer") void CancelEncounter();
    UFUNCTION(BlueprintPure, Category="Regular Customer") ERegularCustomerStage GetStage() const { return Stage; }
    UFUNCTION(BlueprintPure, Category="Regular Customer") FText GetLine() const { return Line; }
    UFUNCTION(BlueprintPure, Category="Regular Customer") TArray<FText> GetChoices() const { return Choices; }
    const FDrinkValidationResult& GetAcceptedSnapshot() const { return Accepted; }
    virtual bool TracePointer(const FVector& Start, const FVector& Direction, double Limit, FMenuPointerHit& Hit) const override;
    virtual bool ActivatePointer(EControllerHand Hand, const FMenuPointerHit& Hit) override;
    int32 ActionAt(const FVector& Point) const;
private:
    bool BeginDrinkOrder();
    void Present(const FString& Text);
    void RefreshPanel();
    void FaceLocalPlayer();
    void Unbind();
    UFUNCTION() void HandleValidation(AServingSpotActor* Spot, ALiquidContainerActor* Cup, const FDrinkValidationResult& Result);
    UFUNCTION() void HandleFinished(ALiquidContainerActor* Cup, bool bCompleted, double ConsumedML);
    UFUNCTION() void HandlePour(AActor* From, AActor* To, float AcceptedML);
    UPROPERTY(Transient) TObjectPtr<URegularCustomerMemorySubsystem> Memory;
    UPROPERTY(Transient) TObjectPtr<UCyberCafeEventSubsystem> Events;
    UPROPERTY(Transient) TWeakObjectPtr<ALiquidContainerActor> AcceptedCup;
    UPROPERTY(Transient) FDrinkValidationResult Accepted;
    UPROPERTY(Transient) ERegularCustomerStage Stage = ERegularCustomerStage::Unavailable;
    UPROPERTY(Transient) FText Line;
    UPROPERTY(Transient) TArray<FText> Choices;
    FName Topic;
    FGuid Visit;
    uint64 LastClickFrame = MAX_uint64;
    double NextCommentTime = 0;
    bool bCommentedPour = false;
    bool bEnding = false;
    uint32 OwnedOrderSerial = 0;
};
