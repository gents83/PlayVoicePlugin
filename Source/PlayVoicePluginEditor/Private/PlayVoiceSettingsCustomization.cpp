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
#include "Async/Async.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayVoiceSettings, Log, All);

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
}

FReply FPlayVoiceSettingsCustomization::OnCheckRequirementsClicked()
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString PythonExec = Settings && !Settings->PythonExecutable.IsEmpty() ? Settings->PythonExecutable : TEXT("python");
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
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString PythonExec = Settings && !Settings->PythonExecutable.IsEmpty() ? Settings->PythonExecutable : TEXT("python");
	FString ReqFile = Settings && !Settings->RequirementsFilePath.IsEmpty() ? Settings->RequirementsFilePath : TEXT("Resources/OpenVoiceService/requirements.txt");
	FString ExtraArgs = Settings ? Settings->ExtraPipArgs : TEXT("");

	FString ResolvedReqFile = FPlayVoicePluginEditorModule::ResolveResourcePath(ReqFile);

	FString UpgradePipArgs = TEXT("-m pip install --upgrade pip");

	FString BaseExtraArgs = ExtraArgs.IsEmpty() ? TEXT("--prefer-binary --no-warn-script-location") : ExtraArgs;

	FString CmdArgs = FString::Printf(TEXT("-m pip install %s -r \"%s\""), *BaseExtraArgs, *ResolvedReqFile);
	FString GitCmdArgs = FString::Printf(TEXT("-m pip install %s --no-deps git+https://github.com/myshell-ai/OpenVoice.git git+https://github.com/myshell-ai/MeloTTS.git"), *BaseExtraArgs);

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

	UE_LOG(LogPlayVoiceSettings, Log, TEXT("Running setup installation: %s %s"), *PythonExec, *CmdArgs);

	Async(EAsyncExecution::Thread, [PythonExec, UpgradePipArgs, CmdArgs, GitCmdArgs, NotificationItem]()
	{
		int32 UpCode = -1;
		FString UpOut, UpErr;
		FPlatformProcess::ExecProcess(*PythonExec, *UpgradePipArgs, &UpCode, &UpOut, &UpErr);

		int32 ReturnCode = -1;
		FString StdOut;
		FString StdErr;
		bool bSuccess = FPlatformProcess::ExecProcess(*PythonExec, *CmdArgs, &ReturnCode, &StdOut, &StdErr);

		if (bSuccess && ReturnCode == 0)
		{
			int32 GitReturnCode = -1;
			FString GitOut;
			FString GitErr;
			bool bGitSuccess = FPlatformProcess::ExecProcess(*PythonExec, *GitCmdArgs, &GitReturnCode, &GitOut, &GitErr);
			StdOut += TEXT("\n") + GitOut;
			StdErr += TEXT("\n") + GitErr;
			if (!bGitSuccess || GitReturnCode != 0)
			{
				bSuccess = false;
				ReturnCode = GitReturnCode != 0 ? GitReturnCode : -1;
			}
		}

		Async(EAsyncExecution::TaskGraphMainThread, [bSuccess, ReturnCode, StdOut, StdErr, NotificationItem]()
		{
			if (bSuccess && ReturnCode == 0)
			{
				UE_LOG(LogPlayVoiceSettings, Log, TEXT("Setup completed successfully:\n%s"), *StdOut);
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
