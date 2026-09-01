// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
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
	 * Plays a character voice line given a text string and optional language code using OpenVoice audio model.
	 * If the voice line is precached in CharacterVoiceAsset, it plays instantly with ZERO DELAY.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static UAudioComponent* PlayCharacterVoice(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FString TextLine,
		FString LanguageCode = TEXT(""),
		UAudioComponent* TargetAudioComponent = nullptr,
		FVector Location = FVector::ZeroVector,
		bool bAttachToActor = false,
		AActor* AttachToActor = nullptr
	);

	/**
	 * Plays a character voice line given a GameplayTag using pre-rendered sound waves from referenced PlayVoiceLines assets.
	 * Plays IMMEDIATELY with ZERO DELAY if precached.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static UAudioComponent* PlayCharacterVoiceFromTag(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FGameplayTag VoiceTag,
		FString LanguageCode = TEXT(""),
		UAudioComponent* TargetAudioComponent = nullptr,
		FVector Location = FVector::ZeroVector,
		bool bAttachToActor = false,
		AActor* AttachToActor = nullptr
	);

	/**
	 * Pre-caches dialog lines configured or discovered in the CharacterVoiceAsset to guarantee zero delay when played.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static void PrecacheCharacterVoiceLines(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FString LanguageCode = TEXT("")
	);

	/**
	 * Generates a USoundWave for a given text line and language code using OpenVoice TTS asynchronously.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static void GenerateVoiceSoundWave(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FString TextLine,
		FString LanguageCode,
		FOnPlayVoiceGenerated OnComplete
	);

	/**
	 * Helper function to check if a voice model has been extracted for the CharacterVoiceAsset in a specific language.
	 */
	UFUNCTION(BlueprintPure, Category = "PlayVoice")
	static bool IsCharacterVoiceModelGenerated(const UCharacterVoiceAsset* CharacterVoiceAsset, FString LanguageCode = TEXT(""));
};
