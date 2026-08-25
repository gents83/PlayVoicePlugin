// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoicePluginEditorModule.h"
#include "PropertyEditorModule.h"
#include "CharacterVoiceAsset.h"
#include "CharacterVoiceAssetCustomization.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceSettingsCustomization.h"

#define LOCTEXT_NAMESPACE "FPlayVoicePluginEditorModule"

void FPlayVoicePluginEditorModule::StartupModule()
{
	RegisterCustomizations();
}

void FPlayVoicePluginEditorModule::ShutdownModule()
{
	UnregisterCustomizations();
}

void FPlayVoicePluginEditorModule::RegisterCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UCharacterVoiceAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCharacterVoiceAssetCustomization::MakeInstance)
	);
	PropertyModule.RegisterCustomClassLayout(
		UPlayVoiceSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FPlayVoiceSettingsCustomization::MakeInstance)
	);
}

void FPlayVoicePluginEditorModule::UnregisterCustomizations()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UCharacterVoiceAsset::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UPlayVoiceSettings::StaticClass()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPlayVoicePluginEditorModule, PlayVoicePluginEditor)
