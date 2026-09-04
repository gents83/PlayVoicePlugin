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
#include "PlayVoiceLineEntryCustomization.h"
#include "PlayVoiceKeyGraphPin.h"
#include "EdGraphUtilities.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "Interfaces/IPluginManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "FPlayVoicePluginEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogPlayVoicePluginEditor, Log, All);

static FProcHandle StaticServiceHandle;
static bool bStaticServiceOwned = false;

EAssetTypeCategories::Type FPlayVoicePluginEditorModule::PlayVoiceAssetCategoryBit = EAssetTypeCategories::Misc;

void FPlayVoicePluginEditorModule::StartupModule()
{
	RegisterCustomizations();
	RegisterAssetTypeActions();

	PlayVoicePinFactory = MakeShared<FPlayVoiceGraphPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(PlayVoicePinFactory);

}

void FPlayVoicePluginEditorModule::ShutdownModule()
{
	if (PlayVoicePinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(PlayVoicePinFactory);
		PlayVoicePinFactory.Reset();
	}

	if (bStaticServiceOwned && StaticServiceHandle.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(StaticServiceHandle))
		{
			FPlatformProcess::TerminateProc(StaticServiceHandle, true);
		}
		FPlatformProcess::CloseProc(StaticServiceHandle);
		StaticServiceHandle.Reset();
		bStaticServiceOwned = false;
	}

	UnregisterCustomizations();
	UnregisterAssetTypeActions();

}

FString FPlayVoicePluginEditorModule::ResolveResourcePath(const FString& RelativeOrAbsolutePath)
{
	if (RelativeOrAbsolutePath.IsEmpty())
	{
		return FString();
	}

	if (!FPaths::IsRelative(RelativeOrAbsolutePath) && IFileManager::Get().FileExists(*RelativeOrAbsolutePath))
	{
		return FPaths::ConvertRelativePathToFull(RelativeOrAbsolutePath);
	}

	// 1. Check Plugin directory
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PlayVoicePlugin"));
	if (Plugin.IsValid())
	{
		FString PluginPath = FPaths::Combine(Plugin->GetBaseDir(), RelativeOrAbsolutePath);
		if (IFileManager::Get().FileExists(*PluginPath))
		{
			return FPaths::ConvertRelativePathToFull(PluginPath);
		}
	}

	// 2. Check Project directory
	FString ProjectPath = FPaths::Combine(FPaths::ProjectDir(), RelativeOrAbsolutePath);
	if (IFileManager::Get().FileExists(*ProjectPath))
	{
		return FPaths::ConvertRelativePathToFull(ProjectPath);
	}

	// 3. Check Engine directory
	FString EnginePath = FPaths::Combine(FPaths::EngineDir(), RelativeOrAbsolutePath);
	if (IFileManager::Get().FileExists(*EnginePath))
	{
		return FPaths::ConvertRelativePathToFull(EnginePath);
	}

	// Fallback to Plugin directory if plugin found, otherwise Project directory
	if (Plugin.IsValid())
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), RelativeOrAbsolutePath));
	}
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RelativeOrAbsolutePath);
}


static bool ProbePython310(const FString& PythonExecutable, FString& OutResolvedExecutable, FString& OutError, const FString& PrefixArguments = TEXT(""))
{
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	const FString ProbeArguments = PrefixArguments + TEXT(" -c \"import sys; print(sys.executable); print(sys.version_info.major); print(sys.version_info.minor)\"");
	if (!FPlatformProcess::ExecProcess(*PythonExecutable, *ProbeArguments, &ReturnCode, &StdOut, &StdErr) || ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("Could not run '%s'. %s"), *PythonExecutable, *StdErr);
		return false;
	}

	TArray<FString> Lines;
	StdOut.ParseIntoArrayLines(Lines);
	if (Lines.Num() < 3)
	{
		OutError = FString::Printf(TEXT("Python probe for '%s' returned incomplete version information."), *PythonExecutable);
		return false;
	}

	const int32 MajorVersion = FCString::Atoi(*Lines[Lines.Num() - 2]);
	const int32 MinorVersion = FCString::Atoi(*Lines[Lines.Num() - 1]);
	if (MajorVersion != 3 || MinorVersion != 10)
	{
		OutError = FString::Printf(TEXT("Python 3.10 is required, but '%s' reports %d.%d."), *PythonExecutable, MajorVersion, MinorVersion);
		return false;
	}

	OutResolvedExecutable = FPaths::ConvertRelativePathToFull(Lines[0].TrimStartAndEnd());
	if (!IFileManager::Get().FileExists(*OutResolvedExecutable))
	{
		OutError = FString::Printf(TEXT("Python probe returned a missing executable path '%s'."), *OutResolvedExecutable);
		return false;
	}
	return true;
}

bool FPlayVoicePluginEditorModule::ValidatePython310(const FString& PythonExecutable, FString& OutResolvedExecutable, FString& OutError)
{
	return ProbePython310(PythonExecutable, OutResolvedExecutable, OutError);
}

FString FPlayVoicePluginEditorModule::GetPythonEnvironmentExecutable()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PlayVoicePlugin"));
	if (!Plugin.IsValid())
	{
		return FString();
	}
#if PLATFORM_WINDOWS
	return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/OpenVoiceService/.venv/Scripts/python.exe"));
#else
	return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/OpenVoiceService/.venv/bin/python"));
#endif
}

bool FPlayVoicePluginEditorModule::ResolvePython310(FString& OutPythonExecutable, FString& OutError)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	const FString ConfiguredExecutable = Settings ? Settings->PythonExecutable.TrimStartAndEnd() : FString();
	const FString VenvExecutable = GetPythonEnvironmentExecutable();

	if (ConfiguredExecutable.IsEmpty() || ConfiguredExecutable.Equals(TEXT("python"), ESearchCase::IgnoreCase))
	{
		if (!VenvExecutable.IsEmpty() && IFileManager::Get().FileExists(*VenvExecutable)
			&& ProbePython310(VenvExecutable, OutPythonExecutable, OutError))
		{
			return true;
		}

#if PLATFORM_WINDOWS
		FString Launcher = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("py.exe"));
		if (!IFileManager::Get().FileExists(*Launcher))
		{
			Launcher = TEXT("py.exe");
		}
		if (ProbePython310(Launcher, OutPythonExecutable, OutError, TEXT("-3.10")))
		{
			return true;
		}
#else
		OutError = TEXT("Automatic Python 3.10 discovery is currently supported only through the configured executable on this platform.");
#endif
		OutError = TEXT("Python 3.10 was not found. Install Python 3.10.x or configure its executable path.");
		return false;
	}

	return ProbePython310(ConfiguredExecutable, OutPythonExecutable, OutError);
}

bool FPlayVoicePluginEditorModule::SavePythonExecutable(const FString& PythonExecutable)
{
	if (PythonExecutable.IsEmpty())
	{
		return false;
	}
	UPlayVoiceSettings* Settings = GetMutableDefault<UPlayVoiceSettings>();
	if (!Settings)
	{
		return false;
	}
	Settings->PythonExecutable = PythonExecutable;
	Settings->SaveConfig();
	return true;
}

static bool WaitForOpenVoiceHealth(const FString& BaseUrl, float TimeoutSeconds)
{
	const double StartTime = FPlatformTime::Seconds();
	while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
	{
		struct FHealthState
		{
			FThreadSafeCounter Completed;
			FThreadSafeCounter Healthy;
		};

		TSharedRef<FHealthState> State = MakeShared<FHealthState>();
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HealthRequest = FHttpModule::Get().CreateRequest();
		HealthRequest->SetURL(BaseUrl + TEXT("/health"));
		HealthRequest->SetVerb(TEXT("GET"));
		HealthRequest->SetTimeout(2.0f);
		HealthRequest->OnProcessRequestComplete().BindLambda([State](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				TSharedPtr<FJsonObject> HealthObject;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				if (FJsonSerializer::Deserialize(Reader, HealthObject) && HealthObject.IsValid()
					&& HealthObject->HasTypedField<EJson::Boolean>(TEXT("ready"))
					&& HealthObject->HasTypedField<EJson::String>(TEXT("service_version"))
					&& HealthObject->GetBoolField(TEXT("ready"))
					&& HealthObject->GetStringField(TEXT("service_version")) == TEXT("1.2.0"))
				{
					State->Healthy.Increment();
				}
			}
			State->Completed.Increment();
		});

		if (!HealthRequest->ProcessRequest())
		{
			return false;
		}

		while (State->Completed.GetValue() == 0 && FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
		{
			FPlatformProcess::Sleep(0.1f);
		}
		if (State->Completed.GetValue() == 0)
		{
			HealthRequest->CancelRequest();
			return false;
		}
		if (State->Healthy.GetValue() > 0)
		{
			return true;
		}
		FPlatformProcess::Sleep(0.1f);
	}
	return false;
}

bool FPlayVoicePluginEditorModule::StartOpenVoiceService(FProcHandle* OutProcHandle)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString PythonExec;
	FString PythonError;
	if (!ResolvePython310(PythonExec, PythonError))
	{
		UE_LOG(LogPlayVoicePluginEditor, Error, TEXT("StartOpenVoiceService: %s"), *PythonError);
		return false;
	}

	FString ScriptPath = Settings ? Settings->PythonScriptPath : TEXT("");
	if (ScriptPath.IsEmpty())
	{
		ScriptPath = TEXT("Resources/OpenVoiceService/openvoice_service.py");
	}

	FString ResolvedScriptPath = ResolveResourcePath(ScriptPath);

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

	if (Host.IsEmpty() || Port < 1 || Port > 65535 || Host.Contains(TEXT("\"")) || Host.Contains(TEXT("'")) || Host.Contains(TEXT(";")) || Host.Contains(TEXT("&")) || Host.Contains(TEXT("|")) || Host.Contains(TEXT(" ")))
	{
		UE_LOG(LogPlayVoicePluginEditor, Warning, TEXT("StartOpenVoiceService: Service URL contains an invalid host or port."));
		return false;
	}

	FString BaseUrl = Settings && !Settings->ServiceUrl.IsEmpty() ? Settings->ServiceUrl.TrimStartAndEnd() : TEXT("http://127.0.0.1:1983");
	BaseUrl.RemoveFromEnd(TEXT("/"));
	const bool bOwnedProcessRunning = bStaticServiceOwned && StaticServiceHandle.IsValid() && FPlatformProcess::IsProcRunning(StaticServiceHandle);
	const float HealthTimeout = bOwnedProcessRunning
		? (Settings && Settings->RequestTimeout > 0.0f ? Settings->RequestTimeout : 300.0f)
		: 5.0f;
	if (WaitForOpenVoiceHealth(BaseUrl, HealthTimeout))
	{
		if (OutProcHandle && bStaticServiceOwned)
		{
			*OutProcHandle = StaticServiceHandle;
		}
		UE_LOG(LogPlayVoicePluginEditor, Log, TEXT("StartOpenVoiceService: Compatible OpenVoice service is already healthy at '%s'."), *BaseUrl);
		return true;
	}
	if (bOwnedProcessRunning)
	{
		UE_LOG(LogPlayVoicePluginEditor, Warning, TEXT("StartOpenVoiceService: Owned service process is running but did not become healthy within the configured timeout."));
		return false;
	}

	FString CmdArgs = FString::Printf(TEXT("\"%s\" --mode server --host \"%s\" --port %d"), *ResolvedScriptPath, *Host, Port);
	FString WorkingDirectory = FPaths::ProjectDir();

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*PythonExec,
		*CmdArgs,
		true,  // bLaunchDetached
		false, // bLaunchHidden
		false, // bLaunchReallyHidden
		nullptr, // OutProcessID
		0,       // PriorityModifier
		*WorkingDirectory, // OptionalWorkingDirectory
		nullptr  // PipeWriteChild
	);

	if (!ProcHandle.IsValid())
	{
		UE_LOG(LogPlayVoicePluginEditor, Error, TEXT("StartOpenVoiceService: Failed to launch Python service process '%s'."), *PythonExec);
		return false;
	}

	StaticServiceHandle = ProcHandle;
	bStaticServiceOwned = true;
	const float StartupHealthTimeout = Settings && Settings->RequestTimeout > 0.0f ? Settings->RequestTimeout : 300.0f;
	if (!WaitForOpenVoiceHealth(BaseUrl, StartupHealthTimeout))
	{
		UE_LOG(LogPlayVoicePluginEditor, Error, TEXT("StartOpenVoiceService: Service process started but did not report a compatible healthy response."));
		if (FPlatformProcess::IsProcRunning(ProcHandle))
		{
			FPlatformProcess::TerminateProc(ProcHandle, true);
		}
		FPlatformProcess::CloseProc(ProcHandle);
		StaticServiceHandle.Reset();
		bStaticServiceOwned = false;
		return false;
	}

	if (OutProcHandle)
	{
		*OutProcHandle = ProcHandle;
	}
	return true;
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
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FPlayVoiceLineEntry::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPlayVoiceLineEntryCustomization::MakeInstance)
	);
}

void FPlayVoicePluginEditorModule::UnregisterCustomizations()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UCharacterVoiceAsset::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UPlayVoiceSettings::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FPlayVoiceLineEntry::StaticStruct()->GetFName());
	}
}

void FPlayVoicePluginEditorModule::RegisterAssetTypeActions()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	PlayVoiceAssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("PlayVoice")), LOCTEXT("PlayVoiceCategory", "PlayVoice"));

	TSharedRef<IAssetTypeActions> ActionVoice = MakeShared<FAssetTypeActions_CharacterVoiceAsset>(PlayVoiceAssetCategoryBit);
	AssetTools.RegisterAssetTypeActions(ActionVoice);
	RegisteredAssetTypeActions.Add(ActionVoice);
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
