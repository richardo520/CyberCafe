// @trace HO-01 HO-02 HO-03 HO-04
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "OrderHologramActor.generated.h"
class UDrinkRecipe;
class AServingSpotActor;
class ALiquidContainerActor;
class UWidgetComponent;
class UStaticMeshComponent;
class UBoxComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UOrderHologramWidget;
class ATutorialNPC;
class AVRPawn;
class UDrinkMenuData;
class UTextRenderComponent;
struct FDrinkMenuEntry;
class AOrderHologramActor;
DECLARE_MULTICAST_DELEGATE_OneParam(FHologramMenuInteraction, AOrderHologramActor*);
USTRUCT(BlueprintType)
struct FHologramDeliveryRecord
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName RecipeId;
    UPROPERTY(BlueprintReadOnly) FText DrinkName;
    UPROPERTY(BlueprintReadOnly) double VolumeML=0;
    UPROPERTY(BlueprintReadOnly) FName CustomerId;
};
/** One world-space display, explicitly associated with a teaching order and serving spot. */
UCLASS(Blueprintable)
class CYBERCAFE_API AOrderHologramActor : public AActor
{
    GENERATED_BODY()
public:
    AOrderHologramActor();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram") TObjectPtr<UStaticMeshComponent> BaseMesh;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram") TObjectPtr<UStaticMeshComponent> ButtonMesh;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram") TObjectPtr<UStaticMeshComponent> StatusLamp;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram") TObjectPtr<UStaticMeshComponent> EmitterMesh;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram") TObjectPtr<UStaticMeshComponent> BeamMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual") TObjectPtr<UMaterialInterface> BeamMaterial;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram") TObjectPtr<UBoxComponent> ButtonZone;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram") TObjectPtr<UWidgetComponent> Panel;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual") TObjectPtr<UMaterialInterface> PanelMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual") TObjectPtr<UMaterialInterface> ShellMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual") TObjectPtr<UMaterialInterface> SignalMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual") TSubclassOf<UOrderHologramWidget> DisplayWidgetClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual",meta=(ClampMin="20",ClampMax="100")) float PanelWidthCM=60.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual") FVector PanelCenter=FVector(0,0,47);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual") FRotator PanelRotation=FRotator(12,0,0);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Visual",meta=(ClampMin="0")) float AnimationSeconds=.3f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Order",meta=(ClampMin="0")) float AutoCloseSeconds=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Customer") FName CustomerId=TEXT("TutorialGuide");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Customer") FText CustomerName;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Customer") TArray<FText> CustomerNotes;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Interaction") FName LeftTouchBone=TEXT("index_03_l");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Interaction") FName RightTouchBone=TEXT("index_03_r");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hologram|Interaction",meta=(ClampMin="0")) float TouchRearmMarginCM=3.f;
    // @trace MN-02. Browsing is presentation-only: never changes the active order.
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Hologram|Menu") TObjectPtr<UDrinkMenuData> RecipeMenu;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram|Menu") TObjectPtr<UBoxComponent> MenuModeZone;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram|Menu") TObjectPtr<UBoxComponent> MenuPreviousZone;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hologram|Menu") TObjectPtr<UBoxComponent> MenuNextZone;
    UFUNCTION(BlueprintCallable,Category="Hologram|Menu") bool ToggleRecipeMenu();
    UFUNCTION(BlueprintCallable,Category="Hologram|Menu") bool BrowseRecipeMenu(int32 Direction);
    UFUNCTION(BlueprintCallable,Category="Hologram|Menu") bool SelectRecipeByPinyin(FName Index);
    UFUNCTION(BlueprintPure,Category="Hologram|Menu") bool IsRecipeMenuMode() const {return bRecipeMenuMode;}
    UFUNCTION(BlueprintPure,Category="Hologram|Menu") int32 GetSelectedMenuIndex() const {return SelectedMenuIndex;}
    const FDrinkMenuEntry* GetSelectedMenuEntry() const;
    bool ProcessMenuTouchSample(EControllerHand Hand,const FVector& Point,bool Tracked=true,bool Holding=false);
    // @trace MT-01. Emitted only for accepted hand input, never for scripted selection.
    FHologramMenuInteraction OnMenuInteractionNative;
    UFUNCTION(BlueprintCallable,Category="Hologram") void PressButton();
    // Called after the teaching step has established the spot's final OrderSerial.
    UFUNCTION(BlueprintCallable,Category="Hologram") bool BeginOrder(UDrinkRecipe* Recipe,AServingSpotActor* Spot,ATutorialNPC* Customer,FText Name);
    UFUNCTION(BlueprintCallable,Category="Hologram") void CancelOrder(AServingSpotActor* Spot,int64 Serial);
    UFUNCTION(BlueprintPure,Category="Hologram") bool IsPanelOpen() const {return bOpen;}
    UFUNCTION(BlueprintPure,Category="Hologram") bool IsPlayerOpened() const {return bPlayerOpened;}
    UFUNCTION(BlueprintPure,Category="Hologram") bool IsPending() const {return bPending;}
    UFUNCTION(BlueprintPure,Category="Hologram") bool IsDelivered() const {return bDelivered;}
    UFUNCTION(BlueprintPure,Category="Hologram") bool IsAmberLit() const {return bPending&&!bOpen;}
    UFUNCTION(BlueprintPure,Category="Hologram") bool HasAutoOpened() const {return bAutoOpened;}
    UFUNCTION(BlueprintPure,Category="Hologram") UDrinkRecipe* GetCurrentRecipe() const {return CurrentRecipe;}
    UFUNCTION(BlueprintPure,Category="Hologram") int64 GetCurrentOrderSerial() const {return CurrentOrderSerial;}
    const TArray<FHologramDeliveryRecord>& GetDeliveryRecords() const {return DeliveryRecords;}
    // Uses the same geometric/debounce path as a tracked hand. Tests supply measured samples.
    bool ProcessTouchSample(EControllerHand Hand,const FVector& WorldPoint,bool bTracked=true,bool bHolding=false);
    UOrderHologramWidget* GetDisplayWidget() const;
private:
    UFUNCTION() void HandleServed(AServingSpotActor* Spot,ALiquidContainerActor* Cup);
    void SetOpen(bool bValue,bool bByPlayer);
    void RefreshDisplay();
    void UpdateVisual(float DeltaSeconds);
    void PollHands();
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MenuKeys;
    UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> MenuLabels;
    bool bRecipeMenuMode=false;
    int32 SelectedMenuIndex=0;
    bool MenuTouchLatched[4]={false,false,false,false};
    bool MenuLeftOutside[4]={true,true,true,true};
    bool MenuRightOutside[4]={true,true,true,true};
    UPROPERTY(Transient) TObjectPtr<UDrinkRecipe> CurrentRecipe;
    UPROPERTY(Transient) TWeakObjectPtr<AServingSpotActor> CurrentSpot;
    UPROPERTY(Transient) TWeakObjectPtr<ATutorialNPC> CurrentCustomer;
    UPROPERTY(Transient) TWeakObjectPtr<AVRPawn> TouchPawn;
    UPROPERTY(Transient) TArray<FHologramDeliveryRecord> DeliveryRecords;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LampMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> EmitterMID;
    uint32 CurrentOrderSerial=0;
    uint64 Generation=0;
    bool bOpen=false,bPlayerOpened=false,bAutoOpened=false,bPending=false,bDelivered=false;
    bool bTouchLatched=false,bLeftOutside=true,bRightOutside=true;
    float AnimationValue=0,CloseRemaining=-1,ButtonPressRemaining=0;
};
