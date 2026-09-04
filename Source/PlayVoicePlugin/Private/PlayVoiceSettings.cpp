// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceSettings.h"

UPlayVoiceSettings::UPlayVoiceSettings()
	: ServiceUrl(TEXT("http://127.0.0.1:1983"))
	, PythonScriptPath(TEXT(""))
	, PythonExecutable(TEXT(""))
	, RequirementsFilePath(TEXT("Resources/OpenVoiceService/requirements.txt"))
	, ExtraPipArgs(TEXT("--prefer-binary --no-warn-script-location"))
	, RequestTimeout(300.0f)
	, bEnableOnTheFlySynthesis(false)
	, DefaultSampleRate(48000)
	, bImproveOutputQuality(true)
{
}
