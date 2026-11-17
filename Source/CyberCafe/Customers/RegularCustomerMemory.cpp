#include "Customers/RegularCustomerMemory.h"
#include "Kismet/GameplayStatics.h"

void URegularCustomerMemorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadProfile(TEXT("CyberCafe_RegularCustomer_v1"));
}

bool URegularCustomerMemorySubsystem::LoadProfile(const FString& Slot)
{
    if (Slot.IsEmpty() || Slot.Len() > 100 || Slot.Contains(TEXT("/")) || Slot.Contains(TEXT("\\")) || Slot.Contains(TEXT(".."))) return false;
    SlotName = Slot;
    Data = nullptr;
    bWritable = false;
    bPersisted = true;
    if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
    {
        Data = Cast<URegularCustomerSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
        if (!Data || Data->Version != 1)
        {
            Data = NewObject<URegularCustomerSaveGame>(this);
            UE_LOG(LogTemp, Warning, TEXT("[RegularCustomer] Save unreadable or unsupported; keeping it intact. Session memory only."));
            bPersisted = false;
            return false;
        }
        for (const auto& Pair : Data->Customers)
        {
            const FRegularCustomerMemory& M = Pair.Value;
            if (Pair.Key.IsNone() || M.CompletedVisits < 0 || M.LastIceCount < 0 ||
                !FMath::IsFinite(M.LastCoffeeFraction) || M.LastCoffeeFraction < 0 || M.LastCoffeeFraction > 1)
            {
                Data = NewObject<URegularCustomerSaveGame>(this);
                bPersisted = false;
                return false;
            }
        }
    }
    else Data = NewObject<URegularCustomerSaveGame>(this);
    bWritable = true;
    return true;
}

FRegularCustomerMemory URegularCustomerMemorySubsystem::Recall(FName CustomerId) const
{
    const FRegularCustomerMemory* Found = Data ? Data->Customers.Find(CustomerId) : nullptr;
    return Found ? *Found : FRegularCustomerMemory();
}

bool URegularCustomerMemorySubsystem::RememberCompleted(FName CustomerId, const FGuid& Visit, FName RecipeId,
    const FString& DrinkName, FName Topic, double CoffeeFraction, int32 IceCount, bool bObservedGrinding)
{
    if (!Data || CustomerId.IsNone() || RecipeId.IsNone() || !Visit.IsValid() || !FMath::IsFinite(CoffeeFraction) ||
        CoffeeFraction < 0 || CoffeeFraction > 1 || IceCount < 0) return false;
    FRegularCustomerMemory& M = Data->Customers.FindOrAdd(CustomerId);
    if (M.LastVisitId == Visit) return bPersisted; // A repeated terminal callback cannot count twice.
    if (M.TopicVisits.IsEmpty() && M.CompletedVisits > 0 && !M.LastTopic.IsNone())
        M.TopicVisits.Add(M.LastTopic, 1); // Existing v1 saves retain their last completed conversation.
    M.CompletedVisits = M.CompletedVisits < MAX_int32 ? M.CompletedVisits + 1 : MAX_int32;
    M.LastVisitId = Visit;
    M.LastRecipeId = RecipeId;
    M.LastDrinkName = DrinkName.Left(100);
    if (!Topic.IsNone())
    {
        int32& Count = M.TopicVisits.FindOrAdd(Topic);
        Count = FMath::Min(Count, MAX_int32 - 1) + 1;
        M.LastTopic = Topic;
    }
    M.bLastObservedGrinding = bObservedGrinding;
    M.LastCoffeeFraction = CoffeeFraction;
    M.LastIceCount = IceCount;
    bPersisted = false;
    return Flush();
}

bool URegularCustomerMemorySubsystem::Flush()
{
    bPersisted = bWritable && Data && UGameplayStatics::SaveGameToSlot(Data, SlotName, 0);
    if (!bPersisted) UE_LOG(LogTemp, Warning, TEXT("[RegularCustomer] Memory retained for this session; could not persist save."));
    return bPersisted;
}
