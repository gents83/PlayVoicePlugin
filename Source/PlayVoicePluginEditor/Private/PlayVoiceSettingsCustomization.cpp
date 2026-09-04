// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceSettingsCustomization.h"
#include "PlayVoicePluginEditorModule.h"
#include "PlayVoiceSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Ticker.h"
#include "Async/Async.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayVoiceSettings, Log, All);

static bool HasUnsafePipArguments(const FString& Arguments)
{
	return Arguments.Contains(TEXT("\n")) || Arguments.Contains(TEXT("\r")) || Arguments.Contains(TEXT("&"))
		|| Arguments.Contains(TEXT("|")) || Arguments.Contains(TEXT(";")) || Arguments.Contains(TEXT("<"))
		|| Arguments.Contains(TEXT(">")) || Arguments.Contains(TEXT("`"));
}

struct FPlayVoiceSetupProgressState
{
	FCriticalSection Mutex;
	FString Stage;
	FString Output;
	TSharedPtr<SNotificationItem> NotificationItem;
	FProcHandle ActiveProcess;
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	FThreadSafeCounter Cancelled;
	FThreadSafeCounter Finished;
	double StartTime = 0.0;
};

static FCriticalSection GPlayVoiceSetupMutex;
static TSharedPtr<FPlayVoiceSetupProgressState> GPlayVoiceSetupState;

static void SetSetupStage(const TSharedRef<FPlayVoiceSetupProgressState>& State, const TCHAR* Stage)
{
	FScopeLock Lock(&State->Mutex);
	State->Stage = Stage;
}

static bool RunSetupProcess(const TSharedRef<FPlayVoiceSetupProgressState>& State, const FString& Executable, const FString& Arguments, int32& OutReturnCode)
{
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("Could not create setup process pipes for '%s'."), *Executable);
		return false;
	}

	FProcHandle Process = FPlatformProcess::CreateProc(*Executable, *Arguments, true, true, true, nullptr, 0, nullptr, WritePipe);
	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("Could not launch setup process '%s'."), *Executable);
		return false;
	}

	{
		FScopeLock Lock(&State->Mutex);
		State->ActiveProcess = Process;
		State->ReadPipe = ReadPipe;
		State->WritePipe = WritePipe;
	}

	while (FPlatformProcess::IsProcRunning(Process))
	{
		const FString OutputChunk = FPlatformProcess::ReadPipe(ReadPipe);
		if (!OutputChunk.IsEmpty())
		{
			FScopeLock Lock(&State->Mutex);
			State->Output += OutputChunk;
			UE_LOG(LogPlayVoiceSettings, Verbose, TEXT("Setup output: %s"), *OutputChunk.TrimStartAndEnd());
		}

		if (State->Cancelled.GetValue() > 0)
		{
			UE_LOG(LogPlayVoiceSettings, Warning, TEXT("Cancelling active PlayVoice setup process."));
			FPlatformProcess::TerminateProc(Process, true);
			break;
		}
		FPlatformProcess::Sleep(0.2f);
	}

	const FString RemainingOutput = FPlatformProcess::ReadPipe(ReadPipe);
	if (!RemainingOutput.IsEmpty())
	{
		FScopeLock Lock(&State->Mutex);
		State->Output += RemainingOutput;
	}
	FPlatformProcess::GetProcReturnCode(Process, &OutReturnCode);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	{
		FScopeLock Lock(&State->Mutex);
		State->ActiveProcess.Reset();
		State->ReadPipe = nullptr;
		State->WritePipe = nullptr;
	}
	return State->Cancelled.GetValue() == 0 && OutReturnCode == 0;
}

TSharedRef<IDetailCustomization> FPlayVoiceSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FPlayVoiceSettingsCustomization());
}

void FPlayVoiceSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& ServiceCategory = DetailBuilder.EditCategory("Service Setup", FText::FromString("Service Setup"));

	ServiceCategory.AddCustomRow(FText::FromString("Start OpenVoice Service"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Service Control"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Start OpenVoice Service"))
		.ToolTipText(FText::FromString("Launch the OpenVoice REST service backend process."))
		.OnClicked(this, &FPlayVoiceSettingsCustomization::OnStartServiceClicked)
	];

	IDetailCategoryBuilder& RequirementsCategory = DetailBuilder.EditCategory("Requirements Setup", FText::FromString("Requirements Setup"));

	RequirementsCategory.AddCustomRow(FText::FromString("Check Requirements"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Verification"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Check Requirements Status"))
		.ToolTipText(FText::FromString("Check if required Python packages specified in requirements.txt are installed."))
		.OnClicked(this, &FPlayVoiceSettingsCustomization::OnCheckRequirementsClicked)
	];

	RequirementsCategory.AddCustomRow(FText::FromString("Install Python 3.10"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Python Runtime"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Install Python 3.10"))
		.ToolTipText(FText::FromString("Download the official Python 3.10.11 Windows installer, run it, then run requirements setup."))
		.OnClicked(this, &FPlayVoiceSettingsCustomization::OnInstallPythonClicked)
	];

	RequirementsCategory.AddCustomRow(FText::FromString("Launch Setup"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Installation"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Launch Setup / Install Requirements"))
		.ToolTipText(FText::FromString("Launch Python pip installation to install or update requirements."))
		.OnClicked(this, &FPlayVoiceSettingsCustomization::OnLaunchSetupClicked)
	];

	RequirementsCategory.AddCustomRow(FText::FromString("Cancel Setup"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Setup Control"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Cancel Setup"))
		.ToolTipText(FText::FromString("Stop the active PlayVoice requirements installation."))
		.OnClicked(this, &FPlayVoiceSettingsCustomization::OnCancelSetupClicked)
	];
}

FReply FPlayVoiceSettingsCustomization::OnCancelSetupClicked()
{
	TSharedPtr<FPlayVoiceSetupProgressState> State;
	{
		FScopeLock Lock(&GPlayVoiceSetupMutex);
		State = GPlayVoiceSetupState;
	}
	if (State.IsValid() && State->Finished.GetValue() == 0)
	{
		State->Cancelled.Increment();
		UE_LOG(LogPlayVoiceSettings, Warning, TEXT("PlayVoice requirements setup cancellation requested."));
		FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Cancelling requirements setup..."));
		NotificationInfo.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}
	else
	{
		UE_LOG(LogPlayVoiceSettings, Log, TEXT("No active PlayVoice requirements setup to cancel."));
	}
	return FReply::Handled();
}

FReply FPlayVoiceSettingsCustomization::OnInstallPythonClicked()
{
	static const TCHAR* PythonInstallerUrl = TEXT("https://www.python.org/ftp/python/3.10.11/python-3.10.11-amd64.exe");
	FString LaunchError;
	FPlatformProcess::LaunchURL(PythonInstallerUrl, nullptr, &LaunchError);
	if (!LaunchError.IsEmpty())
	{
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("Could not open the official Python 3.10.11 installer URL: %s"), *LaunchError);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Could not open the Python 3.10.11 installer download. Download and run it manually, then run requirements setup again.\n\n%s"), *LaunchError)));
		return FReply::Handled();
	}

	UE_LOG(LogPlayVoiceSettings, Log, TEXT("Opened the official Python 3.10.11 installer download."));
	FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Download and install Python 3.10.11, then run setup again."));
	NotificationInfo.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	return FReply::Handled();
}

FReply FPlayVoiceSettingsCustomization::OnCheckRequirementsClicked()
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString PythonExec;
	FString PythonError;
	if (!FPlayVoicePluginEditorModule::ResolvePython310(PythonExec, PythonError))
	{
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("Requirements check: %s"), *PythonError);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(PythonError));
		return FReply::Handled();
	}
	FString ReqFile = Settings && !Settings->RequirementsFilePath.IsEmpty() ? Settings->RequirementsFilePath : TEXT("Resources/OpenVoiceService/requirements.txt");

	FString ResolvedReqFile = FPlayVoicePluginEditorModule::ResolveResourcePath(ReqFile);

	if (!IFileManager::Get().FileExists(*ResolvedReqFile))
	{
		FString ErrorMsg = FString::Printf(TEXT("Requirements check failed: Requirements file not found at '%s'."), *ResolvedReqFile);
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("%s"), *ErrorMsg);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
		return FReply::Handled();
	}

	FString CheckScriptPath = FPlayVoicePluginEditorModule::ResolveResourcePath(TEXT("Resources/OpenVoiceService/check_requirements.py"));
	if (!IFileManager::Get().FileExists(*CheckScriptPath))
	{
		FString ErrorMsg = FString::Printf(TEXT("Requirements check script not found at '%s'."), *CheckScriptPath);
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("%s"), *ErrorMsg);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
		return FReply::Handled();
	}

	FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Checking Python requirements..."));
	NotificationInfo.bFireAndForget = false;
	NotificationInfo.bUseThrobber = true;
	NotificationInfo.bUseLargeFont = false;
	NotificationInfo.bUseSuccessFailIcons = true;
	NotificationInfo.FadeOutDuration = 0.5f;

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	}

	UE_LOG(LogPlayVoiceSettings, Log, TEXT("Checking requirements with Python: %s, script: %s, file: %s"), *PythonExec, *CheckScriptPath, *ResolvedReqFile);

	FString Args = FString::Printf(TEXT("\"%s\" \"%s\""), *CheckScriptPath, *ResolvedReqFile);

	Async(EAsyncExecution::Thread, [PythonExec, Args, NotificationItem]()
	{
		int32 ReturnCode = -1;
		FString StdOut;
		FString StdErr;
		bool bSuccess = FPlatformProcess::ExecProcess(*PythonExec, *Args, &ReturnCode, &StdOut, &StdErr);

		Async(EAsyncExecution::TaskGraphMainThread, [bSuccess, ReturnCode, StdOut, StdErr, NotificationItem]()
		{
			if (bSuccess && ReturnCode == 0)
			{
				UE_LOG(LogPlayVoiceSettings, Log, TEXT("Requirements check successful:\n%s"), *StdOut);
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
					NotificationItem->SetText(FText::FromString("PlayVoice: All requirements are verified!"));
					NotificationItem->SetExpireDuration(3.0f);
					NotificationItem->ExpireAndFadeout();
				}
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("All requirements are successfully installed and verified!")));
			}
			else
			{
				UE_LOG(LogPlayVoiceSettings, Warning, TEXT("Requirements check failed (Code: %d).\nOutput: %s\nError: %s"), ReturnCode, *StdOut, *StdErr);
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
					NotificationItem->SetText(FText::FromString("PlayVoice: Requirements check failed."));
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
				FString ErrorMsg = FString::Printf(TEXT("Requirements check failed or missing dependencies found.\nDetails: %s\n%s"), *StdOut, *StdErr);
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
			}
		});
	});

	return FReply::Handled();
}

FReply FPlayVoiceSettingsCustomization::OnStartServiceClicked()
{
	FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Launching OpenVoice REST Service..."));
	NotificationInfo.bFireAndForget = false;
	NotificationInfo.bUseThrobber = true;
	NotificationInfo.bUseLargeFont = false;
	NotificationInfo.bUseSuccessFailIcons = true;
	NotificationInfo.FadeOutDuration = 0.5f;

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	}

	UE_LOG(LogPlayVoiceSettings, Log, TEXT("Initiating OpenVoice REST Service startup..."));

	Async(EAsyncExecution::Thread, [NotificationItem]()
	{
		FProcHandle ProcHandle;
		bool bStarted = FPlayVoicePluginEditorModule::StartOpenVoiceService(&ProcHandle);

		Async(EAsyncExecution::TaskGraphMainThread, [bStarted, NotificationItem]()
		{
			if (bStarted)
			{
				UE_LOG(LogPlayVoiceSettings, Log, TEXT("OpenVoice REST Service process started successfully."));
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
					NotificationItem->SetText(FText::FromString("PlayVoice: Service launched successfully!"));
					NotificationItem->SetExpireDuration(3.0f);
					NotificationItem->ExpireAndFadeout();
				}
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("OpenVoice REST Service launched successfully!")));
			}
			else
			{
				UE_LOG(LogPlayVoiceSettings, Error, TEXT("Failed to start OpenVoice REST Service."));
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
					NotificationItem->SetText(FText::FromString("PlayVoice: Failed to launch service."));
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Failed to launch OpenVoice REST Service. Please verify Python executable and script settings.")));
			}
		});
	});

	return FReply::Handled();
}

FReply FPlayVoiceSettingsCustomization::OnLaunchSetupClicked()
{
	{
		FScopeLock Lock(&GPlayVoiceSetupMutex);
		if (GPlayVoiceSetupState.IsValid() && GPlayVoiceSetupState->Finished.GetValue() == 0)
		{
			UE_LOG(LogPlayVoiceSettings, Warning, TEXT("Requirements setup is already running."));
			return FReply::Handled();
		}
	}

	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BasePython;
	FString PythonError;
	if (!FPlayVoicePluginEditorModule::ResolvePython310(BasePython, PythonError))
	{
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("Requirements setup: %s"), *PythonError);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(PythonError));
		return FReply::Handled();
	}
	FString ReqFile = Settings && !Settings->RequirementsFilePath.IsEmpty() ? Settings->RequirementsFilePath : TEXT("Resources/OpenVoiceService/requirements.txt");
	FString ExtraArgs = Settings ? Settings->ExtraPipArgs : TEXT("");
	if (HasUnsafePipArguments(ExtraArgs))
	{
		const FString ErrorMessage = TEXT("Extra Pip Arguments contain shell control characters and were rejected.");
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("%s"), *ErrorMessage);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage));
		return FReply::Handled();
	}

	FString ResolvedReqFile = FPlayVoicePluginEditorModule::ResolveResourcePath(ReqFile);

	FString CheckScriptPath = FPlayVoicePluginEditorModule::ResolveResourcePath(TEXT("Resources/OpenVoiceService/check_requirements.py"));
	if (!IFileManager::Get().FileExists(*ResolvedReqFile) || !IFileManager::Get().FileExists(*CheckScriptPath))
	{
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("Setup prerequisites are missing. Requirements='%s', preflight='%s'."), *ResolvedReqFile, *CheckScriptPath);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("PlayVoice setup files are missing. Check the plugin installation.")));
		return FReply::Handled();
	}

	FString PreflightArgs = FString::Printf(TEXT("\"%s\" --preflight-only \"%s\""), *CheckScriptPath, *ResolvedReqFile);
	FString UpgradePipArgs = TEXT("-m pip install --upgrade pip");

	FString BaseExtraArgs = ExtraArgs.IsEmpty() ? TEXT("--prefer-binary --no-warn-script-location") : ExtraArgs;
	FString CmdArgs = FString::Printf(TEXT("-m pip install %s -r \"%s\""), *BaseExtraArgs, *ResolvedReqFile);
	const FString VenvPython = FPlayVoicePluginEditorModule::GetPythonEnvironmentExecutable();
	if (VenvPython.IsEmpty())
	{
		const FString ErrorMessage = TEXT("Could not resolve the PlayVoicePlugin service directory for the Python virtual environment.");
		UE_LOG(LogPlayVoiceSettings, Error, TEXT("%s"), *ErrorMessage);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage));
		return FReply::Handled();
	}
	const FString VenvDirectory = FPaths::GetPath(FPaths::GetPath(VenvPython));
	const FString VenvCreateArgs = FString::Printf(TEXT("-m venv \"%s\""), *VenvDirectory);
	const FString GitInstallArgs = TEXT("-m pip install --no-deps git+https://github.com/myshell-ai/OpenVoice.git@74a1d147b17a8c3092dd5430504bd83ef6c7eb23 git+https://github.com/myshell-ai/MeloTTS.git@209145371cff8fc3bd60d7be902ea69cbdb7965a");
	const FString FinalCheckArgs = FString::Printf(TEXT("\"%s\" \"%s\""), *CheckScriptPath, *ResolvedReqFile);

	FNotificationInfo NotificationInfo(FText::FromString("PlayVoice: Installing Python requirements via pip..."));
	NotificationInfo.bFireAndForget = false;
	NotificationInfo.bUseThrobber = true;
	NotificationInfo.bUseLargeFont = false;
	NotificationInfo.bUseSuccessFailIcons = true;
	NotificationInfo.FadeOutDuration = 0.5f;

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	}

	TSharedRef<FPlayVoiceSetupProgressState> SetupState = MakeShared<FPlayVoiceSetupProgressState>();
	SetupState->NotificationItem = NotificationItem;
	SetupState->StartTime = FPlatformTime::Seconds();
	SetSetupStage(SetupState, TEXT("Preparing Python environment"));
	{
		FScopeLock Lock(&GPlayVoiceSetupMutex);
		GPlayVoiceSetupState = SetupState;
	}
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([SetupState](float DeltaTime)
	{
		if (SetupState->Finished.GetValue() > 0)
		{
			return false;
		}
		FString Stage;
		{
			FScopeLock Lock(&SetupState->Mutex);
			Stage = SetupState->Stage;
		}
		const int32 ElapsedSeconds = FMath::Max(0, FMath::FloorToInt(static_cast<float>(FPlatformTime::Seconds() - SetupState->StartTime)));
		if (SetupState->NotificationItem.IsValid())
		{
			SetupState->NotificationItem->SetText(FText::FromString(FString::Printf(TEXT("PlayVoice: %s (%02d:%02d elapsed)"), *Stage, ElapsedSeconds / 60, ElapsedSeconds % 60)));
		}
		return true;
	}), 1.0f);

	UE_LOG(LogPlayVoiceSettings, Log, TEXT("Running setup installation from base interpreter '%s': %s"), *BasePython, *CmdArgs);

	Async(EAsyncExecution::Thread, [BasePython, VenvPython, VenvCreateArgs, PreflightArgs, UpgradePipArgs, CmdArgs, GitInstallArgs, FinalCheckArgs, SetupState, NotificationItem]()
	{
		FString PythonExec = VenvPython;
		int32 ReturnCode = -1;
		FString StdOut;
		FString StdErr;
		bool bSuccess = true;
		auto RunStage = [&](const TCHAR* Stage, const FString& Executable, const FString& Arguments)
		{
			SetSetupStage(SetupState, Stage);
			const bool bStageSuccess = RunSetupProcess(SetupState, Executable, Arguments, ReturnCode);
			{
				FScopeLock Lock(&SetupState->Mutex);
				StdOut = SetupState->Output;
			}
			return bStageSuccess;
		};

		if (!IFileManager::Get().FileExists(*VenvPython))
		{
			bSuccess = RunStage(TEXT("Creating Python virtual environment"), BasePython, VenvCreateArgs);
		}

		FString VerifiedPython;
		FString ValidationError;
		if (bSuccess)
		{
			SetSetupStage(SetupState, TEXT("Verifying Python 3.10 interpreter"));
			bSuccess = FPlayVoicePluginEditorModule::ValidatePython310(VenvPython, VerifiedPython, ValidationError);
			if (!bSuccess)
			{
				StdErr += TEXT("Python environment validation failed: ") + ValidationError;
				ReturnCode = 1;
			}
			else
			{
				PythonExec = VerifiedPython;
			}
		}

		if (bSuccess)
		{
			bSuccess = RunStage(TEXT("Running Python compatibility preflight"), PythonExec, PreflightArgs);
		}
		if (bSuccess && ReturnCode == 0)
		{
			bSuccess = RunStage(TEXT("Upgrading pip"), PythonExec, UpgradePipArgs);
		}
		if (bSuccess && ReturnCode == 0)
		{
			bSuccess = RunStage(TEXT("Installing Python requirements"), PythonExec, CmdArgs);
		}
		if (bSuccess && ReturnCode == 0)
		{
			bSuccess = RunStage(TEXT("Installing OpenVoice and MeloTTS"), PythonExec, GitInstallArgs);
		}
		if (bSuccess && ReturnCode == 0)
		{
			bSuccess = RunStage(TEXT("Verifying installed requirements"), PythonExec, FinalCheckArgs);
		}

		Async(EAsyncExecution::TaskGraphMainThread, [bSuccess, ReturnCode, StdOut, StdErr, PythonExec, SetupState, NotificationItem]()
		{
			SetupState->Finished.Increment();
			{
				FScopeLock Lock(&GPlayVoiceSetupMutex);
				if (GPlayVoiceSetupState == SetupState)
				{
					GPlayVoiceSetupState.Reset();
				}
			}
			if (bSuccess && ReturnCode == 0)
			{
				if (!FPlayVoicePluginEditorModule::SavePythonExecutable(PythonExec))
				{
					UE_LOG(LogPlayVoiceSettings, Error, TEXT("Setup succeeded but could not persist the verified Python executable '%s'."), *PythonExec);
					if (NotificationItem.IsValid())
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
						NotificationItem->SetText(FText::FromString("PlayVoice: Could not save Python executable setting."));
						NotificationItem->ExpireAndFadeout();
					}
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Requirements installed, but the verified Python executable could not be saved. Configure it manually before starting the service.")));
					return;
				}
				UE_LOG(LogPlayVoiceSettings, Log, TEXT("Setup completed successfully using '%s':\n%s"), *PythonExec, *StdOut);
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
					NotificationItem->SetText(FText::FromString("PlayVoice: Requirements installed successfully!"));
					NotificationItem->SetExpireDuration(3.0f);
					NotificationItem->ExpireAndFadeout();
				}
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Setup completed successfully! All requirements were installed.")));
			}
			else
			{
				UE_LOG(LogPlayVoiceSettings, Error, TEXT("Setup process failed (Exit Code: %d).\nOutput: %s\nError: %s"), ReturnCode, *StdOut, *StdErr);
				if (NotificationItem.IsValid())
				{
					NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
					NotificationItem->SetText(FText::FromString("PlayVoice: Setup process failed."));
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
				FString ErrorMsg = FString::Printf(TEXT("Setup process failed (Exit Code: %d).\nOutput: %s\nError: %s"), ReturnCode, *StdOut, *StdErr);
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
			}
		});
	});

	return FReply::Handled();
}
