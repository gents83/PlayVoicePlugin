// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceBlueprintLibrary.h"
#include "PlayVoiceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayVoiceBlueprint, Log, All);

UAudioComponent* UPlayVoiceBlueprintLibrary::PlayCharacterVoice(
	const UObject* WorldContextObject,
	FText CharacterName,
	FName StringTableId,
	FString Key,
	FString LanguageCode,
	UAudioComponent* TargetAudioComponent,
	FVector Location,
	bool bAttachToActor,
	AActor* AttachToActor)
{
	UE_LOG(LogPlayVoiceBlueprint, Log, TEXT("PlayCharacterVoice: Request CharacterName='%s', StringTableId='%s', Key='%s', Language='%s'."), *CharacterName.ToString(), *StringTableId.ToString(), *Key, *LanguageCode);
	if (!WorldContextObject || CharacterName.IsEmpty() || StringTableId.IsNone() || Key.IsEmpty())
	{
		UE_LOG(LogPlayVoiceBlueprint, Warning, TEXT("PlayCharacterVoice: Invalid input. WorldContext=%s CharacterNameEmpty=%d StringTableIdNone=%d KeyEmpty=%d."), WorldContextObject ? TEXT("valid") : TEXT("null"), CharacterName.IsEmpty(), StringTableId.IsNone(), Key.IsEmpty());
		return nullptr;
	}

	const FName CharacterNameId(*CharacterName.ToString());
	if (CharacterNameId.IsNone())
	{
		UE_LOG(LogPlayVoiceBlueprint, Warning, TEXT("PlayCharacterVoice: CharacterName '%s' converted to None."), *CharacterName.ToString());
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		UE_LOG(LogPlayVoiceBlueprint, Warning, TEXT("PlayCharacterVoice: WorldContext '%s' has no world."), *GetNameSafe(WorldContextObject));
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogPlayVoiceBlueprint, Warning, TEXT("PlayCharacterVoice: World '%s' has no GameInstance."), *GetNameSafe(World));
		return nullptr;
	}

	UPlayVoiceSubsystem* Subsystem = GameInstance->GetSubsystem<UPlayVoiceSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogPlayVoiceBlueprint, Warning, TEXT("PlayCharacterVoice: PlayVoiceSubsystem is unavailable."));
		return nullptr;
	}

	UAudioComponent* Result = Subsystem->PlayCharacterVoiceByIdentifiers(
		WorldContextObject,
		CharacterNameId,
		StringTableId,
		Key,
		LanguageCode,
		TargetAudioComponent,
		Location,
		bAttachToActor,
		AttachToActor
	);
	UE_LOG(LogPlayVoiceBlueprint, Log, TEXT("PlayCharacterVoice: Playback result=%s."), Result ? TEXT("started") : TEXT("not started"));
	return Result;
}

UAudioComponent* UPlayVoiceBlueprintLibrary::PlayCharacterVoiceFromKey(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	FName Key,
	FString LanguageCode,
	UAudioComponent* TargetAudioComponent,
	FVector Location,
	bool bAttachToActor,
	AActor* AttachToActor)
{
	if (!WorldContextObject || !CharacterVoiceAsset || Key.IsNone())
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	UPlayVoiceSubsystem* Subsystem = GameInstance->GetSubsystem<UPlayVoiceSubsystem>();
	if (!Subsystem)
	{
		return nullptr;
	}

	return Subsystem->PlayCharacterVoiceFromKey(
		WorldContextObject,
		CharacterVoiceAsset,
		Key,
		LanguageCode,
		TargetAudioComponent,
		Location,
		bAttachToActor,
		AttachToActor
	);
}

void UPlayVoiceBlueprintLibrary::PrecacheCharacterVoiceLines(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	FString LanguageCode)
{
	if (!WorldContextObject || !CharacterVoiceAsset)
	{
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UPlayVoiceSubsystem* Subsystem = GameInstance->GetSubsystem<UPlayVoiceSubsystem>();
	if (Subsystem)
	{
		Subsystem->PrecacheAllVoiceLines(CharacterVoiceAsset, LanguageCode, FOnPrecacheFinished());
	}
}

void UPlayVoiceBlueprintLibrary::GenerateVoiceSoundWave(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	FString TextLine,
	FString LanguageCode,
	FOnPlayVoiceGenerated OnComplete)
{
	if (!WorldContextObject || !CharacterVoiceAsset)
	{
		OnComplete.ExecuteIfBound(false, nullptr);
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		OnComplete.ExecuteIfBound(false, nullptr);
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		OnComplete.ExecuteIfBound(false, nullptr);
		return;
	}

	UPlayVoiceSubsystem* Subsystem = GameInstance->GetSubsystem<UPlayVoiceSubsystem>();
	if (Subsystem)
	{
		Subsystem->SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, LanguageCode, [OnComplete](bool bSuccess, USoundWave* SoundWave)
		{
			OnComplete.ExecuteIfBound(bSuccess, SoundWave);
		});
	}
	else
	{
		OnComplete.ExecuteIfBound(false, nullptr);
	}
}

bool UPlayVoiceBlueprintLibrary::IsCharacterVoiceModelGenerated(const UCharacterVoiceAsset* CharacterVoiceAsset, FString LanguageCode)
{
	if (!CharacterVoiceAsset)
	{
		return false;
	}
	return CharacterVoiceAsset->IsModelGenerated(LanguageCode);
}
