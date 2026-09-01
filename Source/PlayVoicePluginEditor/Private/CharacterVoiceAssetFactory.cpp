// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAssetFactory.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoicePluginEditorModule.h"

UCharacterVoiceAssetFactory::UCharacterVoiceAssetFactory()
{
	SupportedClass = UCharacterVoiceAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UCharacterVoiceAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UCharacterVoiceAsset>(InParent, InClass, InName, Flags);
}

bool UCharacterVoiceAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}

uint32 UCharacterVoiceAssetFactory::GetMenuCategories() const
{
	return FPlayVoicePluginEditorModule::PlayVoiceAssetCategoryBit;
}
