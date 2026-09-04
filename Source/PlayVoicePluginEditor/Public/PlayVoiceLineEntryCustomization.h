// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "Input/Reply.h"
#include "AudioCaptureCore.h"

class FPlayVoiceLineEntryCustomization : public IPropertyTypeCustomization
{
public:
	~FPlayVoiceLineEntryCustomization() override;

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructPropertyHandle;
	TSharedPtr<IPropertyHandle> StringTableHandle;
	TSharedPtr<IPropertyHandle> KeyHandle;
	TSharedPtr<IPropertyHandle> TextLineHandle;
	TSharedPtr<IPropertyHandle> AudioFileHandle;
	TSharedPtr<IPropertyHandle> GuideSoundWaveHandle;
	TSharedPtr<IPropertyHandle> PrecachedSoundWaveHandle;

	struct FKeyOption
	{
		FName Key = NAME_None;
		FString SourceText;

		FText GetDisplayText() const
		{
			if (Key.IsNone())
			{
				return FText::FromString(TEXT("None"));
			}
			if (SourceText.IsEmpty())
			{
				return FText::FromName(Key);
			}
			return FText::FromString(FString::Printf(TEXT("%s - \"%s\""), *Key.ToString(), *SourceText));
		}
	};

	TArray<TSharedPtr<FKeyOption>> KeyOptions;
	TSharedPtr<FKeyOption> CurrentlySelectedKey;

	void RefreshKeyOptions();
	void OnGuideSoundWaveChanged();
	FReply OnRecordGuideTrackClicked();
	FReply OnStopRecordingClicked();
	FReply OnPlayPreviewClicked();
	FReply OnBrowseAssetClicked();

	bool bIsRecording = false;
	bool bIsPlayingPreview = false;

	struct FRecordingState
	{
		FCriticalSection Section;
		TArray<int16> Samples;
		int32 SampleRate = 24000;
		int32 Channels = 1;
		bool bActive = true;
	};

	Audio::FAudioCapture AudioCapture;
	TSharedPtr<FRecordingState> RecordingState;
	FCriticalSection RecordedPCMSection;
	TArray<int16> RecordedPCMSamples;
	int32 CapturedSampleRate = 24000;
	int32 CapturedChannels = 1;
};
