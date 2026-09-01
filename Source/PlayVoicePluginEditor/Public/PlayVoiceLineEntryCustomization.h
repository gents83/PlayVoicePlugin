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
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructPropertyHandle;
	TSharedPtr<IPropertyHandle> StringTableHandle;
	TSharedPtr<IPropertyHandle> KeyHandle;
	TSharedPtr<IPropertyHandle> TextLineHandle;
	TSharedPtr<IPropertyHandle> AudioFileHandle;

	TArray<TSharedPtr<FName>> KeyOptions;
	TSharedPtr<FName> CurrentlySelectedKey;

	void RefreshKeyOptions();
	FReply OnRecordGuideTrackClicked();
	FReply OnStopRecordingClicked();
	FReply OnPlayPreviewClicked();

	bool bIsRecording = false;
	bool bIsPlayingPreview = false;

	Audio::FAudioCapture AudioCapture;
	FCriticalSection RecordedPCMSection;
	TArray<int16> RecordedPCMSamples;
	int32 CapturedSampleRate = 24000;
	int32 CapturedChannels = 1;
};
