// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceSettingsCustomization.h"
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

	FString ResolvedReqFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ReqFile);
	if (!IFileManager::Get().FileExists(ResolvedReqFile))
	{
		ResolvedReqFile = FPaths::ConvertRelativePathToFull(FPaths::EngineDir(), ReqFile);
	}

	FString Args;
	if (IFileManager::Get().FileExists(ResolvedReqFile))
	{
		Args = FString::Printf(
			TEXT("-c \"import sys; ")
			TEXT("try:\n")
			TEXT("    import importlib.metadata as meta\n")
			TEXT("except ImportError:\n")
			TEXT("    import importlib_metadata as meta\n")
			TEXT("lines = [l.strip() for l in open('%s').readlines() if l.strip() and not l.strip().startswith('#')]; ")
			TEXT("missing = []; ")
			TEXT("installed = {dist.metadata['Name'].lower() for dist in meta.distributions() if dist.metadata and 'Name' in dist.metadata}; ")
			TEXT("for req in lines:\n")
			TEXT("    pkg_name = req.split('>=')[0].split('<=')[0].split('==')[0].split(';')[0].strip().lower(); ")
			TEXT("    if pkg_name and pkg_name not in installed:\n")
			TEXT("        missing.append(pkg_name)\n")
			TEXT("sys.exit(0 if not missing else 1)\""),
			*ResolvedReqFile
		);
	}
	else
	{
		Args = TEXT("-m pip check");
	}

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

FReply FPlayVoiceSettingsCustomization::OnLaunchSetupClicked()
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString PythonExec = Settings && !Settings->PythonExecutable.IsEmpty() ? Settings->PythonExecutable : TEXT("python");
	FString ReqFile = Settings && !Settings->RequirementsFilePath.IsEmpty() ? Settings->RequirementsFilePath : TEXT("Resources/OpenVoiceService/requirements.txt");
	FString TargetDir = Settings ? Settings->TargetInstallDir : TEXT("");
	FString ExtraArgs = Settings ? Settings->ExtraPipArgs : TEXT("");

	FString ResolvedReqFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ReqFile);
	if (!IFileManager::Get().FileExists(ResolvedReqFile))
	{
		ResolvedReqFile = FPaths::ConvertRelativePathToFull(FPaths::EngineDir(), ReqFile);
	}

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
