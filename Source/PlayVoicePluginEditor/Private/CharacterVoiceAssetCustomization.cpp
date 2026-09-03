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
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"

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
		&& HealthObject->GetStringField(TEXT("service_version")) == TEXT("1.1.0");
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

					PollReq->ProcessRequest();
				};

				Context->PollFunc(*Context);
			});
		});
	});

	InitialHealthReq->ProcessRequest();
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
						if (ResponseObj->HasField(TEXT("message")))
						{
							ErrorMessage = ResponseObj->GetStringField(TEXT("message"));
						}
						else if (ResponseObj->HasField(TEXT("detail")))
						{
							ErrorMessage = ResponseObj->GetStringField(TEXT("detail"));
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
						if (ResponseObj->HasField(TEXT("message")))
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
		TSet<FName> ProcessedKeys;
		struct FKeyWorkItem
		{
			FName Key;
			FString TextLine;
			FString GuideAudioFile;
		};
		TArray<FKeyWorkItem> WorkItems;

		for (int32 i = 0; i < VoiceAsset->VoiceLines.Num(); ++i)
		{
			const FPlayVoiceLineEntry& Entry = VoiceAsset->VoiceLines[i];
			if (Entry.Key.IsNone())
			{
				continue;
			}

			if (ProcessedKeys.Contains(Entry.Key))
			{
				UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("Duplicate String Table Key '%s' found in VoiceLines. First entry takes precedence."), *Entry.Key.ToString());
				continue;
			}

			ProcessedKeys.Add(Entry.Key);

			FKeyWorkItem Item;
			Item.Key = Entry.Key;
			Item.TextLine = VoiceAsset->GetResolvedTextLineForEntry(Entry);
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
				FString NormalizedLangCode = CurrentLangCode.TrimStartAndEnd().ToUpper();

				HttpRequest->OnProcessRequestComplete().BindLambda([HttpRequest, WeakTargetAsset, EntryKey, CurrentLangCode, NormalizedLangCode, AssetFolderPath, StepTaskProgress, FailedTasks](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
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
						FString KeySanitized = FString::Printf(TEXT("SW_%s_%s_%s"), *WeakTargetAsset->CharacterName.ToString(), *NormalizedLangCode, *EntryKey.ToString().Replace(TEXT("."), TEXT("_")));
						FString PackagePath = AssetFolderPath / KeySanitized;

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
								WeakTargetAsset->CacheVoiceLineForKey(EntryKey, SoundWave, CurrentLangCode);
								WeakTargetAsset->MarkPackageDirty();

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

					StepTaskProgress();
				});

				HttpRequest->ProcessRequest();
			}
		}
	});

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

		for (USoundWave* SoundWave : EntrySoundWaves)
		{
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
		Entry.PrecachedSoundWave = nullptr;
		Entry.PrecachedSoundWavesByLanguage.Empty();
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

	CollectGarbage(RF_NoFlags);

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
