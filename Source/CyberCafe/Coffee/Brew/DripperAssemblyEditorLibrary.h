#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DripperAssemblyEditorLibrary.generated.h"

class AHandDripperAssemblyActor;
class UWorld;

/** 只在显式调用时替换当前编辑器世界的指定两件器具；保存由调用方负责。 */
UCLASS()
class CYBERCAFE_API UDripperAssemblyEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** 复制当前实例配置与支架世界变换，通过校验并重映射引用后才销毁原对象。 */
    UFUNCTION(BlueprintCallable, Category="Coffee|Assembly|Editor", meta=(DevelopmentOnly))
    static bool InstallAssembly(UWorld* World, TSubclassOf<AHandDripperAssemblyActor> NewClass);
};
