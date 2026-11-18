#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dialogue/RegularDialogueProtocol.h"
#include "Dialogue/RegularDialogueSettings.h"
#include "Tests/RegularDialogueTestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace RegularDialogueTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FClientWorld
{
    UWorld* World = nullptr;
    URegularDialogueClient* Client = nullptr;
    FClientWorld()
    {
        UWorld::InitializationValues IV;
        IV.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &IV);
        AActor* Owner = World->SpawnActor<AActor>();
        Client = NewObject<URegularDialogueClient>(Owner);
        Owner->AddInstanceComponent(Client); Client->RegisterComponent();
    }
    ~FClientWorld() { Client->Cancel(); World->DestroyWorld(false); }
};
FRegularDialogueContext Context()
{
    FRegularDialogueContext C; C.AuthoredLine = TEXT("晚上好，来杯冰拿铁吧。");
    C.CustomerId = TEXT("Regular_NightShift_01"); C.Cue = TEXT("Greeting"); C.RequiredTerms = {TEXT("冰拿铁")};
    C.Facts.Add(TEXT("completed_visits"), TEXT("0")); return C;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDialogueRequestProtocol, "CyberCafe.CloudDialogue.RequestProtocol", RegularDialogueTests::Flags)
bool FDialogueRequestProtocol::RunTest(const FString&)
{
    FRegularDialogueContext C = RegularDialogueTests::Context();
    C.Facts.Add(TEXT("quoted_data"), TEXT("\"not instructions\"\n\\"));
    const FString Body = FRegularDialogueProtocol::BuildRequest(C, TEXT("gpt-6.1-sol"), 1024);
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("Actual serialized request is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root))) return false;
    TestEqual(TEXT("Chosen model"), Root->GetStringField(TEXT("model")), FString(TEXT("gpt-6.1-sol")));
    AddInfo(TEXT("Configured endpoint: ") + GetDefault<URegularDialogueSettings>()->Endpoint);
    TestTrue(TEXT("Project endpoint loads intact from ini"), GetDefault<URegularDialogueSettings>()->Endpoint.StartsWith(TEXT("https://")));
    TestFalse(TEXT("No response storage requested"), Root->GetBoolField(TEXT("store")));
    TestEqual(TEXT("Supported low reasoning effort"), Root->GetObjectField(TEXT("reasoning"))->GetStringField(TEXT("effort")), FString(TEXT("low")));
    TestTrue(TEXT("Strict schema"), Root->GetObjectField(TEXT("text"))->GetObjectField(TEXT("format"))->GetBoolField(TEXT("strict")));
    TestFalse(TEXT("No tools or state-changing function calls"), Root->HasField(TEXT("tools")));
    TestFalse(TEXT("Credential is not part of request body"), Body.Contains(TEXT("unit-test-key")));
    TestEqual(TEXT("Authored facts survive encoding"), RegularDialogueTests::AuthoredFromRequest(Body), C.AuthoredLine);
    FString Output, Reason;
    TestTrue(TEXT("Responses output-message shape parsed"), FRegularDialogueProtocol::ParseResponse(RegularDialogueTests::Response(TEXT("夜里好，冰拿铁，慢慢做就好。")), C, Output, Reason));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMiniMaxRequestProtocol, "CyberCafe.CloudDialogue.MiniMaxProtocol", RegularDialogueTests::Flags)
bool FMiniMaxRequestProtocol::RunTest(const FString&)
{
    const auto C = RegularDialogueTests::Context();
    const FString Body = FRegularDialogueProtocol::BuildRequest(C, TEXT("MiniMax-M3.1-Flash"), 1024, true);
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("MiniMax request is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root))) return false;
    TestEqual(TEXT("MiniMax subscription model"), Root->GetStringField(TEXT("model")), FString(TEXT("MiniMax-M3.1-Flash")));
    TestEqual(TEXT("Flash uses supported low effort, never none"), Root->GetObjectField(TEXT("reasoning"))->GetStringField(TEXT("effort")), FString(TEXT("low")));
    TestFalse(TEXT("No undocumented strict schema parameter for MiniMax"), Root->HasField(TEXT("text")));
    TestFalse(TEXT("No tools"), Root->HasField(TEXT("tools")));
    TestEqual(TEXT("Same authored context reaches MiniMax"), RegularDialogueTests::AuthoredFromRequest(Body), C.AuthoredLine);
    URegularDialogueSettings* Settings = NewObject<URegularDialogueSettings>();
    Settings->Provider = ERegularDialogueProvider::MiniMax;
    TestEqual(TEXT("MiniMax never reuses an OpenAI credential"), FString(Settings->CredentialVariable()), FString(TEXT("MINIMAX_API_KEY")));
    Settings->Provider = ERegularDialogueProvider::OpenAI;
    TestEqual(TEXT("OpenAI credential selection retained"), FString(Settings->CredentialVariable()), FString(TEXT("OPENAI_API_KEY")));
    FString Output, Reason;
    TestFalse(TEXT("Plain unstructured model prose is rejected"), FRegularDialogueProtocol::ParseResponse(
        TEXT("{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":\"冰拿铁\"}]}]}"), C, Output, Reason));
    TestTrue(TEXT("Invalid prose never leaks into UI"), Output.IsEmpty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDialogueOutputGuards, "CyberCafe.CloudDialogue.OutputGuards", RegularDialogueTests::Flags)
bool FDialogueOutputGuards::RunTest(const FString&)
{
    auto C = RegularDialogueTests::Context();
    FString Output, Reason;
    for (const FString& Bad : {FString(TEXT("随便什么都好。")), FString(TEXT("上次那杯冰拿铁我还记得。")),
        FString(TEXT("冰拿铁里糖太多了。")), FString(TEXT("你刚才磨好的冰拿铁很好。")), FString(TEXT("来三杯冰拿铁。")),
        FString(TEXT("维修站爆炸了，来杯冰拿铁。")), FString::ChrN(101, TCHAR(0x597d))})
    {
        TestFalse(TEXT("Unsupported output rejected"), FRegularDialogueProtocol::ParseResponse(RegularDialogueTests::Response(Bad), C, Output, Reason));
        TestTrue(TEXT("Rejected text never leaks into output"), Output.IsEmpty());
    }
    TestFalse(TEXT("Incomplete output rejected"), FRegularDialogueProtocol::ParseResponse(TEXT("{\"status\":\"incomplete\",\"output\":[]}"), C, Output, Reason));
    TestFalse(TEXT("Invalid JSON rejected"), FRegularDialogueProtocol::ParseResponse(TEXT("not json"), C, Output, Reason));
    auto Numeric = C; Numeric.AuthoredLine = TEXT("这杯冰拿铁有1块冰。");
    TestFalse(TEXT("Same digit cannot be repeated to invent a larger quantity"), FRegularDialogueProtocol::ParseResponse(
        RegularDialogueTests::Response(TEXT("这杯冰拿铁有11块冰。")), Numeric, Output, Reason));
    TestFalse(TEXT("Refusal rejected"), FRegularDialogueProtocol::ParseResponse(TEXT("{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"refusal\",\"refusal\":\"no\"}]}]}"), C, Output, Reason));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDialogueCancellation, "CyberCafe.CloudDialogue.CancelAndLateResponse", RegularDialogueTests::Flags)
bool FDialogueCancellation::RunTest(const FString&)
{
    auto Wire = MakeShared<RegularDialogueTests::FTransport>();
    RegularDialogueTests::FClientWorld Fixture;
    URegularDialogueClient* Client = Fixture.Client;
    double Time = 0; Client->UseTestTransport(Wire, [&Time] { return Time; });
    int32 First = 0, Second = 0;
    auto C = RegularDialogueTests::Context();
    Client->Generate(C, [&First](const FString&, bool) { ++First; });
    Client->Generate(C, [&Second](const FString&, bool) { ++Second; });
    Wire->Reply(0, 200, RegularDialogueTests::Response(C.AuthoredLine));
    TestEqual(TEXT("Cancelled response cannot finish newer dialogue"), First, 0);
    TestTrue(TEXT("Newer request remains pending"), Client->IsPending());
    Wire->Reply(1, 200, RegularDialogueTests::Response(C.AuthoredLine));
    Wire->Reply(1, 200, RegularDialogueTests::Response(C.AuthoredLine));
    TestEqual(TEXT("Only one terminal callback"), Second, 1);
    Client->Generate(C, [&First](const FString&, bool) { ++First; });
    Client->Cancel();
    Wire->Reply(2, 200, RegularDialogueTests::Response(C.AuthoredLine));
    TestEqual(TEXT("Explicit cancellation is silent"), First, 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDialogueFallbacks, "CyberCafe.CloudDialogue.TimeoutErrorsAndBudget", RegularDialogueTests::Flags)
bool FDialogueFallbacks::RunTest(const FString&)
{
    auto Wire = MakeShared<RegularDialogueTests::FTransport>();
    RegularDialogueTests::FClientWorld Fixture;
    URegularDialogueClient* Client = Fixture.Client;
    double Time = 0; Client->UseTestTransport(Wire, [&Time] { return Time; });
    const auto C = RegularDialogueTests::Context();
    int32 Finished = 0; FString Text; bool Generated = true;
    auto Done = [&Finished, &Text, &Generated](const FString& Result, bool AI) { ++Finished; Text = Result; Generated = AI; };
    Client->Generate(C, Done);
    Time = 100;
    static_cast<UActorComponent*>(Client)->TickComponent(.02f, LEVELTICK_All, nullptr);
    TestEqual(TEXT("Deadline returns original line"), Text, C.AuthoredLine);
    TestFalse(TEXT("Timeout is not reported as AI"), Generated);
    Wire->Reply(0, 200, RegularDialogueTests::Response(C.AuthoredLine));
    TestEqual(TEXT("Late completion after timeout ignored"), Finished, 1);
    Client->Generate(C, Done); Wire->Reply(1, 401, TEXT("private provider body must not be logged"));
    TestEqual(TEXT("Unauthorized fallback"), Client->GetLastResult(), FString(TEXT("http_401")));
    Client->Generate(C, Done);
    TestEqual(TEXT("Auth failure does not trigger a request storm"), Wire->Bodies.Num(), 2);
    TestEqual(TEXT("Cooldown fallback"), Client->GetLastResult(), FString(TEXT("provider_cooldown")));
    Time += 61;
    const int32 Budget = FMath::Clamp(GetDefault<URegularDialogueSettings>()->MaxRequestsPerMinute, 1, 12);
    for (int32 I = 2; I < Budget + 2; ++I)
    { Client->Generate(C, Done); Wire->Reply(I, 200, RegularDialogueTests::Response(C.AuthoredLine)); }
    Client->Generate(C, Done);
    TestEqual(TEXT("Rolling minute cap counts all sends including cancelled requests"), Wire->Bodies.Num(), Budget + 2);
    TestEqual(TEXT("Budget fallback"), Client->GetLastResult(), FString(TEXT("minute_budget")));
    Time += 61;
    Client->Generate(C, Done);
    TestEqual(TEXT("A long conversation can use AI again after the window expires"), Wire->Bodies.Num(), Budget + 3);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDialogueDiagnosticsTest, "CyberCafe.CloudDialogue.SafeLiveDiagnostics", RegularDialogueTests::Flags)
bool FDialogueDiagnosticsTest::RunTest(const FString&)
{
    auto Wire = MakeShared<RegularDialogueTests::FTransport>();
    RegularDialogueTests::FClientWorld F;
    double Time = 0;
    F.Client->UseTestTransport(Wire, [&Time] { return Time; });
    const auto C = RegularDialogueTests::Context();
    TestEqual(TEXT("No request before generation"), F.Client->GetDiagnostics().TotalRequests, 0);
    F.Client->Generate(C, [](const FString&, bool) {});
    Time = 2.25;
    auto D = F.Client->GetDiagnostics();
    TestTrue(TEXT("Pending diagnostics distinguish waiting from successful generation"), D.bPending && D.LastRequestResult == TEXT("not_requested"));
    TestEqual(TEXT("Live pending elapsed time"), D.PendingSeconds, 2.25);
    TestEqual(TEXT("Actual request attempts counted"), D.TotalRequests, 1);
    Wire->Reply(0, 200, RegularDialogueTests::Response(C.AuthoredLine));
    D = F.Client->GetDiagnostics();
    TestTrue(TEXT("Validated result and elapsed belong to completed request"), !D.bPending && D.LastRequestResult == TEXT("validated") && D.LastRequestSeconds == 2.25);
    Time = 3; F.Client->Generate(C, [](const FString&, bool) {});
    Time = 4.5; F.Client->Cancel();
    TestTrue(TEXT("Cancellation has its own duration and outcome"), F.Client->GetDiagnostics().LastRequestResult == TEXT("cancelled") && F.Client->GetDiagnostics().LastRequestSeconds == 1.5);
    Wire->Reply(1, 200, RegularDialogueTests::Response(C.AuthoredLine));
    TestTrue(TEXT("Late callback cannot overwrite cancelled diagnostics"), F.Client->GetDiagnostics().LastRequestResult == TEXT("cancelled"));
    Time = 5; F.Client->Generate(C, [](const FString&, bool) {});
    Wire->Reply(2, 429, TEXT("private response body must never enter the diagnostics"));
    TestTrue(TEXT("Only fixed HTTP outcome reaches diagnostics"), F.Client->GetDiagnostics().LastRequestResult == TEXT("http_429"));
    Time = 65;
    TestEqual(TEXT("Rolling usage expires even without a new Generate call"), F.Client->GetDiagnostics().RecentRequests, 0);
    TestEqual(TEXT("Session total retains cancelled and failed attempts"), F.Client->GetDiagnostics().TotalRequests, 3);
    using namespace RegularDialogueDiagnostics;
    TestTrue(TEXT("Missing key is local mode, not a successful AI call"), SourceForResult(false, TEXT("missing_key")) == ERegularDialogueSource::Local);
    TestTrue(TEXT("Timeout is a fallback"), SourceForResult(false, TEXT("timeout")) == ERegularDialogueSource::Fallback);
    TestTrue(TEXT("Only generated lines get AI source"), SourceForResult(true, TEXT("validated")) == ERegularDialogueSource::AI);
    TestFalse(TEXT("Unknown strings cannot expose raw provider data"), ReasonLabel(TEXT("private response body")).Contains(TEXT("private")));
    return !HasAnyErrors();
}
#endif
