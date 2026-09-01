// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "PlayVoiceLinesAsset.h"

/**
 * Custom asset type actions for UPlayVoiceLinesAsset in Unreal Editor.
 */
class FAssetTypeActions_PlayVoiceLinesAsset : public FAssetTypeActions_Base
{
public:
	FAssetTypeActions_PlayVoiceLinesAsset(EAssetTypeCategories::Type InAssetCategory)
		: AssetCategory(InAssetCategory)
	{}

	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_PlayVoiceLinesAsset", "Play Voice Lines"); }
	virtual FColor GetTypeColor() const override { return FColor(255, 128, 0); }
	virtual UClass* GetSupportedClass() const override { return UPlayVoiceLinesAsset::StaticClass(); }
	virtual uint32 GetCategories() override { return AssetCategory; }

private:
	EAssetTypeCategories::Type AssetCategory;
};
