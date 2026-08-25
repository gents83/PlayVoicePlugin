// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceBlueprintLibrary.h"
#include "PlayVoiceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UAudioComponent* UPlayVoiceBlueprintLibrary::PlayCharacterVoice(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	FString TextLine,
	UAudioComponent* TargetAudioComponent,
	FVector Location,
	bool bAttachToActor,
	AActor* AttachToActor)
{
	if (!WorldContextObject || !CharacterVoiceAsset)
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

	return Subsystem->PlayCharacterVoice(
		WorldContextObject,
		CharacterVoiceAsset,
		TextLine,
		TargetAudioComponent,
		Location,
		bAttachToActor,
		AttachToActor
	);
}

void UPlayVoiceBlueprintLibrary::PrecacheCharacterVoiceLines(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset)
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
		Subsystem->PrecacheAllVoiceLines(CharacterVoiceAsset, FOnPrecacheFinished());
	}
}

void UPlayVoiceBlueprintLibrary::GenerateVoiceSoundWave(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	FString TextLine,
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
		Subsystem->SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, [OnComplete](bool bSuccess, USoundWave* SoundWave)
		{
			OnComplete.ExecuteIfBound(bSuccess, SoundWave);
		});
	}
	else
	{
		OnComplete.ExecuteIfBound(false, nullptr);
	}
}

bool UPlayVoiceBlueprintLibrary::IsCharacterVoiceModelGenerated(const UCharacterVoiceAsset* CharacterVoiceAsset)
{
	return CharacterVoiceAsset ? CharacterVoiceAsset->bIsModelGenerated : false;
}
