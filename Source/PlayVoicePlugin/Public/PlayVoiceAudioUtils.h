// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Sound/SoundWave.h"
#include "PlayVoiceAudioUtils.generated.h"

UCLASS()
class PLAYVOICEPLUGIN_API UPlayVoiceAudioUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates a complete RIFF WAV header and buffer from raw 16-bit PCM audio samples.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	static TArray<uint8> CreateWAVBufferFromPCM(const TArray<uint8>& PCMData, int32 SampleRate = 24000, int32 NumChannels = 1);

	/**
	 * Creates a USoundWave object from raw PCM 16-bit audio buffer.
	 * If Outer is specified, the SoundWave will be created in that Outer package/object.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	static USoundWave* CreateSoundWaveFromPCM(const TArray<uint8>& PCMData, int32 SampleRate = 24000, int32 NumChannels = 1, UObject* Outer = nullptr, FName Name = NAME_None);

	/**
	 * Creates a USoundWave object from full WAV file binary buffer.
	 * If Outer is specified, the SoundWave will be created in that Outer package/object.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	static USoundWave* CreateSoundWaveFromWAVBuffer(const TArray<uint8>& WAVData, UObject* Outer = nullptr, FName Name = NAME_None);

	/** Returns the peak absolute sample value in a 16-bit PCM buffer. */
	static int32 GetPCM16Peak(const TArray<uint8>& PCMData);

	/** Scales a 16-bit PCM buffer to the requested peak sample value. */
	static bool NormalizePCM16ToPeak(TArray<uint8>& PCMData, int32 TargetPeak);

	/**
	 * Exports a USoundWave asset to a temporary .wav disk file for backend service consumption.
	 * Returns the absolute disk path of the created .wav file, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	static FString ExportSoundWaveToTempWAVFile(USoundWave* SoundWave);
};
