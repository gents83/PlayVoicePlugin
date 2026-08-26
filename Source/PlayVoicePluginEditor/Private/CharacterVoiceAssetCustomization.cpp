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

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	if (Asset->Languages.Num() == 0)
	{
		Asset->GetOrAddLanguageData(Asset->DefaultLanguage.IsEmpty() ? TEXT("EN") : Asset->DefaultLanguage);
	}

	TArray<FString> DiscoveredBlueprintLines = RetrieveVoiceLinesFromProjectBlueprints(Asset);

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");

	int32 TotalLangsProcessed = 0;
	for (FCharacterLanguageData& LangData : Asset->Languages)
	{
		TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFilesForLanguage(LangData.LanguageCode);
		if (RefAudioFiles.Num() == 0)
		{
			continue;
		}

		TotalLangsProcessed++;

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

		TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = TargetVoiceAsset;
		FString CurrentLangCode = LangData.LanguageCode;
		float CurrentSpeed = LangData.Speed;

		ExtractReq->OnProcessRequestComplete().BindLambda([WeakAsset, BaseUrl, CurrentLangCode, CurrentSpeed, DiscoveredBlueprintLines](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bExtractSuccess)
		{
			if (bExtractSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode()) && WeakAsset.IsValid())
			{
				TSharedPtr<FJsonObject> ResObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
				if (FJsonSerializer::Deserialize(Reader, ResObj) && ResObj.IsValid())
				{
					FCharacterLanguageData* TargetLangData = WeakAsset->FindLanguageData(CurrentLangCode);
					if (TargetLangData)
					{
						TargetLangData->ToneColorEmbeddingData = ResObj->GetStringField(TEXT("embedding_data"));
						TargetLangData->bIsModelGenerated = !TargetLangData->ToneColorEmbeddingData.IsEmpty();
						WeakAsset->SaveModelToFile(TEXT(""), CurrentLangCode);
						WeakAsset->MarkPackageDirty();
					}
				}
			}

			if (!WeakAsset.IsValid() || DiscoveredBlueprintLines.Num() == 0)
			{
				return;
			}

			UCharacterVoiceAsset* VoiceAsset = WeakAsset.Get();
			FCharacterLanguageData* TargetLangData = VoiceAsset->FindLanguageData(CurrentLangCode);
			FString EmbeddingData = TargetLangData ? TargetLangData->ToneColorEmbeddingData : TEXT("");

			UPackage* OuterPackage = VoiceAsset->GetOutermost();
			FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

			for (const FString& LineText : DiscoveredBlueprintLines)
			{
				if (VoiceAsset->HasPrecachedVoiceLine(LineText, CurrentLangCode))
				{
					continue;
				}

				TSharedPtr<FJsonObject> SynthObj = MakeShared<FJsonObject>();
				SynthObj->SetStringField(TEXT("character_name"), VoiceAsset->CharacterName.ToString());
				SynthObj->SetStringField(TEXT("text"), LineText);
				SynthObj->SetStringField(TEXT("language"), CurrentLangCode);
				SynthObj->SetNumberField(TEXT("speed"), CurrentSpeed);
				SynthObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

				FString SynthPayload;
				TSharedRef<TJsonWriter<>> SynthWriter = TJsonWriterFactory<>::Create(&SynthPayload);
				FJsonSerializer::Serialize(SynthObj.ToSharedRef(), SynthWriter);

				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> SynthReq = FHttpModule::Get().CreateRequest();
				SynthReq->SetURL(BaseUrl + TEXT("/synthesize"));
				SynthReq->SetVerb(TEXT("POST"));
				SynthReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				SynthReq->SetContentAsString(SynthPayload);

				SynthReq->OnProcessRequestComplete().BindLambda([WeakAsset, LineText, CurrentLangCode, AssetFolderPath](FHttpRequestPtr SReq, FHttpResponsePtr SRes, bool bSynthSuccess)
				{
					if (bSynthSuccess && SRes.IsValid() && EHttpResponseCodes::IsOk(SRes->GetResponseCode()) && WeakAsset.IsValid())
					{
						FString LineSanitized = FString::Printf(TEXT("SW_%s_%s_%u"), *WeakAsset->CharacterName.ToString(), *CurrentLangCode, GetTypeHash(LineText));
						FString SoundWavePackagePath = AssetFolderPath / LineSanitized;

						UPackage* SoundWavePackage = CreatePackage(*SoundWavePackagePath);
						USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(SRes->GetContent(), SoundWavePackage, FName(*LineSanitized));

						if (SoundWave)
						{
							WeakAsset->CacheVoiceLine(LineText, SoundWave, CurrentLangCode);
							SoundWave->MarkPackageDirty();
							WeakAsset->MarkPackageDirty();

							if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
							{
								FAssetRegistryModule::AssetCreated(SoundWave);
							}
						}
					}
				});

				SynthReq->ProcessRequest();
			}
		});

		ExtractReq->ProcessRequest();
	}

	if (TotalLangsProcessed == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify Reference Audio Files or Reference Audio Folder for at least one language before generating models."));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(FText::FromString("Started model generation and voice line precaching for {0} languages and {1} discovered Blueprint dialogue lines."), FText::AsNumber(TotalLangsProcessed), FText::AsNumber(DiscoveredBlueprintLines.Num())));
	}

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnGenerateModelClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	if (Asset->Languages.Num() == 0)
	{
		Asset->GetOrAddLanguageData(Asset->DefaultLanguage.IsEmpty() ? TEXT("EN") : Asset->DefaultLanguage);
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString Url = (Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983")) + TEXT("/extract");

	int32 ProcessedLangs = 0;
	for (FCharacterLanguageData& LangData : Asset->Languages)
	{
		TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFilesForLanguage(LangData.LanguageCode);
		if (RefAudioFiles.Num() == 0)
		{
			continue;
		}

		ProcessedLangs++;

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

		TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = TargetVoiceAsset;
		FString CurrentLangCode = LangData.LanguageCode;

		HttpRequest->OnProcessRequestComplete().BindLambda([WeakAsset, CurrentLangCode](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				TSharedPtr<FJsonObject> ResponseObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
				{
					if (WeakAsset.IsValid())
					{
						FCharacterLanguageData* TargetLangData = WeakAsset->FindLanguageData(CurrentLangCode);
						if (TargetLangData)
						{
							TargetLangData->ToneColorEmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
							TargetLangData->bIsModelGenerated = !TargetLangData->ToneColorEmbeddingData.IsEmpty();
							WeakAsset->SaveModelToFile(TEXT(""), CurrentLangCode);
							WeakAsset->MarkPackageDirty();
						}
					}
				}
			}
		});

		HttpRequest->ProcessRequest();
	}

	if (ProcessedLangs == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify Reference Audio Files or Reference Audio Folder for at least one language."));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(FText::FromString("Triggered model generation for {0} languages."), FText::AsNumber(ProcessedLangs)));
	}

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnPrecacheLinesClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	TArray<FString> DiscoveredBlueprintLines = RetrieveVoiceLinesFromProjectBlueprints(Asset);

	if (DiscoveredBlueprintLines.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("No dialogue lines found in Blueprint graph nodes referencing this asset."));
		return FReply::Handled();
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");

	TWeakObjectPtr<UCharacterVoiceAsset> WeakAsset = TargetVoiceAsset;
	UPackage* OuterPackage = Asset->GetOutermost();
	FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

	for (const FCharacterLanguageData& LangData : Asset->Languages)
	{
		FString CurrentLangCode = LangData.LanguageCode;
		float CurrentSpeed = LangData.Speed;
		FString EmbeddingData = LangData.ToneColorEmbeddingData;

		for (const FString& LineText : DiscoveredBlueprintLines)
		{
			if (Asset->HasPrecachedVoiceLine(LineText, CurrentLangCode))
			{
				continue;
			}

			TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
			JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
			JsonObj->SetStringField(TEXT("text"), LineText);
			JsonObj->SetStringField(TEXT("language"), CurrentLangCode);
			JsonObj->SetNumberField(TEXT("speed"), CurrentSpeed);
			JsonObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

			FString PayloadStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
			FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
			HttpRequest->SetURL(BaseUrl + TEXT("/synthesize"));
			HttpRequest->SetVerb(TEXT("POST"));
			HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			HttpRequest->SetContentAsString(PayloadStr);

			HttpRequest->OnProcessRequestComplete().BindLambda([WeakAsset, LineText, CurrentLangCode, AssetFolderPath](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
			{
				if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()) && WeakAsset.IsValid())
				{
					FString LineSanitized = FString::Printf(TEXT("SW_%s_%s_%u"), *WeakAsset->CharacterName.ToString(), *CurrentLangCode, GetTypeHash(LineText));
					FString SoundWavePackagePath = AssetFolderPath / LineSanitized;

					UPackage* SoundWavePackage = CreatePackage(*SoundWavePackagePath);
					USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(Response->GetContent(), SoundWavePackage, FName(*LineSanitized));

					if (SoundWave)
					{
						WeakAsset->CacheVoiceLine(LineText, SoundWave, CurrentLangCode);
						SoundWave->MarkPackageDirty();
						WeakAsset->MarkPackageDirty();

						if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
						{
							FAssetRegistryModule::AssetCreated(SoundWave);
						}
					}
				}
			});

			HttpRequest->ProcessRequest();
		}
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(FText::FromString("Pre-rendering initiated for {0} discovered dialogue lines across {1} languages."), FText::AsNumber(DiscoveredBlueprintLines.Num()), FText::AsNumber(Asset->Languages.Num())));

	return FReply::Handled();
}
