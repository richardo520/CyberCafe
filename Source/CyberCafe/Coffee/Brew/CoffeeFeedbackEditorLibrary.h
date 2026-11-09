#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoffeeFeedbackEditorLibrary.generated.h"

class UParticleSystem;

/** 编辑器显式调用的诊断和素材桥；不会自动运行或修改源素材。 */
UCLASS()
class CYBERCAFE_API UCoffeeFeedbackEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** 返回原 Cascade 素材的当前字段及分布采样 JSON，不启动粒子模拟。 */
    UFUNCTION(BlueprintCallable, Category="Coffee|Feedback|Editor", meta=(DevelopmentOnly))
    static FString InspectSteam();

    /** 仅调整调用方已复制的指定落点蒸汽资产；全部校验通过后写入，返回 JSON，由调用方保存。 */
    UFUNCTION(BlueprintCallable, Category="Coffee|Feedback|Editor", meta=(DevelopmentOnly))
    static FString TunePourSteam(UParticleSystem* Target);

    /** 仅创建受限反馈路径中的独立循环 Cue；返回 JSON，由调用方显式保存。 */
    UFUNCTION(BlueprintCallable, Category="Coffee|Feedback|Editor", meta=(DevelopmentOnly))
    static FString CreateGrindLoopCue(const FString& PackagePath);
};
