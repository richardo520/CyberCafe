#include "Editor/RegularAttentionEditorLibrary.h"
#if WITH_EDITOR
#include "Customers/RegularCustomerAttention.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LookAt.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "Misc/PackageName.h"

namespace
{
template<class T> T* AttentionNode(UEdGraph* Graph, int32 X)
{
    T* Node = NewObject<T>(Graph);
    Graph->AddNode(Node, false, false); Node->CreateNewGuid(); Node->PostPlacedNewNode();
    Node->NodePosX = X; Node->AllocateDefaultPins(); return Node;
}
bool AttentionLink(UEdGraphNode* Source, FName Output, UEdGraphNode* Target, FName Input)
{
    UEdGraphPin* A = Source->FindPin(Output, EGPD_Output);
    UEdGraphPin* B = Target->FindPin(Input, EGPD_Input);
    return A && B && Source->GetSchema()->TryCreateConnection(A, B);
}
}

TSubclassOf<UAnimInstance> URegularAttentionEditorLibrary::CreateHeadAttentionLayer(USkeletalMeshComponent* Mesh)
{
    if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Mesh->GetSkeletalMeshAsset()->GetSkeleton()) return nullptr;
    const TCHAR* Path = TEXT("/Game/CyberCafe/Characters/BarGirl/RegularCustomer/ABP_RegularCustomerAttention");
    if (FPackageName::DoesPackageExist(Path))
    {
        UAnimBlueprint* Existing = LoadObject<UAnimBlueprint>(nullptr, *(FString(Path) + TEXT(".ABP_RegularCustomerAttention")));
        return Existing && Existing->GeneratedClass && Existing->GeneratedClass->IsChildOf(URegularCustomerAttentionAnimation::StaticClass())
            ? Existing->GeneratedClass.Get() : nullptr;
    }
    UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
    Factory->ParentClass = URegularCustomerAttentionAnimation::StaticClass();
    Factory->TargetSkeleton = Mesh->GetSkeletalMeshAsset()->GetSkeleton();
    auto* BP = Cast<UAnimBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(
        TEXT("ABP_RegularCustomerAttention"), TEXT("/Game/CyberCafe/Characters/BarGirl/RegularCustomer"), UAnimBlueprint::StaticClass(), Factory));
    if (!BP) return nullptr;
    UEdGraph* Graph = nullptr;
    for (UEdGraph* Candidate : BP->FunctionGraphs) if (Candidate->GetFName() == TEXT("AnimGraph")) Graph = Candidate;
    if (!Graph) return nullptr;
    UAnimGraphNode_Root* Root = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes) if (auto* R = Cast<UAnimGraphNode_Root>(Node)) Root = R;
    if (!Root || Graph->Nodes.Num() != 1) return nullptr;
    auto* Input = AttentionNode<UAnimGraphNode_LinkedInputPose>(Graph, -900);
    auto* ToComponent = AttentionNode<UAnimGraphNode_LocalToComponentSpace>(Graph, -650);
    auto* Look = AttentionNode<UAnimGraphNode_LookAt>(Graph, -400);
    auto* ToLocal = AttentionNode<UAnimGraphNode_ComponentToLocalSpace>(Graph, -150);
    Look->Node.BoneToModify.BoneName = TEXT("head");
    Look->Node.LookAtClamp = 28;
    Look->Node.InterpolationTime = 0; // Runtime component already smooths the target.
    const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    const int32 Head = Skeleton.FindBoneIndex(TEXT("head"));
    if (Head == INDEX_NONE) return nullptr;
    FTransform HeadRef = Skeleton.GetRefBonePose()[Head];
    for (int32 Parent = Skeleton.GetParentIndex(Head); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
        HeadRef = HeadRef * Skeleton.GetRefBonePose()[Parent];
    const FVector MeshForward = Mesh->GetRelativeRotation().UnrotateVector(FVector::ForwardVector);
    Look->Node.LookAt_Axis.Axis = HeadRef.GetRotation().UnrotateVector(MeshForward).GetSafeNormal();
    Look->Node.LookAt_Axis.bInLocalSpace = true;
    for (FOptionalPinFromProperty& Pin : Look->ShowPinForProperties)
        if (Pin.PropertyName == TEXT("LookAtLocation") || Pin.PropertyName == TEXT("Alpha")) Pin.bShowPin = true;
    Look->ReconstructNode();
    auto* Target = AttentionNode<UK2Node_VariableGet>(Graph, -700);
    Target->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(URegularCustomerAttentionAnimation, AttentionTarget));
    Target->ReconstructNode(); Target->NodePosY = 230;
    auto* Alpha = AttentionNode<UK2Node_VariableGet>(Graph, -700);
    Alpha->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(URegularCustomerAttentionAnimation, AttentionAlpha));
    Alpha->ReconstructNode(); Alpha->NodePosY = 350;
    const bool Linked = AttentionLink(Input, TEXT("Pose"), ToComponent, TEXT("LocalPose")) &&
        AttentionLink(ToComponent, TEXT("ComponentPose"), Look, TEXT("ComponentPose")) &&
        AttentionLink(Look, TEXT("Pose"), ToLocal, TEXT("ComponentPose")) &&
        AttentionLink(ToLocal, TEXT("Pose"), Root, TEXT("Result")) &&
        AttentionLink(Target, TEXT("AttentionTarget"), Look, TEXT("LookAtLocation")) &&
        AttentionLink(Alpha, TEXT("AttentionAlpha"), Look, TEXT("Alpha"));
    if (!Linked)
    {
        for (UEdGraphNode* Node : Graph->Nodes) for (UEdGraphPin* Pin : Node->Pins)
            UE_LOG(LogTemp, Warning, TEXT("[AttentionGraph] %s pin %s dir %d"), *Node->GetClass()->GetName(), *Pin->PinName.ToString(), int32(Pin->Direction));
        return nullptr;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    return BP->Status != BS_Error ? BP->GeneratedClass.Get() : nullptr;
}
#endif
