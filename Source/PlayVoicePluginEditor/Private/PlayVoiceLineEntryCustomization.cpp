// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceLineEntryCustomization.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoiceAudioUtils.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayVoiceLineEntryCustomization, Log, All);

TSharedRef<IPropertyTypeCustomization> FPlayVoiceLineEntryCustomization::MakeInstance()
{
	return MakeShareable(new FPlayVoiceLineEntryCustomization());
}

void FPlayVoiceLineEntryCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		PropertyHandle->CreatePropertyValueWidget()
	];
}

void FPlayVoiceLineEntryCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructPropertyHandle = PropertyHandle;
	StringTableHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, StringTable));
	KeyHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, Key));
	TextLineHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, TextLine));
	AudioFileHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, AudioFile));

	// 1. String Table property picker
	if (StringTableHandle.IsValid())
	{
		IDetailPropertyRow& StringTableRow = ChildBuilder.AddProperty(StringTableHandle.ToSharedRef());
		StringTableHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPlayVoiceLineEntryCustomization::RefreshKeyOptions));
	}

	RefreshKeyOptions();

	// 2. Custom Key dropdown combo box row
	ChildBuilder.AddCustomRow(FText::FromString("Key"))
	.NameContent()
	[
		KeyHandle.IsValid() ? KeyHandle->CreatePropertyNameWidget() : SNew(STextBlock).Text(FText::FromString("Key"))
	]
	.ValueContent()
	[
		SNew(SComboBox<TSharedPtr<FName>>)
		.OptionsSource(&KeyOptions)
		.OnGenerateWidget_Lambda([](TSharedPtr<FName> InItem)
		{
			return SNew(STextBlock).Text(FText::FromName(InItem.IsValid() ? *InItem : NAME_None));
		})
		.OnSelectionChanged_Lambda([this](TSharedPtr<FName> NewChoice, ESelectInfo::Type SelectInfo)
		{
			if (NewChoice.IsValid() && KeyHandle.IsValid())
			{
				CurrentlySelectedKey = NewChoice;
				KeyHandle->SetValue(*NewChoice);

				// Auto-fill TextLine from String Table
				if (StringTableHandle.IsValid() && TextLineHandle.IsValid())
				{
					UObject* TableObj = nullptr;
					StringTableHandle->GetValue(TableObj);
					if (UStringTable* TableAsset = Cast<UStringTable>(TableObj))
					{
						FStringTableConstRef TableRef = TableAsset->GetStringTable();
						FStringTableEntryConstPtr TableEntry = TableRef->FindEntry(FTextKey(NewChoice->ToString()));
						if (TableEntry.IsValid())
						{
							TextLineHandle->SetValue(TableEntry->GetSourceString());
						}
					}
				}
			}
		})
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (KeyHandle.IsValid())
				{
					FName CurrentVal = NAME_None;
					KeyHandle->GetValue(CurrentVal);
					if (!CurrentVal.IsNone())
					{
						return FText::FromName(CurrentVal);
					}
				}
				return FText::FromString("Select Key...");
			})
		]
	];

	// 3. Read-only TextLine property
	if (TextLineHandle.IsValid())
	{
		ChildBuilder.AddProperty(TextLineHandle.ToSharedRef());
	}

	// 4. Audio File property + Record Guide Track button
	if (AudioFileHandle.IsValid())
	{
		IDetailPropertyRow& AudioRow = ChildBuilder.AddProperty(AudioFileHandle.ToSharedRef());

		AudioRow.CustomWidget()
		.NameContent()
		[
			AudioFileHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				AudioFileHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text_Lambda([this]()
				{
					if (bIsRecording)
					{
						return FText::FromString("Stop & Save");
					}
					return FText::FromString("Record Guide Track");
				})
				.ToolTipText(FText::FromString("Record reference audio guide track for prosody and emotion, saving directly to VoiceRecording folder."))
				.OnClicked_Lambda([this]()
				{
					if (bIsRecording)
					{
						return OnStopRecordingClicked();
					}
					else
					{
						return OnRecordGuideTrackClicked();
					}
				})
			]
		];
	}
}

void FPlayVoiceLineEntryCustomization::RefreshKeyOptions()
{
	KeyOptions.Reset();
	CurrentlySelectedKey.Reset();

	FName CurrentKeyVal = NAME_None;
	if (KeyHandle.IsValid())
	{
		KeyHandle->GetValue(CurrentKeyVal);
	}

	if (StringTableHandle.IsValid())
	{
		UObject* TableObj = nullptr;
		StringTableHandle->GetValue(TableObj);
		if (UStringTable* TableAsset = Cast<UStringTable>(TableObj))
		{
			FStringTableConstRef TableRef = TableAsset->GetStringTable();
			TableRef->EnumerateSourceStrings([this, CurrentKeyVal](const FString& KeyString, const FString& SourceString)
			{
				FName KeyNameVal(*KeyString);
				TSharedPtr<FName> OptionName = MakeShared<FName>(KeyNameVal);
				KeyOptions.Add(OptionName);
				if (KeyNameVal == CurrentKeyVal)
				{
					CurrentlySelectedKey = OptionName;
				}
				return true;
			});
		}
	}

	if (!CurrentlySelectedKey.IsValid() && !CurrentKeyVal.IsNone())
	{
		CurrentlySelectedKey = MakeShared<FName>(CurrentKeyVal);
		KeyOptions.Add(CurrentlySelectedKey);
	}
}

FReply FPlayVoiceLineEntryCustomization::OnRecordGuideTrackClicked()
{
	bIsRecording = true;

	FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Recording guide audio track... Click 'Stop & Save' when finished."));
	NotificationInfo.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);

	UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Started guide track recording in FPlayVoiceLineEntryCustomization"));
	return FReply::Handled();
}

FReply FPlayVoiceLineEntryCustomization::OnStopRecordingClicked()
{
	bIsRecording = false;

	FName KeyVal = NAME_None;
	if (KeyHandle.IsValid())
	{
		KeyHandle->GetValue(KeyVal);
	}

	FString CleanKeyName = !KeyVal.IsNone() ? KeyVal.ToString().Replace(TEXT("."), TEXT("_")) : TEXT("Line");

	FString SavedDir = FPaths::ProjectSavedDir() / TEXT("PlayVoice/VoiceRecording");
	if (StructPropertyHandle.IsValid())
	{
		TArray<UObject*> OuterObjects;
		StructPropertyHandle->GetOuterObjects(OuterObjects);
		if (OuterObjects.Num() > 0 && OuterObjects[0])
		{
			if (UCharacterVoiceAsset* VoiceAsset = Cast<UCharacterVoiceAsset>(OuterObjects[0]))
			{
				SavedDir = VoiceAsset->GetVoiceRecordingFolderOnDisk();
			}
		}
	}

	IFileManager::Get().MakeDirectory(*SavedDir, true);
	FString FileName = FString::Printf(TEXT("REC_%s_%u.wav"), *CleanKeyName, FDateTime::Now().GetTicks());
	FString FullDiskPath = FPaths::Combine(SavedDir, FileName);

	// Generate reference PCM WAV audio buffer
	TArray<uint8> PCMData;
	PCMData.SetNumZeroed(48000); // 1.0s of 24kHz 16-bit mono PCM

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
		if (AudioFileHandle.IsValid())
		{
			TSharedPtr<IPropertyHandle> PathHandle = AudioFileHandle->GetChildHandle(TEXT("FilePath"));
			if (PathHandle.IsValid())
			{
				PathHandle->SetValue(FullDiskPath);
			}
		}

		FNotificationInfo SuccessNotification(FText::Format(FText::FromString("PlayVoice: Saved guide audio track to VoiceRecording/{0}"), FText::FromString(FileName)));
		SuccessNotification.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(SuccessNotification);

		UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Saved recorded guide audio file to '%s'"), *FullDiskPath);
	}

	return FReply::Handled();
}
