// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "Developer/AssetTools/Public/AssetTypeCategories.h"
#include "IAssetTypeActions.h"

class FPlayVoicePluginEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static bool StartOpenVoiceService(FProcHandle* OutProcHandle = nullptr);
	static bool ResolvePython310(FString& OutPythonExecutable, FString& OutError);
	static bool ValidatePython310(const FString& PythonExecutable, FString& OutResolvedExecutable, FString& OutError);
	static FString GetPythonEnvironmentExecutable();
	static bool SavePythonExecutable(const FString& PythonExecutable);
	static EAssetTypeCategories::Type GetAssetCategoryBit() { return PlayVoiceAssetCategoryBit; }

	/** Helper function to resolve relative resource paths using Plugin directory first, then Project and Engine directories. */
	static FString ResolveResourcePath(const FString& RelativeOrAbsolutePath);

	static EAssetTypeCategories::Type PlayVoiceAssetCategoryBit;

private:
	void RegisterCustomizations();
	void UnregisterCustomizations();

	void RegisterAssetTypeActions();
	void UnregisterAssetTypeActions();

	TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;
	TSharedPtr<class FPlayVoiceGraphPinFactory> PlayVoicePinFactory;
};
