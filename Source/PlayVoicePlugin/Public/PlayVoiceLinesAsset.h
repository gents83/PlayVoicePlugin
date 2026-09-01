// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "PlayVoiceLinesAsset.generated.h"

/**
 * Represents a single dialogue voice line entry mapped to a String Table Key, dialogue string, and reference audio file.
 */
USTRUCT(BlueprintType)
struct PLAYVOICEPLUGIN_API FPlayVoiceLineEntry
{
	GENERATED_BODY()

	/** Unique String Table key identifying this voice line entry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FName Key;

	/** Optional String Table override for this specific entry. If null, uses asset-level String Table */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TObjectPtr<UStringTable> StringTableOverride;

	/** Dialogue text string corresponding to this voice line key (automatically resolved from String Table if left empty) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FString TextLine;

	/** Reference audio file (WAV/MP3/FLAC) supplying cadence, speed, and emotion guide for synthesis, or recorded via Editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (FilePathFilter = "wav,mp3,flac"))
	FFilePath AudioFile;
};

/**
 * Data asset storing a list of PlayVoiceLine entries (String Table Key -> Dialogue Text & Guide Audio File)
 * used as reference source material for CharacterVoiceAssets.
 */
UCLASS(BlueprintType, Blueprintable)
class PLAYVOICEPLUGIN_API UPlayVoiceLinesAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPlayVoiceLinesAsset();

	/** Primary String Table used for resolving dialogue text strings for entries in this asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TObjectPtr<UStringTable> StringTable;

	/** List of dialogue voice line entries in this asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TArray<FPlayVoiceLineEntry> Lines;

	/** Finds an entry matching the specified String Table key. Returns nullptr if not found. */
	const FPlayVoiceLineEntry* FindLineByKey(FName InKey) const;

	/** Checks if this asset contains an entry for the specified String Table key */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool HasLineForKey(FName InKey) const;

	/** Resolves the dialogue text string for a given entry, looking up from String Table if TextLine is empty */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	FString GetResolvedTextLineForEntry(const FPlayVoiceLineEntry& Entry) const;

	/** Helper method to get the absolute disk path of the VoiceRecording folder in the same directory as this asset */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	FString GetVoiceRecordingFolderOnDisk() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
