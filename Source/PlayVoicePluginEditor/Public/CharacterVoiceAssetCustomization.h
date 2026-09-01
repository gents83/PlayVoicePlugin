// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"

class FCharacterVoiceAssetCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

public:
	/** Scans all Blueprint assets in the project and extracts dialogue lines used in PlayVoice nodes matching TargetAsset */
	static TArray<FString> RetrieveVoiceLinesFromProjectBlueprints(const class UCharacterVoiceAsset* TargetAsset, int32* OutMatchingNodesCount = nullptr, TArray<FString>* OutMatchingBlueprints = nullptr);

private:
	FReply OnGenerateAndProcessAllClicked();
	FReply OnGenerateModelClicked();
	FReply OnGenerateFromVoiceLinesClicked();
	FReply OnPrecacheLinesClicked();
	FReply OnCleanPrecachedSoundWavesClicked();

	TWeakObjectPtr<class UCharacterVoiceAsset> TargetVoiceAsset;
};
