#pragma once
#include "CoreMinimal.h"

/** Authored conversation facts; model wording cannot start orders or choose recipes. */
namespace RegularCustomerConversation
{
    FString Reply(FName Topic, int32 Turn);
}
