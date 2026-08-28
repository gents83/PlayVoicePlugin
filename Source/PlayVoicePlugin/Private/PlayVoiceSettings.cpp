// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceSettings.h"

UPlayVoiceSettings::UPlayVoiceSettings()
	: ServiceUrl(TEXT("http://127.0.0.1:1983"))
	, PythonScriptPath(TEXT(""))
	, bAutoStartServiceOnEditorStartup(true)
	, PythonExecutable(TEXT("python"))
	, RequirementsFilePath(TEXT("Resources/OpenVoiceService/requirements.txt"))
	, TargetInstallDir(TEXT(""))
	, ExtraPipArgs(TEXT("--prefer-binary --no-warn-script-location"))
	, RequestTimeout(300.0f)
	, bAutoPrecacheOnStartup(true)
	, DefaultSampleRate(24000)
{
}
