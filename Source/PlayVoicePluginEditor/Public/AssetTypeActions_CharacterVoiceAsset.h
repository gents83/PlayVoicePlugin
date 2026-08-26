// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "CharacterVoiceAsset.h"

/**
 * Custom asset type actions for UCharacterVoiceAsset in Unreal Editor.
 */
class FAssetTypeActions_CharacterVoiceAsset : public FAssetTypeActions_Base
{
public:
	FAssetTypeActions_CharacterVoiceAsset(EAssetTypeCategories::Type InAssetCategory)
		: AssetCategory(InAssetCategory)
	{}

	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_CharacterVoiceAsset", "Character Voice Asset"); }
	virtual FColor GetTypeColor() const override { return FColor(0, 175, 255); }
	virtual UClass* GetSupportedClass() const override { return UCharacterVoiceAsset::StaticClass(); }
	virtual uint32 GetCategories() override { return AssetCategory; }

private:
	EAssetTypeCategories::Type AssetCategory;
};
