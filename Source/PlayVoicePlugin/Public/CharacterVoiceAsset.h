// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Sound/SoundWaveProcedural.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "CharacterVoiceAsset.generated.h"

/**
 * Represents a single dialogue voice line entry mapped to a String Table Key, dialogue string, reference audio file, and precached SoundWave.
 */
USTRUCT(BlueprintType)
struct PLAYVOICEPLUGIN_API FPlayVoiceLineEntry
{
	GENERATED_BODY()

	/** String Table asset providing the dialogue text for this entry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TObjectPtr<UStringTable> StringTable;

	/** String Table key identifying this voice line entry */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FName Key;

	/** Dialogue text string automatically resolved from the String Table and Key (Read-only) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlayVoice")
	FString TextLine;

	/** Reference audio file (WAV/MP3/FLAC) supplying cadence, speed, and emotion guide for synthesis, or recorded via Editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (FilePathFilter = "wav,mp3,flac"))
	FFilePath AudioFile;

	/** Optional imported SoundWave used as the guide track source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TObjectPtr<USoundWave> GuideSoundWave = nullptr;

	/** Legacy default-language SoundWave reference retained for asset migration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TObjectPtr<USoundWave> PrecachedSoundWave = nullptr;

	/** Precached SoundWaves indexed by normalized language code */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TMap<FString, TObjectPtr<USoundWave>> PrecachedSoundWavesByLanguage;
};


/**
 * Stores settings, reference audio, and OpenVoice model parameters for a specific language.
 */
USTRUCT(BlueprintType)
struct PLAYVOICEPLUGIN_API FCharacterLanguageData
{
	GENERATED_BODY()

	/** Language identifier code (e.g., "EN", "ES", "FR", "ZH", "JP", "DE", "IT") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FString LanguageCode = TEXT("EN");

	/** List of file paths to audio clips (WAV/MP3/FLAC) used as reference for this language */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (FilePathFilter = "wav,mp3,flac"))
	TArray<FFilePath> ReferenceAudioFiles;

	/** Folder path containing reference audio tracks for this language */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (ContentDir))
	FDirectoryPath ReferenceAudioFolder;

	/** Speech speed multiplier for this language (0.5 to 2.0, default: 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float Speed = 1.0f;

	/** Serialized OpenVoice tone color embedding data generated from reference audio for this language */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlayVoice")
	FString ToneColorEmbeddingData;

	/** Path to the saved OpenVoice speaker embedding checkpoint file for this language */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlayVoice")
	FString ModelCheckpointPath;

	/** Whether the OpenVoice model embedding has been successfully generated for this language */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlayVoice")
	bool bIsModelGenerated = false;
};

USTRUCT(BlueprintType)
struct PLAYVOICEPLUGIN_API FPrecachedVoiceLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FString LineText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlayVoice")
	TObjectPtr<USoundWave> GeneratedSoundWave = nullptr;
};

/**
 * CharacterVoiceAsset stores reference audio clips, OpenVoice model parameters,
 * and pre-rendered/precached voice lines for zero-delay playback in Blueprints across multiple languages.
 */
UCLASS(BlueprintType, Blueprintable)
class PLAYVOICEPLUGIN_API UCharacterVoiceAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UCharacterVoiceAsset();

	/** Unique identifier for the character voice */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FName CharacterName;

	/** Default or primary language code (e.g., "EN") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FString DefaultLanguage;

	/** List of language configurations for this character voice */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TArray<FCharacterLanguageData> Languages;

	/** List of dialogue voice line entries in this asset mapped to String Table keys */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TArray<FPlayVoiceLineEntry> VoiceLines;

	/** Clears all cached SoundWave entries in PrecachedSoundWaves and PrecachedKeySoundWaves */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void ClearPrecachedVoiceLines();

	/** Finds the language data for the specified language code (or default if empty). Returns nullptr if not found. */
	FCharacterLanguageData* FindLanguageData(const FString& InLanguageCode = TEXT(""));
	const FCharacterLanguageData* FindLanguageData(const FString& InLanguageCode = TEXT("")) const;

	/** Finds or adds language data for the specified language code */
	FCharacterLanguageData& GetOrAddLanguageData(const FString& InLanguageCode = TEXT("EN"));

	/** Resolves the dialogue text string for a given entry, looking up from String Table if TextLine is empty */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	FString GetResolvedTextLineForEntry(const FPlayVoiceLineEntry& Entry) const;

	/** Finds a voice line entry matching the specified String Table key. Returns nullptr if not found. */
	const FPlayVoiceLineEntry* FindVoiceLineByKey(FName InKey) const;

	/** Finds a voice line entry matching an exact String Table ID and key. */
	const FPlayVoiceLineEntry* FindVoiceLineByStringTableIdAndKey(FName StringTableId, const FString& Key) const;

	/** Gets a language-specific cached SoundWave for an exact String Table ID and key. */
	USoundWave* GetPrecachedVoiceLineForStringTableIdAndKey(FName StringTableId, const FString& Key, const FString& LanguageCode = TEXT("")) const;

	/** Checks if this asset contains an entry for the specified String Table key */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool HasVoiceLineForKey(FName InKey) const;

	/** Helper method to get the absolute disk path of the VoiceRecording folder in the same directory as this asset */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	FString GetVoiceRecordingFolderOnDisk() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PostLoad() override;

	/** Verifies and fixes audio file references in VoiceLines, placing recorded guide tracks in VoiceRecording subfolder */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void FixupVoiceLineAudioReferences();

	/** Register a precached SoundWave for a specific text line and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void CacheVoiceLine(const FString& TextLine, USoundWave* InSoundWave, const FString& LanguageCode = TEXT(""));

	/** Try retrieving a precached SoundWave for a given text line and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	USoundWave* GetPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode = TEXT("")) const;

	/** Check if a voice line is precached for a given language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool HasPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode = TEXT("")) const;

	/** Register a precached SoundWave for a specific String Table Key and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void CacheVoiceLineForKey(FName Key, USoundWave* InSoundWave, const FString& LanguageCode = TEXT(""));

	/** Try retrieving a precached SoundWave for a given String Table Key and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	USoundWave* GetPrecachedVoiceLineForKey(FName Key, const FString& LanguageCode = TEXT("")) const;

	/** Check if a voice line is precached for a given String Table Key and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool HasPrecachedVoiceLineForKey(FName Key, const FString& LanguageCode = TEXT("")) const;

	/** Makes a normalized lookup key for PrecachedKeySoundWaves map */
	static FString MakeKeyCacheKey(FName Key, const FString& LanguageCode);

	/** Save extracted model embedding data to a file on disk for a specific language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool SaveModelToFile(const FString& FilePath = TEXT(""), const FString& LanguageCode = TEXT(""));

	/** Load extracted model embedding data from a file on disk for a specific language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool LoadModelFromFile(const FString& FilePath = TEXT(""), const FString& LanguageCode = TEXT(""));


	/** Retrieves all resolved reference audio file paths from ReferenceAudioFiles and ReferenceAudioFolder for a language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	TArray<FString> GetResolvedReferenceAudioFilesForLanguage(const FString& LanguageCode = TEXT("")) const;

	/** Retrieves all resolved reference audio file paths across all configured languages */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	TArray<FString> GetResolvedReferenceAudioFiles() const;

	/** Helper static method to resolve audio file paths given file array and folder path */
	static TArray<FString> ResolveAudioFilesFromFolderAndFiles(const TArray<FFilePath>& FilePaths, const FDirectoryPath& FolderPath);

	/** Helper method to convert any folder path (relative, /Game/ content path, or absolute) to a full disk path */
	static FString ResolveFolderPathToDisk(const FString& InFolderPath);

	/** Converts a filesystem guide-track path to a canonical absolute path. */
	static FString ResolveAudioFilePath(const FString& InFilePath);

	/** Scans the asset directory on disk and automatically links any pre-rendered USoundWave assets into PrecachedSoundWaves */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void AutoLinkPrecachedSoundWaves();

	/** Gets the absolute folder path on disk where this asset is saved */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	FString GetAssetDiskFolder() const;

	/** Makes a normalized lookup key for PrecachedSoundWaves map */
	static FString MakeCacheKey(const FString& TextLine, const FString& LanguageCode);

	/** Convenience accessors for single-language / default language backwards compatibility */
	FString GetLanguage() const;
	float GetSpeed(const FString& LanguageCode = TEXT("")) const;
	bool IsModelGenerated(const FString& LanguageCode = TEXT("")) const;
};
