// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "SGraphPin.h"
#include "Widgets/Input/SComboBox.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"

class SPlayVoiceKeyGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SPlayVoiceKeyGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	struct FKeyOption
	{
		FName Key = NAME_None;
		FString SourceText;

		FText GetDisplayText() const
		{
			if (Key.IsNone())
			{
				return FText::FromString(TEXT("None"));
			}
			if (SourceText.IsEmpty())
			{
				return FText::FromName(Key);
			}
			return FText::FromString(FString::Printf(TEXT("%s - \"%s\""), *Key.ToString(), *SourceText));
		}
	};

	TArray<TSharedPtr<FKeyOption>> KeyOptions;
	TSharedPtr<FKeyOption> CurrentlySelectedKey;

	void RefreshKeyOptions();
};

class FPlayVoiceGraphPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* InPin) const override;
};
