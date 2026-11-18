#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dialogue/RegularDialogueClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

namespace RegularHttpTests
{
struct FState
{
    TSharedPtr<IRegularDialogueTransport> Transport;
    bool bDone = false, bConnected = false;
    int32 Code = 0;
    FString Body;
    FRegularDialogueContext Context;
};
class FWait : public IAutomationLatentCommand
{
    FAutomationTestBase& Test;
    TSharedPtr<FState> State;
    double Started = FPlatformTime::Seconds();
public:
    FWait(FAutomationTestBase& InTest, TSharedPtr<FState> InState) : Test(InTest), State(MoveTemp(InState)) {}
    virtual bool Update() override
    {
        if (!State->bDone && FPlatformTime::Seconds() - Started < 10) return false;
        Test.TestTrue(TEXT("Real UE HTTP request completed"), State->bDone && State->bConnected);
        Test.TestEqual(TEXT("Mock accepted native headers, request schema and UTF-8 body"), State->Code, 200);
        FString Output, Reason;
        Test.TestTrue(TEXT("Real HTTP response passes same native parser"), FRegularDialogueProtocol::ParseResponse(State->Body, State->Context, Output, Reason));
        Test.TestEqual(TEXT("Chinese body survived real HTTP round trip"), Output, State->Context.AuthoredLine);
        State->Transport->Cancel();
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegularRealHttpTransport, "CyberCafe.CloudDialogue.HttpTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRegularRealHttpTransport::RunTest(const FString&)
{
    const FString URL = FPlatformMisc::GetEnvironmentVariable(TEXT("CYBERCAFE_MOCK_OPENAI_ENDPOINT"));
    if (!URL.StartsWith(TEXT("http://127.0.0.1:")))
    { AddError(TEXT("Run Tools/Test-CloudDialogue.ps1 to provide the local mock endpoint. This test never contacts a cloud provider.")); return false; }
    auto State = MakeShared<RegularHttpTests::FState>();
    State->Context.AuthoredLine = TEXT("晚上好，来杯冰拿铁吧。");
    State->Context.RequiredTerms = {TEXT("冰拿铁")};
    State->Transport = CreateRegularDialogueHttpTransport();
    TWeakPtr<RegularHttpTests::FState> Weak(State);
    TestTrue(TEXT("Actual native transport starts"), State->Transport->Send(URL, TEXT("unit-test-key"),
        FRegularDialogueProtocol::BuildRequest(State->Context, TEXT("MiniMax-M3.1-Flash"), 1024, true), 5,
        [Weak](int32 Code, const FString& Body, bool Connected)
        { if (auto Pinned = Weak.Pin()) { Pinned->Code = Code; Pinned->Body = Body; Pinned->bConnected = Connected; Pinned->bDone = true; } }));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<RegularHttpTests::FWait>(*this, State));
    return true;
}
#endif
