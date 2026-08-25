// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoicePluginModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FPlayVoicePluginModule"

void FPlayVoicePluginModule::StartupModule()
{
	// Initialization code
}

void FPlayVoicePluginModule::ShutdownModule()
{
	// Cleanup code
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPlayVoicePluginModule, PlayVoicePlugin)
