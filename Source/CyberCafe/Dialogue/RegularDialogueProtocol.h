#pragma once
#include "CoreMinimal.h"

/** Immutable authored facts copied before any asynchronous work. No UObject references. */
struct FRegularDialogueContext
{
    FString AuthoredLine;
    FString CustomerId;
    FString Cue;
    TMap<FString, FString> Facts;
    TArray<FString> RequiredTerms;
    bool bHasPastVisit = false;
    bool bObservedGrinding = false;
};

struct FRegularDialogueProtocol
{
    static FString BuildRequest(const FRegularDialogueContext& Context, const FString& Model, int32 MaxTokens, bool bMiniMax = false);
    static bool ParseResponse(const FString& Body, const FRegularDialogueContext& Context, FString& OutLine, FString& OutReason);
    static bool ValidateLine(const FString& Text, const FRegularDialogueContext& Context, FString& OutReason);
};
