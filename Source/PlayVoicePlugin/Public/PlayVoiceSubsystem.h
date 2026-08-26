// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CharacterVoiceAsset.h"
#include "Components/AudioComponent.h"
#include "PlayVoiceSubsystem.generated.h"

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnVoiceSynthesized, bool, bSuccess, USoundWave*, GeneratedSoundWave);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnPrecacheFinished, int32, NumPrecachedLines);

/**
 * PlayVoiceSubsystem manages real-time audio playback, background synthesis,
 * HTTP API requests to the OpenVoice TTS backend, and zero-delay precaching.
 */
UCLASS()
class PLAYVOICEPLUGIN_API UPlayVoiceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Get instance of PlayVoiceSubsystem from any world context object in C++ code */
	static UPlayVoiceSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Pre-generates and caches all lines in LinesToPreprocess for the given CharacterVoiceAsset ensuring ZERO-DELAY playback during gameplay */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void PrecacheAllVoiceLines(UCharacterVoiceAsset* CharacterVoiceAsset, FOnPrecacheFinished OnComplete);

	/** Pre-generates and caches a single voice line asynchronously if not already cached */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void PrecacheVoiceLine(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, FOnVoiceSynthesized OnComplete);
	void PrecacheVoiceLine(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete);

	/**
	 * Plays a voice line using the reference CharacterVoiceAsset.
	 * If the line is already precached, it plays IMMEDIATELY with ZERO DELAY.
	 * If not precached, it triggers background synthesis and plays upon completion.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	UAudioComponent* PlayCharacterVoice(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		const FString& TextLine,
		UAudioComponent* TargetAudioComponent = nullptr,
		FVector Location = FVector::ZeroVector,
		bool bAttachToActor = false,
		AActor* AttachToActor = nullptr
	);

	/** Synthesizes voice line via OpenVoice backend service asynchronously */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void SynthesizeVoiceLineAsync(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, FOnVoiceSynthesized OnComplete);
	void SynthesizeVoiceLineAsync(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete);

	/** Request OpenVoice model extraction for a CharacterVoiceAsset */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void ExtractCharacterVoiceModel(UCharacterVoiceAsset* CharacterVoiceAsset, FOnVoiceSynthesized OnComplete);
	void ExtractCharacterVoiceModel(UCharacterVoiceAsset* CharacterVoiceAsset, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete);

private:
	void SendTTSHttpRequest(const FString& Endpoint, const FString& JsonPayload, TFunction<void(bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)> Callback);
};
