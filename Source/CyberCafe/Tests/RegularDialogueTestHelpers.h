#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Dialogue/RegularDialogueClient.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RegularDialogueTests
{
inline FString Encode(const TSharedRef<FJsonObject>& Object)
{
    FString Result; FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Result)); return Result;
}
inline FString Response(const FString& Text)
{
    auto LineObject = MakeShared<FJsonObject>(); LineObject->SetStringField(TEXT("line"), Text);
    auto Part = MakeShared<FJsonObject>(); Part->SetStringField(TEXT("type"), TEXT("output_text")); Part->SetStringField(TEXT("text"), Encode(LineObject));
    auto Message = MakeShared<FJsonObject>(); Message->SetStringField(TEXT("type"), TEXT("message")); Message->SetStringField(TEXT("role"), TEXT("assistant"));
    Message->SetArrayField(TEXT("content"), {MakeShared<FJsonValueObject>(Part)});
    auto Root = MakeShared<FJsonObject>(); Root->SetStringField(TEXT("status"), TEXT("completed"));
    Root->SetArrayField(TEXT("output"), {MakeShared<FJsonValueObject>(Message)});
    return Encode(Root);
}
inline FString AuthoredFromRequest(const FString& Body)
{
    TSharedPtr<FJsonObject> Root, Input;
    FString Data;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) && Root->TryGetStringField(TEXT("input"), Data) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data), Input))
        return Input->GetStringField(TEXT("authored_line"));
    return FString();
}
class FTransport final : public IRegularDialogueTransport
{
public:
    TArray<FRegularTransportCompletion> Callbacks;
    TArray<FString> Bodies;
    bool bStart = true;
    int32 Cancels = 0;
    virtual bool Send(const FString&, const FString&, const FString& Body, float, FRegularTransportCompletion Done) override
    { Bodies.Add(Body); Callbacks.Add(MoveTemp(Done)); return bStart; }
    virtual void Cancel() override { ++Cancels; } // Keep callbacks so tests can deliver cancelled requests late.
    void Reply(int32 Index, int32 Status, const FString& Body, bool Connected = true)
    { if (Callbacks.IsValidIndex(Index)) Callbacks[Index](Status, Body, Connected); }
};
}
#endif
