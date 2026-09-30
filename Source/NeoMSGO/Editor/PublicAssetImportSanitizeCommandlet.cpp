// Copyright NeoMSGO Project. All Rights Reserved.

#include "Editor/PublicAssetImportSanitizeCommandlet.h"

#if WITH_EDITOR

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Linker.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace PublicAssetImportSanitize
{
	struct FResult
	{
		FString Package;
		FString Status;
		int32 Replaced = 0;
		bool bPrivatePathRemains = false;
	};

	// Check only absolute source-machine path prefixes. Never copy matching text into logs or reports.
	bool IsPrivatePathAt(const TArray<uint8>& Bytes, const int64 Offset, const int32 Stride)
	{
		const int64 Count = Bytes.Num();
		auto CharacterAt = [&Bytes, Count, Offset, Stride](const int32 Index) -> uint8
		{
			const int64 Position = Offset + static_cast<int64>(Index) * Stride;
			if (Position < 0 || Position + Stride > Count || (Stride == 2 && Bytes[Position + 1] != 0))
			{
				return 0;
			}
			const uint8 Value = Bytes[Position];
			return Value >= 'A' && Value <= 'Z' ? Value + ('a' - 'A') : Value;
		};
		const uint8 Drive = CharacterAt(0);
		if (Drive < 'a' || Drive > 'z' || CharacterAt(1) != ':')
		{
			return false;
		}
		const uint8 Slash = CharacterAt(2);
		if (Slash != '/' && Slash != '\\')
		{
			return false;
		}
		const bool bSrc = CharacterAt(3) == 's' && CharacterAt(4) == 'r' && CharacterAt(5) == 'c';
		const bool bUsers = CharacterAt(3) == 'u' && CharacterAt(4) == 's' && CharacterAt(5) == 'e'
			&& CharacterAt(6) == 'r' && CharacterAt(7) == 's';
		const int32 BoundaryIndex = bUsers ? 8 : 6;
		const uint8 Boundary = CharacterAt(BoundaryIndex);
		return (bSrc || bUsers) && (Boundary == '/' || Boundary == '\\' || Boundary == 0);
	}

	bool ContainsPrivatePath(const FString& Filename)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Filename))
		{
			// A read failure must not be mistaken for a clean package.
			return true;
		}
		for (int64 Offset = 0; Offset < Bytes.Num(); ++Offset)
		{
			if (IsPrivatePathAt(Bytes, Offset, 1) || IsPrivatePathAt(Bytes, Offset, 2))
			{
				return true;
			}
		}
		return false;
	}

	bool IsPublicExport(const FString& ProjectDir)
	{
		FString ManifestText;
		if (!FFileHelper::LoadFileToString(ManifestText, *FPaths::Combine(ProjectDir, TEXT("PublicSourceManifest.json"))))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Manifest;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestText);
		FString Kind;
		return FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid()
			&& Manifest->TryGetStringField(TEXT("kind"), Kind)
			&& Kind == TEXT("neomsgo-public-source-export");
	}

	bool IsInsideContent(const FString& Filename, const FString& ContentDir)
	{
		FString FullName = FPaths::ConvertRelativePathToFull(Filename);
		FPaths::NormalizeFilename(FullName);
		return FullName.StartsWith(ContentDir + TEXT("/"), ESearchCase::IgnoreCase);
	}

	bool WriteReport(const FString& ProjectDir, const TArray<FResult>& Results, const int32 Scanned, const int32 Candidates,
		const int32 Saved, const int32 Failures)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("kind"), TEXT("neomsgo-public-asset-import-sanitize-report"));
		Root->SetNumberField(TEXT("scanned_files"), Scanned);
		Root->SetNumberField(TEXT("candidate_files"), Candidates);
		Root->SetNumberField(TEXT("saved_packages"), Saved);
		Root->SetNumberField(TEXT("failed_packages"), Failures);
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FResult& Result : Results)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("package"), Result.Package);
			Entry->SetStringField(TEXT("status"), Result.Status);
			Entry->SetNumberField(TEXT("import_data_replaced"), Result.Replaced);
			Entry->SetBoolField(TEXT("private_path_remains"), Result.bPrivatePathRemains);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Root->SetArrayField(TEXT("results"), Entries);
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			return false;
		}
		const FString ReportDir = FPaths::Combine(ProjectDir, TEXT(".local"));
		if (!IFileManager::Get().MakeDirectory(*ReportDir, true))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(Json, *FPaths::Combine(ReportDir, TEXT("PublicAssetImportSanitizeReport.json")));
	}
}

int32 UPublicAssetImportSanitizeCommandlet::Main(const FString& Params)
{
	using namespace PublicAssetImportSanitize;
	if (!FParse::Param(*Params, TEXT("RequirePublicSource")))
	{
		UE_LOG(LogTemp, Error, TEXT("PublicAssetImportSanitize requires -RequirePublicSource."));
		return 2;
	}
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!IsPublicExport(ProjectDir))
	{
		UE_LOG(LogTemp, Error, TEXT("PublicAssetImportSanitize requires a valid public export manifest in this project."));
		return 2;
	}
	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::NormalizeDirectoryName(ContentDir);
	if (!IFileManager::Get().DirectoryExists(*ContentDir))
	{
		UE_LOG(LogTemp, Error, TEXT("PublicAssetImportSanitize requires an existing project Content directory."));
		return 2;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *ContentDir, TEXT("*"), true, false);
	TArray<FResult> Results;
	int32 Scanned = 0;
	int32 Candidates = 0;
	int32 Saved = 0;
	int32 Failures = 0;
	for (const FString& Filename : Files)
	{
		const FString Extension = FPaths::GetExtension(Filename, true);
		if (Extension != TEXT(".uasset") && Extension != TEXT(".umap"))
		{
			continue;
		}
		++Scanned;
		if (!IsInsideContent(Filename, ContentDir))
		{
			++Failures;
			continue;
		}
		if (!ContainsPrivatePath(Filename))
		{
			continue;
		}
		++Candidates;
		FResult Result;
		if (!FPackageName::TryConvertFilenameToLongPackageName(Filename, Result.Package)
			|| !Result.Package.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
		{
			Result.Status = TEXT("invalid_package_path");
			++Failures;
			Results.Add(MoveTemp(Result));
			continue;
		}
		const FString ExpectedFilename = FPackageName::LongPackageNameToFilename(Result.Package, Extension);
		FString NormalizedFilename = FPaths::ConvertRelativePathToFull(Filename);
		FString NormalizedExpected = FPaths::ConvertRelativePathToFull(ExpectedFilename);
		FPaths::NormalizeFilename(NormalizedFilename);
		FPaths::NormalizeFilename(NormalizedExpected);
		if (!NormalizedFilename.Equals(NormalizedExpected, ESearchCase::IgnoreCase) || Extension == TEXT(".umap"))
		{
			Result.Status = TEXT("unsupported_or_mismatched_content_file");
			++Failures;
			Results.Add(MoveTemp(Result));
			continue;
		}
		UPackage* Package = LoadPackage(nullptr, *Result.Package, LOAD_None);
		if (!Package)
		{
			Result.Status = TEXT("load_failed");
			++Failures;
			Results.Add(MoveTemp(Result));
			continue;
		}
		TArray<UObject*> Objects;
		GetObjectsWithPackage(Package, Objects, true);
		for (UObject* Owner : Objects)
		{
			if (!Owner || Owner->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			FObjectPropertyBase* ImportProperty = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), TEXT("AssetImportData"));
			if (!ImportProperty || ImportProperty->ArrayDim != 1)
			{
				continue;
			}
			UAssetImportData* Existing = Cast<UAssetImportData>(ImportProperty->GetObjectPropertyValue_InContainer(Owner));
			if (!Existing || Existing->GetOuter() != Owner || Existing->GetClass()->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			// A new instance of the same import-data class has empty source filenames,
			// Interchange node containers and serialized pipeline caches. Never alter the asset payload.
			UAssetImportData* Replacement = NewObject<UAssetImportData>(Owner, Existing->GetClass(), NAME_None, RF_Transactional);
			if (!Replacement || !Replacement->ExtractFilenames().IsEmpty())
			{
				continue;
			}
			ImportProperty->SetObjectPropertyValue_InContainer(Owner, Replacement);
			Existing->SetFlags(RF_Transient);
			Existing->ClearFlags(RF_Public | RF_Standalone);
			Owner->MarkPackageDirty();
			++Result.Replaced;
		}
		if (Result.Replaced == 0)
		{
			Result.Status = TEXT("no_replaceable_import_data");
			Result.bPrivatePathRemains = true;
			++Failures;
			Results.Add(MoveTemp(Result));
			continue;
		}
		ResetLoadersForSave(Package, *Filename);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, nullptr, *Filename, SaveArgs))
		{
			Result.Status = TEXT("save_failed");
			Result.bPrivatePathRemains = true;
			++Failures;
			Results.Add(MoveTemp(Result));
			continue;
		}
		++Saved;
		Result.bPrivatePathRemains = ContainsPrivatePath(Filename);
		Result.Status = Result.bPrivatePathRemains ? TEXT("private_path_remains_after_save") : TEXT("clean");
		Failures += Result.bPrivatePathRemains ? 1 : 0;
		Results.Add(MoveTemp(Result));
	}
	if (!WriteReport(ProjectDir, Results, Scanned, Candidates, Saved, Failures))
	{
		UE_LOG(LogTemp, Error, TEXT("PublicAssetImportSanitize could not write its local report."));
		return 3;
	}
	UE_LOG(LogTemp, Display, TEXT("PublicAssetImportSanitize: scanned=%d candidates=%d saved=%d failures=%d; see .local/PublicAssetImportSanitizeReport.json"),
		Scanned, Candidates, Saved, Failures);
	return Failures == 0 ? 0 : 1;
}

#endif // WITH_EDITOR
