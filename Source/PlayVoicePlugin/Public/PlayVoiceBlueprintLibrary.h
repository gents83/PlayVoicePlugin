// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CharacterVoiceAsset.h"
#include "Components/AudioComponent.h"
#include "PlayVoiceBlueprintLibrary.generated.h"

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnPlayVoiceGenerated, bool, bSuccess, USoundWave*, SoundWave);

/**
 * Blueprint Function Library for PlayVoice operations in Unreal Engine.
 */
UCLASS()
class PLAYVOICEPLUGIN_API UPlayVoiceBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Plays a character voice line given a text string using OpenVoice audio model.
	 * If the voice line is precached in CharacterVoiceAsset, it plays instantly with ZERO DELAY.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static UAudioComponent* PlayCharacterVoice(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FString TextLine,
		UAudioComponent* TargetAudioComponent = nullptr,
		FVector Location = FVector::ZeroVector,
		bool bAttachToActor = false,
		AActor* AttachToActor = nullptr
	);

	/**
	 * Pre-caches all dialog lines configured in the CharacterVoiceAsset to guarantee zero delay when played.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static void PrecacheCharacterVoiceLines(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset
	);

	/**
	 * Generates a USoundWave for a given text line using OpenVoice TTS asynchronously.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static void GenerateVoiceSoundWave(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FString TextLine,
		FOnPlayVoiceGenerated OnComplete
	);

	/**
	 * Helper function to check if a voice model has been extracted for the CharacterVoiceAsset.
	 */
	UFUNCTION(BlueprintPure, Category = "PlayVoice")
	static bool IsCharacterVoiceModelGenerated(const UCharacterVoiceAsset* CharacterVoiceAsset);
};
