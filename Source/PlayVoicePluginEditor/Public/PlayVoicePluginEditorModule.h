// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"

class FPlayVoicePluginEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static bool StartOpenVoiceService(FProcHandle* OutProcHandle = nullptr);

private:
	void RegisterCustomizations();
	void UnregisterCustomizations();

	FProcHandle AutoStartedServiceHandle;
};
