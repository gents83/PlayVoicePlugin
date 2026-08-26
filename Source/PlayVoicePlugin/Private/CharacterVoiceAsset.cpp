// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

bool UCharacterVoiceAsset::SaveModelToFile(const FString& InFilePath)
{
	FString TargetPath = InFilePath;
	if (TargetPath.IsEmpty())
	{
		FString SavedDir = FPaths::ProjectSavedDir() / TEXT("PlayVoice");
		IFileManager::Get().MakeDirectory(*SavedDir, true);
		TargetPath = SavedDir / FString::Printf(TEXT("%s_se.json"), *CharacterName.ToString());
	}

	if (FFileHelper::SaveStringToFile(ToneColorEmbeddingData, *TargetPath))
	{
		ModelCheckpointPath = TargetPath;
		return true;
	}
	return false;
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
