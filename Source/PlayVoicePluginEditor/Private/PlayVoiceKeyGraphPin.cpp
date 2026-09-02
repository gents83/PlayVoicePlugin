// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceKeyGraphPin.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoiceBlueprintLibrary.h"

void SPlayVoiceKeyGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget> SPlayVoiceKeyGraphPin::GetDefaultValueWidget()
{
	RefreshKeyOptions();

	FString CurrentDefaultValue = GraphPinObj ? GraphPinObj->GetDefaultAsString() : FString();
	FName CurrentDefaultKey(*CurrentDefaultValue);

	for (const TSharedPtr<FKeyOption>& Option : KeyOptions)
	{
		if (Option.IsValid() && Option->Key == CurrentDefaultKey)
		{
			CurrentlySelectedKey = Option;
			break;
		}
	}

	return SNew(SComboBox<TSharedPtr<FKeyOption>>)
		.OptionsSource(&KeyOptions)
		.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
		.OnGenerateWidget_Lambda([](TSharedPtr<FKeyOption> InItem)
		{
			return SNew(STextBlock).Text(InItem.IsValid() ? InItem->GetDisplayText() : FText::FromString("None"));
		})
		.OnSelectionChanged_Lambda([this](TSharedPtr<FKeyOption> NewChoice, ESelectInfo::Type SelectInfo)
		{
			if (NewChoice.IsValid() && GraphPinObj)
			{
				CurrentlySelectedKey = NewChoice;
				FString NewValStr = NewChoice->Key.IsNone() ? FString() : NewChoice->Key.ToString();
				if (GraphPinObj->GetDefaultAsString() != NewValStr)
				{
					const UEdGraphSchema* Schema = GraphPinObj->GetSchema();
					if (Schema)
					{
						Schema->TrySetDefaultValue(*GraphPinObj, NewValStr);
					}
				}
			}
		})
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (CurrentlySelectedKey.IsValid())
				{
					return CurrentlySelectedKey->GetDisplayText();
				}
				if (GraphPinObj)
				{
					FString DefaultVal = GraphPinObj->GetDefaultAsString();
					if (!DefaultVal.IsEmpty() && DefaultVal != TEXT("None"))
					{
						return FText::FromString(DefaultVal);
					}
				}
				return FText::FromString("Select Key...");
			})
		];
}

void SPlayVoiceKeyGraphPin::RefreshKeyOptions()
{
	KeyOptions.Reset();
	CurrentlySelectedKey.Reset();

	if (!GraphPinObj || !GraphPinObj->GetOwningNode())
	{
		return;
	}

	UEdGraphNode* OwningNode = GraphPinObj->GetOwningNode();

	// Check if there is a CharacterVoiceAsset or StringTable connected/configured on adjacent pins of the node
	UObject* FoundTargetObject = nullptr;
	for (UEdGraphPin* Pin : OwningNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			if (Pin->PinType.PinSubCategoryObject.IsValid())
			{
				UObject* PinSubObj = Pin->PinType.PinSubCategoryObject.Get();
				if (PinSubObj && (PinSubObj->IsChildOf(UCharacterVoiceAsset::StaticClass()) || PinSubObj->IsChildOf(UStringTable::StaticClass())))
				{
					if (Pin->DefaultObject)
					{
						FoundTargetObject = Pin->DefaultObject;
					}
				}
			}
		}
	}

	if (FoundTargetObject)
	{
		if (UStringTable* TableAsset = Cast<UStringTable>(FoundTargetObject))
		{
			FStringTableConstRef TableRef = TableAsset->GetStringTable();
			TableRef->EnumerateSourceStrings([this](const FString& KeyString, const FString& SourceString)
			{
				TSharedPtr<FKeyOption> Option = MakeShared<FKeyOption>();
				Option->Key = FName(*KeyString);
				Option->SourceText = SourceString;
				KeyOptions.Add(Option);
				return true;
			});
		}
		else if (UCharacterVoiceAsset* VoiceAsset = Cast<UCharacterVoiceAsset>(FoundTargetObject))
		{
			for (const FPlayVoiceLineEntry& Entry : VoiceAsset->VoiceLines)
			{
				if (!Entry.Key.IsNone())
				{
					TSharedPtr<FKeyOption> Option = MakeShared<FKeyOption>();
					Option->Key = Entry.Key;
					Option->SourceText = VoiceAsset->GetResolvedTextLineForEntry(Entry);
					KeyOptions.Add(Option);
				}
			}
		}
	}

	// Fallback: If no target object pin found or empty options, enumerate all loaded String Tables in AssetRegistry/Memory
	if (KeyOptions.Num() == 0)
	{
		for (TObjectIterator<UStringTable> It; It; ++It)
		{
			UStringTable* TableAsset = *It;
			if (TableAsset)
			{
				FStringTableConstRef TableRef = TableAsset->GetStringTable();
				TableRef->EnumerateSourceStrings([this](const FString& KeyString, const FString& SourceString)
				{
					TSharedPtr<FKeyOption> Option = MakeShared<FKeyOption>();
					Option->Key = FName(*KeyString);
					Option->SourceText = SourceString;
					KeyOptions.Add(Option);
					return true;
				});
			}
		}
	}
}

TSharedPtr<SGraphPin> FPlayVoiceGraphPinFactory::CreatePin(UEdGraphPin* InPin)
{
	if (InPin && InPin->PinName == FName(TEXT("Key")))
	{
		UEdGraphNode* OwningNode = InPin->GetOwningNode();
		if (OwningNode)
		{
			FString NodeTitle = OwningNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (NodeTitle.Contains(TEXT("PlayCharacterVoiceFromKey")) || NodeTitle.Contains(TEXT("Play Character Voice From Key")))
			{
				return SNew(SPlayVoiceKeyGraphPin, InPin);
			}
		}
	}
	return nullptr;
}
