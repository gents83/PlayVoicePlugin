// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceSettings.h"

UPlayVoiceSettings::UPlayVoiceSettings()
	: ServiceUrl(TEXT("http://127.0.0.1:8000"))
	, PythonScriptPath(TEXT(""))
	, RequestTimeout(30.0f)
	, bAutoPrecacheOnStartup(true)
	, DefaultSampleRate(24000)
{
}
