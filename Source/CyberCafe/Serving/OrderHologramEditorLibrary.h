#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OrderHologramEditorLibrary.generated.h"
class AOrderHologramActor;
class UWorld;
UCLASS()
class UOrderHologramEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable,Category="Hologram|Editor")
    static AOrderHologramActor* AddDisplay(UWorld* World,TSubclassOf<AOrderHologramActor> DisplayClass,FVector Location,FRotator Rotation);
    UFUNCTION(BlueprintCallable,Category="Hologram|Editor")
    static bool CaptureDisplay(AOrderHologramActor* Display,const FString& Filename);
    UFUNCTION(BlueprintCallable,Category="Hologram|Editor")
    static bool EmptyWidgetBlueprint(UObject* Blueprint,bool bReset);
    UFUNCTION(BlueprintCallable,Category="Hologram|Editor")
    static FString InspectPlacement(AOrderHologramActor* Display);
#endif
};
