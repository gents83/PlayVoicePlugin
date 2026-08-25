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
		.ToolTipText(FText::FromString("Extract tone color embedding from the reference audio clips."))
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

FReply FCharacterVoiceAssetCustomization::OnGenerateModelClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	if (Asset->ReferenceAudioFiles.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify at least one reference audio file before generating the model."));
		return FReply::Handled();
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString Url = (Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:8000")) + TEXT("/extract");

	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());

	TArray<TSharedPtr<FJsonValue>> AudioPathValues;
	for (const FFilePath& Path : Asset->ReferenceAudioFiles)
	{
		AudioPathValues.Add(MakeShared<FJsonValueString>(Path.FilePath));
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
					WeakAsset->ModelCheckpointPath = ResponseObj->GetStringField(TEXT("model_checkpoint"));
					WeakAsset->bIsModelGenerated = true;
					WeakAsset->MarkPackageDirty();
				}
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Successfully generated OpenVoice model tone color embedding!"));
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
	FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:8000");

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
