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
	 * Creates a USoundWave in-memory object from raw PCM 16-bit audio buffer.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	static USoundWave* CreateSoundWaveFromPCM(const TArray<uint8>& PCMData, int32 SampleRate = 24000, int32 NumChannels = 1);

	/**
	 * Creates a USoundWave in-memory object from full WAV file binary buffer.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	static USoundWave* CreateSoundWaveFromWAVBuffer(const TArray<uint8>& WAVData);
};
