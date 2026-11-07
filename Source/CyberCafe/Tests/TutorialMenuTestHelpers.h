#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Serving/OrderHologramActor.h"
#include "Serving/DrinkMenuData.h"
#include "Components/BoxComponent.h"
namespace TutorialMenuTestHelpers
{
inline bool TouchKey(AOrderHologramActor* Screen,UBoxComponent* Zone)
{
    if(!IsValid(Screen)||!IsValid(Zone))return false;
    const FVector Away=Screen->GetActorLocation()+FVector(500,500,500);
    Screen->ProcessMenuTouchSample(EControllerHand::Left,Away);
    Screen->ProcessMenuTouchSample(EControllerHand::Right,Away);
    return Screen->ProcessMenuTouchSample(EControllerHand::Left,Zone->GetComponentLocation());
}
inline bool FindAndReturn(AOrderHologramActor* Screen,FName Code)
{
    if(!IsValid(Screen)||!IsValid(Screen->RecipeMenu)||Screen->RecipeMenu->FindPinyinIndex(Code)==INDEX_NONE)return false;
    // Replay the same accepted hand samples as VR, not scripted selection/completion.
    if(Screen->IsRecipeMenuMode()&&!TouchKey(Screen,Screen->MenuModeZone))return false;
    if(!TouchKey(Screen,Screen->MenuModeZone))return false;
    for(int32 I=0;I<Screen->RecipeMenu->Entries.Num();++I)
    {
        const auto* Entry=Screen->GetSelectedMenuEntry();
        if(Entry&&Entry->PinyinIndex==Code)break;
        if(!TouchKey(Screen,Screen->MenuNextZone))return false;
    }
    const auto* Entry=Screen->GetSelectedMenuEntry();
    return Entry&&Entry->PinyinIndex==Code&&TouchKey(Screen,Screen->MenuModeZone);
}
}
#endif
