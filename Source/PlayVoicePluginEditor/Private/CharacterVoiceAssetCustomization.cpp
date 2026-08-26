// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAssetCustomization.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceAudioUtils.h"
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
		.ToolTipText(FText::FromString("One-click pipeline: Scans folder for reference tracks, trains model, auto-discovers Blueprint voice lines, transcribes, and stores all generated assets in the asset folder."))
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
		.ToolTipText(FText::FromString("Extract tone color embedding from the reference audio clips and save to model file."))
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
		.Text(FText::FromString("Pre-process All Voice Lines"))
		.ToolTipText(FText::FromString("Synthesizes and caches all voice lines in advance to eliminate in-game latency."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnPrecacheLinesClicked)
	];
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
				if (FuncName == TEXT("PlayCharacterVoice") || FuncName == TEXT("GenerateVoiceSoundWave") || FuncName == TEXT("PrecacheCharacterVoiceLines"))
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

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFiles();
	if (RefAudioFiles.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify reference audio files or set a valid Reference Audio Folder before running model generation & line processing."));
		return FReply::Handled();
	}

	// Retrieve dialogue lines from all project Blueprints
	TArray<FString> DiscoveredBlueprintLines = RetrieveVoiceLinesFromProjectBlueprints(Asset);
	for (const FString& Line : DiscoveredBlueprintLines)
	{
		Asset->LinesToPreprocess.AddUnique(Line);
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");

	// Auto-transcribe reference tracks to extract text lines automatically
	TSharedPtr<FJsonObject> TranscribeObj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> RefJsonValues;
	for (const FString& RefPath : RefAudioFiles)
	{
		RefJsonValues.Add(MakeShared<FJsonValueString>(RefPath));
	}
	TranscribeObj->SetArrayField(TEXT("reference_audio_files"), RefJsonValues);

	FString TranscribePayload;
	TSharedRef<TJsonWriter<>> TranscribeWriter = TJsonWriterFactory<>::Create(&TranscribePayload);
	FJsonSerializer::Serialize(TranscribeObj.ToSharedRef(), TranscribeWriter);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> TranscribeReq = FHttpModule::Get().CreateRequest();
	TranscribeReq->SetURL(BaseUrl + TEXT("/transcribe"));
	TranscribeReq->SetVerb(TEXT("POST"));
	TranscribeReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	TranscribeReq->SetContentAsString(TranscribePayload);

	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = TargetVoiceAsset;
	TranscribeReq->OnProcessRequestComplete().BindLambda([WeakAsset, RefAudioFiles, BaseUrl](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			TSharedPtr<FJsonObject> ResponseObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
			{
				const TSharedPtr<FJsonObject>* TranscriptionsMap;
				if (ResponseObj->TryGetObjectField(TEXT("transcriptions"), TranscriptionsMap))
				{
					for (auto& Pair : (*TranscriptionsMap)->Values)
					{
						FString Text = Pair.Value->AsString();
						if (!Text.IsEmpty() && WeakAsset.IsValid())
						{
							WeakAsset->LinesToPreprocess.AddUnique(Text);
						}
					}
				}
			}
		}

		// Proceed to Model Extraction
		TSharedPtr<FJsonObject> ExtractObj = MakeShared<FJsonObject>();
		if (WeakAsset.IsValid())
		{
			ExtractObj->SetStringField(TEXT("character_name"), WeakAsset->CharacterName.ToString());
		}

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

		ExtractReq->OnProcessRequestComplete().BindLambda([WeakAsset, BaseUrl](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bExtractSuccess)
		{
			if (bExtractSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode()))
			{
				TSharedPtr<FJsonObject> ResObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
				if (FJsonSerializer::Deserialize(Reader, ResObj) && ResObj.IsValid() && WeakAsset.IsValid())
				{
					WeakAsset->ToneColorEmbeddingData = ResObj->GetStringField(TEXT("embedding_data"));
					WeakAsset->bIsModelGenerated = !WeakAsset->ToneColorEmbeddingData.IsEmpty();
					WeakAsset->SaveModelToFile(); // Saved directly in asset's folder
					WeakAsset->MarkPackageDirty();
				}
			}

			// Precache lines and save generated USoundWave assets in the asset's package folder
			if (!WeakAsset.IsValid() || WeakAsset->LinesToPreprocess.Num() == 0)
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Model generated successfully, but no dialogue lines were found to process."));
				return;
			}

			UCharacterVoiceAsset* VoiceAsset = WeakAsset.Get();
			TSharedPtr<int32> Remaining = MakeShared<int32>(VoiceAsset->LinesToPreprocess.Num());
			TSharedPtr<int32> SuccessCount = MakeShared<int32>(0);

			UPackage* OuterPackage = VoiceAsset->GetOutermost();
			FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

			for (const FString& LineText : VoiceAsset->LinesToPreprocess)
			{
				if (VoiceAsset->HasPrecachedVoiceLine(LineText))
				{
					(*SuccessCount)++;
					(*Remaining)--;
					if (*Remaining <= 0)
					{
						FMessageDialog::Open(EAppMsgType::Ok, FText::Format(FText::FromString("Full pipeline complete! Model generated and {0} voice lines precached and saved in asset folder!"), FText::AsNumber(*SuccessCount)));
					}
					continue;
				}

				TSharedPtr<FJsonObject> SynthObj = MakeShared<FJsonObject>();
				SynthObj->SetStringField(TEXT("character_name"), VoiceAsset->CharacterName.ToString());
				SynthObj->SetStringField(TEXT("text"), LineText);
				SynthObj->SetStringField(TEXT("language"), VoiceAsset->Language);
				SynthObj->SetNumberField(TEXT("speed"), VoiceAsset->Speed);
				SynthObj->SetStringField(TEXT("embedding_data"), VoiceAsset->ToneColorEmbeddingData);

				FString SynthPayload;
				TSharedRef<TJsonWriter<>> SynthWriter = TJsonWriterFactory<>::Create(&SynthPayload);
				FJsonSerializer::Serialize(SynthObj.ToSharedRef(), SynthWriter);

				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> SynthReq = FHttpModule::Get().CreateRequest();
				SynthReq->SetURL(BaseUrl + TEXT("/synthesize"));
				SynthReq->SetVerb(TEXT("POST"));
				SynthReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				SynthReq->SetContentAsString(SynthPayload);

				SynthReq->OnProcessRequestComplete().BindLambda([WeakAsset, LineText, AssetFolderPath, Remaining, SuccessCount](FHttpRequestPtr SReq, FHttpResponsePtr SRes, bool bSynthSuccess)
				{
					if (bSynthSuccess && SRes.IsValid() && EHttpResponseCodes::IsOk(SRes->GetResponseCode()) && WeakAsset.IsValid())
					{
						FString LineSanitized = FString::Printf(TEXT("SW_%s_%u"), *WeakAsset->CharacterName.ToString(), GetTypeHash(LineText));
						FString SoundWavePackagePath = AssetFolderPath / LineSanitized;

						UPackage* SoundWavePackage = CreatePackage(*SoundWavePackagePath);
						USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(SRes->GetContent(), SoundWavePackage, FName(*LineSanitized));

						if (SoundWave)
						{
							WeakAsset->CacheVoiceLine(LineText, SoundWave);
							SoundWave->MarkPackageDirty();
							WeakAsset->MarkPackageDirty();

							if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
							{
								FAssetRegistryModule::AssetCreated(SoundWave);
							}
							(*SuccessCount)++;
						}
					}

					(*Remaining)--;
					if (*Remaining <= 0)
					{
						FMessageDialog::Open(EAppMsgType::Ok, FText::Format(FText::FromString("Full pipeline complete! Model generated and {0} voice lines precached and saved in asset folder!"), FText::AsNumber(*SuccessCount)));
					}
				});

				SynthReq->ProcessRequest();
			}
		});

		ExtractReq->ProcessRequest();
	});

	TranscribeReq->ProcessRequest();
	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnGenerateModelClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFiles();
	if (RefAudioFiles.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify at least one reference audio file before generating the model."));
		return FReply::Handled();
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString Url = (Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983")) + TEXT("/extract");

	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());

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

	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = TargetVoiceAsset;
	HttpRequest->OnProcessRequestComplete().BindLambda([WeakAsset](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
	{
		if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			TSharedPtr<FJsonObject> ResponseObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
			{
				if (WeakAsset.IsValid())
				{
					WeakAsset->ToneColorEmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
					WeakAsset->bIsModelGenerated = !WeakAsset->ToneColorEmbeddingData.IsEmpty();
					WeakAsset->SaveModelToFile();
					WeakAsset->MarkPackageDirty();
				}
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Successfully generated and saved OpenVoice model tone color embedding file!"));
				return;
			}
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to generate OpenVoice model. Please check Python backend service status."));
	});

	HttpRequest->ProcessRequest();
	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnPrecacheLinesClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	if (Asset->LinesToPreprocess.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("No dialog lines listed in 'Lines To Preprocess'. Add text lines first."));
		return FReply::Handled();
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");

	TSharedPtr<int32> RemainingCount = MakeShared<int32>(Asset->LinesToPreprocess.Num());
	TSharedPtr<int32> SuccessCount = MakeShared<int32>(0);
	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = TargetVoiceAsset;

	for (const FString& LineText : Asset->LinesToPreprocess)
	{
		if (Asset->HasPrecachedVoiceLine(LineText))
		{
			(*SuccessCount)++;
			(*RemainingCount)--;
			if (*RemainingCount <= 0)
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::Format(FText::FromString("Successfully precached {0} voice lines for zero-delay playback!"), FText::AsNumber(*SuccessCount)));
			}
			continue;
		}

		TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
		JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
		JsonObj->SetStringField(TEXT("text"), LineText);
		JsonObj->SetStringField(TEXT("language"), Asset->Language);
		JsonObj->SetNumberField(TEXT("speed"), Asset->Speed);
		JsonObj->SetStringField(TEXT("embedding_data"), Asset->ToneColorEmbeddingData);

		FString PayloadStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL(BaseUrl + TEXT("/synthesize"));
		HttpRequest->SetVerb(TEXT("POST"));
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetContentAsString(PayloadStr);

		HttpRequest->OnProcessRequestComplete().BindLambda([WeakAsset, LineText, RemainingCount, SuccessCount](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(Response->GetContent());
				if (SoundWave && WeakAsset.IsValid())
				{
					WeakAsset->CacheVoiceLine(LineText, SoundWave);
					WeakAsset->MarkPackageDirty();
					(*SuccessCount)++;
				}
			}

			(*RemainingCount)--;
			if (*RemainingCount <= 0)
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::Format(FText::FromString("Successfully precached {0} voice lines for zero-delay playback!"), FText::AsNumber(*SuccessCount)));
			}
		});

		HttpRequest->ProcessRequest();
	}

	return FReply::Handled();
}
