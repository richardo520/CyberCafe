#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RegularCustomerMemory.generated.h"

USTRUCT(BlueprintType)
struct CYBERCAFE_API FRegularCustomerMemory
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly) int32 CompletedVisits = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly) FName LastRecipeId;
    UPROPERTY(SaveGame, BlueprintReadOnly) FString LastDrinkName;
    UPROPERTY(SaveGame, BlueprintReadOnly) FName LastTopic;
    UPROPERTY(SaveGame, BlueprintReadOnly) TMap<FName, int32> TopicVisits;
    UPROPERTY(SaveGame, BlueprintReadOnly) bool bLastObservedGrinding = false;
    int32 GetTopicVisits(FName Topic) const
    {
        if (const int32* Count = TopicVisits.Find(Topic)) return *Count;
        return CompletedVisits > 0 && !Topic.IsNone() && LastTopic == Topic ? 1 : 0;
    }
    UPROPERTY(SaveGame, BlueprintReadOnly) double LastCoffeeFraction = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly) int32 LastIceCount = 0;
    UPROPERTY(SaveGame) FGuid LastVisitId;
};

UCLASS()
class CYBERCAFE_API URegularCustomerSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 Version = 1;
    UPROPERTY(SaveGame) TMap<FName, FRegularCustomerMemory> Customers;
};

/** Small local structured memories. No transcripts, network calls, or actor references. */
UCLASS()
class CYBERCAFE_API URegularCustomerMemorySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    // Switching profile never silently overwrites an unreadable or newer save.
    bool LoadProfile(const FString& Slot);
    UFUNCTION(BlueprintPure, Category="Customer|Memory") FRegularCustomerMemory Recall(FName CustomerId) const;
    bool RememberCompleted(FName CustomerId, const FGuid& Visit, FName RecipeId, const FString& DrinkName,
        FName Topic, double CoffeeFraction, int32 IceCount, bool bObservedGrinding = false);
    bool Flush();
    bool IsWritable() const { return bWritable; }
    bool IsPersisted() const { return bPersisted; }
private:
    UPROPERTY(Transient) TObjectPtr<URegularCustomerSaveGame> Data;
    FString SlotName;
    bool bWritable = false;
    bool bPersisted = true;
};
