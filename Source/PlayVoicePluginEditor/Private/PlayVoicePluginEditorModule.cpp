// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoicePluginEditorModule.h"
#include "PropertyEditorModule.h"
#include "CharacterVoiceAsset.h"
#include "CharacterVoiceAssetCustomization.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceSettingsCustomization.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "FPlayVoicePluginEditorModule"

void FPlayVoicePluginEditorModule::StartupModule()
{
	RegisterCustomizations();

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	if (Settings && Settings->bAutoStartServiceOnEditorStartup)
	{
		StartOpenVoiceService(&AutoStartedServiceHandle);
	}
}

void FPlayVoicePluginEditorModule::ShutdownModule()
{
	UnregisterCustomizations();

	if (AutoStartedServiceHandle.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(AutoStartedServiceHandle))
		{
			FPlatformProcess::TerminateProc(AutoStartedServiceHandle, true);
		}
		FPlatformProcess::CloseProc(AutoStartedServiceHandle);
	}
}

bool FPlayVoicePluginEditorModule::StartOpenVoiceService(FProcHandle* OutProcHandle)
{
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPlayVoicePluginEditorModule, PlayVoicePluginEditor)
