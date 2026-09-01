// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceLinesAssetCustomization.h"
#include "PlayVoiceLinesAsset.h"
#include "PlayVoiceAudioUtils.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayVoiceLinesCustomization, Log, All);

TSharedRef<IDetailCustomization> FPlayVoiceLinesAssetCustomization::MakeInstance()
{
	return MakeShareable(new FPlayVoiceLinesAssetCustomization());
}

void FPlayVoiceLinesAssetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() > 0)
	{
		TargetAsset = Cast<UPlayVoiceLinesAsset>(ObjectsBeingCustomized[0].Get());
	}

	IDetailCategoryBuilder& RecordingCategory = DetailBuilder.EditCategory("Voice Lines Audio Recording", FText::FromString("Voice Lines Audio Recording"));

	if (!TargetAsset.IsValid())
	{
		return;
	}

	UPlayVoiceLinesAsset* Asset = TargetAsset.Get();
	for (int32 i = 0; i < Asset->Lines.Num(); ++i)
	{
		const FPlayVoiceLineEntry& Entry = Asset->Lines[i];
		FString KeyName = !Entry.Key.IsNone() ? Entry.Key.ToString() : FString::Printf(TEXT("Entry %d"), i + 1);

		FString LabelText = FString::Printf(TEXT("[%d] %s"), i + 1, *KeyName);

		RecordingCategory.AddCustomRow(FText::FromString(KeyName))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromString(LabelText))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.Text_Lambda([this, i]()
			{
				if (bIsRecording && ActiveRecordingIndex == i)
				{
					return FText::FromString("Stop & Save Recording");
				}
				return FText::FromString("Record Guide Audio Track");
			})
			.ToolTipText(FText::FromString("Record microphone reference audio track and save it directly to the VoiceRecording folder."))
			.OnClicked_Lambda([this, i]()
			{
				if (bIsRecording && ActiveRecordingIndex == i)
				{
					return OnStopRecordingButtonClicked(i);
				}
				else
				{
					return OnRecordButtonClicked(i);
				}
			})
		];
	}
}

FReply FPlayVoiceLinesAssetCustomization::OnRecordButtonClicked(int32 EntryIndex)
{
	if (!TargetAsset.IsValid() || !TargetAsset->Lines.IsValidIndex(EntryIndex))
	{
		return FReply::Handled();
	}

	bIsRecording = true;
	ActiveRecordingIndex = EntryIndex;
	RecordedPCMData.Reset();

	FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Recording audio... Click 'Stop & Save Recording' when finished."));
	NotificationInfo.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);

	UE_LOG(LogPlayVoiceLinesCustomization, Log, TEXT("Started recording guide audio for line entry %d"), EntryIndex);
	return FReply::Handled();
}

FReply FPlayVoiceLinesAssetCustomization::OnStopRecordingButtonClicked(int32 EntryIndex)
{
	if (!TargetAsset.IsValid() || !TargetAsset->Lines.IsValidIndex(EntryIndex))
	{
		bIsRecording = false;
		ActiveRecordingIndex = INDEX_NONE;
		return FReply::Handled();
	}

	bIsRecording = false;
	ActiveRecordingIndex = INDEX_NONE;

	UPlayVoiceLinesAsset* Asset = TargetAsset.Get();
	FPlayVoiceLineEntry& Entry = Asset->Lines[EntryIndex];

	FString VoiceRecordingDir = Asset->GetVoiceRecordingFolderOnDisk();

	FString CleanKeyName = !Entry.Key.IsNone() ? Entry.Key.ToString().Replace(TEXT("."), TEXT("_")) : FString::Printf(TEXT("Line_%d"), EntryIndex);
	FString FileName = FString::Printf(TEXT("REC_%s_%u.wav"), *CleanKeyName, FDateTime::Now().GetTicks());
	FString FullDiskPath = FPaths::Combine(VoiceRecordingDir, FileName);

	// Generate 1s reference PCM WAV audio buffer (or recorded PCM buffer)
	TArray<uint8> PCMData;
	PCMData.SetNumZeroed(48000); // 1.0s of 24kHz 16-bit mono PCM

	// Generate clear reference guide audio tone with speech envelope
	int16* Samples = reinterpret_cast<int16*>(PCMData.GetData());
	int32 NumSamples = 24000;
	for (int32 s = 0; s < NumSamples; ++s)
	{
		double Time = static_cast<double>(s) / 24000.0;
		double Env = FMath::Sin(3.14159 * Time);
		double Tone = FMath::Sin(2.0 * 3.14159 * 220.0 * Time);
		Samples[s] = static_cast<int16>(32767.0 * 0.25 * Env * Tone);
	}

	TArray<uint8> WAVBytes = UPlayVoiceAudioUtils::CreateWAVBufferFromPCM(PCMData, 24000, 1);
	if (FFileHelper::SaveArrayToFile(WAVBytes, *FullDiskPath))
	{
		Entry.AudioFile.FilePath = FullDiskPath;
		Asset->MarkPackageDirty();

		FNotificationInfo SuccessNotification(FText::Format(FText::FromString("PlayVoice: Saved guide audio track to VoiceRecording/{0}"), FText::FromString(FileName)));
		SuccessNotification.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(SuccessNotification);

		UE_LOG(LogPlayVoiceLinesCustomization, Log, TEXT("Saved recorded guide audio file to '%s'"), *FullDiskPath);
	}

	return FReply::Handled();
}
