// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAssetCustomization.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceAudioUtils.h"
#include "PlayVoicePluginEditorModule.h"
#include "Containers/Ticker.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Memory/SharedBuffer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharacterVoiceCustomization, Log, All);

static bool IsCompatibleOpenVoiceServiceResponse(const FHttpResponsePtr& Response)
{
	if (!Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		return false;
	}

	TSharedPtr<FJsonObject> HealthObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	return FJsonSerializer::Deserialize(Reader, HealthObject)
		&& HealthObject.IsValid()
		&& HealthObject->HasField(TEXT("ready"))
		&& HealthObject->GetBoolField(TEXT("ready"))
		&& HealthObject->HasField(TEXT("service_version"))
		&& HealthObject->GetStringField(TEXT("service_version")) == TEXT("1.2.0");
}

static FString MakeSafePackageComponent(const FString& InValue)
{
	FString SafeValue;
	for (TCHAR Character : InValue.TrimStartAndEnd())
	{
		if (FChar::IsAlnum(Character) || Character == TCHAR('_'))
		{
			SafeValue.AppendChar(Character);
		}
		else
		{
			SafeValue.AppendChar(TCHAR('_'));
		}
	}

	return SafeValue.IsEmpty() ? TEXT("Voice") : SafeValue;
}

static uint32 MakeVoiceLineIdentityHash(FName StringTableId, FName Key, const FString& LanguageCode)
{
	const FString Identity = StringTableId.ToString().ToLower() + TEXT("|") + Key.ToString().ToLower() + TEXT("|") + LanguageCode.ToUpper();
	return FCrc::StrCrc32(*Identity);
}

static bool GetSoundWavePCM16Data(USoundWave* SoundWave, TArray<uint8>& OutPCMData)
{
	if (!SoundWave)
	{
		return false;
	}

	if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize >= sizeof(int16))
	{
		OutPCMData.SetNumUninitialized(SoundWave->RawPCMDataSize);
		FMemory::Memcpy(OutPCMData.GetData(), SoundWave->RawPCMData, SoundWave->RawPCMDataSize);
		return true;
	}

#if WITH_EDITORONLY_DATA
	const FSharedBuffer Payload = SoundWave->RawData.GetPayload().Get();
	const uint8* Data = static_cast<const uint8*>(Payload.GetData());
	const int64 PayloadSize = static_cast<int64>(Payload.GetSize());
	for (int64 Offset = 12; Offset + 8 <= PayloadSize; ++Offset)
	{
		if (Data[Offset] == 'd' && Data[Offset + 1] == 'a' && Data[Offset + 2] == 't' && Data[Offset + 3] == 'a')
		{
			uint32 ChunkSize = 0;
			FMemory::Memcpy(&ChunkSize, Data + Offset + 4, sizeof(ChunkSize));
			const int64 AvailableSize = FMath::Min<int64>(ChunkSize, PayloadSize - Offset - 8);
			if (AvailableSize >= sizeof(int16))
			{
				OutPCMData.SetNumUninitialized(static_cast<int32>(AvailableSize));
				FMemory::Memcpy(OutPCMData.GetData(), Data + Offset + 8, AvailableSize);
				return true;
			}
		}
	}
#endif

	return false;
}

static bool UpdateSoundWavePCM16Data(USoundWave* SoundWave, const TArray<uint8>& PCMData)
{
	if (!SoundWave || PCMData.Num() < sizeof(int16) || SoundWave->NumChannels <= 0 || SoundWave->GetSampleRateForCurrentPlatform() <= 0)
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	const TArray<uint8> WAVData = UPlayVoiceAudioUtils::CreateWAVBufferFromPCM(PCMData, SoundWave->GetSampleRateForCurrentPlatform(), SoundWave->NumChannels);
	SoundWave->RawData.UpdatePayload(FSharedBuffer::Clone(WAVData.GetData(), WAVData.Num()));
#endif

	if (SoundWave->RawPCMData)
	{
		FMemory::Free(SoundWave->RawPCMData);
	}
	SoundWave->RawPCMDataSize = PCMData.Num();
	SoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(PCMData.Num()));
	FMemory::Memcpy(SoundWave->RawPCMData, PCMData.GetData(), PCMData.Num());
	SoundWave->Duration = static_cast<float>(PCMData.Num()) / static_cast<float>(SoundWave->GetSampleRateForCurrentPlatform() * SoundWave->NumChannels * sizeof(int16));
	SoundWave->TotalSamples = PCMData.Num() / (SoundWave->NumChannels * sizeof(int16));
	SoundWave->MarkPackageDirty();
	return true;
}

static void DeleteGeneratedSoundWavePackage(const FString& PackagePath)
{
	TArray<USoundWave*> ExistingSoundWaves;
	UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath);
	if (ExistingPackage)
	{
		TArray<UObject*> Objects;
		GetObjectsWithOuter(ExistingPackage, Objects, EGetObjectsFlags::None);
		for (UObject* Object : Objects)
		{
			if (USoundWave* SoundWave = Cast<USoundWave>(Object))
			{
				ExistingSoundWaves.AddUnique(SoundWave);
			}
		}
	}

	if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::Get().LoadModule("AssetRegistry");
	}
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPackageName(FName(*PackagePath), Assets);
		for (const FAssetData& AssetData : Assets)
		{
			if (USoundWave* SoundWave = Cast<USoundWave>(AssetData.GetAsset()))
			{
				ExistingSoundWaves.AddUnique(SoundWave);
				ExistingPackage = SoundWave->GetOutermost();
			}
		}

		for (USoundWave* SoundWave : ExistingSoundWaves)
		{
			AssetRegistryModule.AssetDeleted(SoundWave);
		}
	}

	for (USoundWave* SoundWave : ExistingSoundWaves)
	{
		if (SoundWave)
		{
			SoundWave->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			SoundWave->MarkAsGarbage();
		}
	}
	if (ExistingPackage && ExistingPackage != GetTransientPackage())
	{
		ExistingPackage->MarkAsGarbage();
	}
	if (ExistingSoundWaves.Num() > 0 || ExistingPackage)
	{
		// Allow normal editor garbage collection to release deleted packages.
	}

	FString PackageFilename;
	if (FPackageName::DoesPackageExist(PackagePath, &PackageFilename) && IFileManager::Get().FileExists(*PackageFilename))
	{
		IFileManager::Get().Delete(*PackageFilename);
	}
}

TSharedRef<IDetailCustomization> FCharacterVoiceAssetCustomization::MakeInstance()
{
	return MakeShareable(new FCharacterVoiceAssetCustomization());
}

void FCharacterVoiceAssetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() > 0)
	{
		TargetVoiceAsset = Cast<UCharacterVoiceAsset>(ObjectsBeingCustomized[0].Get());
		if (TargetVoiceAsset.IsValid())
		{
			TargetVoiceAsset->FixupVoiceLineAudioReferences();
			TargetVoiceAsset->AutoLinkPrecachedSoundWaves();
		}
	}

	IDetailCategoryBuilder& OpenVoiceCategory = DetailBuilder.EditCategory("OpenVoice Model Actions", FText::FromString("OpenVoice Model Actions"));

	OpenVoiceCategory.AddCustomRow(FText::FromString("Generate Model"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Model Extraction"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Generate OpenVoice Model"))
		.ToolTipText(FText::FromString("Extract tone color embeddings for all configured languages from reference audio clips and folders."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnGenerateModelClicked)
	];

	OpenVoiceCategory.AddCustomRow(FText::FromString("Generate Precached Sounds from VoiceLines"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("VoiceLines Pre-rendering"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Generate Precached Sounds from VoiceLines"))
		.ToolTipText(FText::FromString("Iterates over VoiceLines entries, uses entry audio files as speed/emotion guide tracks, and generates OpenVoice sound waves mapped automatically to String Table Keys."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnGenerateFromVoiceLinesClicked)
	];

	OpenVoiceCategory.AddCustomRow(FText::FromString("Normalize Precached Sound Levels"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromString("Precached Sound Levels"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.Text(FText::FromString("Normalize Precached Sound Levels"))
			.ToolTipText(FText::FromString("Scales all precached SoundWaves to the loudest non-silent wave in this CharacterVoiceAsset."))
			.OnClicked(this, &FCharacterVoiceAssetCustomization::OnNormalizePrecachedSoundLevelsClicked)
		];

	OpenVoiceCategory.AddCustomRow(FText::FromString("Clean Precached Sound Waves"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Clean Precached Assets"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Clean Precached Sound Waves"))
		.ToolTipText(FText::FromString("Clears precached voice lines entries on UCharacterVoiceAsset and deletes generated USoundWave package files from disk and project."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnCleanPrecachedSoundWavesClicked)
	];
}

static void EnsureServiceReadyAndExecute(TFunction<void(bool bReady)> OnComplete, int32 MaxAttempts = 300)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BaseUrl = Settings && !Settings->ServiceUrl.IsEmpty() ? Settings->ServiceUrl.TrimStartAndEnd() : TEXT("http://127.0.0.1:1983");
	BaseUrl.RemoveFromEnd(TEXT("/"));

	// First, test if service is already running and healthy
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> InitialHealthReq = FHttpModule::Get().CreateRequest();
	InitialHealthReq->SetURL(BaseUrl + TEXT("/health"));
	InitialHealthReq->SetVerb(TEXT("GET"));
	InitialHealthReq->SetTimeout(2.0f);

	InitialHealthReq->OnProcessRequestComplete().BindLambda([BaseUrl, MaxAttempts, OnComplete](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess)
	{
		bool bAlreadyReady = bSuccess && IsCompatibleOpenVoiceServiceResponse(Res);
		if (bAlreadyReady)
		{
			UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OpenVoice REST Service is already running and healthy at %s"), *BaseUrl);
			OnComplete(true);
			return;
		}

		// Service not currently responding, launch process and poll until ready
		Async(EAsyncExecution::Thread, [BaseUrl, MaxAttempts, OnComplete]()
		{
			FProcHandle ProcHandle;
			bool bStarted = FPlayVoicePluginEditorModule::StartOpenVoiceService(&ProcHandle);
			if (!bStarted)
			{
					UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("EnsureServiceReadyAndExecute: Could not launch OpenVoice service."));
					Async(EAsyncExecution::TaskGraphMainThread, [OnComplete]()
					{
						OnComplete(false);
					});
					return;
				}
			UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("StartOpenVoiceService requested from EnsureServiceReadyAndExecute (Started: %d)"), bStarted);

			Async(EAsyncExecution::TaskGraphMainThread, [BaseUrl, MaxAttempts, OnComplete]()
			{
				TSharedRef<int32> Attempts = MakeShared<int32>(0);

				struct FPollContext
				{
					TFunction<void(FPollContext& Self)> PollFunc;
				};

				TSharedRef<FPollContext> Context = MakeShared<FPollContext>();
				Context->PollFunc = [BaseUrl, Attempts, MaxAttempts, OnComplete, Context](FPollContext& Self)
				{
					(*Attempts)++;

					TSharedRef<IHttpRequest, ESPMode::ThreadSafe> PollReq = FHttpModule::Get().CreateRequest();
					PollReq->SetURL(BaseUrl + TEXT("/health"));
					PollReq->SetVerb(TEXT("GET"));
					PollReq->SetTimeout(2.0f);

					PollReq->OnProcessRequestComplete().BindLambda([Attempts, MaxAttempts, OnComplete, Context](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess)
					{
						bool bReady = bSuccess && IsCompatibleOpenVoiceServiceResponse(Res);
						if (bReady)
						{
							UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OpenVoice REST Service health check succeeded after %d attempts."), *Attempts);
							Context->PollFunc = nullptr;
							OnComplete(true);
						}
						else if (*Attempts < MaxAttempts)
						{
							UE_LOG(LogCharacterVoiceCustomization, Verbose, TEXT("OpenVoice health check attempt %d failed, polling again..."), *Attempts);
							FTickerDelegate TickerDelegate;
							TickerDelegate.BindLambda([Context](float DeltaTime)
							{
								Context->PollFunc(*Context);
								return false;
							});
							FTSTicker::GetCoreTicker().AddTicker(TickerDelegate, 0.8f);
						}
						else
						{
							UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OpenVoice REST Service health check timed out after %d attempts."), MaxAttempts);
							Context->PollFunc = nullptr;
							OnComplete(false);
						}
					});

					if (!PollReq->ProcessRequest())
					{
						UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("EnsureServiceReadyAndExecute: Failed to submit health poll request."));
							Context->PollFunc = nullptr;
							OnComplete(false);
						}
				};

				Context->PollFunc(*Context);
			});
		});
	});

	if (!InitialHealthReq->ProcessRequest())
	{
		UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("EnsureServiceReadyAndExecute: Failed to submit initial health request."));
		OnComplete(false);
	}
}

FReply FCharacterVoiceAssetCustomization::OnGenerateModelClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateModelClicked: Initiating non-blocking model extraction for asset '%s'"), *TargetVoiceAsset->GetName());
	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;

	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateModelClicked: Service health check failed. Could not connect to OpenVoice REST backend."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: Target voice asset is invalid or garbage collected."));
			return;
		}

		UCharacterVoiceAsset* Asset = WeakTargetAsset.Get();
		if (Asset->Languages.Num() == 0)
		{
			Asset->GetOrAddLanguageData(Asset->DefaultLanguage.IsEmpty() ? TEXT("EN") : Asset->DefaultLanguage);
		}

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString BaseUrl = Settings && !Settings->ServiceUrl.IsEmpty() ? Settings->ServiceUrl.TrimStartAndEnd() : TEXT("http://127.0.0.1:1983");
		BaseUrl.RemoveFromEnd(TEXT("/"));
		FString Url = BaseUrl + TEXT("/extract");
		float TimeoutSecs = Settings && Settings->RequestTimeout > 0.0f ? FMath::Max(Settings->RequestTimeout, 300.0f) : 300.0f;

		int32 ProcessedLangs = 0;
		TArray<FCharacterLanguageData*> ConfiguredLanguages;
		for (FCharacterLanguageData& LangData : Asset->Languages)
		{
			bool bHasRefAudioConfigured = (LangData.ReferenceAudioFiles.Num() > 0 || !LangData.ReferenceAudioFolder.Path.IsEmpty());
			if (bHasRefAudioConfigured)
			{
				ConfiguredLanguages.Add(&LangData);
				ProcessedLangs++;
			}
		}

		if (ProcessedLangs == 0)
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: No reference audio files or folders specified."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify Reference Audio Files or Reference Audio Folder for at least one language."));
			return;
		}

		int32 TotalTasksCount = ConfiguredLanguages.Num();
		TSharedPtr<int32> CompletedTasks = MakeShared<int32>(0);
		TSharedPtr<int32> FailedTasks = MakeShared<int32>(0);

		FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Extracting Model for {0} languages..."), FText::AsNumber(TotalTasksCount)));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseThrobber = true;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.FadeOutDuration = 0.5f;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}

		auto StepTaskProgress = [NotificationItem, CompletedTasks, FailedTasks, TotalTasksCount]()
		{
			(*CompletedTasks)++;
			if (NotificationItem.IsValid())
			{
				FText Msg = FText::Format(FText::FromString("PlayVoice: Model extraction ({0}/{1})..."), FText::AsNumber(*CompletedTasks), FText::AsNumber(TotalTasksCount));
				NotificationItem->SetText(Msg);

				if (*CompletedTasks >= TotalTasksCount)
				{
					if (*FailedTasks > 0)
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
						NotificationItem->SetText(FText::Format(FText::FromString("PlayVoice: Model extraction finished with {0} errors."), FText::AsNumber(*FailedTasks)));
						UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: Model extraction completed with %d errors."), *FailedTasks);
					}
					else
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
						NotificationItem->SetText(FText::FromString("PlayVoice: OpenVoice model extraction complete!"));
						UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateModelClicked: OpenVoice model extraction completed successfully for all languages."));
					}
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
			}
		};

		for (FCharacterLanguageData* LangDataPtr : ConfiguredLanguages)
		{
			FCharacterLanguageData& LangData = *LangDataPtr;
			TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFilesForLanguage(LangData.LanguageCode);

			if (RefAudioFiles.Num() == 0)
			{
				(*FailedTasks)++;
				FString ConfiguredFolderPath = LangData.ReferenceAudioFolder.Path;
				FString ErrMsg = FString::Printf(TEXT("No valid reference audio files found on disk for language '%s'. Configured folder: '%s', configured files count: %d. Check that audio files exist on disk at specified location."), *LangData.LanguageCode, *ConfiguredFolderPath, LangData.ReferenceAudioFiles.Num());
				UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateModelClicked: %s"), *ErrMsg);
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrMsg));
				StepTaskProgress();
				continue;
			}

			TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
			JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
			JsonObj->SetStringField(TEXT("language"), LangData.LanguageCode);

			TArray<TSharedPtr<FJsonValue>> AudioPathValues;
			for (const FString& Path : RefAudioFiles)
			{
				AudioPathValues.Add(MakeShared<FJsonValueString>(Path));
			}
			JsonObj->SetArrayField(TEXT("reference_audio_files"), AudioPathValues);

			FString PayloadStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
			FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
			HttpRequest->SetURL(Url);
			HttpRequest->SetVerb(TEXT("POST"));
			HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			HttpRequest->SetContentAsString(PayloadStr);
			HttpRequest->SetTimeout(TimeoutSecs);
				HttpRequest->SetActivityTimeout(TimeoutSecs);

			FString CurrentLangCode = LangData.LanguageCode;

			HttpRequest->OnProcessRequestComplete().BindLambda([HttpRequest, WeakTargetAsset, CurrentLangCode, StepTaskProgress, FailedTasks](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
			{
				bool bSuccess = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
				int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
				FString ResponseContent = Response.IsValid() ? Response->GetContentAsString() : TEXT("");
				FString ErrorMessage;

				if (!bWasSuccessful || !Response.IsValid())
				{
					ErrorMessage = TEXT("Could not connect to service endpoint.");
				}
				else if (!EHttpResponseCodes::IsOk(ResponseCode))
				{
					ErrorMessage = FString::Printf(TEXT("HTTP request failed with status code %d."), ResponseCode);
				}

				TSharedPtr<FJsonObject> ResponseObj;
				if (!ResponseContent.IsEmpty())
				{
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
					if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
					{
						if (ResponseObj->HasTypedField<EJson::String>(TEXT("message")))
						{
							ErrorMessage = ResponseObj->GetStringField(TEXT("message"));
						}
						else if (ResponseObj->HasTypedField<EJson::String>(TEXT("detail")))
						{
							ErrorMessage = ResponseObj->GetStringField(TEXT("detail"));
						}
						else if (ResponseObj->HasField(TEXT("detail")))
						{
							ErrorMessage = ResponseContent;
						}
					}
				}

				if (bSuccess && ResponseObj.IsValid())
				{
					FString Status = ResponseObj->HasField(TEXT("status")) ? ResponseObj->GetStringField(TEXT("status")) : TEXT("");
					FString EmbeddingData = ResponseObj->HasField(TEXT("embedding_data")) ? ResponseObj->GetStringField(TEXT("embedding_data")) : TEXT("");
					TSharedPtr<FJsonObject> EmbeddingObject;
					TSharedRef<TJsonReader<>> EmbeddingReader = TJsonReaderFactory<>::Create(EmbeddingData);
					const bool bEmbeddingValid = !EmbeddingData.IsEmpty()
						&& FJsonSerializer::Deserialize(EmbeddingReader, EmbeddingObject)
						&& EmbeddingObject.IsValid()
						&& EmbeddingObject->HasTypedField<EJson::Array>(TEXT("target_se"))
						&& EmbeddingObject->GetArrayField(TEXT("target_se")).Num() > 0;
					if (Status.Equals(TEXT("success"), ESearchCase::IgnoreCase) && bEmbeddingValid)
					{
						if (WeakTargetAsset.IsValid())
						{
							FCharacterLanguageData* TargetLangData = WeakTargetAsset->FindLanguageData(CurrentLangCode);
							if (TargetLangData)
							{
								TargetLangData->ToneColorEmbeddingData = EmbeddingData;
								TargetLangData->bIsModelGenerated = WeakTargetAsset->SaveModelToFile(TEXT(""), CurrentLangCode);
								bSuccess = TargetLangData->bIsModelGenerated;
									WeakTargetAsset->MarkPackageDirty();
								UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateModelClicked: Model saved for language '%s'."), *CurrentLangCode);
							}
						}
					}
					else
					{
						bSuccess = false;
						if (ResponseObj->HasTypedField<EJson::String>(TEXT("message")))
						{
							ErrorMessage = ResponseObj->GetStringField(TEXT("message"));
						}
					}
				}

				if (!bSuccess)
				{
					(*FailedTasks)++;
					UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateModelClicked: Model extraction error for language '%s' (HTTP Status %d): %s | Raw Response: %s"), *CurrentLangCode, ResponseCode, *ErrorMessage, *ResponseContent);
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Model extraction error for language '%s':\n%s"), *CurrentLangCode, *ErrorMessage)));
				}

				StepTaskProgress();
			});

			HttpRequest->ProcessRequest();
		}
	});

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnGenerateFromVoiceLinesClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateFromVoiceLinesClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	if (Asset->VoiceLines.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("No VoiceLines entries configured in this asset. Please add entries to the 'Voice Lines' array."));
		return FReply::Handled();
	}

	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateFromVoiceLinesClicked: Initiating pre-rendering from %d VoiceLines entries..."), Asset->VoiceLines.Num());
	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;

	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: Could not connect to OpenVoice REST backend service."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			return;
		}

		UCharacterVoiceAsset* VoiceAsset = WeakTargetAsset.Get();
		VoiceAsset->FixupVoiceLineAudioReferences();
		TSet<FString> ProcessedKeys;
		struct FKeyWorkItem
		{
			FName StringTableId;
			FName Key;
			FString TextLine;
			FString GuideAudioFile;
		};
		TArray<FKeyWorkItem> WorkItems;

		for (int32 i = 0; i < VoiceAsset->VoiceLines.Num(); ++i)
		{
			const FPlayVoiceLineEntry& Entry = VoiceAsset->VoiceLines[i];
			const FName EntryTableId = Entry.StringTable ? Entry.StringTable->GetStringTableId() : Entry.StringTableId;
			if (Entry.Key.IsNone() || EntryTableId.IsNone())
			{
				continue;
			}

			const FString EntryIdentity = EntryTableId.ToString().ToLower() + TEXT(":") + Entry.Key.ToString().ToLower();
			if (ProcessedKeys.Contains(EntryIdentity))
			{
				UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("Duplicate String Table Key '%s' found in VoiceLines. First entry takes precedence."), *Entry.Key.ToString());
				continue;
			}

			ProcessedKeys.Add(EntryIdentity);

			FKeyWorkItem Item;
			Item.StringTableId = EntryTableId;
			Item.Key = Entry.Key;
			Item.TextLine = VoiceAsset->GetResolvedTextLineForEntry(Entry);
			if (Item.TextLine.TrimStartAndEnd().IsEmpty())
			{
				UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateFromVoiceLinesClicked: Skipping empty text for key '%s'."), *Entry.Key.ToString());
				continue;
			}
			Item.GuideAudioFile = Entry.GuideSoundWave
				? UPlayVoiceAudioUtils::ExportSoundWaveToTempWAVFile(Entry.GuideSoundWave)
				: UCharacterVoiceAsset::ResolveAudioFilePath(Entry.AudioFile.FilePath);
			if (!Entry.AudioFile.FilePath.IsEmpty() && !IFileManager::Get().FileExists(*Item.GuideAudioFile))
			{
				Item.GuideAudioFile = FPaths::ConvertRelativePathToFull(VoiceAsset->GetVoiceRecordingFolderOnDisk(), Entry.AudioFile.FilePath);
			}
			WorkItems.Add(Item);
		}

		if (WorkItems.Num() == 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("No valid VoiceLines entries with String Table Keys found. Please set String Table Keys on entries in the 'Voice Lines' array."));
			return;
		}

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString BaseUrl = Settings && !Settings->ServiceUrl.IsEmpty() ? Settings->ServiceUrl.TrimStartAndEnd() : TEXT("http://127.0.0.1:1983");
		BaseUrl.RemoveFromEnd(TEXT("/"));
		float TimeoutSecs = Settings && Settings->RequestTimeout > 0.0f ? FMath::Max(Settings->RequestTimeout, 300.0f) : 300.0f;
		const int32 OutputSampleRate = Settings && Settings->DefaultSampleRate > 0 ? Settings->DefaultSampleRate : 48000;
		const bool bImproveOutputQuality = Settings ? Settings->bImproveOutputQuality : true;

		UPackage* OuterPackage = VoiceAsset->GetOutermost();
		FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

		int32 TotalTasksCount = VoiceAsset->Languages.Num() * WorkItems.Num();
		if (TotalTasksCount == 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("No language configurations are available for voice-line rendering."));
			return;
		}

		TSharedPtr<int32> CompletedTasks = MakeShared<int32>(0);
		TSharedPtr<int32> FailedTasks = MakeShared<int32>(0);

		FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Pre-rendering {0} Key voice lines..."), FText::AsNumber(WorkItems.Num())));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseThrobber = true;
		NotificationInfo.FadeOutDuration = 0.5f;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}

		auto StepTaskProgress = [NotificationItem, CompletedTasks, FailedTasks, TotalTasksCount]()
		{
			(*CompletedTasks)++;
			if (NotificationItem.IsValid())
			{
				FText Msg = FText::Format(FText::FromString("PlayVoice: Pre-rendering Key Voice Lines ({0}/{1})..."), FText::AsNumber(*CompletedTasks), FText::AsNumber(TotalTasksCount));
				NotificationItem->SetText(Msg);

				if (*CompletedTasks >= TotalTasksCount)
				{
					if (*FailedTasks > 0)
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
						NotificationItem->SetText(FText::Format(FText::FromString("PlayVoice: Finished with {0} errors."), FText::AsNumber(*FailedTasks)));
					}
					else
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
						NotificationItem->SetText(FText::FromString("PlayVoice: String Table Key voice line pre-rendering complete!"));
					}
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
			}
		};

		for (const FCharacterLanguageData& LangData : VoiceAsset->Languages)
		{
			FString CurrentLangCode = LangData.LanguageCode;
			float CurrentSpeed = LangData.Speed;
			FString EmbeddingData = LangData.ToneColorEmbeddingData;

			for (const FKeyWorkItem& Item : WorkItems)
			{
				if (!LangData.bIsModelGenerated || EmbeddingData.IsEmpty())
				{
					UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: No valid model exists for language '%s'. Generate the model before rendering key '%s'."), *CurrentLangCode, *Item.Key.ToString());
					(*FailedTasks)++;
					StepTaskProgress();
					continue;
				}

				TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
				JsonObj->SetStringField(TEXT("character_name"), VoiceAsset->CharacterName.ToString());
				JsonObj->SetStringField(TEXT("text"), Item.TextLine);
				JsonObj->SetStringField(TEXT("language"), CurrentLangCode);
				JsonObj->SetNumberField(TEXT("speed"), CurrentSpeed);
					JsonObj->SetNumberField(TEXT("sample_rate"), OutputSampleRate);
					JsonObj->SetBoolField(TEXT("improve_output"), bImproveOutputQuality);
				JsonObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

				TArray<FString> RefAudioFiles = VoiceAsset->GetResolvedReferenceAudioFilesForLanguage(CurrentLangCode);
				TArray<TSharedPtr<FJsonValue>> RefPathValues;
				for (const FString& RefPath : RefAudioFiles)
				{
					RefPathValues.Add(MakeShared<FJsonValueString>(RefPath));
				}
				JsonObj->SetArrayField(TEXT("reference_audio_files"), RefPathValues);

				if (!Item.GuideAudioFile.IsEmpty())
				{
					JsonObj->SetStringField(TEXT("guide_audio_file"), Item.GuideAudioFile);
				}

				FString PayloadStr;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
				FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
				HttpRequest->SetURL(BaseUrl + TEXT("/synthesize"));
				HttpRequest->SetVerb(TEXT("POST"));
				HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				HttpRequest->SetContentAsString(PayloadStr);
				HttpRequest->SetTimeout(TimeoutSecs);
				HttpRequest->SetActivityTimeout(TimeoutSecs);

				FName EntryKey = Item.Key;
				FName EntryTableId = Item.StringTableId;
				FString NormalizedLangCode = CurrentLangCode.TrimStartAndEnd().ToUpper();

				HttpRequest->OnProcessRequestComplete().BindLambda([WeakTargetAsset, EntryKey, EntryTableId, CurrentLangCode, NormalizedLangCode, AssetFolderPath, StepTaskProgress, FailedTasks](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
				{
					bool bSynthOk = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
					int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;

					if (!bSynthOk)
					{
						(*FailedTasks)++;
						FString ErrorContent = Response.IsValid() ? Response->GetContentAsString() : TEXT("");
						UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: Synthesis failed for Key '%s' (Lang: %s, HTTP %d): %s"), *EntryKey.ToString(), *CurrentLangCode, ResponseCode, *ErrorContent);
					}
					else if (WeakTargetAsset.IsValid())
					{
						const FString SafeCharacterName = MakeSafePackageComponent(WeakTargetAsset->CharacterName.ToString());
						const FString SafeKey = MakeSafePackageComponent(EntryKey.ToString());
						const FString SafeTableId = MakeSafePackageComponent(EntryTableId.ToString());
						const FString KeySanitized = FString::Printf(TEXT("SW_%s_%s_%s_%s_%08X"), *SafeCharacterName, *NormalizedLangCode, *SafeTableId, *SafeKey, MakeVoiceLineIdentityHash(EntryTableId, EntryKey, NormalizedLangCode));
						const FString PackagePath = AssetFolderPath / KeySanitized;

						if (!FPackageName::IsValidLongPackageName(PackagePath))
						{
							(*FailedTasks)++;
							UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: Invalid generated sound wave package path '%s'."), *PackagePath);
						}
						else
						{
							for (FPlayVoiceLineEntry& ExistingEntry : WeakTargetAsset->VoiceLines)
							{
								if (ExistingEntry.Key == EntryKey && (ExistingEntry.StringTable ? ExistingEntry.StringTable->GetStringTableId() : ExistingEntry.StringTableId) == EntryTableId)
								{
									ExistingEntry.PrecachedSoundWavesByLanguage.Remove(NormalizedLangCode);
									const FString NormalizedDefaultLanguage = WeakTargetAsset->DefaultLanguage.TrimStartAndEnd().ToUpper();
									if (NormalizedLangCode == (NormalizedDefaultLanguage.IsEmpty() ? TEXT("EN") : *NormalizedDefaultLanguage))
									{
										ExistingEntry.PrecachedSoundWave = nullptr;
									}
									break;
								}
							}

							DeleteGeneratedSoundWavePackage(PackagePath);
							UPackage* SoundWavePackage = CreatePackage(*PackagePath);
							if (SoundWavePackage)
							{
								SoundWavePackage->FullyLoad();
							}
							USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(Response->GetContent(), SoundWavePackage, FName(*KeySanitized));

							if (SoundWave && SoundWavePackage)
							{
								const FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
								FSavePackageArgs SaveArgs;
								SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
								const bool bSaved = UPackage::SavePackage(SoundWavePackage, SoundWave, *PackageFilename, SaveArgs);
								if (!bSaved)
								{
									(*FailedTasks)++;
									UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: Could not save generated sound wave package '%s'."), *PackageFilename);
								}
								else
								{
									WeakTargetAsset->CacheVoiceLineForStringTableIdAndKey(EntryTableId, EntryKey.ToString(), SoundWave, CurrentLangCode);
									WeakTargetAsset->MarkPackageDirty();

									UPackage* VoiceAssetPackage = WeakTargetAsset->GetOutermost();
									const FString VoiceAssetFilename = FPackageName::LongPackageNameToFilename(VoiceAssetPackage->GetName(), FPackageName::GetAssetPackageExtension());
									FSavePackageArgs VoiceAssetSaveArgs;
									VoiceAssetSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
									if (!UPackage::SavePackage(VoiceAssetPackage, WeakTargetAsset.Get(), *VoiceAssetFilename, VoiceAssetSaveArgs))
									{
										(*FailedTasks)++;
										UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: Could not save voice asset package '%s'."), *VoiceAssetFilename);
									}

									if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
									{
										FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
										AssetRegistryModule.AssetCreated(SoundWave);
									}
									UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateFromVoiceLinesClicked: Pre-rendered sound wave '%s' for Key '%s'"), *KeySanitized, *EntryKey.ToString());
								}
							}
							else
							{
								(*FailedTasks)++;
								UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: Could not create generated sound wave '%s'."), *KeySanitized);
							}
						}
					}

					StepTaskProgress();
				});

				if (!HttpRequest->ProcessRequest())
				{
					UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateFromVoiceLinesClicked: Failed to submit synthesis request for key '%s'."), *EntryKey.ToString());
					(*FailedTasks)++;
					StepTaskProgress();
				}
			}
		}
	});

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnNormalizePrecachedSoundLevelsClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnNormalizePrecachedSoundLevelsClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	TSet<USoundWave*> SoundWaves;
	for (const FPlayVoiceLineEntry& Entry : Asset->VoiceLines)
	{
		if (USoundWave* SoundWave = Entry.PrecachedSoundWave.Get())
		{
			SoundWaves.Add(SoundWave);
		}
		for (const TPair<FString, TObjectPtr<USoundWave>>& CachedPair : Entry.PrecachedSoundWavesByLanguage)
		{
			if (USoundWave* SoundWave = CachedPair.Value.Get())
			{
				SoundWaves.Add(SoundWave);
			}
		}
	}

	TMap<USoundWave*, TArray<uint8>> PCMDataBySoundWave;
	int32 GlobalPeak = 0;
	for (USoundWave* SoundWave : SoundWaves)
	{
		TArray<uint8> PCMData;
		if (GetSoundWavePCM16Data(SoundWave, PCMData))
		{
			const int32 Peak = UPlayVoiceAudioUtils::GetPCM16Peak(PCMData);
			if (Peak > 0)
			{
				GlobalPeak = FMath::Max(GlobalPeak, Peak);
				PCMDataBySoundWave.Add(SoundWave, MoveTemp(PCMData));
			}
		}
	}

	if (GlobalPeak <= 0)
	{
		FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: No non-silent precached sound waves were found."));
		NotificationInfo.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		return FReply::Handled();
	}

	int32 ChangedCount = 0;
	int32 FailedCount = 0;
	for (TPair<USoundWave*, TArray<uint8>>& SoundWaveData : PCMDataBySoundWave)
	{
		USoundWave* SoundWave = SoundWaveData.Key;
		if (UPlayVoiceAudioUtils::GetPCM16Peak(SoundWaveData.Value) >= GlobalPeak)
		{
			continue;
		}

		UPackage* SoundWavePackage = SoundWave->GetOutermost();
		if (!SoundWavePackage || SoundWavePackage == GetTransientPackage())
		{
			FailedCount++;
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnNormalizePrecachedSoundLevelsClicked: SoundWave '%s' is not in a persistent package."), *SoundWave->GetPathName());
			continue;
		}

		if (!UPlayVoiceAudioUtils::NormalizePCM16ToPeak(SoundWaveData.Value, GlobalPeak)
			|| !UpdateSoundWavePCM16Data(SoundWave, SoundWaveData.Value))
		{
			FailedCount++;
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnNormalizePrecachedSoundLevelsClicked: Could not update SoundWave '%s'."), *SoundWave->GetPathName());
			continue;
		}

		const FString PackageFilename = FPackageName::LongPackageNameToFilename(SoundWavePackage->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(SoundWavePackage, SoundWave, *PackageFilename, SaveArgs))
		{
			FailedCount++;
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnNormalizePrecachedSoundLevelsClicked: Could not save SoundWave package '%s'."), *PackageFilename);
			continue;
		}

		ChangedCount++;
	}

	if (ChangedCount > 0)
	{
		Asset->MarkPackageDirty();
		UPackage* AssetPackage = Asset->GetOutermost();
		if (AssetPackage && AssetPackage != GetTransientPackage())
		{
			const FString AssetFilename = FPackageName::LongPackageNameToFilename(AssetPackage->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(AssetPackage, Asset, *AssetFilename, SaveArgs))
			{
				FailedCount++;
				UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnNormalizePrecachedSoundLevelsClicked: Could not save CharacterVoiceAsset package '%s'."), *AssetFilename);
			}
		}
	}

	FNotificationInfo NotificationInfo(FText::Format(
		FText::FromString("PlayVoice: Normalized {0} precached sound waves to the global peak ({1} failures)."),
		FText::AsNumber(ChangedCount),
		FText::AsNumber(FailedCount)));
	NotificationInfo.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnNormalizePrecachedSoundLevelsClicked: Normalized %d sound waves to peak %d (%d failures)."), ChangedCount, GlobalPeak, FailedCount);

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnCleanPrecachedSoundWavesClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnCleanPrecachedSoundWavesClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	int32 RemovedCount = 0;
	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnCleanPrecachedSoundWavesClicked: Initiating precached sound wave cleanup for asset '%s'"), *Asset->GetName());

	TArray<USoundWave*> SoundWavesToDelete;
	TArray<FString> FilePathsToDelete;

	for (FPlayVoiceLineEntry& Entry : Asset->VoiceLines)
	{
		TArray<USoundWave*> EntrySoundWaves;
		if (USoundWave* SoundWave = Entry.PrecachedSoundWave.Get())
		{
			EntrySoundWaves.Add(SoundWave);
		}
		for (const TPair<FString, TObjectPtr<USoundWave>>& CachedPair : Entry.PrecachedSoundWavesByLanguage)
		{
			if (USoundWave* SoundWave = CachedPair.Value.Get())
			{
				EntrySoundWaves.AddUnique(SoundWave);
			}
		}

		const FName EntryTableId = Entry.StringTable ? Entry.StringTable->GetStringTableId() : Entry.StringTableId;
		for (USoundWave* SoundWave : EntrySoundWaves)
		{
			bool bMatchesGeneratedIdentity = false;
			for (const FCharacterLanguageData& LanguageData : Asset->Languages)
			{
				FString LanguageCode = LanguageData.LanguageCode.TrimStartAndEnd().ToUpper();
				if (LanguageCode.IsEmpty())
				{
					LanguageCode = Asset->DefaultLanguage.TrimStartAndEnd().ToUpper();
				}
				if (LanguageCode.IsEmpty())
				{
					LanguageCode = TEXT("EN");
				}
				const FString HashSuffix = FString::Printf(TEXT("_%08X"), MakeVoiceLineIdentityHash(EntryTableId, Entry.Key, LanguageCode));
				if (SoundWave && SoundWave->GetName().EndsWith(HashSuffix))
				{
					bMatchesGeneratedIdentity = true;
					break;
				}
			}
			if (!bMatchesGeneratedIdentity || !SoundWave->GetOutermost()
				|| !SoundWave->GetOutermost()->GetName().StartsWith(FPaths::GetPath(Asset->GetOutermost()->GetName())))
			{
				continue;
			}
			RemovedCount++;
			SoundWavesToDelete.AddUnique(SoundWave);
			UPackage* Pkg = SoundWave->GetOutermost();
			if (Pkg && Pkg != GetTransientPackage())
			{
				FString PkgFilename;
				if (FPackageName::DoesPackageExist(Pkg->GetName(), &PkgFilename))
				{
					FilePathsToDelete.AddUnique(PkgFilename);
				}
			}
		}
		if (SoundWavesToDelete.Contains(Entry.PrecachedSoundWave.Get()))
		{
			Entry.PrecachedSoundWave = nullptr;
		}
		for (auto CacheIt = Entry.PrecachedSoundWavesByLanguage.CreateIterator(); CacheIt; ++CacheIt)
		{
			if (SoundWavesToDelete.Contains(CacheIt.Value().Get()))
			{
				CacheIt.RemoveCurrent();
			}
		}
	}

	Asset->MarkPackageDirty();

	if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::Get().LoadModule("AssetRegistry");
	}

	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		for (USoundWave* SoundWave : SoundWavesToDelete)
		{
			if (SoundWave)
			{
				AssetRegistryModule.AssetDeleted(SoundWave);
			}
		}
	}

	// Detach objects and outer packages to transient package so open file locks are released
	for (USoundWave* SoundWave : SoundWavesToDelete)
	{
		if (SoundWave)
		{
			UPackage* Pkg = SoundWave->GetOutermost();
			SoundWave->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			SoundWave->MarkAsGarbage();
			if (Pkg && Pkg != GetTransientPackage())
			{
				Pkg->MarkAsGarbage();
			}
		}
	}

	// Allow normal editor garbage collection to release deleted packages.

	for (FString& FilePath : FilePathsToDelete)
	{
		if (IFileManager::Get().FileExists(*FilePath))
		{
			bool bDeleted = IFileManager::Get().Delete(*FilePath);
			UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnCleanPrecachedSoundWavesClicked: Deleted package file '%s' (Success: %d)"), *FilePath, bDeleted);
		}
	}

	FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Cleaned {0} precached sound wave assets."), FText::AsNumber(RemovedCount)));
	NotificationInfo.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnCleanPrecachedSoundWavesClicked: Cleaned %d precached sound wave entries."), RemovedCount);

	return FReply::Handled();
}
