// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sound/SoundWaveProcedural.h"
#include "CharacterVoiceAsset.generated.h"

USTRUCT(BlueprintType)
struct PLAYVOICEPLUGIN_API FPrecachedVoiceLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Line")
	FString LineText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voice Line")
	TObjectPtr<USoundWave> GeneratedSoundWave = nullptr;
};

/**
 * CharacterVoiceAsset stores reference audio clips, OpenVoice model parameters,
 * and pre-rendered/precached voice lines for zero-delay playback in Blueprints.
 */
UCLASS(BlueprintType)
class PLAYVOICEPLUGIN_API UCharacterVoiceAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UCharacterVoiceAsset();

	/** Unique identifier for the character voice */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Voice Info")
	FName CharacterName;

	/** List of file paths to audio clips (WAV/MP3) used as reference for OpenVoice tone, speed, and color */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reference Audio", meta = (FilePathFilter = "wav,mp3,flac"))
	TArray<FFilePath> ReferenceAudioFiles;

	/** Target language for TTS synthesis (e.g., "EN", "ES", "FR", "ZH", "JP") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Configuration")
	FString Language;

	/** Speech speed multiplier (0.5 to 2.0, default: 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Configuration", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float Speed;

	/** Serialized OpenVoice tone color embedding data generated from reference audio */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVoice Model")
	FString ToneColorEmbeddingData;

	/** Path to the saved OpenVoice speaker embedding checkpoint file */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVoice Model")
	FString ModelCheckpointPath;

	/** Whether the OpenVoice model embedding has been successfully generated */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVoice Model")
	bool bIsModelGenerated;

	/** List of dialog/text lines that should be pre-processed & cached for zero-latency gameplay playback */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pre-processed Dialog Lines")
	TArray<FString> LinesToPreprocess;

	/** Map of pre-rendered SoundWaves keyed by normalized text line for zero-delay instant playback. Persisted upon saving asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pre-processed Dialog Lines")
	TMap<FString, TObjectPtr<USoundWave>> PrecachedSoundWaves;

	/** Register a precached SoundWave for a specific text line */
	UFUNCTION(BlueprintCallable, Category = "Character Voice")
	void CacheVoiceLine(const FString& TextLine, USoundWave* InSoundWave);

	/** Try retrieving a precached SoundWave for a given text line */
	UFUNCTION(BlueprintCallable, Category = "Character Voice")
	USoundWave* GetPrecachedVoiceLine(const FString& TextLine) const;

	/** Check if a voice line is precached */
	UFUNCTION(BlueprintCallable, Category = "Character Voice")
	bool HasPrecachedVoiceLine(const FString& TextLine) const;
};
