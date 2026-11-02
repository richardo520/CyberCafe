#include "UI/Narrative/NarrativeEditorLibrary.h"
#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"

int32 UNarrativeEditorLibrary::RepairWidgetIdentity(UObject* WidgetBlueprintAsset)
{
    UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(WidgetBlueprintAsset);
    if (!BP || !BP->WidgetTree) return -1;
    BP->Modify();
    BP->WidgetTree->Modify();
    int32 Added = 0;
    BP->WidgetTree->ForEachWidget([BP, &Added](UWidget* Widget)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
        {
            BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
            ++Added;
        }
        if (Widget->GetName().StartsWith(TEXT("Text_")))
        {
            Widget->Modify();
            Widget->bIsVariable = true;
        }
    });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    return Added;
}
#endif
