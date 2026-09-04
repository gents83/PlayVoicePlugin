// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class FPlayVoiceSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnCheckRequirementsClicked();
	FReply OnInstallPythonClicked();
	FReply OnCancelSetupClicked();
	FReply OnLaunchSetupClicked();
	FReply OnStartServiceClicked();
};
