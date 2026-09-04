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
 * PlayVoiceSubsystem manages authored SoundWave playback and editor-only voice authoring requests.
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

	/** Pre-generates and caches all lines for the given CharacterVoiceAsset and language ensuring ZERO-DELAY playback during gameplay */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void PrecacheAllVoiceLines(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& LanguageCode, FOnPrecacheFinished OnComplete);

	/** Pre-generates and caches a single voice line asynchronously if not already cached */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void PrecacheVoiceLine(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, FOnVoiceSynthesized OnComplete);
	void PrecacheVoiceLine(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete);

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
		const FString& LanguageCode = TEXT(""),
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
	UAudioComponent* PlayCharacterVoiceFromKey(
		const UObject* WorldContextObject,
		UCharacterVoiceAsset* CharacterVoiceAsset,
		FName Key,
		const FString& LanguageCode = TEXT(""),
		UAudioComponent* TargetAudioComponent = nullptr,
		FVector Location = FVector::ZeroVector,
		bool bAttachToActor = false,
		AActor* AttachToActor = nullptr
	);

	/** Plays a language-specific cached line by character, String Table ID, and key. */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice", meta = (WorldContext = "WorldContextObject"))
	UAudioComponent* PlayCharacterVoiceByIdentifiers(
		const UObject* WorldContextObject,
		FName CharacterName,
		FName StringTableId,
		const FString& Key,
		const FString& LanguageCode = TEXT(""),
		UAudioComponent* TargetAudioComponent = nullptr,
		FVector Location = FVector::ZeroVector,
		bool bAttachToActor = false,
		AActor* AttachToActor = nullptr
	);

	/** Synthesizes voice line via OpenVoice backend service asynchronously */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void SynthesizeVoiceLineAsync(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, FOnVoiceSynthesized OnComplete);
	void SynthesizeVoiceLineAsync(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete);

	/** Request OpenVoice model extraction for a CharacterVoiceAsset and language */
	UFUNCTION(BlueprintCallable, Category = "PlayVoice")
	void ExtractCharacterVoiceModel(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& LanguageCode, FOnVoiceSynthesized OnComplete);
	void ExtractCharacterVoiceModel(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& LanguageCode, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete);

private:
	UCharacterVoiceAsset* FindVoiceAssetByCharacterName(FName CharacterName);
	UAudioComponent* PlayCachedSoundWave(const UObject* WorldContextObject, USoundWave* SoundWave, UAudioComponent* TargetAudioComponent, FVector Location, bool bAttachToActor, AActor* AttachToActor) const;
	void SendTTSHttpRequest(const FString& Endpoint, const FString& JsonPayload, TFunction<void(bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)> Callback);

	TMap<FName, TArray<TWeakObjectPtr<UCharacterVoiceAsset>>> VoiceAssetsByCharacterName;
};
