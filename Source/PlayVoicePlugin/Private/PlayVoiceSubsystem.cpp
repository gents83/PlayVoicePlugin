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
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

UPlayVoiceSubsystem* UPlayVoiceSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject)
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				return GI->GetSubsystem<UPlayVoiceSubsystem>();
			}
		}
	}
	return nullptr;
}

void UPlayVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UPlayVoiceSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UPlayVoiceSubsystem::PrecacheAllVoiceLines(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& LanguageCode, FOnPrecacheFinished OnComplete)
{
	if (!CharacterVoiceAsset)
	{
		OnComplete.ExecuteIfBound(0);
		return;
	}

	FString TargetLang = LanguageCode.IsEmpty() ? CharacterVoiceAsset->DefaultLanguage : LanguageCode;
	const FCharacterLanguageData* LangData = CharacterVoiceAsset->FindLanguageData(TargetLang);

	TArray<FString> RefFiles = CharacterVoiceAsset->GetResolvedReferenceAudioFilesForLanguage(TargetLang);
	if (RefFiles.Num() == 0)
	{
		OnComplete.ExecuteIfBound(0);
		return;
	}

	OnComplete.ExecuteIfBound(RefFiles.Num());
}

void UPlayVoiceSubsystem::PrecacheVoiceLine(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, FOnVoiceSynthesized OnComplete)
{
	PrecacheVoiceLine(CharacterVoiceAsset, TextLine, LanguageCode, [OnComplete](bool bSuccess, USoundWave* SoundWave)
	{
		OnComplete.ExecuteIfBound(bSuccess, SoundWave);
	});
}

void UPlayVoiceSubsystem::PrecacheVoiceLine(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete)
{
	if (!CharacterVoiceAsset || TextLine.IsEmpty())
	{
		if (OnComplete)
		{
			OnComplete(false, nullptr);
		}
		return;
	}

	if (USoundWave* Existing = CharacterVoiceAsset->GetPrecachedVoiceLine(TextLine, LanguageCode))
	{
		if (OnComplete)
		{
			OnComplete(true, Existing);
		}
		return;
	}

	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = CharacterVoiceAsset;
	SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, LanguageCode, [WeakAsset, TextLine, LanguageCode, OnComplete](bool bSuccess, USoundWave* SoundWave)
	{
		if (bSuccess && SoundWave && WeakAsset.IsValid())
		{
			WeakAsset->CacheVoiceLine(TextLine, SoundWave, LanguageCode);
		}
		if (OnComplete)
		{
			OnComplete(bSuccess, SoundWave);
		}
	});
}

UAudioComponent* UPlayVoiceSubsystem::PlayCharacterVoice(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	const FString& TextLine,
	const FString& LanguageCode,
	UAudioComponent* TargetAudioComponent,
	FVector Location,
	bool bAttachToActor,
	AActor* AttachToActor)
{
	if (!CharacterVoiceAsset || TextLine.IsEmpty())
	{
		return nullptr;
	}

	FString TargetLang = LanguageCode.IsEmpty() ? CharacterVoiceAsset->DefaultLanguage : LanguageCode;

	// ZERO DELAY CHECK: If precached, play immediately!
	if (USoundWave* CachedSound = CharacterVoiceAsset->GetPrecachedVoiceLine(TextLine, TargetLang))
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

	SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, TargetLang, [this, WorldContextObject, WeakAsset, TextLine, TargetLang, WeakAudioComponent, Location, bAttachToActor, WeakAttachActor](bool bSuccess, USoundWave* SoundWave)
	{
		if (bSuccess && SoundWave)
		{
			if (WeakAsset.IsValid())
			{
				WeakAsset->CacheVoiceLine(TextLine, SoundWave, TargetLang);
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
	});

	return nullptr;
}

void UPlayVoiceSubsystem::SynthesizeVoiceLineAsync(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, FOnVoiceSynthesized OnComplete)
{
	SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, LanguageCode, [OnComplete](bool bSuccess, USoundWave* SoundWave)
	{
		OnComplete.ExecuteIfBound(bSuccess, SoundWave);
	});
}

void UPlayVoiceSubsystem::SynthesizeVoiceLineAsync(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& TextLine, const FString& LanguageCode, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete)
{
	if (!CharacterVoiceAsset || TextLine.IsEmpty())
	{
		if (OnComplete)
		{
			OnComplete(false, nullptr);
		}
		return;
	}

	FString TargetLang = LanguageCode.IsEmpty() ? CharacterVoiceAsset->DefaultLanguage : LanguageCode;
	const FCharacterLanguageData* LangData = CharacterVoiceAsset->FindLanguageData(TargetLang);

	float Speed = LangData ? LangData->Speed : 1.0f;
	FString EmbeddingData = LangData ? LangData->ToneColorEmbeddingData : TEXT("");

	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("character_name"), CharacterVoiceAsset->CharacterName.ToString());
	JsonObj->SetStringField(TEXT("text"), TextLine);
	JsonObj->SetStringField(TEXT("language"), TargetLang);
	JsonObj->SetNumberField(TEXT("speed"), Speed);
	JsonObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

	TArray<FString> AudioPaths = CharacterVoiceAsset->GetResolvedReferenceAudioFilesForLanguage(TargetLang);
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
			if (OnComplete)
			{
				OnComplete(false, nullptr);
			}
			return;
		}

		USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(ResponseBytes);
		if (OnComplete)
		{
			OnComplete(SoundWave != nullptr, SoundWave);
		}
	});
}

void UPlayVoiceSubsystem::ExtractCharacterVoiceModel(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& LanguageCode, FOnVoiceSynthesized OnComplete)
{
	ExtractCharacterVoiceModel(CharacterVoiceAsset, LanguageCode, [OnComplete](bool bSuccess, USoundWave* SoundWave)
	{
		OnComplete.ExecuteIfBound(bSuccess, SoundWave);
	});
}

void UPlayVoiceSubsystem::ExtractCharacterVoiceModel(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& LanguageCode, TFunction<void(bool bSuccess, USoundWave* SoundWave)> OnComplete)
{
	if (!CharacterVoiceAsset)
	{
		if (OnComplete)
		{
			OnComplete(false, nullptr);
		}
		return;
	}

	FString TargetLang = LanguageCode.IsEmpty() ? CharacterVoiceAsset->DefaultLanguage : LanguageCode;

	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("character_name"), CharacterVoiceAsset->CharacterName.ToString());
	JsonObj->SetStringField(TEXT("language"), TargetLang);

	TArray<FString> AudioPaths = CharacterVoiceAsset->GetResolvedReferenceAudioFilesForLanguage(TargetLang);
	TArray<TSharedPtr<FJsonValue>> AudioPathValues;
	for (const FString& PathStr : AudioPaths)
	{
		AudioPathValues.Add(MakeShared<FJsonValueString>(PathStr));
	}
	JsonObj->SetArrayField(TEXT("reference_audio_files"), AudioPathValues);

	FString PayloadStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = CharacterVoiceAsset;
	SendTTSHttpRequest(TEXT("/extract"), PayloadStr, [WeakAsset, TargetLang, OnComplete](bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)
	{
		if (bSuccess && !ResponseString.IsEmpty())
		{
			TSharedPtr<FJsonObject> ResponseObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);
			if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
			{
				if (WeakAsset.IsValid())
				{
					FCharacterLanguageData* LangData = WeakAsset->FindLanguageData(TargetLang);
					if (LangData)
					{
						LangData->ToneColorEmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
						LangData->bIsModelGenerated = !LangData->ToneColorEmbeddingData.IsEmpty();
						WeakAsset->SaveModelToFile(TEXT(""), TargetLang);
					}
				}
				if (OnComplete)
				{
					OnComplete(true, nullptr);
				}
				return;
			}
		}
		if (OnComplete)
		{
			OnComplete(false, nullptr);
		}
	});
}

void UPlayVoiceSubsystem::SendTTSHttpRequest(const FString& Endpoint, const FString& JsonPayload, TFunction<void(bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)> Callback)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString FullUrl = (Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983")) + Endpoint;

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
