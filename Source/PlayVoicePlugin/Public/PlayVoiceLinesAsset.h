// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "PlayVoiceLinesAsset.generated.h"

/**
 * Represents a single dialogue voice line entry mapped to a GameplayTag, dialogue string, and reference audio file.
 */
USTRUCT(BlueprintType)
struct PLAYVOICEPLUGIN_API FPlayVoiceLineEntry
{
	GENERATED_BODY()

	/** Unique GameplayTag identifying this voice line (e.g. Dialogue.Hero.Greeting) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FGameplayTag VoiceTag;

	/** Dialogue text string corresponding to this voice line tag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FString TextLine;

	/** Reference audio file (WAV/MP3/FLAC) supplying cadence, speed, and emotion guide for synthesis, or recorded via Editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (FilePathFilter = "wav,mp3,flac"))
	FFilePath AudioFile;
};

/**
 * Data asset storing a list of PlayVoiceLine entries (GameplayTag -> Dialogue Text & Guide Audio File)
 * used as reference source material for CharacterVoiceAssets.
 */
UCLASS(BlueprintType, Blueprintable)
class PLAYVOICEPLUGIN_API UPlayVoiceLinesAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPlayVoiceLinesAsset();

	/** List of dialogue voice line entries in this asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TArray<FPlayVoiceLineEntry> Lines;

	/** Finds an entry matching the specified GameplayTag. Returns nullptr if not found. */
	const FPlayVoiceLineEntry* FindLineByTag(const FGameplayTag& InTag) const;

	/** Checks if this asset contains an entry for the specified GameplayTag */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool HasLineForTag(const FGameplayTag& InTag) const;

	/** Helper method to get the absolute disk path of the VoiceRecording folder in the same directory as this asset */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	FString GetVoiceRecordingFolderOnDisk() const;
};
