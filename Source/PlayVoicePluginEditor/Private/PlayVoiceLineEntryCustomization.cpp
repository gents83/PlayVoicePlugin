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
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"

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
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text_Lambda([this]()
				{
					if (bIsPlayingPreview)
					{
						return FText::FromString("Stop Preview");
					}
					return FText::FromString("Play Guide Track");
				})
				.ToolTipText(FText::FromString("Listen to the recorded or selected audio guide track directly in place."))
				.OnClicked(this, &FPlayVoiceLineEntryCustomization::OnPlayPreviewClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString("Browse Asset"))
				.ToolTipText(FText::FromString("Navigate to and focus the precached SoundWave asset in the Content Browser."))
				.OnClicked(this, &FPlayVoiceLineEntryCustomization::OnBrowseAssetClicked)
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
	{
		FScopeLock Lock(&RecordedPCMSection);
		RecordedPCMSamples.Reset();
	}

	Audio::FAudioCaptureDeviceParams Params;
	Audio::FOnAudioCaptureFunction CaptureCallback = [this](const void* InAudioData, int32 NumFrames, int32 NumChannels, int32 SampleRate, double SampleTime, bool bOverflow)
	{
		const float* AudioData = static_cast<const float*>(InAudioData);
		if (AudioData && NumFrames > 0 && NumChannels > 0)
		{
			FScopeLock Lock(&RecordedPCMSection);
			CapturedSampleRate = SampleRate;
			CapturedChannels = NumChannels;

			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				float MonoSample = 0.0f;
				for (int32 Ch = 0; Ch < NumChannels; ++Ch)
				{
					MonoSample += AudioData[Frame * NumChannels + Ch];
				}
				MonoSample /= static_cast<float>(NumChannels);

				float ClampedSample = FMath::Clamp(MonoSample, -1.0f, 1.0f);
				int16 IntSample = static_cast<int16>(ClampedSample < 0.0f ? ClampedSample * 32768.0f : ClampedSample * 32767.0f);
				RecordedPCMSamples.Add(IntSample);
			}
		}
	};
	bool bStreamOpened = AudioCapture.OpenAudioCaptureStream(Params, MoveTemp(CaptureCallback), 1024);

	if (bStreamOpened)
	{
		AudioCapture.StartStream();
		bIsRecording = true;

		FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Recording microphone guide track... Click 'Stop & Save' when finished."));
		NotificationInfo.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Started FAudioCapture stream for guide track recording"));
	}
	else
	{
		UE_LOG(LogPlayVoiceLineEntryCustomization, Warning, TEXT("Failed to open audio capture device. Falling back to synthetic guide buffer."));
		bIsRecording = true;
	}

	return FReply::Handled();
}

FReply FPlayVoiceLineEntryCustomization::OnBrowseAssetClicked()
{
	USoundWave* TargetSoundWave = nullptr;
	if (StructPropertyHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> SoundHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, PrecachedSoundWave));
		if (SoundHandle.IsValid())
		{
			UObject* SoundObj = nullptr;
			SoundHandle->GetValue(SoundObj);
			TargetSoundWave = Cast<USoundWave>(SoundObj);
		}
	}

	if (TargetSoundWave)
	{
		TArray<UObject*> SyncObjects;
		SyncObjects.Add(TargetSoundWave);

		if (GEditor)
		{
			GEditor->SyncBrowserToObjects(SyncObjects);
			UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Browsing to asset '%s' in Content Browser."), *TargetSoundWave->GetName());
		}
	}
	else
	{
		FNotificationInfo Info(FText::FromString("PlayVoice: No precached SoundWave asset linked to browse."));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		UE_LOG(LogPlayVoiceLineEntryCustomization, Warning, TEXT("OnBrowseAssetClicked: No sound wave asset present."));
	}

	return FReply::Handled();
}

FReply FPlayVoiceLineEntryCustomization::OnPlayPreviewClicked()
{
	if (bIsPlayingPreview)
	{
		if (GEditor)
		{
			GEditor->ResetPreviewAudioComponent();
		}
		bIsPlayingPreview = false;
		UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Stopped audio guide track preview playback."));
		return FReply::Handled();
	}

	FString FilePathStr;
	if (AudioFileHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> PathHandle = AudioFileHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFilePath, FilePath));
		if (PathHandle.IsValid())
		{
			PathHandle->GetValue(FilePathStr);
		}
	}

	USoundWave* TargetSoundWave = nullptr;
	if (StructPropertyHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> SoundHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, PrecachedSoundWave));
		if (SoundHandle.IsValid())
		{
			UObject* SoundObj = nullptr;
			SoundHandle->GetValue(SoundObj);
			TargetSoundWave = Cast<USoundWave>(SoundObj);
		}
	}

	if (!TargetSoundWave && !FilePathStr.IsEmpty() && IFileManager::Get().FileExists(*FilePathStr))
	{
		TArray<uint8> WAVBytes;
		if (FFileHelper::LoadFileToArray(WAVBytes, *FilePathStr) && WAVBytes.Num() > 0)
		{
			TargetSoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(WAVBytes, GetTransientPackage(), FName(TEXT("PreviewGuideTrack")));
		}
	}

	if (!TargetSoundWave)
	{
		FNotificationInfo Info(FText::FromString("PlayVoice: No audio guide track file or precached SoundWave available to play."));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		UE_LOG(LogPlayVoiceLineEntryCustomization, Warning, TEXT("OnPlayPreviewClicked: No guide track file or sound wave present for entry."));
		return FReply::Handled();
	}

	if (GEditor)
	{
		GEditor->ResetPreviewAudioComponent();
		GEditor->PlayPreviewSound(TargetSoundWave);
		bIsPlayingPreview = true;
		UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Started audio guide track preview playback for path '%s' (SoundWave: %s)"), *FilePathStr, *TargetSoundWave->GetName());

		FNotificationInfo Info(FText::Format(FText::FromString("PlayVoice: Playing guide track preview ({0})..."), FText::FromString(FPaths::GetCleanFilename(FilePathStr))));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	return FReply::Handled();
}

FReply FPlayVoiceLineEntryCustomization::OnStopRecordingClicked()
{
	if (bIsRecording)
	{
		AudioCapture.StopStream();
		AudioCapture.CloseStream();
		bIsRecording = false;
	}

	TArray<int16> PCM16Data;
	int32 SR = 24000;
	{
		FScopeLock Lock(&RecordedPCMSection);
		PCM16Data = RecordedPCMSamples;
		SR = CapturedSampleRate > 0 ? CapturedSampleRate : 24000;
	}

	if (PCM16Data.Num() == 0)
	{
		PCM16Data.SetNumZeroed(24000);
		SR = 24000;
	}

	TArray<uint8> PCMBytes;
	PCMBytes.SetNumUninitialized(PCM16Data.Num() * sizeof(int16));
	FMemory::Memcpy(PCMBytes.GetData(), PCM16Data.GetData(), PCMBytes.Num());

	TArray<uint8> WAVBytes = UPlayVoiceAudioUtils::CreateWAVBufferFromPCM(PCMBytes, SR, 1);

	FName KeyVal = NAME_None;
	if (KeyHandle.IsValid())
	{
		KeyHandle->GetValue(KeyVal);
	}

	FString CleanKeyName = !KeyVal.IsNone() ? KeyVal.ToString().Replace(TEXT("."), TEXT("_")) : TEXT("Line");
	FString SavedDir = FPaths::ProjectSavedDir() / TEXT("PlayVoice/VoiceRecording");

	UCharacterVoiceAsset* TargetVoiceAsset = nullptr;
	if (StructPropertyHandle.IsValid())
	{
		TArray<UObject*> OuterObjects;
		StructPropertyHandle->GetOuterObjects(OuterObjects);
		if (OuterObjects.Num() > 0 && OuterObjects[0])
		{
			TargetVoiceAsset = Cast<UCharacterVoiceAsset>(OuterObjects[0]);
			if (TargetVoiceAsset)
			{
				SavedDir = TargetVoiceAsset->GetVoiceRecordingFolderOnDisk();
			}
		}
	}

	IFileManager::Get().MakeDirectory(*SavedDir, true);
	FString FileName = FString::Printf(TEXT("REC_%s_%u.wav"), *CleanKeyName, FDateTime::Now().GetTicks());
	FString FullDiskPath = FPaths::Combine(SavedDir, FileName);

	// Clean up previous recorded audio file and precached sound wave if present
	FString OldFilePath;
	if (AudioFileHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> PathHandle = AudioFileHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFilePath, FilePath));
		if (PathHandle.IsValid())
		{
			PathHandle->GetValue(OldFilePath);
		}
	}

	if (!OldFilePath.IsEmpty() && OldFilePath != FullDiskPath && IFileManager::Get().FileExists(*OldFilePath))
	{
		bool bDeletedOld = IFileManager::Get().Delete(*OldFilePath);
		UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Deleted previous recorded guide track file '%s' (Success: %d)"), *OldFilePath, bDeletedOld);
	}

	USoundWave* OldSoundWave = nullptr;
	if (StructPropertyHandle.IsValid())
	{
		TSharedPtr<IPropertyHandle> SoundHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, PrecachedSoundWave));
		if (SoundHandle.IsValid())
		{
			UObject* SoundObj = nullptr;
			SoundHandle->GetValue(SoundObj);
			OldSoundWave = Cast<USoundWave>(SoundObj);
		}
	}

	if (OldSoundWave)
	{
		FString OldPkgFilename;
		UPackage* OldPkg = OldSoundWave->GetOutermost();
		if (OldPkg && OldPkg != GetTransientPackage())
		{
			FPackageName::DoesPackageExist(OldPkg->GetName(), &OldPkgFilename);
		}

		if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			AssetRegistryModule.AssetDeleted(OldSoundWave);
		}

		OldSoundWave->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		OldSoundWave->MarkAsGarbage();
		if (OldPkg && OldPkg != GetTransientPackage())
		{
			OldPkg->MarkAsGarbage();
		}
		CollectGarbage(RF_NoFlags);

		if (!OldPkgFilename.IsEmpty() && IFileManager::Get().FileExists(*OldPkgFilename))
		{
			IFileManager::Get().Delete(*OldPkgFilename);
			UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Deleted previous precached SoundWave package file '%s'"), *OldPkgFilename);
		}
	}

	if (FFileHelper::SaveArrayToFile(WAVBytes, *FullDiskPath))
	{
		if (AudioFileHandle.IsValid())
		{
			TSharedPtr<IPropertyHandle> PathHandle = AudioFileHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFilePath, FilePath));
			if (PathHandle.IsValid())
			{
				PathHandle->SetValue(FullDiskPath);
			}
			AudioFileHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		}
		if (StructPropertyHandle.IsValid())
		{
			StructPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		}

		if (TargetVoiceAsset)
		{
			UPackage* OuterPackage = TargetVoiceAsset->GetOutermost();
			FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());
			FString KeySanitized = FString::Printf(TEXT("SW_%s_%s_%s"), *TargetVoiceAsset->CharacterName.ToString(), *TargetVoiceAsset->DefaultLanguage, *CleanKeyName);
			FString PackagePath = AssetFolderPath / KeySanitized;

			UPackage* SoundWavePackage = CreatePackage(*PackagePath);
			USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(WAVBytes, SoundWavePackage, FName(*KeySanitized));

			if (SoundWave)
			{
				TargetVoiceAsset->CacheVoiceLineForKey(KeyVal, SoundWave, TargetVoiceAsset->DefaultLanguage);

				TSharedPtr<IPropertyHandle> SoundHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPlayVoiceLineEntry, PrecachedSoundWave));
				if (SoundHandle.IsValid())
				{
					SoundHandle->SetValue(SoundWave);
				}

				SoundWave->MarkPackageDirty();
				TargetVoiceAsset->MarkPackageDirty();

				if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
				{
					FAssetRegistryModule::AssetCreated(SoundWave);
				}
				UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Registered recorded guide track as precached SoundWave asset '%s'"), *KeySanitized);
			}
		}

		FNotificationInfo SuccessNotification(FText::Format(FText::FromString("PlayVoice: Saved guide track and created precached SoundWave: VoiceRecording/{0}"), FText::FromString(FileName)));
		SuccessNotification.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(SuccessNotification);

		UE_LOG(LogPlayVoiceLineEntryCustomization, Log, TEXT("Saved recorded guide audio file to '%s'"), *FullDiskPath);
	}

	return FReply::Handled();
}
