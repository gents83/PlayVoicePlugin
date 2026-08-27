// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoicePluginEditorModule.h"
#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "CharacterVoiceAsset.h"
#include "CharacterVoiceAssetCustomization.h"
#include "AssetTypeActions_CharacterVoiceAsset.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceSettingsCustomization.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "FPlayVoicePluginEditorModule"

EAssetTypeCategories::Type FPlayVoicePluginEditorModule::PlayVoiceAssetCategoryBit = EAssetTypeCategories::Misc;

void FPlayVoicePluginEditorModule::StartupModule()
{
	RegisterCustomizations();
	RegisterAssetTypeActions();

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	if (Settings && Settings->bAutoStartServiceOnEditorStartup)
	{
		StartOpenVoiceService(&AutoStartedServiceHandle);
	}
}

void FPlayVoicePluginEditorModule::ShutdownModule()
{
	UnregisterCustomizations();
	UnregisterAssetTypeActions();

	if (AutoStartedServiceHandle.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(AutoStartedServiceHandle))
		{
			FPlatformProcess::TerminateProc(AutoStartedServiceHandle, true);
		}
		FPlatformProcess::CloseProc(AutoStartedServiceHandle);
	}
}

static FProcHandle StaticServiceHandle;

bool FPlayVoicePluginEditorModule::StartOpenVoiceService(FProcHandle* OutProcHandle)
{
	if (StaticServiceHandle.IsValid() && FPlatformProcess::IsProcRunning(StaticServiceHandle))
	{
		if (OutProcHandle)
		{
			*OutProcHandle = StaticServiceHandle;
		}
		return true;
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString PythonExec = Settings && !Settings->PythonExecutable.IsEmpty() ? Settings->PythonExecutable : TEXT("python");

	FString ScriptPath = Settings ? Settings->PythonScriptPath : TEXT("");
	if (ScriptPath.IsEmpty())
	{
		ScriptPath = TEXT("Resources/OpenVoiceService/openvoice_service.py");
	}

	FString ResolvedScriptPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ScriptPath);
	if (!IFileManager::Get().FileExists(*ResolvedScriptPath))
	{
		ResolvedScriptPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir(), ScriptPath);
	}

	FString Host = TEXT("127.0.0.1");
	int32 Port = 1983;

	if (Settings && !Settings->ServiceUrl.IsEmpty())
	{
		FString Url = Settings->ServiceUrl;
		Url.RemoveFromStart(TEXT("http://"));
		Url.RemoveFromStart(TEXT("https://"));
		int32 ColonIndex = -1;
		if (Url.FindChar(':', ColonIndex))
		{
			Host = Url.Left(ColonIndex);
			FString PortStr = Url.RightChop(ColonIndex + 1);
			int32 SlashIndex = -1;
			if (PortStr.FindChar('/', SlashIndex))
			{
				PortStr = PortStr.Left(SlashIndex);
			}
			if (PortStr.IsNumeric())
			{
				Port = FCString::Atoi(*PortStr);
			}
		}
		else
		{
			int32 SlashIndex = -1;
			if (Url.FindChar('/', SlashIndex))
			{
				Host = Url.Left(SlashIndex);
			}
			else
			{
				Host = Url;
			}
		}
	}

	FString CmdArgs = FString::Printf(TEXT("\"%s\" --mode server --host %s --port %d"), *ResolvedScriptPath, *Host, Port);

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*PythonExec,
		*CmdArgs,
		true,  // bLaunchDetached
		false, // bLaunchHidden
		false, // bLaunchReallyHidden
		nullptr, // OutProcessID
		0,       // PriorityModifier
		nullptr, // OptionalWorkingDirectory
		nullptr  // PipeWriteChild
	);

	if (ProcHandle.IsValid())
	{
		StaticServiceHandle = ProcHandle;

		// Brief warm-up sleep to allow Python process to bind to host:port
		for (int32 Check = 0; Check < 6; ++Check)
		{
			if (FPlatformProcess::IsProcRunning(ProcHandle))
			{
				FPlatformProcess::Sleep(0.5f);
				break;
			}
			FPlatformProcess::Sleep(0.2f);
		}
	}

	if (OutProcHandle)
	{
		*OutProcHandle = ProcHandle;
	}

	return ProcHandle.IsValid();
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

void FPlayVoicePluginEditorModule::RegisterAssetTypeActions()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	PlayVoiceAssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("PlayVoice")), LOCTEXT("PlayVoiceCategory", "PlayVoice"));

	TSharedRef<IAssetTypeActions> Action = MakeShared<FAssetTypeActions_CharacterVoiceAsset>(PlayVoiceAssetCategoryBit);
	AssetTools.RegisterAssetTypeActions(Action);
	RegisteredAssetTypeActions.Add(Action);
}

void FPlayVoicePluginEditorModule::UnregisterAssetTypeActions()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto& Action : RegisteredAssetTypeActions)
		{
			if (Action.IsValid())
			{
				AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
			}
		}
	}
	RegisteredAssetTypeActions.Empty();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPlayVoicePluginEditorModule, PlayVoicePluginEditor)
