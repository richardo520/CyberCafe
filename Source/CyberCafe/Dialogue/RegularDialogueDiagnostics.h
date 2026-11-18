#pragma once
#include "CoreMinimal.h"

enum class ERegularDialogueSource : uint8 { Local, AI, Fallback };

/** Safe counters and fixed outcomes only: never credentials, URLs or provider response bodies. */
struct FRegularDialogueDiagnostics
{
    FString Model;
    FString LastRequestResult = TEXT("not_requested");
    double LastRequestSeconds = 0;
    double PendingSeconds = 0;
    int32 TotalRequests = 0;
    int32 RecentRequests = 0;
    int32 RequestLimit = 8;
    bool bPending = false;
};

/** Owned by the presenter, so a successful but discarded reply cannot relabel the visible line. */
struct FRegularDialogueLinkView
{
    ERegularDialogueSource Source = ERegularDialogueSource::Local;
    FString CurrentReason = TEXT("authored_line");
    bool bWaiting = false;
    bool bReadyForReading = false;
    bool bPlaceholder = false;
    FRegularDialogueDiagnostics Diagnostics;
};

namespace RegularDialogueDiagnostics
{
    FString ReasonLabel(const FString& Reason);
    FString SourceLabel(ERegularDialogueSource Source);
    ERegularDialogueSource SourceForResult(bool bGenerated, const FString& Reason);
}
