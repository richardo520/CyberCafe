#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"
#include "UI/OrderHologramWidget.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Liquid/CupActor.h"
#include "Liquid/IceInteraction.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
namespace TutorialMenuTestHelpers
{
inline FVector SceneCupScale(UClass* Class)
{
    // These are the approved map instance scales, not nonexistent BP root defaults.
    if(Class&&Class->GetName()==TEXT("BP_MenuCup_Highball_C"))return FVector(1.1);
    if(Class&&Class->GetName()==TEXT("BP_MenuCup_Coupe_C"))return FVector(1.35);
    return FVector::OneVector;
}
inline bool TouchAction(AOrderHologramActor* Screen,int32 Action)
{
    if(!IsValid(Screen)||!IsValid(Screen->Panel))return false;
    const FVector Away=Screen->GetActorLocation()+FVector(500,500,500);
    Screen->ProcessMenuTouchSample(EControllerHand::Left,Away);Screen->ProcessMenuTouchSample(EControllerHand::Right,Away);
    const FIntRect Rect=UOrderHologramWidget::GetScreenActionRect(Action);const FVector2D Pixel((Rect.Min.X+Rect.Max.X)*.5,(Rect.Min.Y+Rect.Max.Y)*.5);
    return Screen->ProcessMenuTouchSample(EControllerHand::Left,Screen->Panel->GetComponentTransform().TransformPosition(FVector(0,600-Pixel.X,375-Pixel.Y)));
}
inline bool FindRecipe(AOrderHologramActor* Screen,FName Code)
{
    if(!IsValid(Screen)||!IsValid(Screen->RecipeMenu))return false;
    if(!Screen->IsPanelOpen())return false;
    if(!Screen->IsRecipeMenuMode()&&!TouchAction(Screen,1))return false;
    if(Screen->GetMenuFilter()!=0&&!TouchAction(Screen,10))return false;
    const TArray<int32> Rows=Screen->GetVisibleMenuIndices();const int32 Actual=Screen->RecipeMenu->FindPinyinIndex(Code),Row=Rows.IndexOfByKey(Actual);
    return Row!=INDEX_NONE&&TouchAction(Screen,100+Row);
}
// Old caller name retained for source compatibility. No forced return anymore.
inline bool FindAndReturn(AOrderHologramActor* Screen,FName Code){return FindRecipe(Screen,Code);}
inline bool AddIce(ACupActor* Cup,int32 Count)
{
    if(!IsValid(Cup))return false;auto* IceComponent=Cup->FindComponentByClass<UCupIceComponent>();if(!IceComponent)return false;
    UClass* IceClass=LoadClass<AIceCubeActor>(nullptr,TEXT("/Game/VRTemplate/Ice/Blueprints/BP_IceCube.BP_IceCube_C"));if(!IceClass)return false;
    const FTransform Entry=Cup->PourEntryPoint->GetComponentTransform();
    for(int32 I=0;I<Count;++I)
    {
        const FVector Previous=Entry.TransformPosition(FVector(0,0,4)),Current=Entry.TransformPosition(FVector(0,0,-3));
        auto* Ice=Cup->GetWorld()->SpawnActor<AIceCubeActor>(IceClass,FTransform(Current));if(!Ice)return false;
        Ice->Mesh->SetSimulatePhysics(false);Ice->Mesh->SetEnableGravity(false);
        if(!IceComponent->TryAccept(Ice,Previous,Current))return false;
    }
    return IceComponent->GetIceCount()>=Count;
}
}
#endif
