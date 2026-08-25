// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceSubsystem.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceAudioUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

void UPlayVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UPlayVoiceSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UPlayVoiceSubsystem::PrecacheAllVoiceLines(UCharacterVoiceAsset* CharacterVoiceAsset, FOnPrecacheFinished OnComplete)
{
	if (!CharacterVoiceAsset)
	{
		OnComplete.ExecuteIfBound(0);
		return;
	}

	const TArray<FString>& Lines = CharacterVoiceAsset->LinesToPreprocess;
	if (Lines.Num() == 0)
	{
		OnComplete.ExecuteIfBound(0);
		return;
	}

	TSharedPtr<int32> RemainingCount = MakeShared<int32>(Lines.Num());
	TSharedPtr<int32> SuccessCount = MakeShared<int32>(0);
	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = CharacterVoiceAsset;

	for (const FString& Line : Lines)
	{
		if (WeakAsset.IsValid() && WeakAsset->HasPrecachedVoiceLine(Line))
		{
			(*SuccessCount)++;
			(*RemainingCount)--;
			if (*RemainingCount <= 0)
			{
				OnComplete.ExecuteIfBound(*SuccessCount);
			}
			continue;
		}

		SynthesizeVoiceLineAsync(CharacterVoiceAsset, Line, FOnVoiceSynthesized::CreateLambda([WeakAsset, Line, RemainingCount, SuccessCount, OnComplete](bool bSuccess, USoundWave* SoundWave)
		{
			if (bSuccess && SoundWave && WeakAsset.IsValid())
			{
				WeakAsset->CacheVoiceLine(Line, SoundWave);
				(*SuccessCount)++;
			}

			(*RemainingCount)--;
			if (*RemainingCount <= 0)
			{
				OnComplete.ExecuteIfBound(*SuccessCount);
			}
		}));
	}
}

void UPlayVoiceSubsystem::PrecacheVoiceLine(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, FOnVoiceSynthesized OnComplete)
{
	if (!CharacterVoiceAsset || TextLine.IsEmpty())
	{
		OnComplete.ExecuteIfBound(false, nullptr);
		return;
	}

	if (USoundWave* Existing = CharacterVoiceAsset->GetPrecachedVoiceLine(TextLine))
	{
		OnComplete.ExecuteIfBound(true, Existing);
		return;
	}

	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = CharacterVoiceAsset;
	SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, FOnVoiceSynthesized::CreateLambda([WeakAsset, TextLine, OnComplete](bool bSuccess, USoundWave* SoundWave)
	{
		if (bSuccess && SoundWave && WeakAsset.IsValid())
		{
			WeakAsset->CacheVoiceLine(TextLine, SoundWave);
		}
		OnComplete.ExecuteIfBound(bSuccess, SoundWave);
	}));
}

UAudioComponent* UPlayVoiceSubsystem::PlayCharacterVoice(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	const FString& TextLine,
	UAudioComponent* TargetAudioComponent,
	FVector Location,
	bool bAttachToActor,
	AActor* AttachToActor)
{
	if (!CharacterVoiceAsset || TextLine.IsEmpty())
	{
		return nullptr;
	}

	// ZERO DELAY CHECK: If precached, play immediately!
	if (USoundWave* CachedSound = CharacterVoiceAsset->GetPrecachedVoiceLine(TextLine))
	{
		if (TargetAudioComponent && TargetAudioComponent->IsValidLowLevel())
		{
			TargetAudioComponent->SetSound(CachedSound);
			TargetAudioComponent->Play();
			return TargetAudioComponent;
		}
		else if (bAttachToActor && AttachToActor && AttachToActor->IsValidLowLevel())
		{
			return UGameplayStatics::SpawnSoundAttached(CachedSound, AttachToActor->GetRootComponent());
		}
		else
		{
			return UGameplayStatics::SpawnSoundAtLocation(WorldContextObject ? WorldContextObject : GetWorld(), CachedSound, Location);
		}
	}

	// FALLBACK: Async synthesize and play when ready
	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = CharacterVoiceAsset;
	TWeakObjectPtr<UAudioComponent> WeakAudioComponent = TargetAudioComponent;
	TWeakObjectPtr<AActor> WeakAttachActor = AttachToActor;

	SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, FOnVoiceSynthesized::CreateLambda([this, WorldContextObject, WeakAsset, TextLine, WeakAudioComponent, Location, bAttachToActor, WeakAttachActor](bool bSuccess, USoundWave* SoundWave)
	{
		if (bSuccess && SoundWave)
		{
			if (WeakAsset.IsValid())
			{
				WeakAsset->CacheVoiceLine(TextLine, SoundWave);
			}

			if (WeakAudioComponent.IsValid())
			{
				WeakAudioComponent->SetSound(SoundWave);
				WeakAudioComponent->Play();
			}
			else if (bAttachToActor && WeakAttachActor.IsValid())
			{
				UGameplayStatics::SpawnSoundAttached(SoundWave, WeakAttachActor->GetRootComponent());
			}
			else
			{
				UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : GetWorld();
				if (World)
				{
					UGameplayStatics::SpawnSoundAtLocation(World, SoundWave, Location);
				}
			}
		}
	}));

	return nullptr;
}

void UPlayVoiceSubsystem::SynthesizeVoiceLineAsync(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, FOnVoiceSynthesized OnComplete)
{
	if (!CharacterVoiceAsset || TextLine.IsEmpty())
	{
		OnComplete.ExecuteIfBound(false, nullptr);
		return;
	}

	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("character_name"), CharacterVoiceAsset->CharacterName.ToString());
	JsonObj->SetStringField(TEXT("text"), TextLine);
	JsonObj->SetStringField(TEXT("language"), CharacterVoiceAsset->Language);
	JsonObj->SetNumberField(TEXT("speed"), CharacterVoiceAsset->Speed);
	JsonObj->SetStringField(TEXT("embedding_data"), CharacterVoiceAsset->ToneColorEmbeddingData);

	TArray<FString> AudioPaths;
	for (const FFilePath& Path : CharacterVoiceAsset->ReferenceAudioFiles)
	{
		AudioPaths.Add(Path.FilePath);
	}
	TArray<TSharedPtr<FJsonValue>> AudioPathValues;
	for (const FString& PathStr : AudioPaths)
	{
		AudioPathValues.Add(MakeShared<FJsonValueString>(PathStr));
	}
	JsonObj->SetArrayField(TEXT("reference_audio_files"), AudioPathValues);

	FString PayloadStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	SendTTSHttpRequest(TEXT("/synthesize"), PayloadStr, [OnComplete](bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)
	{
		if (!bSuccess || ResponseBytes.Num() == 0)
		{
			OnComplete.ExecuteIfBound(false, nullptr);
			return;
		}

		USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(ResponseBytes);
		OnComplete.ExecuteIfBound(SoundWave != nullptr, SoundWave);
	});
}

void UPlayVoiceSubsystem::ExtractCharacterVoiceModel(UCharacterVoiceAsset* CharacterVoiceAsset, FOnVoiceSynthesized OnComplete)
{
	if (!CharacterVoiceAsset)
	{
		OnComplete.ExecuteIfBound(false, nullptr);
		return;
	}

	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("character_name"), CharacterVoiceAsset->CharacterName.ToString());

	TArray<TSharedPtr<FJsonValue>> AudioPathValues;
	for (const FFilePath& Path : CharacterVoiceAsset->ReferenceAudioFiles)
	{
		AudioPathValues.Add(MakeShared<FJsonValueString>(Path.FilePath));
	}
	JsonObj->SetArrayField(TEXT("reference_audio_files"), AudioPathValues);

	FString PayloadStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = CharacterVoiceAsset;
	SendTTSHttpRequest(TEXT("/extract"), PayloadStr, [WeakAsset, OnComplete](bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)
	{
		if (bSuccess && !ResponseString.IsEmpty())
		{
			TSharedPtr<FJsonObject> ResponseObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);
			if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
			{
				if (WeakAsset.IsValid())
				{
					WeakAsset->ToneColorEmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
					WeakAsset->ModelCheckpointPath = ResponseObj->GetStringField(TEXT("model_checkpoint"));
					WeakAsset->bIsModelGenerated = true;
				}
				OnComplete.ExecuteIfBound(true, nullptr);
				return;
			}
		}
		OnComplete.ExecuteIfBound(false, nullptr);
	});
}

void UPlayVoiceSubsystem::SendTTSHttpRequest(const FString& Endpoint, const FString& JsonPayload, TFunction<void(bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)> Callback)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString FullUrl = (Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:8000")) + Endpoint;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(FullUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(JsonPayload);
	if (Settings)
	{
		HttpRequest->SetTimeout(Settings->RequestTimeout);
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([Callback](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Callback(true, Response->GetContent(), Response->GetContentAsString());
		}
		else
		{
			Callback(false, TArray<uint8>(), FString());
		}
	});

	HttpRequest->ProcessRequest();
}
