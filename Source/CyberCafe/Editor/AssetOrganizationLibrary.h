#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetOrganizationLibrary.generated.h"

class UAssetImportData;
class UPackage;
class UWorld;

USTRUCT(BlueprintType)
struct FAssetImportPathSnapshot
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UAssetImportData> Data;
    UPROPERTY() TArray<FString> Filenames;
};

/** Editor migration support. No asset relocation runs automatically at startup. */
UCLASS()
class UAssetOrganizationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable,Category="Assets|Organization")
    static TArray<FAssetImportPathSnapshot> CaptureImportPaths(UObject* Asset);
    // Preserve original timestamps/hashes while re-basing relative import paths after a move.
    UFUNCTION(BlueprintCallable,Category="Assets|Organization")
    static bool RestoreImportPaths(const TArray<FAssetImportPathSnapshot>& Snapshots);
    UFUNCTION(BlueprintCallable,Category="Assets|Organization")
    static bool EnsureCompatibilityRedirector(const FString& OriginalPackageName, UObject* Destination);
    // Explicit package API for a reviewed move with redirects always retained.
    UFUNCTION(BlueprintCallable,Category="Assets|Organization")
    static bool MoveReviewedAsset(UObject* Asset, const FString& DestinationPackageName);
    UFUNCTION(BlueprintCallable,Category="Assets|Organization")
    static bool MoveMainCafeMap(UWorld* World);
    UFUNCTION(BlueprintCallable,Category="Assets|Organization")
    static TArray<UPackage*> LoadProjectPackages(const TArray<FString>& PackageNames);
    UFUNCTION(BlueprintCallable,Category="Assets|Organization")
    static void MountOriginalBackupForAudit();
#endif
};
