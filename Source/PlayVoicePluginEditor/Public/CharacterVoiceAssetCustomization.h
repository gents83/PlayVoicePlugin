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

	/** Returns whether a SoundWave name has the generated identity suffix for a voice line. */
	static bool IsGeneratedSoundWaveNameForVoiceLine(const FString& SoundWaveName, FName StringTableId, FName Key, const FString& LanguageCode);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnGenerateModelClicked();
	FReply OnGenerateFromVoiceLinesClicked();
	FReply OnNormalizePrecachedSoundLevelsClicked();
	FReply OnCleanPrecachedSoundWavesClicked();

	TWeakObjectPtr<class UCharacterVoiceAsset> TargetVoiceAsset;
};
