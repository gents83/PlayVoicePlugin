// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "PlayVoiceLinesAsset.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;

/**
 * Custom details panel layout for UPlayVoiceLinesAsset enabling audio recording per voice line entry.
 */
class FPlayVoiceLinesAssetCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TWeakObjectPtr<UPlayVoiceLinesAsset> TargetAsset;
	bool bIsRecording = false;
	int32 ActiveRecordingIndex = INDEX_NONE;
	TArray<uint8> RecordedPCMData;

	FReply OnRecordButtonClicked(int32 EntryIndex);
	FReply OnStopRecordingButtonClicked(int32 EntryIndex);
};
