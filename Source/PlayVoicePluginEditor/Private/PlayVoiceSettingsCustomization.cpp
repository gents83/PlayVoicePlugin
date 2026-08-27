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
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
		return FReply::Handled();
	}

	FString CheckScriptPath = FPlayVoicePluginEditorModule::ResolveResourcePath(TEXT("Resources/OpenVoiceService/check_requirements.py"));
	if (!IFileManager::Get().FileExists(*CheckScriptPath))
	{
		FString ErrorMsg = FString::Printf(TEXT("Requirements check script not found at '%s'."), *CheckScriptPath);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
		return FReply::Handled();
	}

	FString Args = FString::Printf(TEXT("\"%s\" \"%s\""), *CheckScriptPath, *ResolvedReqFile);

	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	bool bSuccess = FPlatformProcess::ExecProcess(*PythonExec, *Args, &ReturnCode, &StdOut, &StdErr);

	if (bSuccess && ReturnCode == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("All requirements are successfully installed and verified!")));
	}
	else
	{
		FString ErrorMsg = FString::Printf(TEXT("Requirements check failed or missing dependencies found.\nDetails: %s\n%s"), *StdOut, *StdErr);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
	}

	return FReply::Handled();
}

FReply FPlayVoiceSettingsCustomization::OnStartServiceClicked()
{
	FProcHandle ProcHandle;
	bool bStarted = FPlayVoicePluginEditorModule::StartOpenVoiceService(&ProcHandle);

	if (bStarted)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("OpenVoice REST Service launched successfully!")));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Failed to launch OpenVoice REST Service. Please verify Python executable and script settings.")));
	}

	return FReply::Handled();
}

FReply FPlayVoiceSettingsCustomization::OnLaunchSetupClicked()
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString PythonExec = Settings && !Settings->PythonExecutable.IsEmpty() ? Settings->PythonExecutable : TEXT("python");
	FString ReqFile = Settings && !Settings->RequirementsFilePath.IsEmpty() ? Settings->RequirementsFilePath : TEXT("Resources/OpenVoiceService/requirements.txt");
	FString TargetDir = Settings ? Settings->TargetInstallDir : TEXT("");
	FString ExtraArgs = Settings ? Settings->ExtraPipArgs : TEXT("");

	FString ResolvedReqFile = FPlayVoicePluginEditorModule::ResolveResourcePath(ReqFile);

	FString CmdArgs = FString::Printf(TEXT("-m pip install -r \"%s\""), *ResolvedReqFile);
	if (!TargetDir.IsEmpty())
	{
		CmdArgs += FString::Printf(TEXT(" --target \"%s\""), *TargetDir);
	}
	if (!ExtraArgs.IsEmpty())
	{
		CmdArgs += FString::Printf(TEXT(" %s"), *ExtraArgs);
	}

	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	bool bSuccess = FPlatformProcess::ExecProcess(*PythonExec, *CmdArgs, &ReturnCode, &StdOut, &StdErr);

	if (bSuccess && ReturnCode == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Setup completed successfully! All requirements were installed.")));
	}
	else
	{
		FString ErrorMsg = FString::Printf(TEXT("Setup process failed (Exit Code: %d).\nOutput: %s\nError: %s"), ReturnCode, *StdOut, *StdErr);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMsg));
	}

	return FReply::Handled();
}
