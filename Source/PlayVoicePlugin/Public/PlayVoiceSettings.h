// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PlayVoiceSettings.generated.h"

UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "PlayVoice Settings"))
class PLAYVOICEPLUGIN_API UPlayVoiceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPlayVoiceSettings();

	/** URL endpoint of the local or remote OpenVoice TTS service (e.g. http://127.0.0.1:8000) */
	UPROPERTY(EditAnywhere, Config, Category = "Service Setup", meta = (DisplayName = "Service URL"))
	FString ServiceUrl;

	/** Directory path to python executable or openvoice script */
	UPROPERTY(EditAnywhere, Config, Category = "Service Setup", meta = (DisplayName = "Python Script Path"))
	FString PythonScriptPath;

	/** Timeout in seconds for HTTP generation requests */
	UPROPERTY(EditAnywhere, Config, Category = "Service Setup", meta = (DisplayName = "Request Timeout (Seconds)"))
	float RequestTimeout;

	/** Automatically precache registered voice lines on game startup */
	UPROPERTY(EditAnywhere, Config, Category = "Zero Latency Settings", meta = (DisplayName = "Auto Precache On Startup"))
	bool bAutoPrecacheOnStartup;

	/** Output audio sample rate in Hz (default: 24000 for OpenVoice) */
	UPROPERTY(EditAnywhere, Config, Category = "Audio Settings", meta = (DisplayName = "Default Sample Rate"))
	int32 DefaultSampleRate;
};
