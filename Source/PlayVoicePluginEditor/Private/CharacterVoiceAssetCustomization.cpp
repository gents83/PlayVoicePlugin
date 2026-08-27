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
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharacterVoiceCustomization, Log, All);

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

	OpenVoiceCategory.AddCustomRow(FText::FromString("Generate Model and Process Lines"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Full Pipeline (One-Click)"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Generate Model & Process All Lines"))
		.ToolTipText(FText::FromString("One-click pipeline: Auto-discovers Blueprint voice lines, extracts model embeddings for all configured languages, transcribes, synthesizes, and saves generated audio assets."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnGenerateAndProcessAllClicked)
	];

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

	OpenVoiceCategory.AddCustomRow(FText::FromString("Precache Voice Lines"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Zero-Delay Pre-rendering"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Pre-process Blueprint Voice Lines"))
		.ToolTipText(FText::FromString("Scans all Blueprint nodes for dialogue lines and pre-renders sound wave assets across all configured languages to eliminate in-game latency."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnPrecacheLinesClicked)
	];
}

static void EnsureServiceReadyAndExecute(TFunction<void(bool bReady)> OnComplete, int32 MaxAttempts = 30)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BaseUrl = Settings && !Settings->ServiceUrl.IsEmpty() ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");
	BaseUrl.RemoveFromEnd(TEXT("/"));

	// First, test if service is already running and healthy
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> InitialHealthReq = FHttpModule::Get().CreateRequest();
	InitialHealthReq->SetURL(BaseUrl + TEXT("/health"));
	InitialHealthReq->SetVerb(TEXT("GET"));
	InitialHealthReq->SetTimeout(2.0f);

	InitialHealthReq->OnProcessRequestComplete().BindLambda([BaseUrl, MaxAttempts, OnComplete](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess)
	{
		bool bAlreadyReady = bSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode());
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
						bool bReady = bSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode());
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

TArray<FString> FCharacterVoiceAssetCustomization::RetrieveVoiceLinesFromProjectBlueprints(const UCharacterVoiceAsset* TargetAsset)
{
	TArray<FString> DiscoveredLines;
	if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::Get().LoadModule("AssetRegistry");
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> BlueprintAssetList;
	AssetRegistryModule.Get().GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BlueprintAssetList, true);

	for (const FAssetData& AssetData : BlueprintAssetList)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint)
		{
			continue;
		}

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (!CallNode)
				{
					continue;
				}

				UFunction* TargetFunc = CallNode->GetTargetFunction();
				if (!TargetFunc)
				{
					continue;
				}

				FString FuncName = TargetFunc->GetName();
				if (FuncName == TEXT("PlayCharacterVoice") || FuncName == TEXT("GenerateVoiceSoundWave") || FuncName == TEXT("PrecacheCharacterVoiceLines") || FuncName == TEXT("PrecacheVoiceLine"))
				{
					UEdGraphPin* AssetPin = CallNode->FindPin(TEXT("CharacterVoiceAsset"));
					UEdGraphPin* TextPin = CallNode->FindPin(TEXT("TextLine"));

					bool bMatchesAsset = false;
					if (AssetPin)
					{
						if (AssetPin->DefaultObject == TargetAsset)
						{
							bMatchesAsset = true;
						}
						else if (!AssetPin->DefaultObject && AssetPin->LinkedTo.Num() == 0)
						{
							bMatchesAsset = true;
						}
					}
					else
					{
						bMatchesAsset = true;
					}

					if (bMatchesAsset && TextPin)
					{
						FString TextVal = TextPin->GetDefaultAsString();
						TextVal.TrimStartAndEndInline();
						if (TextVal.StartsWith(TEXT("\"")) && TextVal.EndsWith(TEXT("\"")) && TextVal.Len() >= 2)
						{
							TextVal = TextVal.Mid(1, TextVal.Len() - 2);
						}
						if (!TextVal.IsEmpty())
						{
							DiscoveredLines.AddUnique(TextVal);
						}
					}
				}
			}
		}
	}

	return DiscoveredLines;
}

FReply FCharacterVoiceAssetCustomization::OnGenerateAndProcessAllClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;
	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			return;
		}

		UCharacterVoiceAsset* Asset = WeakTargetAsset.Get();
		if (Asset->Languages.Num() == 0)
		{
			Asset->GetOrAddLanguageData(Asset->DefaultLanguage.IsEmpty() ? TEXT("EN") : Asset->DefaultLanguage);
		}

		TArray<FString> DiscoveredBlueprintLines = RetrieveVoiceLinesFromProjectBlueprints(Asset);

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");

		int32 TotalLangsProcessed = 0;
		TArray<FCharacterLanguageData*> ConfiguredLanguages;
		for (FCharacterLanguageData& LangData : Asset->Languages)
		{
			bool bHasRefAudioConfigured = (LangData.ReferenceAudioFiles.Num() > 0 || !LangData.ReferenceAudioFolder.Path.IsEmpty());
			if (bHasRefAudioConfigured)
			{
				ConfiguredLanguages.Add(&LangData);
				TotalLangsProcessed++;
			}
		}

		if (TotalLangsProcessed == 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify Reference Audio Files or Reference Audio Folder for at least one language before generating models."));
			return;
		}

		int32 TotalTasksCount = ConfiguredLanguages.Num() * (1 + DiscoveredBlueprintLines.Num());
		TSharedPtr<int32> CompletedTasks = MakeShared<int32>(0);
		TSharedPtr<int32> FailedTasks = MakeShared<int32>(0);

		FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Processing Model Extraction & {0} Lines..."), FText::AsNumber(DiscoveredBlueprintLines.Num())));
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
				FText Msg = FText::Format(FText::FromString("PlayVoice: Processing ({0}/{1})..."), FText::AsNumber(*CompletedTasks), FText::AsNumber(TotalTasksCount));
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
						NotificationItem->SetText(FText::FromString("PlayVoice: Full pipeline processing complete!"));
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

			// Step 1: Extract model embeddings for this language
			TSharedPtr<FJsonObject> ExtractObj = MakeShared<FJsonObject>();
			ExtractObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
			ExtractObj->SetStringField(TEXT("language"), LangData.LanguageCode);

			TArray<TSharedPtr<FJsonValue>> AudioPathValues;
			for (const FString& Path : RefAudioFiles)
			{
				AudioPathValues.Add(MakeShared<FJsonValueString>(Path));
			}
			ExtractObj->SetArrayField(TEXT("reference_audio_files"), AudioPathValues);

			FString ExtractPayload;
			TSharedRef<TJsonWriter<>> ExtractWriter = TJsonWriterFactory<>::Create(&ExtractPayload);
			FJsonSerializer::Serialize(ExtractObj.ToSharedRef(), ExtractWriter);

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> ExtractReq = FHttpModule::Get().CreateRequest();
			ExtractReq->SetURL(BaseUrl + TEXT("/extract"));
			ExtractReq->SetVerb(TEXT("POST"));
			ExtractReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			ExtractReq->SetContentAsString(ExtractPayload);

			FString CurrentLangCode = LangData.LanguageCode;
			float CurrentSpeed = LangData.Speed;

			ExtractReq->OnProcessRequestComplete().BindLambda([WeakTargetAsset, BaseUrl, CurrentLangCode, CurrentSpeed, DiscoveredBlueprintLines, StepTaskProgress, FailedTasks](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bExtractSuccess)
			{
				bool bSuccess = bExtractSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode());
				if (!bSuccess)
				{
					(*FailedTasks)++;
				}

				if (bSuccess && WeakTargetAsset.IsValid())
				{
					TSharedPtr<FJsonObject> ResObj;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
					if (FJsonSerializer::Deserialize(Reader, ResObj) && ResObj.IsValid())
					{
						FCharacterLanguageData* TargetLangData = WeakTargetAsset->FindLanguageData(CurrentLangCode);
						if (TargetLangData)
						{
							TargetLangData->ToneColorEmbeddingData = ResObj->GetStringField(TEXT("embedding_data"));
							TargetLangData->bIsModelGenerated = !TargetLangData->ToneColorEmbeddingData.IsEmpty();
							WeakTargetAsset->SaveModelToFile(TEXT(""), CurrentLangCode);
							WeakTargetAsset->MarkPackageDirty();
						}
					}
				}

				StepTaskProgress();

				if (!WeakTargetAsset.IsValid() || DiscoveredBlueprintLines.Num() == 0)
				{
					return;
				}

				UCharacterVoiceAsset* VoiceAsset = WeakTargetAsset.Get();
				FCharacterLanguageData* TargetLangData = VoiceAsset->FindLanguageData(CurrentLangCode);
				FString EmbeddingData = TargetLangData ? TargetLangData->ToneColorEmbeddingData : TEXT("");

				UPackage* OuterPackage = VoiceAsset->GetOutermost();
				FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

				for (const FString& LineText : DiscoveredBlueprintLines)
				{
					if (!VoiceAsset->bRegenerateExistingVoiceLines && VoiceAsset->HasPrecachedVoiceLine(LineText, CurrentLangCode))
					{
						StepTaskProgress();
						continue;
					}

					TSharedPtr<FJsonObject> SynthObj = MakeShared<FJsonObject>();
					SynthObj->SetStringField(TEXT("character_name"), VoiceAsset->CharacterName.ToString());
					SynthObj->SetStringField(TEXT("text"), LineText);
					SynthObj->SetStringField(TEXT("language"), CurrentLangCode);
					SynthObj->SetNumberField(TEXT("speed"), CurrentSpeed);
					SynthObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

					FString GuideFile = VoiceAsset->GetResolvedGuideAudioFileForLine(LineText);
					if (!GuideFile.IsEmpty())
					{
						SynthObj->SetStringField(TEXT("guide_audio_file"), GuideFile);
					}

					FString SynthPayload;
					TSharedRef<TJsonWriter<>> SynthWriter = TJsonWriterFactory<>::Create(&SynthPayload);
					FJsonSerializer::Serialize(SynthObj.ToSharedRef(), SynthWriter);

					TSharedRef<IHttpRequest, ESPMode::ThreadSafe> SynthReq = FHttpModule::Get().CreateRequest();
					SynthReq->SetURL(BaseUrl + TEXT("/synthesize"));
					SynthReq->SetVerb(TEXT("POST"));
					SynthReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
					SynthReq->SetContentAsString(SynthPayload);

					SynthReq->OnProcessRequestComplete().BindLambda([WeakTargetAsset, LineText, CurrentLangCode, AssetFolderPath, StepTaskProgress, FailedTasks](FHttpRequestPtr SReq, FHttpResponsePtr SRes, bool bSynthSuccess)
					{
						bool bSynthOk = bSynthSuccess && SRes.IsValid() && EHttpResponseCodes::IsOk(SRes->GetResponseCode());
						if (!bSynthOk)
						{
							(*FailedTasks)++;
						}

						if (bSynthOk && WeakTargetAsset.IsValid())
						{
							FString LineSanitized = FString::Printf(TEXT("SW_%s_%s_%u"), *WeakTargetAsset->CharacterName.ToString(), *CurrentLangCode, GetTypeHash(LineText));
							FString SoundWavePackagePath = AssetFolderPath / LineSanitized;

							UPackage* SoundWavePackage = CreatePackage(*SoundWavePackagePath);
							USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(SRes->GetContent(), SoundWavePackage, FName(*LineSanitized));

							if (SoundWave)
							{
								WeakTargetAsset->CacheVoiceLine(LineText, SoundWave, CurrentLangCode);
								SoundWave->MarkPackageDirty();
								WeakTargetAsset->MarkPackageDirty();

								if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
								{
									FAssetRegistryModule::AssetCreated(SoundWave);
								}
							}
						}

						StepTaskProgress();
					});

					SynthReq->ProcessRequest();
				}
			});

			ExtractReq->ProcessRequest();
		}
	});

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnGenerateModelClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;
	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			return;
		}

		UCharacterVoiceAsset* Asset = WeakTargetAsset.Get();
		if (Asset->Languages.Num() == 0)
		{
			Asset->GetOrAddLanguageData(Asset->DefaultLanguage.IsEmpty() ? TEXT("EN") : Asset->DefaultLanguage);
		}

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString Url = (Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983")) + TEXT("/extract");

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
					}
					else
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
						NotificationItem->SetText(FText::FromString("PlayVoice: OpenVoice model extraction complete!"));
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

			FString CurrentLangCode = LangData.LanguageCode;

			HttpRequest->OnProcessRequestComplete().BindLambda([WeakTargetAsset, CurrentLangCode, StepTaskProgress, FailedTasks](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
			{
				bool bSuccess = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
				if (!bSuccess)
				{
					(*FailedTasks)++;
				}

				if (bSuccess)
				{
					TSharedPtr<FJsonObject> ResponseObj;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
					if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
					{
						if (WeakTargetAsset.IsValid())
						{
							FCharacterLanguageData* TargetLangData = WeakTargetAsset->FindLanguageData(CurrentLangCode);
							if (TargetLangData)
							{
								TargetLangData->ToneColorEmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
								TargetLangData->bIsModelGenerated = !TargetLangData->ToneColorEmbeddingData.IsEmpty();
								WeakTargetAsset->SaveModelToFile(TEXT(""), CurrentLangCode);
								WeakTargetAsset->MarkPackageDirty();
							}
						}
					}
				}

				StepTaskProgress();
			});

			HttpRequest->ProcessRequest();
		}
	});

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnPrecacheLinesClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;
	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			return;
		}

		UCharacterVoiceAsset* Asset = WeakTargetAsset.Get();
		TArray<FString> DiscoveredBlueprintLines = RetrieveVoiceLinesFromProjectBlueprints(Asset);

		if (DiscoveredBlueprintLines.Num() == 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("No dialogue lines found in Blueprint graph nodes referencing this asset."));
			return;
		}

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");

		UPackage* OuterPackage = Asset->GetOutermost();
		FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

		int32 TotalTasksCount = Asset->Languages.Num() * DiscoveredBlueprintLines.Num();
		TSharedPtr<int32> CompletedTasks = MakeShared<int32>(0);
		TSharedPtr<int32> FailedTasks = MakeShared<int32>(0);

		FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Pre-processing {0} dialogue lines..."), FText::AsNumber(DiscoveredBlueprintLines.Num())));
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
				FText Msg = FText::Format(FText::FromString("PlayVoice: Pre-processing dialogue lines ({0}/{1})..."), FText::AsNumber(*CompletedTasks), FText::AsNumber(TotalTasksCount));
				NotificationItem->SetText(Msg);

				if (*CompletedTasks >= TotalTasksCount)
				{
					if (*FailedTasks > 0)
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
						NotificationItem->SetText(FText::Format(FText::FromString("PlayVoice: Voice line pre-processing finished with {0} errors."), FText::AsNumber(*FailedTasks)));
					}
					else
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
						NotificationItem->SetText(FText::FromString("PlayVoice: Voice line pre-processing complete!"));
					}
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
			}
		};

		for (const FCharacterLanguageData& LangData : Asset->Languages)
		{
			FString CurrentLangCode = LangData.LanguageCode;
			float CurrentSpeed = LangData.Speed;
			FString EmbeddingData = LangData.ToneColorEmbeddingData;

			for (const FString& LineText : DiscoveredBlueprintLines)
			{
				if (!Asset->bRegenerateExistingVoiceLines && Asset->HasPrecachedVoiceLine(LineText, CurrentLangCode))
				{
					StepTaskProgress();
					continue;
				}

				TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
				JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
				JsonObj->SetStringField(TEXT("text"), LineText);
				JsonObj->SetStringField(TEXT("language"), CurrentLangCode);
				JsonObj->SetNumberField(TEXT("speed"), CurrentSpeed);
				JsonObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

				FString GuideFile = Asset->GetResolvedGuideAudioFileForLine(LineText);
				if (!GuideFile.IsEmpty())
				{
					JsonObj->SetStringField(TEXT("guide_audio_file"), GuideFile);
				}

				FString PayloadStr;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
				FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
				HttpRequest->SetURL(BaseUrl + TEXT("/synthesize"));
				HttpRequest->SetVerb(TEXT("POST"));
				HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				HttpRequest->SetContentAsString(PayloadStr);

				HttpRequest->OnProcessRequestComplete().BindLambda([WeakTargetAsset, LineText, CurrentLangCode, AssetFolderPath, StepTaskProgress, FailedTasks](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
				{
					bool bSynthOk = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
					if (!bSynthOk)
					{
						(*FailedTasks)++;
					}

					if (bSynthOk && WeakTargetAsset.IsValid())
					{
						FString LineSanitized = FString::Printf(TEXT("SW_%s_%s_%u"), *WeakTargetAsset->CharacterName.ToString(), *CurrentLangCode, GetTypeHash(LineText));
						FString SoundWavePackagePath = AssetFolderPath / LineSanitized;

						UPackage* SoundWavePackage = CreatePackage(*SoundWavePackagePath);
						USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(Response->GetContent(), SoundWavePackage, FName(*LineSanitized));

						if (SoundWave)
						{
							WeakTargetAsset->CacheVoiceLine(LineText, SoundWave, CurrentLangCode);
							SoundWave->MarkPackageDirty();
							WeakTargetAsset->MarkPackageDirty();

							if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
							{
								FAssetRegistryModule::AssetCreated(SoundWave);
							}
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
