#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dialogue/RegularDialogueProtocol.h"
#include "Dialogue/RegularDialogueDiagnostics.h"
#include "RegularDialogueClient.generated.h"

using FRegularDialogueCompletion = TFunction<void(const FString&, bool)>;
using FRegularTransportCompletion = TFunction<void(int32, const FString&, bool)>;

class IRegularDialogueTransport
{
public:
    virtual ~IRegularDialogueTransport() = default;
    virtual bool Send(const FString& URL, const FString& Key, const FString& Body, float Timeout, FRegularTransportCompletion Done) = 0;
    virtual void Cancel() = 0;
};
CYBERCAFE_API TSharedPtr<IRegularDialogueTransport> CreateRegularDialogueHttpTransport();

/** One bounded request per encounter. Late/cancelled replies never reach presentation. */
UCLASS()
class CYBERCAFE_API URegularDialogueClient : public UActorComponent
{
    GENERATED_BODY()
public:
    URegularDialogueClient();
    void Generate(const FRegularDialogueContext& Context, FRegularDialogueCompletion Done);
    void Cancel();
    UFUNCTION(BlueprintPure, Category="Customer|AI") FString GetLastResult() const { return LastResult; }
    bool IsPending() const { return bPending; }
    double GetRequestTime() const { return Now(); }
    FRegularDialogueDiagnostics GetDiagnostics() const;
#if WITH_DEV_AUTOMATION_TESTS
    void UseTestTransport(TSharedPtr<IRegularDialogueTransport> InTransport, TFunction<double()> Clock);
#endif
protected:
    virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    double Now() const;
    void Finish(uint64 Request, const FString& Text, bool bGenerated, const FString& Reason);
    void Receive(uint64 Request, int32 Status, const FString& Body, bool bConnected);
    TSharedPtr<IRegularDialogueTransport> Transport;
    FRegularDialogueCompletion Completion;
    FRegularDialogueContext ActiveContext;
    FString LastResult = TEXT("not_requested");
    uint64 Generation = 0;
    double Deadline = 0, CooldownUntil = 0;
    TArray<double> RequestTimes;
    int32 TotalRequests = 0;
    double RequestStarted = 0;
    double LastRequestSeconds = 0;
    FString LastRequestResult = TEXT("not_requested");
    bool bPending = false;
#if WITH_DEV_AUTOMATION_TESTS
    bool bTestTransport = false;
    TFunction<double()> TestClock;
#endif
};
