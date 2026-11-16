#include "Editor/AssetOrganizationLibrary.h"
#if WITH_EDITOR
#include "EditorFramework/AssetImportData.h"
#include "UObject/UObjectHash.h"
#include "UObject/ObjectRedirector.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "ObjectTools.h"
#include "Misc/Paths.h"

TArray<FAssetImportPathSnapshot> UAssetOrganizationLibrary::CaptureImportPaths(UObject* Asset)
{
    TArray<FAssetImportPathSnapshot> Result;
    if (!IsValid(Asset)) return Result;
    ForEachObjectWithOuter(Asset,[&](UObject* Object)
    {
        if (auto* Data=Cast<UAssetImportData>(Object))
        {
            FAssetImportPathSnapshot Snapshot;
            Snapshot.Data=Data;
            Snapshot.Filenames=Data->ExtractFilenames();
            Result.Add(MoveTemp(Snapshot));
        }
    },true);
    return Result;
}

bool UAssetOrganizationLibrary::RestoreImportPaths(const TArray<FAssetImportPathSnapshot>& Snapshots)
{
    for (const auto& Snapshot:Snapshots)
    {
        if (!IsValid(Snapshot.Data)) return false;
        const auto Current=Snapshot.Data->ExtractFilenames();
        if (Current.Num()!=Snapshot.Filenames.Num()) return false;
        bool bModified=false;
        for (int32 Index=0;Index<Current.Num();++Index)
        {
            if (Current[Index]!=Snapshot.Filenames[Index])
            {
                if (!bModified) Snapshot.Data->Modify();
                Snapshot.Data->UpdateFilenameOnly(Snapshot.Filenames[Index],Index);
                bModified=true;
            }
        }
        if (bModified) Snapshot.Data->MarkPackageDirty();
        if (Snapshot.Data->ExtractFilenames()!=Snapshot.Filenames) return false;
    }
    return true;
}

bool UAssetOrganizationLibrary::EnsureCompatibilityRedirector(const FString& OriginalPackageName, UObject* Destination)
{
    if (!IsValid(Destination) || !OriginalPackageName.StartsWith(TEXT("/Game/")) ||
        Destination->GetOutermost()->GetName()==OriginalPackageName) return false;
    UPackage* Package=FindPackage(nullptr,*OriginalPackageName);
    if (!Package && FPackageName::DoesPackageExist(OriginalPackageName))
        Package=LoadPackage(nullptr,*OriginalPackageName,LOAD_None);
    if (!Package) Package=CreatePackage(*OriginalPackageName);
    const FString Name=FPackageName::GetLongPackageAssetName(OriginalPackageName);
    auto Ensure=[Package](const FString& ObjectName,UObject* Target)
    {
        UObject* Existing=StaticFindObjectFast(UObject::StaticClass(),Package,FName(*ObjectName));
        auto* Redirector=Cast<UObjectRedirector>(Existing);
        if (Existing && !Redirector) return false;
        if (!Redirector)
        {
            Redirector=NewObject<UObjectRedirector>(Package,FName(*ObjectName),RF_Public|RF_Standalone);
            FAssetRegistryModule::AssetCreated(Redirector);
        }
        Redirector->DestinationObject=Target;
        Redirector->MarkPackageDirty();
        return true;
    };
    if (!Ensure(Name,Destination)) return false;
    if (Destination->IsA<UWorld>()) Package->ThisContainsMap();
    if (auto* Blueprint=Cast<UBlueprint>(Destination))
    {
        if (Blueprint->GeneratedClass && !Ensure(Name+TEXT("_C"),Blueprint->GeneratedClass)) return false;
    }
    return true;
}

bool UAssetOrganizationLibrary::MoveReviewedAsset(UObject* Asset, const FString& DestinationPackageName)
{
    // Scope is intentionally limited: no maps, no asset-name changes, no overwrite.
    if (!IsValid(Asset) || Asset->IsA<UWorld>() || !Asset->IsAsset() ||
        !Asset->GetOutermost()->GetName().StartsWith(TEXT("/Game/")) ||
        !DestinationPackageName.StartsWith(TEXT("/Game/CyberCafe/")) ||
        FPackageName::GetLongPackageAssetName(DestinationPackageName)!=Asset->GetName() ||
        FPackageName::DoesPackageExist(DestinationPackageName)) return false;
    const FString OriginalPackageName=Asset->GetOutermost()->GetName();
    ObjectTools::FPackageGroupName Target;
    Target.PackageName=DestinationPackageName;
    Target.ObjectName=Asset->GetName();
    TSet<UPackage*> RefusedPackages;
    FText Error;
    if (!ObjectTools::RenameSingleObject(Asset,Target,RefusedPackages,Error,nullptr,true))
    {
        UE_LOG(LogTemp,Error,TEXT("Reviewed asset move failed: %s"),*Error.ToString());
        return false;
    }
    return EnsureCompatibilityRedirector(OriginalPackageName,Asset);
}

bool UAssetOrganizationLibrary::MoveMainCafeMap(UWorld* World)
{
    const FString Source=TEXT("/Game/VRTemplate/Maps/L_Cafe");
    const FString Destination=TEXT("/Game/CyberCafe/Maps/L_Cafe");
    if (!IsValid(World) || World->GetOutermost()->GetName()!=Source || FPackageName::DoesPackageExist(Destination)) return false;
    ObjectTools::FPackageGroupName Target;
    Target.PackageName=Destination;
    Target.ObjectName=TEXT("L_Cafe");
    TSet<UPackage*> Refused;
    FText Error;
    if (!ObjectTools::RenameSingleObject(World,Target,Refused,Error,nullptr,true))
    {
        UE_LOG(LogTemp,Error,TEXT("Main cafe map move failed: %s"),*Error.ToString());
        return false;
    }
    World->GetOutermost()->ThisContainsMap();
    return EnsureCompatibilityRedirector(Source,World);
}

TArray<UPackage*> UAssetOrganizationLibrary::LoadProjectPackages(const TArray<FString>& PackageNames)
{
    TArray<UPackage*> Result;
    for (const FString& Name:PackageNames)
    {
        if (!Name.StartsWith(TEXT("/Game/"))) return {};
        UPackage* Package=LoadPackage(nullptr,*Name,LOAD_None);
        if (!Package) return {};
        Result.AddUnique(Package);
    }
    return Result;
}

void UAssetOrganizationLibrary::MountOriginalBackupForAudit()
{
    // Metadata scan only: the audit never loads or saves assets under this mount.
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("AssetOrganization/Backup/Content/"));
    FPackageName::RegisterMountPoint(TEXT("/AssetOrganizationBaseline/"),Directory);
}
#endif
