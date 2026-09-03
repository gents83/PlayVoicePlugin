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
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayVoice, Log, All);

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
	VoiceAssetsByCharacterName.Empty();
	Super::Deinitialize();
}

UCharacterVoiceAsset* UPlayVoiceSubsystem::FindVoiceAssetByCharacterName(FName CharacterName)
{
	if (CharacterName.IsNone())
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("FindVoiceAssetByCharacterName: CharacterName is None."));
		return nullptr;
	}

	if (!VoiceAssetsByCharacterName.Contains(CharacterName))
	{
		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByClass(UCharacterVoiceAsset::StaticClass()->GetClassPathName(), Assets, true);
		UE_LOG(LogPlayVoice, Log, TEXT("FindVoiceAssetByCharacterName: Asset Registry returned %d CharacterVoiceAsset assets for requested name '%s'."), Assets.Num(), *CharacterName.ToString());

		TArray<TWeakObjectPtr<UCharacterVoiceAsset>>& Matches = VoiceAssetsByCharacterName.Add(CharacterName);
		for (const FAssetData& AssetData : Assets)
		{
			if (UCharacterVoiceAsset* VoiceAsset = Cast<UCharacterVoiceAsset>(AssetData.GetAsset()))
			{
				if (VoiceAsset->CharacterName == CharacterName)
				{
					Matches.Add(VoiceAsset);
					UE_LOG(LogPlayVoice, Log, TEXT("FindVoiceAssetByCharacterName: Matched '%s' at '%s'."), *VoiceAsset->GetName(), *VoiceAsset->GetPathName());
				}
			}
		}
	}

	const TArray<TWeakObjectPtr<UCharacterVoiceAsset>>* Matches = VoiceAssetsByCharacterName.Find(CharacterName);
	if (!Matches || Matches->Num() != 1)
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("Character voice lookup for '%s' is missing or ambiguous (%d matches)."), *CharacterName.ToString(), Matches ? Matches->Num() : 0);
		return nullptr;
	}

	return (*Matches)[0].Get();
}

UAudioComponent* UPlayVoiceSubsystem::PlayCachedSoundWave(const UObject* WorldContextObject, USoundWave* SoundWave, UAudioComponent* TargetAudioComponent, FVector Location, bool bAttachToActor, AActor* AttachToActor) const
{
	if (!SoundWave)
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("PlayCachedSoundWave: SoundWave is null."));
		return nullptr;
	}

	if (TargetAudioComponent && TargetAudioComponent->IsValidLowLevel())
	{
		TargetAudioComponent->SetSound(SoundWave);
		TargetAudioComponent->Play();
		UE_LOG(LogPlayVoice, Log, TEXT("PlayCachedSoundWave: Playing on supplied AudioComponent '%s'."), *TargetAudioComponent->GetPathName());
		return TargetAudioComponent;
	}
	if (bAttachToActor && AttachToActor && AttachToActor->GetRootComponent())
	{
		UAudioComponent* Result = UGameplayStatics::SpawnSoundAttached(SoundWave, AttachToActor->GetRootComponent());
		UE_LOG(LogPlayVoice, Log, TEXT("PlayCachedSoundWave: Attached playback result=%s on '%s'."), Result ? TEXT("started") : TEXT("not started"), *AttachToActor->GetPathName());
		return Result;
	}
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : GetWorld();
	if (!World)
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("PlayCachedSoundWave: No world available for SoundWave '%s'."), *SoundWave->GetPathName());
		return nullptr;
	}
	UAudioComponent* Result = UGameplayStatics::SpawnSoundAtLocation(World, SoundWave, Location);
	UE_LOG(LogPlayVoice, Log, TEXT("PlayCachedSoundWave: Location playback result=%s at (%s)."), Result ? TEXT("started") : TEXT("not started"), *Location.ToString());
	return Result;
}

UAudioComponent* UPlayVoiceSubsystem::PlayCharacterVoiceByIdentifiers(const UObject* WorldContextObject, FName CharacterName, FName StringTableId, const FString& Key, const FString& LanguageCode, UAudioComponent* TargetAudioComponent, FVector Location, bool bAttachToActor, AActor* AttachToActor)
{
	UE_LOG(LogPlayVoice, Log, TEXT("PlayCharacterVoiceByIdentifiers: Character='%s', Table='%s', Key='%s', Language='%s'."), *CharacterName.ToString(), *StringTableId.ToString(), *Key, *LanguageCode);
	if (!WorldContextObject || CharacterName.IsNone() || StringTableId.IsNone() || Key.IsEmpty())
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("PlayCharacterVoiceByIdentifiers: Invalid input. WorldContext=%s CharacterNone=%d TableNone=%d KeyEmpty=%d."), WorldContextObject ? TEXT("valid") : TEXT("null"), CharacterName.IsNone(), StringTableId.IsNone(), Key.IsEmpty());
		return nullptr;
	}

	UCharacterVoiceAsset* VoiceAsset = FindVoiceAssetByCharacterName(CharacterName);
	if (!VoiceAsset)
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("PlayCharacterVoiceByIdentifiers: No unique CharacterVoiceAsset found for '%s'."), *CharacterName.ToString());
		return nullptr;
	}
	UE_LOG(LogPlayVoice, Log, TEXT("PlayCharacterVoiceByIdentifiers: Resolved voice asset '%s'."), *VoiceAsset->GetPathName());

	const FPlayVoiceLineEntry* Entry = VoiceAsset->FindVoiceLineByStringTableIdAndKey(StringTableId, Key);
	if (!Entry)
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("PlayCharacterVoiceByIdentifiers: No unique VoiceLines entry for table '%s' and key '%s' in '%s'."), *StringTableId.ToString(), *Key, *VoiceAsset->GetPathName());
		return nullptr;
	}

	USoundWave* SoundWave = VoiceAsset->GetPrecachedVoiceLineForStringTableIdAndKey(StringTableId, Key, LanguageCode);
	if (!SoundWave)
	{
		UE_LOG(LogPlayVoice, Warning, TEXT("PlayCharacterVoiceByIdentifiers: Entry matched, but no precached SoundWave exists for language '%s'."), *LanguageCode);
		return nullptr;
	}
	UE_LOG(LogPlayVoice, Log, TEXT("PlayCharacterVoiceByIdentifiers: Resolved SoundWave '%s'."), *SoundWave->GetPathName());

	UAudioComponent* Result = PlayCachedSoundWave(WorldContextObject, SoundWave, TargetAudioComponent, Location, bAttachToActor, AttachToActor);
	UE_LOG(LogPlayVoice, Log, TEXT("PlayCharacterVoiceByIdentifiers: Audio playback result=%s."), Result ? TEXT("started") : TEXT("not started"));
	return Result;
}

void UPlayVoiceSubsystem::PrecacheAllVoiceLines(UCharacterVoiceAsset* CharacterVoiceAsset, const FString& LanguageCode, FOnPrecacheFinished OnComplete)
{
	if (!CharacterVoiceAsset)
	{
		OnComplete.ExecuteIfBound(0);
		return;
	}

	int32 PrecachedCount = 0;
	for (const FPlayVoiceLineEntry& Entry : CharacterVoiceAsset->VoiceLines)
	{
		if (CharacterVoiceAsset->GetPrecachedVoiceLineForKey(Entry.Key, LanguageCode) != nullptr)
		{
			PrecachedCount++;
		}
	}

	OnComplete.ExecuteIfBound(PrecachedCount);
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

#if WITH_EDITOR
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
#else
	UE_LOG(LogPlayVoice, Warning, TEXT("PrecacheVoiceLine: Voice line '%s' is not precached and live TTS synthesis is disabled at runtime."), *TextLine);
	if (OnComplete)
	{
		OnComplete(false, nullptr);
	}
#endif
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

	// Runtime zero-delay playback using pre-generated SoundWave
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

	// Dynamic on-the-fly voice synthesis fallback if line is not precached
#if WITH_EDITOR
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	const bool bAllowOnTheFly = Settings ? Settings->bEnableOnTheFlySynthesis : false;
#else
	const bool bAllowOnTheFly = false;
#endif

	if (bAllowOnTheFly)
	{
		UE_LOG(LogPlayVoice, Log, TEXT("PlayCharacterVoice: Voice line '%s' (Lang: %s) is not precached in asset '%s'. Triggering on-the-fly OpenVoice synthesis..."), *TextLine, *TargetLang, *GetNameSafe(CharacterVoiceAsset));

		UWorld* World = WorldContextObject ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : GetWorld();
		if (!World && CharacterVoiceAsset)
		{
			World = GEngine->GetWorldFromContextObject(CharacterVoiceAsset, EGetWorldErrorMode::LogAndReturnNull);
		}

		UAudioComponent* AudioComp = TargetAudioComponent;
		if (!AudioComp && World)
		{
			AudioComp = UGameplayStatics::CreateSound2D(World, nullptr, 1.0f, 1.0f, 0.0f, nullptr, false, false);
			if (AudioComp)
			{
				if (bAttachToActor && AttachToActor && AttachToActor->IsValidLowLevel())
				{
					AudioComp->AttachToComponent(AttachToActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				}
				else if (Location != FVector::ZeroVector)
				{
					AudioComp->SetWorldLocation(Location);
				}
			}
		}

		TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = CharacterVoiceAsset;
		TWeakObjectPtr<UAudioComponent> WeakAudioComp = AudioComp;

		SynthesizeVoiceLineAsync(CharacterVoiceAsset, TextLine, TargetLang, [WeakAsset, TextLine, TargetLang, WeakAudioComp](bool bSuccess, USoundWave* SoundWave)
		{
			if (bSuccess && SoundWave)
			{
				if (WeakAsset.IsValid())
				{
					WeakAsset->CacheVoiceLine(TextLine, SoundWave, TargetLang);
				}
				if (WeakAudioComp.IsValid())
				{
					WeakAudioComp->SetSound(SoundWave);
					WeakAudioComp->Play();
				}
			}
			else
			{
				UE_LOG(LogPlayVoice, Warning, TEXT("PlayCharacterVoice: OpenVoice REST service is not running or dynamic synthesis failed for line '%s' (Lang: %s). Ensure service is running or pre-render dialogue lines."), *TextLine, *TargetLang);
			}
		});

		return AudioComp;
	}

	UE_LOG(LogPlayVoice, Warning, TEXT("PlayCharacterVoice: Voice line '%s' (Lang: %s) is not precached in asset '%s' and on-the-fly synthesis is disabled. Ensure all Blueprint dialogue lines are pre-processed in the Editor before runtime."), *TextLine, *TargetLang, *GetNameSafe(CharacterVoiceAsset));

	return nullptr;
}

UAudioComponent* UPlayVoiceSubsystem::PlayCharacterVoiceFromKey(
	const UObject* WorldContextObject,
	UCharacterVoiceAsset* CharacterVoiceAsset,
	FName Key,
	const FString& LanguageCode,
	UAudioComponent* TargetAudioComponent,
	FVector Location,
	bool bAttachToActor,
	AActor* AttachToActor)
{
	if (!CharacterVoiceAsset || Key.IsNone())
	{
		return nullptr;
	}

	FString TargetLang = LanguageCode.IsEmpty() ? CharacterVoiceAsset->DefaultLanguage : LanguageCode;

	// Instant zero-delay playback using pre-generated SoundWave for String Table Key
	if (USoundWave* CachedSound = CharacterVoiceAsset->GetPrecachedVoiceLineForKey(Key, TargetLang))
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

	// Lookup text line from VoiceLines in CharacterVoiceAsset if present
	FString EntryText;
	if (const FPlayVoiceLineEntry* Entry = CharacterVoiceAsset->FindVoiceLineByKey(Key))
	{
		EntryText = CharacterVoiceAsset->GetResolvedTextLineForEntry(*Entry);
	}

	if (!EntryText.IsEmpty())
	{
		return PlayCharacterVoice(WorldContextObject, CharacterVoiceAsset, EntryText, TargetLang, TargetAudioComponent, Location, bAttachToActor, AttachToActor);
	}

	UE_LOG(LogPlayVoice, Warning, TEXT("PlayCharacterVoiceFromKey: Key '%s' (Lang: %s) is not precached in asset '%s' and no matching VoiceLines entry was found."), *Key.ToString(), *TargetLang, *GetNameSafe(CharacterVoiceAsset));
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
#if WITH_EDITOR
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
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	const int32 OutputSampleRate = Settings && Settings->DefaultSampleRate > 0 ? Settings->DefaultSampleRate : 48000;
	const bool bImproveOutputQuality = Settings ? Settings->bImproveOutputQuality : true;
	JsonObj->SetNumberField(TEXT("sample_rate"), OutputSampleRate);
	JsonObj->SetBoolField(TEXT("improve_output"), bImproveOutputQuality);
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
#else
	UE_LOG(LogPlayVoice, Warning, TEXT("SynthesizeVoiceLineAsync: Dynamic synthesis via backend service is disabled at runtime. Pre-generate all dialogue lines in the Editor."));
	if (OnComplete)
	{
		OnComplete(false, nullptr);
	}
#endif
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
#if WITH_EDITOR
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
			if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid()
				&& ResponseObj->HasField(TEXT("status"))
				&& ResponseObj->GetStringField(TEXT("status")).Equals(TEXT("success"), ESearchCase::IgnoreCase)
				&& ResponseObj->HasField(TEXT("embedding_data")))
			{
				const FString EmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
				TSharedPtr<FJsonObject> EmbeddingObject;
				TSharedRef<TJsonReader<>> EmbeddingReader = TJsonReaderFactory<>::Create(EmbeddingData);
				const bool bEmbeddingValid = FJsonSerializer::Deserialize(EmbeddingReader, EmbeddingObject)
					&& EmbeddingObject.IsValid()
					&& EmbeddingObject->HasTypedField<EJson::Array>(TEXT("target_se"))
					&& EmbeddingObject->GetArrayField(TEXT("target_se")).Num() > 0;
				bool bSaved = false;
				if (bEmbeddingValid && WeakAsset.IsValid())
				{
					FCharacterLanguageData* LangData = WeakAsset->FindLanguageData(TargetLang);
					if (LangData)
					{
						LangData->ToneColorEmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
						bSaved = !LangData->ToneColorEmbeddingData.IsEmpty() && WeakAsset->SaveModelToFile(TEXT(""), TargetLang);
						LangData->bIsModelGenerated = bSaved;
					}
				}
				if (OnComplete)
				{
					OnComplete(bSaved, nullptr);
				}
				return;
			}
		}
		if (OnComplete)
		{
			OnComplete(false, nullptr);
		}
	});
#else
	UE_LOG(LogPlayVoice, Warning, TEXT("ExtractCharacterVoiceModel: Model extraction via backend service is disabled at runtime. Extract models in the Editor."));
	if (OnComplete)
	{
		OnComplete(false, nullptr);
	}
#endif
}

void UPlayVoiceSubsystem::SendTTSHttpRequest(const FString& Endpoint, const FString& JsonPayload, TFunction<void(bool bSuccess, const TArray<uint8>& ResponseBytes, const FString& ResponseString)> Callback)
{
#if WITH_EDITOR
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
		HttpRequest->SetActivityTimeout(Settings->RequestTimeout);
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
#else
	UE_LOG(LogPlayVoice, Warning, TEXT("SendTTSHttpRequest: HTTP requests to local TTS service are disabled at runtime."));
	if (Callback)
	{
		Callback(false, TArray<uint8>(), FString());
	}
#endif
}
