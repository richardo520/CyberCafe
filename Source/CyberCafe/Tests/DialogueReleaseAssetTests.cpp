#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Serving/DrinkRecipe.h"
#include "Liquid/IceInteraction.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIceMaterialReleaseUsage,"CyberCafe.ReleaseAssets.IceBucketInstancedMaterialUsage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FIceMaterialReleaseUsage::RunTest(const FString&)
{
 auto* M=LoadObject<UMaterial>(nullptr,TEXT("/Game/VRTemplate/Ice/Materials/M_IceClear.M_IceClear"));
 if(!TestNotNull(TEXT("Saved ice material"),M))return false;
 TestTrue(TEXT("Instanced usage is persisted before invoking any auto-usage check"),M->bUsedWithInstancedStaticMeshes!=0);
 auto* C=LoadClass<AIceBucketActor>(nullptr,TEXT("/Game/VRTemplate/Ice/Blueprints/BP_IceBucket.BP_IceBucket_C"));
 if(!TestNotNull(TEXT("Actual bucket Blueprint"),C))return false;
 auto* B=Cast<AIceBucketActor>(C->GetDefaultObject());
 if(!TestNotNull(TEXT("Actual saved bucket ice mesh"),B->IceMesh.Get()))return false;
 auto* Used=B->IceMesh->GetMaterial(0);
 TestTrue(TEXT("Bucket uses this saved base material, not a new substitute"),Used&&Used->GetBaseMaterial()==M);
 TestTrue(TEXT("Instanced usage supported"),M->CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes));
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FApprovedDialogueReleaseRules,"CyberCafe.ReleaseAssets.ApprovedDialogueAndUnchangedRules",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FApprovedDialogueReleaseRules::RunTest(const FString&)
{
 auto* R=LoadObject<UDrinkRecipe>(nullptr,TEXT("/Game/VRTemplate/Data/Recipes/DA_Latte.DA_Latte"));if(!TestNotNull(TEXT("Saved Latte"),R))return false;
 TestEqual(TEXT("Min unchanged"),R->MinVolumeML,180.0);TestEqual(TEXT("Max unchanged"),R->MaxVolumeML,220.0);TestEqual(TEXT("Target unchanged"),R->TargetVolumeML,200.0);
 TestEqual(TEXT("Milk wording"),R->MissingMilkFeedback.ToString(),FString(TEXT("牛奶还没加啊。对照订单补上牛奶，做好再送来。")));
 const FText* T=R->FailureFeedbackOverrides.Find(EDrinkValidationReason::TooLittle);TestTrue(TEXT("Exact approved short-volume wording"),T&&T->ToString()==TEXT("这杯太少了，你们店的拿铁一杯至少要200ml的，做好再送来。"));
 auto* Default=GetDefault<UDrinkRecipe>();TestEqual(TEXT("Other recipes receive no global overrides"),Default->FailureFeedbackOverrides.Num(),0);TestTrue(TEXT("Default missing-milk override remains empty"),Default->MissingMilkFeedback.IsEmpty());
 return !HasAnyErrors();
}
#endif
