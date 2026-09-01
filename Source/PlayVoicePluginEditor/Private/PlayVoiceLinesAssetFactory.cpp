// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceLinesAssetFactory.h"
#include "PlayVoiceLinesAsset.h"
#include "PlayVoicePluginEditorModule.h"

UPlayVoiceLinesAssetFactory::UPlayVoiceLinesAssetFactory()
{
	SupportedClass = UPlayVoiceLinesAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UPlayVoiceLinesAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UPlayVoiceLinesAsset>(InParent, InClass, InName, Flags);
}

bool UPlayVoiceLinesAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}

uint32 UPlayVoiceLinesAssetFactory::GetMenuCategories() const
{
	return FPlayVoicePluginEditorModule::PlayVoiceAssetCategoryBit;
}
