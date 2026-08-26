// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "CharacterVoiceAssetFactory.generated.h"

/**
 * Factory for creating UCharacterVoiceAsset instances in Unreal Editor.
 */
UCLASS()
class PLAYVOICEPLUGINEDITOR_API UCharacterVoiceAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UCharacterVoiceAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
	virtual uint32 GetMenuCategories() const override;
};
