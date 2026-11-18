#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RegularDialogueSettings.generated.h"

UENUM()
enum class ERegularDialogueProvider : uint8 { OpenAI, MiniMax };

/** Public request settings only. Credentials are read from the selected provider's environment. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Regular Customer Cloud Dialogue"))
class CYBERCAFE_API URegularDialogueSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Cloud Dialogue") bool bEnabled = true;
    UPROPERTY(Config, EditAnywhere, Category="Cloud Dialogue") ERegularDialogueProvider Provider = ERegularDialogueProvider::MiniMax;
    UPROPERTY(Config, EditAnywhere, Category="Cloud Dialogue") FString Model = TEXT("MiniMax-M3.1-Flash");
    UPROPERTY(Config, EditAnywhere, Category="Cloud Dialogue") FString Endpoint = TEXT("https://api.minimax.cn/v1/responses");
    UPROPERTY(Config, EditAnywhere, Category="Cloud Dialogue", meta=(ClampMin="1", ClampMax="15")) float DeadlineSeconds = 6;
    UPROPERTY(Config, EditAnywhere, Category="Cloud Dialogue", meta=(ClampMin="256", ClampMax="2048")) int32 MaxOutputTokens = 1024;
    UPROPERTY(Config, EditAnywhere, Category="Cloud Dialogue", meta=(ClampMin="1", ClampMax="12")) int32 MaxRequestsPerMinute = 8;
    const TCHAR* CredentialVariable() const { return Provider == ERegularDialogueProvider::MiniMax ? TEXT("MINIMAX_API_KEY") : TEXT("OPENAI_API_KEY"); }
};
