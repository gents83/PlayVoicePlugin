// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"

UCharacterVoiceAsset::UCharacterVoiceAsset()
	: CharacterName(TEXT("NewCharacter"))
	, Language(TEXT("EN"))
	, Speed(1.0f)
	, bIsModelGenerated(false)
{
}

void UCharacterVoiceAsset::CacheVoiceLine(const FString& TextLine, USoundWave* InSoundWave)
{
	if (!TextLine.IsEmpty() && InSoundWave)
	{
		FString NormalizedKey = TextLine.TrimStartAndEnd().ToLower();
		PrecachedSoundWaves.Add(NormalizedKey, InSoundWave);
	}
}

USoundWave* UCharacterVoiceAsset::GetPrecachedVoiceLine(const FString& TextLine) const
{
	FString NormalizedKey = TextLine.TrimStartAndEnd().ToLower();
	if (const TObjectPtr<USoundWave>* FoundSound = PrecachedSoundWaves.Find(NormalizedKey))
	{
		return *FoundSound;
	}
	return nullptr;
}

bool UCharacterVoiceAsset::HasPrecachedVoiceLine(const FString& TextLine) const
{
	FString NormalizedKey = TextLine.TrimStartAndEnd().ToLower();
	return PrecachedSoundWaves.Contains(NormalizedKey);
}

FString UCharacterVoiceAsset::GetAssetDiskFolder() const
{
	UPackage* Package = GetOutermost();
	if (Package && Package != GetTransientPackage())
	{
		FString PackagePath = Package->GetName();
		FString AssetFolder = FPaths::GetPath(PackagePath);
		FString DiskFolder = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(AssetFolder, TEXT("")));
		if (!DiskFolder.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*DiskFolder, true);
			return DiskFolder;
		}
	}
	FString SavedDir = FPaths::ProjectSavedDir() / TEXT("PlayVoice");
	IFileManager::Get().MakeDirectory(*SavedDir, true);
	return SavedDir;
}

bool UCharacterVoiceAsset::SaveModelToFile(const FString& InFilePath)
{
	FString TargetPath = InFilePath;
	if (TargetPath.IsEmpty())
	{
		FString TargetDir = GetAssetDiskFolder();
		TargetPath = TargetDir / FString::Printf(TEXT("%s_se.json"), *CharacterName.ToString());
	}

	if (FFileHelper::SaveStringToFile(ToneColorEmbeddingData, *TargetPath))
	{
		ModelCheckpointPath = TargetPath;
		return true;
	}
	return false;
}

TArray<FString> UCharacterVoiceAsset::GetResolvedReferenceAudioFiles() const
{
	TArray<FString> ResolvedFiles;
	for (const FFilePath& FilePath : ReferenceAudioFiles)
	{
		if (!FilePath.FilePath.IsEmpty() && FPaths::FileExists(FilePath.FilePath))
		{
			ResolvedFiles.AddUnique(FilePath.FilePath);
		}
	}

	if (!ReferenceAudioFolder.Path.IsEmpty())
	{
		FString FolderFullPath = ReferenceAudioFolder.Path;
		if (FPaths::IsRelative(FolderFullPath))
		{
			FolderFullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FolderFullPath);
		}

		TArray<FString> FoundFiles;
		IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.wav"), true, false);
		IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.mp3"), true, false);
		IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.flac"), true, false);

		for (const FString& FoundFile : FoundFiles)
		{
			ResolvedFiles.AddUnique(FoundFile);
		}
	}

	return ResolvedFiles;
}

bool UCharacterVoiceAsset::LoadModelFromFile(const FString& InFilePath)
{
	FString TargetPath = InFilePath.IsEmpty() ? ModelCheckpointPath : InFilePath;
	if (TargetPath.IsEmpty())
	{
		return false;
	}

	FString LoadedData;
	if (FFileHelper::LoadFileToString(LoadedData, *TargetPath))
	{
		ToneColorEmbeddingData = LoadedData;
		ModelCheckpointPath = TargetPath;
		bIsModelGenerated = !ToneColorEmbeddingData.IsEmpty();
		return true;
	}
	return false;
}
