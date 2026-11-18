#include "Dialogue/RegularDialogueClient.h"
#include "Dialogue/RegularDialogueSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "CoreGlobals.h"
#include "Misc/App.h"

namespace
{
class FRegularHttpTransport final : public IRegularDialogueTransport
{
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request;
public:
    virtual bool Send(const FString& URL, const FString& Key, const FString& Body, float Timeout, FRegularTransportCompletion Done) override
    {
        Cancel();
        Request = FHttpModule::Get().CreateRequest();
        Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
        Request->SetURL(URL); Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Key);
        Request->SetContentAsString(Body); Request->SetTimeout(Timeout);
        Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Done)](FHttpRequestPtr, FHttpResponsePtr Response, bool Connected)
            { Callback(Response ? Response->GetResponseCode() : 0, Response ? Response->GetContentAsString() : FString(), Connected); });
        return Request->ProcessRequest();
    }
    virtual void Cancel() override
    {
        if (Request) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
    }
    virtual ~FRegularHttpTransport() override { Cancel(); }
};
}

TSharedPtr<IRegularDialogueTransport> CreateRegularDialogueHttpTransport() { return MakeShared<FRegularHttpTransport>(); }

URegularDialogueClient::URegularDialogueClient()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
}

double URegularDialogueClient::Now() const
{
#if WITH_DEV_AUTOMATION_TESTS
    if (TestClock) return TestClock();
#endif
    return FPlatformTime::Seconds();
}

void URegularDialogueClient::Generate(const FRegularDialogueContext& Context, FRegularDialogueCompletion Done)
{
    Cancel();
    const auto* Settings = GetDefault<URegularDialogueSettings>();
    const double RequestTime = Now();
    RequestTimes.RemoveAll([RequestTime](double Sent) { return RequestTime - Sent >= 60.; });
    FString Key = FPlatformMisc::GetEnvironmentVariable(Settings->CredentialVariable());
    bool bAllowed = Settings->bEnabled && FPlatformMisc::GetEnvironmentVariable(TEXT("CYBERCAFE_DISABLE_CLOUD_DIALOGUE")) != TEXT("1")
        && !IsRunningCommandlet() && !FApp::IsUnattended() && !GIsAutomationTesting;
#if UE_BUILD_SHIPPING
    bAllowed = false; // A distributed client needs a game backend, not a shared provider credential.
#endif
#if WITH_DEV_AUTOMATION_TESTS
    if (bTestTransport) { bAllowed = true; Key = TEXT("unit-test-key"); }
#endif
    FString Unavailable;
    if (!bAllowed) Unavailable = TEXT("disabled_or_test_run");
    else if (Key.IsEmpty()) Unavailable = TEXT("missing_key");
    else if (Key.Contains(TEXT("\r")) || Key.Contains(TEXT("\n"))) Unavailable = TEXT("invalid_key_format");
    else if (!Settings->Endpoint.StartsWith(TEXT("https://")) || Settings->Model.IsEmpty() || !FMath::IsFinite(Settings->DeadlineSeconds)) Unavailable = TEXT("invalid_configuration");
    else if (RequestTimes.Num() >= FMath::Clamp(Settings->MaxRequestsPerMinute, 1, 12)) Unavailable = TEXT("minute_budget");
    else if (Now() < CooldownUntil) Unavailable = TEXT("provider_cooldown");
    if (!Unavailable.IsEmpty())
    {
        if (LastResult != Unavailable) UE_LOG(LogTemp, Log, TEXT("[RegularDialogue] using authored dialogue; reason=%s"), *Unavailable);
        LastResult = Unavailable;
        if (Done) Done(Context.AuthoredLine, false);
        return;
    }
    if (!Transport) Transport = CreateRegularDialogueHttpTransport();
    ActiveContext = Context; Completion = MoveTemp(Done); bPending = true;
    const uint64 Request = Generation;
    const float Timeout = FMath::Clamp(Settings->DeadlineSeconds, 1.f, 15.f);
    Deadline = Now() + Timeout;
    RequestTimes.Add(RequestTime); TotalRequests = FMath::Min(TotalRequests, MAX_int32 - 1) + 1;
    RequestStarted = RequestTime;
    SetComponentTickEnabled(true); LastResult = TEXT("pending");
    TWeakObjectPtr<URegularDialogueClient> Weak(this);
    const bool Started = Transport->Send(Settings->Endpoint, Key, FRegularDialogueProtocol::BuildRequest(Context, Settings->Model, Settings->MaxOutputTokens,
        Settings->Provider == ERegularDialogueProvider::MiniMax), Timeout,
        [Weak, Request](int32 Code, const FString& Body, bool Connected)
        { if (Weak.IsValid()) Weak->Receive(Request, Code, Body, Connected); });
    if (!Started) Finish(Request, Context.AuthoredLine, false, TEXT("transport_not_started"));
}

void URegularDialogueClient::Receive(uint64 Request, int32 Status, const FString& Body, bool bConnected)
{
    if (!bPending || Request != Generation) return;
    if (Now() > Deadline) { Finish(Request, ActiveContext.AuthoredLine, false, TEXT("timeout")); return; }
    if (!bConnected || Status != 200)
    {
        CooldownUntil = Now() + ((Status == 401 || Status == 403 || Status == 429) ? 60 : 10);
        Finish(Request, ActiveContext.AuthoredLine, false, FString::Printf(TEXT("http_%d"), Status));
        return;
    }
    FString Text, Reason;
    const bool Valid = FRegularDialogueProtocol::ParseResponse(Body, ActiveContext, Text, Reason);
    Finish(Request, Valid ? Text : ActiveContext.AuthoredLine, Valid, Reason);
}

void URegularDialogueClient::Finish(uint64 Request, const FString& Text, bool bGenerated, const FString& Reason)
{
    if (!bPending || Request != Generation) return;
    LastRequestSeconds = FMath::Max(0., Now() - RequestStarted); LastRequestResult = Reason;
    bPending = false; SetComponentTickEnabled(false); LastResult = Reason;
    UE_LOG(LogTemp, Log, TEXT("[RegularDialogue] result=%s generated=%d request=%llu"), *Reason, bGenerated, Request);
    FRegularDialogueCompletion Done = MoveTemp(Completion);
    if (Done) Done(Text, bGenerated);
}

void URegularDialogueClient::Cancel()
{
    if (bPending) { LastRequestSeconds = FMath::Max(0., Now() - RequestStarted); LastRequestResult = TEXT("cancelled"); }
    ++Generation; bPending = false; Completion = nullptr; SetComponentTickEnabled(false);
    if (Transport) Transport->Cancel();
}

FRegularDialogueDiagnostics URegularDialogueClient::GetDiagnostics() const
{
    FRegularDialogueDiagnostics Result;
    const auto* Settings = GetDefault<URegularDialogueSettings>();
    Result.Model = Settings->Model.Left(64);
    Result.RequestLimit = FMath::Clamp(Settings->MaxRequestsPerMinute, 1, 12);
    Result.TotalRequests = TotalRequests;
    const double Time = Now();
    for (double Sent : RequestTimes) if (Time - Sent < 60.) ++Result.RecentRequests;
    Result.bPending = bPending;
    Result.PendingSeconds = bPending ? FMath::Max(0., Time - RequestStarted) : 0;
    Result.LastRequestSeconds = LastRequestSeconds;
    Result.LastRequestResult = LastRequestResult;
    return Result;
}

void URegularDialogueClient::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt, TickType, TickFunction);
    if (bPending && Now() >= Deadline)
    {
        const uint64 Request = Generation;
        const FString Fallback = ActiveContext.AuthoredLine;
        if (Transport) Transport->Cancel();
        Finish(Request, Fallback, false, TEXT("timeout"));
    }
}

void URegularDialogueClient::EndPlay(const EEndPlayReason::Type Reason)
{
    Cancel(); Transport.Reset();
    Super::EndPlay(Reason);
}

#if WITH_DEV_AUTOMATION_TESTS
void URegularDialogueClient::UseTestTransport(TSharedPtr<IRegularDialogueTransport> InTransport, TFunction<double()> Clock)
{
    Cancel(); Transport = MoveTemp(InTransport); TestClock = MoveTemp(Clock); bTestTransport = true;
}
#endif
