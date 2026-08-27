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
#include "Misc/Base64.h"
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

	// Escape backslashes for python string literal on Windows
	FString EscapedReqFile = ResolvedReqFile.Replace(TEXT("\\"), TEXT("/"));

	FString PyScriptCode = FString::Printf(
		TEXT("import sys, base64\n")
		TEXT("try:\n")
		TEXT("    import importlib.metadata as meta\n")
		TEXT("except ImportError:\n")
		TEXT("    import importlib_metadata as meta\n")
		TEXT("with open('%s', 'r', encoding='utf-8') as f:\n")
		TEXT("    lines = [l.strip() for l in f if l.strip() and not l.strip().startswith('#')]\n")
		TEXT("installed = set()\n")
		TEXT("for dist in meta.distributions():\n")
		TEXT("    if dist.metadata:\n")
		TEXT("        name = dist.metadata.get('Name')\n")
		TEXT("        if name:\n")
		TEXT("            installed.add(name.lower().replace('-', '_'))\n")
		TEXT("missing = []\n")
		TEXT("for req in lines:\n")
		TEXT("    raw_pkg = req.split(';')[0].split('>=')[0].split('<=')[0].split('==')[0].split('~=')[0].split('!=')[0].strip().lower()\n")
		TEXT("    norm_pkg = raw_pkg.replace('-', '_')\n")
		TEXT("    if norm_pkg and norm_pkg not in installed:\n")
		TEXT("        try:\n")
		TEXT("            __import__(norm_pkg)\n")
		TEXT("        except Exception:\n")
		TEXT("            missing.append(raw_pkg)\n")
		TEXT("if missing:\n")
		TEXT("    print('Missing packages:', ', '.join(missing))\n")
		TEXT("    sys.exit(1)\n")
		TEXT("else:\n")
		TEXT("    print('All requirements satisfied')\n")
		TEXT("    sys.exit(0)\n"),
		*EscapedReqFile
	);

	FString EncodedScript = FBase64::Encode(PyScriptCode);
	FString Args = FString::Printf(TEXT("-c \"import base64; exec(base64.b64decode('%s').decode('utf-8'))\""), *EncodedScript);

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
