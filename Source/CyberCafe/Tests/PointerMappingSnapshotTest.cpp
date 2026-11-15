#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPointerExportBeforeMapping, "CyberCafe.Authoring.ExportBeforePointerMappings", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPointerExportBeforeMapping::RunTest(const FString&)
{
    auto* C=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/VRTemplate/Input/IMC_Default.IMC_Default"));
    FString Path; if(!TestNotNull(TEXT("Original context"),C)||!TestTrue(TEXT("Output path"),FParse::Value(FCommandLine::Get(),TEXT("PointerSnapshot="),Path)))return false;
    if(!TestEqual(TEXT("Original mappings before append"),C->GetMappings().Num(),37))return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("original_sha256"),TEXT("b580c3d047cc5f9578ebbf7205a42071afc773be628aea3fd6869a746af054a7"));TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& M:C->GetMappings()){FString Text;FEnhancedActionKeyMapping::StaticStruct()->ExportText(Text,&M,nullptr,C,PPF_None,nullptr,false);Rows.Add(MakeShared<FJsonValueString>(Text));}
    J->SetArrayField(TEXT("rows"),Rows);FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));return TestTrue(TEXT("Persist original full mapping snapshot"),FFileHelper::SaveStringToFile(Json,*Path));
}
#endif
