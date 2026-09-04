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

	virtual FName GetContainerName() const override
	{
		return TEXT("Project");
	}

	virtual FName GetCategoryName() const override
	{
		return TEXT("PlayVoice");
	}

	UFUNCTION(BlueprintPure, Category = "PlayVoice Settings")
	static const UPlayVoiceSettings* Get()
	{
		return GetDefault<UPlayVoiceSettings>();
	}

	UPROPERTY(EditAnywhere, Config, Category = "Service Setup", meta = (DisplayName = "Service URL"))
	FString ServiceUrl;

	UPROPERTY(EditAnywhere, Config, Category = "Service Setup", meta = (DisplayName = "Python Script Path"))
	FString PythonScriptPath;

	UPROPERTY(EditAnywhere, Config, Category = "Requirements Setup", meta = (DisplayName = "Python Executable Path (optional; auto-detect Python 3.10)"))
	FString PythonExecutable;

	UPROPERTY(EditAnywhere, Config, Category = "Requirements Setup", meta = (DisplayName = "Requirements File Path"))
	FString RequirementsFilePath;

	UPROPERTY(EditAnywhere, Config, Category = "Requirements Setup", meta = (DisplayName = "Extra Pip Arguments"))
	FString ExtraPipArgs;

	UPROPERTY(EditAnywhere, Config, Category = "Service Setup", meta = (DisplayName = "Request Timeout (Seconds)", ClampMin = "1.0", ClampMax = "3600.0"))
	float RequestTimeout;

	UPROPERTY(EditAnywhere, Config, Category = "Voice Playback", meta = (DisplayName = "Enable On-The-Fly Synthesis", DeprecatedProperty, EditCondition = "false"))
	bool bEnableOnTheFlySynthesis;

	UPROPERTY(EditAnywhere, Config, Category = "Audio Settings", meta = (DisplayName = "Default Sample Rate", ClampMin = "8000", ClampMax = "192000"))
	int32 DefaultSampleRate;

	UPROPERTY(EditAnywhere, Config, Category = "Audio Settings", meta = (DisplayName = "Improve Output Quality"))
	bool bImproveOutputQuality;
};
