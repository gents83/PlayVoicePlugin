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
	 * Plays a precached character voice line using CharacterName, StringTableId, and a String Table key.
	 * CharacterName is Text; StringTableId is FName; Key matches StringTableIdAndKeyFromText output.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static UAudioComponent* PlayCharacterVoice(
		const UObject* WorldContextObject,
		FText CharacterName,
		FName StringTableId,
		FString Key,
		FString LanguageCode = TEXT(""),
		UAudioComponent* TargetAudioComponent = nullptr,
		FVector Location = FVector::ZeroVector,
		bool bAttachToActor = false,
		AActor* AttachToActor = nullptr
	);

	/**
	 * Plays a character voice line given a String Table Key using pre-rendered sound waves from referenced PlayVoiceLines assets.
	 * Plays IMMEDIATELY with ZERO DELAY if precached.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	static UAudioComponent* PlayCharacterVoiceFromKey(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FName Key,
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
