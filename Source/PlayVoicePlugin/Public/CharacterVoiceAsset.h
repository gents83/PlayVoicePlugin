// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sound/SoundWaveProcedural.h"
#include "CharacterVoiceAsset.generated.h"

USTRUCT(BlueprintType)
struct PLAYVOICEPLUGIN_API FVoiceLineGuideTrack
{
	GENERATED_BODY()

	/** Dialogue text line corresponding to this guide track */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FString LineText;

	/** Optional recorded reference audio guide track (WAV/MP3/FLAC) supplying custom performance, cadence, and emotion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (FilePathFilter = "wav,mp3,flac"))
	FFilePath GuideAudioFile;

	/** Optional emotion performance tag (e.g. "neutral", "happy", "angry", "sad", "excited") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	FString Emotion = TEXT("neutral");

	/** Speed multiplier override for this specific line (default 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float Speed = 1.0f;
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

	/** Optional guide tracks supplying recorded reference audio, speed, and emotion for specific dialogue lines in this language */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	TArray<FVoiceLineGuideTrack> GuideTracks;

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

	/** Whether to re-generate and overwrite existing voice line audio files during Blueprint pre-processing or model pipeline execution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayVoice")
	bool bRegenerateExistingVoiceLines = true;

	/** Map of pre-rendered SoundWaves keyed by normalized text line and language for zero-delay instant playback. Persisted upon saving asset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlayVoice")
	TMap<FString, TObjectPtr<USoundWave>> PrecachedSoundWaves;

	/** Clears all cached SoundWave entries in PrecachedSoundWaves */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void ClearPrecachedVoiceLines();

	/** Finds the language data for the specified language code (or default if empty). Returns nullptr if not found. */
	FCharacterLanguageData* FindLanguageData(const FString& InLanguageCode = TEXT(""));
	const FCharacterLanguageData* FindLanguageData(const FString& InLanguageCode = TEXT("")) const;

	/** Finds or adds language data for the specified language code */
	FCharacterLanguageData& GetOrAddLanguageData(const FString& InLanguageCode = TEXT("EN"));

	/** Register a precached SoundWave for a specific text line and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void CacheVoiceLine(const FString& TextLine, USoundWave* InSoundWave, const FString& LanguageCode = TEXT(""));

	/** Try retrieving a precached SoundWave for a given text line and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	USoundWave* GetPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode = TEXT("")) const;

	/** Check if a voice line is precached for a given language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool HasPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode = TEXT("")) const;

	/** Save extracted model embedding data to a file on disk for a specific language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool SaveModelToFile(const FString& FilePath = TEXT(""), const FString& LanguageCode = TEXT(""));

	/** Load extracted model embedding data from a file on disk for a specific language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	bool LoadModelFromFile(const FString& FilePath = TEXT(""), const FString& LanguageCode = TEXT(""));

	/** Finds the optional guide track configuration for a dialogue line and language if present */
	const FVoiceLineGuideTrack* FindGuideTrackForLine(const FString& TextLine, const FString& LanguageCode = TEXT("")) const;

	/** Resolves the optional guide track audio file path for a dialogue line and language if configured */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	FString GetResolvedGuideAudioFileForLine(const FString& TextLine, const FString& LanguageCode = TEXT("")) const;

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
