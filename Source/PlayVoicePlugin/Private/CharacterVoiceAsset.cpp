// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAsset.h"

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
