#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GlassEditorLibrary.generated.h"
class UWorld;class UGlassBreakProfile;class AGlassRecoveryActor;
UCLASS()
class CYBERCAFE_API UGlassEditorLibrary:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
#if WITH_EDITOR
 UFUNCTION(BlueprintCallable) static AGlassRecoveryActor* InstallPrototype(UWorld* World,UGlassBreakProfile* CupProfile,UGlassBreakProfile* BottleProfile);
#endif
};
