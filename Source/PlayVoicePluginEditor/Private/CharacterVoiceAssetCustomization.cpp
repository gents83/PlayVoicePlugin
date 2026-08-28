// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAssetCustomization.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceAudioUtils.h"
#include "PlayVoicePluginEditorModule.h"
#include "Containers/Ticker.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/MessageDialog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Knot.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharacterVoiceCustomization, Log, All);

TSharedRef<IDetailCustomization> FCharacterVoiceAssetCustomization::MakeInstance()
{
	return MakeShareable(new FCharacterVoiceAssetCustomization());
}

void FCharacterVoiceAssetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() > 0)
	{
		TargetVoiceAsset = Cast<UCharacterVoiceAsset>(ObjectsBeingCustomized[0].Get());
		if (TargetVoiceAsset.IsValid())
		{
			TargetVoiceAsset->AutoLinkPrecachedSoundWaves();
		}
	}

	IDetailCategoryBuilder& OpenVoiceCategory = DetailBuilder.EditCategory("OpenVoice Model Actions", FText::FromString("OpenVoice Model Actions"));

	OpenVoiceCategory.AddCustomRow(FText::FromString("Generate Model and Process Lines"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Full Pipeline (One-Click)"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Generate Model & Process All Lines"))
		.ToolTipText(FText::FromString("One-click pipeline: Auto-discovers Blueprint voice lines, extracts model embeddings for all configured languages, transcribes, synthesizes, and saves generated audio assets."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnGenerateAndProcessAllClicked)
	];

	OpenVoiceCategory.AddCustomRow(FText::FromString("Generate Model"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Model Extraction"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Generate OpenVoice Model"))
		.ToolTipText(FText::FromString("Extract tone color embeddings for all configured languages from reference audio clips and folders."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnGenerateModelClicked)
	];

	OpenVoiceCategory.AddCustomRow(FText::FromString("Precache Voice Lines"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Zero-Delay Pre-rendering"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Pre-process Blueprint Voice Lines"))
		.ToolTipText(FText::FromString("Scans all Blueprint nodes for dialogue lines and pre-renders sound wave assets across all configured languages to eliminate in-game latency."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnPrecacheLinesClicked)
	];

	OpenVoiceCategory.AddCustomRow(FText::FromString("Clean Precached Sound Waves"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Clean Precached Assets"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.Text(FText::FromString("Clean Precached Sound Waves"))
		.ToolTipText(FText::FromString("Clears precached voice lines map from UCharacterVoiceAsset and deletes generated USoundWave package files from disk and project."))
		.OnClicked(this, &FCharacterVoiceAssetCustomization::OnCleanPrecachedSoundWavesClicked)
	];
}

static void EnsureServiceReadyAndExecute(TFunction<void(bool bReady)> OnComplete, int32 MaxAttempts = 30)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	FString BaseUrl = Settings && !Settings->ServiceUrl.IsEmpty() ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");
	BaseUrl.RemoveFromEnd(TEXT("/"));

	// First, test if service is already running and healthy
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> InitialHealthReq = FHttpModule::Get().CreateRequest();
	InitialHealthReq->SetURL(BaseUrl + TEXT("/health"));
	InitialHealthReq->SetVerb(TEXT("GET"));
	InitialHealthReq->SetTimeout(2.0f);

	InitialHealthReq->OnProcessRequestComplete().BindLambda([BaseUrl, MaxAttempts, OnComplete](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess)
	{
		bool bAlreadyReady = bSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode());
		if (bAlreadyReady)
		{
			UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OpenVoice REST Service is already running and healthy at %s"), *BaseUrl);
			OnComplete(true);
			return;
		}

		// Service not currently responding, launch process and poll until ready
		Async(EAsyncExecution::Thread, [BaseUrl, MaxAttempts, OnComplete]()
		{
			FProcHandle ProcHandle;
			bool bStarted = FPlayVoicePluginEditorModule::StartOpenVoiceService(&ProcHandle);
			UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("StartOpenVoiceService requested from EnsureServiceReadyAndExecute (Started: %d)"), bStarted);

			Async(EAsyncExecution::TaskGraphMainThread, [BaseUrl, MaxAttempts, OnComplete]()
			{
				TSharedRef<int32> Attempts = MakeShared<int32>(0);

				struct FPollContext
				{
					TFunction<void(FPollContext& Self)> PollFunc;
				};

				TSharedRef<FPollContext> Context = MakeShared<FPollContext>();
				Context->PollFunc = [BaseUrl, Attempts, MaxAttempts, OnComplete, Context](FPollContext& Self)
				{
					(*Attempts)++;

					TSharedRef<IHttpRequest, ESPMode::ThreadSafe> PollReq = FHttpModule::Get().CreateRequest();
					PollReq->SetURL(BaseUrl + TEXT("/health"));
					PollReq->SetVerb(TEXT("GET"));
					PollReq->SetTimeout(2.0f);

					PollReq->OnProcessRequestComplete().BindLambda([Attempts, MaxAttempts, OnComplete, Context](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess)
					{
						bool bReady = bSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode());
						if (bReady)
						{
							UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OpenVoice REST Service health check succeeded after %d attempts."), *Attempts);
							Context->PollFunc = nullptr;
							OnComplete(true);
						}
						else if (*Attempts < MaxAttempts)
						{
							UE_LOG(LogCharacterVoiceCustomization, Verbose, TEXT("OpenVoice health check attempt %d failed, polling again..."), *Attempts);
							FTickerDelegate TickerDelegate;
							TickerDelegate.BindLambda([Context](float DeltaTime)
							{
								Context->PollFunc(*Context);
								return false;
							});
							FTSTicker::GetCoreTicker().AddTicker(TickerDelegate, 0.8f);
						}
						else
						{
							UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OpenVoice REST Service health check timed out after %d attempts."), MaxAttempts);
							Context->PollFunc = nullptr;
							OnComplete(false);
						}
					});

					PollReq->ProcessRequest();
				};

				Context->PollFunc(*Context);
			});
		});
	});

	InitialHealthReq->ProcessRequest();
}

static FString SanitizeTextString(const FString& InRaw)
{
	FString S = InRaw.TrimStartAndEnd();
	if (S.IsEmpty())
	{
		return FString();
	}

	// Reject engine/script default object references or class paths
	if (S.Contains(TEXT("/Script/")) ||
		S.Contains(TEXT("Default__")) ||
		S.Contains(TEXT("KismetTextLibrary")) ||
		S.Contains(TEXT("KismetStringLibrary")) ||
		S.StartsWith(TEXT("/Engine/")) ||
		S.StartsWith(TEXT("Class'")) ||
		S.StartsWith(TEXT("Function'")) ||
		S.StartsWith(TEXT("UserDefinedStruct'")) ||
		S.StartsWith(TEXT("Blueprint'")))
	{
		return FString();
	}

	// Handle FText pin format (SourceString="...", Namespace="...", Key="...")
	int32 SourceIdx = S.Find(TEXT("SourceString="));
	if (SourceIdx != INDEX_NONE)
	{
		int32 StartQuote = S.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, SourceIdx);
		if (StartQuote != INDEX_NONE)
		{
			int32 EndQuote = S.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, StartQuote + 1);
			if (EndQuote != INDEX_NONE)
			{
				return S.Mid(StartQuote + 1, EndQuote - StartQuote - 1).TrimStartAndEnd();
			}
		}
	}

	// Handle NSLOCTEXT("...", "...", "Text") or INVTEXT("Text")
	if (S.StartsWith(TEXT("NSLOCTEXT(")) || S.StartsWith(TEXT("INVTEXT(")))
	{
		int32 LastQuoteEnd = S.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastQuoteEnd != INDEX_NONE)
		{
			int32 LastQuoteStart = S.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromEnd, LastQuoteEnd - 1);
			if (LastQuoteStart != INDEX_NONE && LastQuoteStart < LastQuoteEnd)
			{
				return S.Mid(LastQuoteStart + 1, LastQuoteEnd - LastQuoteStart - 1).TrimStartAndEnd();
			}
		}
	}

	// Strip enclosing double quotes
	if (S.StartsWith(TEXT("\"")) && S.EndsWith(TEXT("\"")) && S.Len() >= 2)
	{
		S = S.Mid(1, S.Len() - 2);
	}

	// Reject raw struct/object representations like "(Link=...)"
	if (S.StartsWith(TEXT("(")) && S.EndsWith(TEXT(")")))
	{
		return FString();
	}

	return S.TrimStartAndEnd();
}

static FString ExtractTextFromPin(const UEdGraphPin* Pin, int32 Depth = 0)
{
	if (!Pin || Depth > 10)
	{
		return FString();
	}

	// Ignore Target / self / WorldContextObject pins (target objects for function calls, not text inputs)
	FString PinName = Pin->PinName.ToString();
	if (PinName.Equals(TEXT("self"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("Target"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("WorldContextObject"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("WorldContext"), ESearchCase::IgnoreCase))
	{
		return FString();
	}

	// 1. If this is an input pin, check its direct default value first
	if (Pin->Direction == EGPD_Input)
	{
		FString DirectVal = SanitizeTextString(Pin->GetDefaultAsString());
		if (!DirectVal.IsEmpty())
		{
			return DirectVal;
		}
	}

	// 2. If the pin is linked to upstream pin(s), traverse connected nodes
	if (Pin->LinkedTo.Num() > 0)
	{
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin)
			{
				continue;
			}

			const UEdGraphNode* OwningNode = LinkedPin->GetOwningNode();
			if (!OwningNode)
			{
				continue;
			}

			// Handle Reroute Nodes (UK2Node_Knot)
			if (const UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(OwningNode))
			{
				if (const UEdGraphPin* KnotInput = KnotNode->GetInputPin())
				{
					FString KnotVal = ExtractTextFromPin(KnotInput, Depth + 1);
					if (!KnotVal.IsEmpty())
					{
						return KnotVal;
					}
				}
			}

			// Handle UK2Node_CallFunction (e.g. MakeLiteralString, MakeLiteralText, Conv_TextToString, etc.)
			if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(OwningNode))
			{
				UFunction* TargetFunc = CallNode->GetTargetFunction();
				FString FuncName = TargetFunc ? TargetFunc->GetName() : TEXT("");

				// If literal string/text/name builder function
				if (FuncName.Equals(TEXT("MakeLiteralString"), ESearchCase::IgnoreCase) ||
					FuncName.Equals(TEXT("MakeLiteralText"), ESearchCase::IgnoreCase) ||
					FuncName.Equals(TEXT("MakeLiteralName"), ESearchCase::IgnoreCase))
				{
					if (const UEdGraphPin* ValPin = CallNode->FindPin(TEXT("Value")))
					{
						FString Val = ExtractTextFromPin(ValPin, Depth + 1);
						if (!Val.IsEmpty())
						{
							return Val;
						}
					}
				}

				// For conversion functions (Conv_TextToString, Conv_NameToText, etc.), prioritize text input pins
				TArray<FString> PreferredPinNames = { TEXT("InText"), TEXT("InString"), TEXT("InName"), TEXT("Text"), TEXT("String"), TEXT("Name"), TEXT("Value") };
				for (const FString& PrefName : PreferredPinNames)
				{
					if (const UEdGraphPin* InputPin = CallNode->FindPin(*PrefName))
					{
						if (InputPin->Direction == EGPD_Input)
						{
							FString InputVal = ExtractTextFromPin(InputPin, Depth + 1);
							if (!InputVal.IsEmpty())
							{
								return InputVal;
							}
						}
					}
				}

				// Check all other non-target input pins
				for (const UEdGraphPin* NodePin : CallNode->Pins)
				{
					if (NodePin && NodePin->Direction == EGPD_Input)
					{
						FString NodePinName = NodePin->PinName.ToString();
						if (!NodePinName.Equals(TEXT("self"), ESearchCase::IgnoreCase) &&
							!NodePinName.Equals(TEXT("Target"), ESearchCase::IgnoreCase) &&
							!NodePinName.Equals(TEXT("WorldContextObject"), ESearchCase::IgnoreCase))
						{
							FString InputVal = ExtractTextFromPin(NodePin, Depth + 1);
							if (!InputVal.IsEmpty())
							{
								return InputVal;
							}
						}
					}
				}
			}

			// Handle Blueprint variable read nodes
			const UBlueprint* BP = OwningNode->GetTypedOuter<UBlueprint>();
			if (BP && OwningNode->GetClass()->GetName().Contains(TEXT("Variable")))
			{
				FString NodeTitle = OwningNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
				for (const FBPVariableDescription& VarDesc : BP->NewVariables)
				{
					if (NodeTitle.Contains(VarDesc.VarName.ToString()))
					{
						FString VarDefault = SanitizeTextString(VarDesc.DefaultValue);
						if (!VarDefault.IsEmpty())
						{
							return VarDefault;
						}
					}
				}
			}

			// Generic traversal for other nodes: inspect non-target input pins
			for (const UEdGraphPin* NodePin : OwningNode->Pins)
			{
				if (NodePin && NodePin->Direction == EGPD_Input)
				{
					FString NodePinName = NodePin->PinName.ToString();
					if (!NodePinName.Equals(TEXT("self"), ESearchCase::IgnoreCase) &&
						!NodePinName.Equals(TEXT("Target"), ESearchCase::IgnoreCase) &&
						!NodePinName.Equals(TEXT("WorldContextObject"), ESearchCase::IgnoreCase))
					{
						FString InputVal = ExtractTextFromPin(NodePin, Depth + 1);
						if (!InputVal.IsEmpty())
						{
							return InputVal;
						}
					}
				}
			}
		}
	}

	return FString();
}

TArray<FString> FCharacterVoiceAssetCustomization::RetrieveVoiceLinesFromProjectBlueprints(const UCharacterVoiceAsset* TargetAsset)
{
	TArray<FString> DiscoveredLines;

	if (TargetAsset)
	{
		for (const FCharacterLanguageData& LangData : TargetAsset->Languages)
		{
			for (const FVoiceLineGuideTrack& GuideTrack : LangData.GuideTracks)
			{
				FString CleanLine = GuideTrack.LineText.TrimStartAndEnd();
				if (!CleanLine.IsEmpty())
				{
					DiscoveredLines.AddUnique(CleanLine);
				}
			}
		}

		for (const auto& KVP : TargetAsset->PrecachedSoundWaves)
		{
			FString Key = KVP.Key;
			int32 ColonIdx = -1;
			if (Key.FindChar(TEXT(':'), ColonIdx) && ColonIdx >= 0 && ColonIdx < Key.Len() - 1)
			{
				Key = Key.Mid(ColonIdx + 1);
			}
			Key.TrimStartAndEndInline();
			if (!Key.IsEmpty())
			{
				DiscoveredLines.AddUnique(Key);
			}
		}
	}
	if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::Get().LoadModule("AssetRegistry");
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> BlueprintAssetList;
	AssetRegistryModule.Get().GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BlueprintAssetList, true);

	for (const FAssetData& AssetData : BlueprintAssetList)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint)
		{
			continue;
		}

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (!CallNode)
				{
					continue;
				}

				UFunction* TargetFunc = CallNode->GetTargetFunction();
				if (!TargetFunc)
				{
					continue;
				}

				FString FuncName = TargetFunc->GetName();
				if (FuncName == TEXT("PlayCharacterVoice") || FuncName == TEXT("GenerateVoiceSoundWave") || FuncName == TEXT("PrecacheCharacterVoiceLines") || FuncName == TEXT("PrecacheVoiceLine"))
				{
					UEdGraphPin* AssetPin = CallNode->FindPin(TEXT("CharacterVoiceAsset"));
					UEdGraphPin* TextPin = CallNode->FindPin(TEXT("TextLine"));

					bool bMatchesAsset = false;
					if (AssetPin)
					{
						if (AssetPin->DefaultObject == TargetAsset)
						{
							bMatchesAsset = true;
						}
						else if (!AssetPin->DefaultObject && AssetPin->LinkedTo.Num() == 0)
						{
							bMatchesAsset = true;
						}
					}
					else
					{
						bMatchesAsset = true;
					}

					if (bMatchesAsset && TextPin)
					{
						FString ExtractedText = ExtractTextFromPin(TextPin);
						if (!ExtractedText.IsEmpty())
						{
							DiscoveredLines.AddUnique(ExtractedText);
						}
					}
				}
			}
		}
	}

	return DiscoveredLines;
}

FReply FCharacterVoiceAssetCustomization::OnGenerateAndProcessAllClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateAndProcessAllClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateAndProcessAllClicked: Initiating non-blocking model pipeline for asset '%s'"), *TargetVoiceAsset->GetName());
	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;

	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateAndProcessAllClicked: Service health check failed. Could not connect to OpenVoice REST backend."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateAndProcessAllClicked: Target voice asset is invalid or garbage collected."));
			return;
		}

		UCharacterVoiceAsset* Asset = WeakTargetAsset.Get();
		if (Asset->Languages.Num() == 0)
		{
			Asset->GetOrAddLanguageData(Asset->DefaultLanguage.IsEmpty() ? TEXT("EN") : Asset->DefaultLanguage);
		}

		TArray<FString> DiscoveredBlueprintLines = RetrieveVoiceLinesFromProjectBlueprints(Asset);
		UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateAndProcessAllClicked: Discovered %d dialogue lines for pre-processing."), DiscoveredBlueprintLines.Num());

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");
		float TimeoutSecs = Settings && Settings->RequestTimeout > 0.0f ? Settings->RequestTimeout : 120.0f;

		int32 TotalLangsProcessed = 0;
		TArray<FCharacterLanguageData*> ConfiguredLanguages;
		for (FCharacterLanguageData& LangData : Asset->Languages)
		{
			bool bHasRefAudioConfigured = (LangData.ReferenceAudioFiles.Num() > 0 || !LangData.ReferenceAudioFolder.Path.IsEmpty());
			if (bHasRefAudioConfigured)
			{
				ConfiguredLanguages.Add(&LangData);
				TotalLangsProcessed++;
			}
		}

		if (TotalLangsProcessed == 0)
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateAndProcessAllClicked: No reference audio files or folders configured."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify Reference Audio Files or Reference Audio Folder for at least one language before generating models."));
			return;
		}

		int32 TotalTasksCount = ConfiguredLanguages.Num() * (1 + DiscoveredBlueprintLines.Num());
		TSharedPtr<int32> CompletedTasks = MakeShared<int32>(0);
		TSharedPtr<int32> FailedTasks = MakeShared<int32>(0);

		FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Processing Model Extraction & {0} Lines..."), FText::AsNumber(DiscoveredBlueprintLines.Num())));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseThrobber = true;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.FadeOutDuration = 0.5f;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}

		auto StepTaskProgress = [NotificationItem, CompletedTasks, FailedTasks, TotalTasksCount]()
		{
			(*CompletedTasks)++;
			if (NotificationItem.IsValid())
			{
				FText Msg = FText::Format(FText::FromString("PlayVoice: Processing ({0}/{1})..."), FText::AsNumber(*CompletedTasks), FText::AsNumber(TotalTasksCount));
				NotificationItem->SetText(Msg);

				if (*CompletedTasks >= TotalTasksCount)
				{
					if (*FailedTasks > 0)
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
						NotificationItem->SetText(FText::Format(FText::FromString("PlayVoice: Finished with {0} errors."), FText::AsNumber(*FailedTasks)));
						UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateAndProcessAllClicked: Completed background pipeline with %d errors."), *FailedTasks);
					}
					else
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
						NotificationItem->SetText(FText::FromString("PlayVoice: Full pipeline processing complete!"));
						UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateAndProcessAllClicked: Successfully completed full model extraction & line pre-processing pipeline!"));
					}
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
			}
		};

		for (FCharacterLanguageData* LangDataPtr : ConfiguredLanguages)
		{
			FCharacterLanguageData& LangData = *LangDataPtr;
			TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFilesForLanguage(LangData.LanguageCode);

			TSharedPtr<FJsonObject> ExtractObj = MakeShared<FJsonObject>();
			ExtractObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
			ExtractObj->SetStringField(TEXT("language"), LangData.LanguageCode);

			TArray<TSharedPtr<FJsonValue>> AudioPathValues;
			for (const FString& Path : RefAudioFiles)
			{
				AudioPathValues.Add(MakeShared<FJsonValueString>(Path));
			}
			ExtractObj->SetArrayField(TEXT("reference_audio_files"), AudioPathValues);

			FString ExtractPayload;
			TSharedRef<TJsonWriter<>> ExtractWriter = TJsonWriterFactory<>::Create(&ExtractPayload);
			FJsonSerializer::Serialize(ExtractObj.ToSharedRef(), ExtractWriter);

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> ExtractReq = FHttpModule::Get().CreateRequest();
			ExtractReq->SetURL(BaseUrl + TEXT("/extract"));
			ExtractReq->SetVerb(TEXT("POST"));
			ExtractReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			ExtractReq->SetContentAsString(ExtractPayload);
			ExtractReq->SetTimeout(TimeoutSecs);

			FString CurrentLangCode = LangData.LanguageCode;
			float CurrentSpeed = LangData.Speed;

			ExtractReq->OnProcessRequestComplete().BindLambda([WeakTargetAsset, BaseUrl, CurrentLangCode, CurrentSpeed, TimeoutSecs, DiscoveredBlueprintLines, StepTaskProgress, FailedTasks](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bExtractSuccess)
			{
				bool bSuccess = bExtractSuccess && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode());
				if (!bSuccess)
				{
					(*FailedTasks)++;
					UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateAndProcessAllClicked: Model extraction failed for language '%s'."), *CurrentLangCode);
				}

				if (bSuccess && WeakTargetAsset.IsValid())
				{
					TSharedPtr<FJsonObject> ResObj;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
					if (FJsonSerializer::Deserialize(Reader, ResObj) && ResObj.IsValid())
					{
								FString Status = ResObj->GetStringField(TEXT("status"));
								if (Status.Equals(TEXT("success"), ESearchCase::IgnoreCase))
						{
									FCharacterLanguageData* TargetLangData = WeakTargetAsset->FindLanguageData(CurrentLangCode);
									if (TargetLangData)
									{
										TargetLangData->ToneColorEmbeddingData = ResObj->GetStringField(TEXT("embedding_data"));
										TargetLangData->bIsModelGenerated = !TargetLangData->ToneColorEmbeddingData.IsEmpty();
										WeakTargetAsset->SaveModelToFile(TEXT(""), CurrentLangCode);
										WeakTargetAsset->MarkPackageDirty();
										UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateAndProcessAllClicked: Model extraction succeeded for language '%s'."), *CurrentLangCode);
									}
								}
								else
								{
									(*FailedTasks)++;
									FString ErrMsg = ResObj->GetStringField(TEXT("message"));
									UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateAndProcessAllClicked: Model extraction error for language '%s': %s"), *CurrentLangCode, *ErrMsg);
						}
					}
				}

				StepTaskProgress();

				if (!WeakTargetAsset.IsValid() || DiscoveredBlueprintLines.Num() == 0)
				{
					return;
				}

				UCharacterVoiceAsset* VoiceAsset = WeakTargetAsset.Get();
				FCharacterLanguageData* TargetLangData = VoiceAsset->FindLanguageData(CurrentLangCode);
				FString EmbeddingData = TargetLangData ? TargetLangData->ToneColorEmbeddingData : TEXT("");

				UPackage* OuterPackage = VoiceAsset->GetOutermost();
				FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

				for (const FString& LineText : DiscoveredBlueprintLines)
				{
					if (!VoiceAsset->bRegenerateExistingVoiceLines && VoiceAsset->HasPrecachedVoiceLine(LineText, CurrentLangCode))
					{
						UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateAndProcessAllClicked: Skipping line '%s' (already precached)."), *LineText);
						StepTaskProgress();
						continue;
					}

					TSharedPtr<FJsonObject> SynthObj = MakeShared<FJsonObject>();
					SynthObj->SetStringField(TEXT("character_name"), VoiceAsset->CharacterName.ToString());
					SynthObj->SetStringField(TEXT("text"), LineText);
					SynthObj->SetStringField(TEXT("language"), CurrentLangCode);
					SynthObj->SetNumberField(TEXT("speed"), CurrentSpeed);
					SynthObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

					TArray<FString> RefAudioFiles = VoiceAsset->GetResolvedReferenceAudioFilesForLanguage(CurrentLangCode);
					TArray<TSharedPtr<FJsonValue>> RefPathValues;
					for (const FString& RefPath : RefAudioFiles)
					{
						RefPathValues.Add(MakeShared<FJsonValueString>(RefPath));
					}
					SynthObj->SetArrayField(TEXT("reference_audio_files"), RefPathValues);

					const FVoiceLineGuideTrack* GuideTrack = VoiceAsset->FindGuideTrackForLine(LineText, CurrentLangCode);
					FString GuideFile = VoiceAsset->GetResolvedGuideAudioFileForLine(LineText, CurrentLangCode);
					if (!GuideFile.IsEmpty())
					{
						SynthObj->SetStringField(TEXT("guide_audio_file"), GuideFile);
					}
					if (GuideTrack)
					{
						if (!GuideTrack->Emotion.IsEmpty())
						{
							SynthObj->SetStringField(TEXT("emotion"), GuideTrack->Emotion);
						}
						if (FMath::Abs(GuideTrack->Speed - 1.0f) > 0.01f)
						{
							SynthObj->SetNumberField(TEXT("speed"), CurrentSpeed * GuideTrack->Speed);
						}
					}

					FString SynthPayload;
					TSharedRef<TJsonWriter<>> SynthWriter = TJsonWriterFactory<>::Create(&SynthPayload);
					FJsonSerializer::Serialize(SynthObj.ToSharedRef(), SynthWriter);

					TSharedRef<IHttpRequest, ESPMode::ThreadSafe> SynthReq = FHttpModule::Get().CreateRequest();
					SynthReq->SetURL(BaseUrl + TEXT("/synthesize"));
					SynthReq->SetVerb(TEXT("POST"));
					SynthReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
					SynthReq->SetContentAsString(SynthPayload);
					SynthReq->SetTimeout(TimeoutSecs);

					SynthReq->OnProcessRequestComplete().BindLambda([WeakTargetAsset, LineText, CurrentLangCode, AssetFolderPath, StepTaskProgress, FailedTasks](FHttpRequestPtr SReq, FHttpResponsePtr SRes, bool bSynthSuccess)
					{
						bool bSynthOk = bSynthSuccess && SRes.IsValid() && EHttpResponseCodes::IsOk(SRes->GetResponseCode());
						if (!bSynthOk)
						{
							(*FailedTasks)++;
							UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateAndProcessAllClicked: Failed to synthesize line '%s' for language '%s'."), *LineText, *CurrentLangCode);
						}

						if (bSynthOk && WeakTargetAsset.IsValid())
						{
							FString LineSanitized = FString::Printf(TEXT("SW_%s_%s_%u"), *WeakTargetAsset->CharacterName.ToString(), *CurrentLangCode, GetTypeHash(LineText));
							FString SoundWavePackagePath = AssetFolderPath / LineSanitized;

							UPackage* SoundWavePackage = CreatePackage(*SoundWavePackagePath);
							USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(SRes->GetContent(), SoundWavePackage, FName(*LineSanitized));

							if (SoundWave)
							{
								WeakTargetAsset->CacheVoiceLine(LineText, SoundWave, CurrentLangCode);
								SoundWave->MarkPackageDirty();
								WeakTargetAsset->MarkPackageDirty();

								if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
								{
									FAssetRegistryModule::AssetCreated(SoundWave);
								}
								UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateAndProcessAllClicked: Successfully pre-rendered sound wave asset '%s'."), *LineSanitized);
							}
						}

						StepTaskProgress();
					});

					SynthReq->ProcessRequest();
				}
			});

			ExtractReq->ProcessRequest();
		}
	});

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnGenerateModelClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateModelClicked: Initiating non-blocking model extraction for asset '%s'"), *TargetVoiceAsset->GetName());
	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;

	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateModelClicked: Service health check failed. Could not connect to OpenVoice REST backend."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: Target voice asset is invalid or garbage collected."));
			return;
		}

		UCharacterVoiceAsset* Asset = WeakTargetAsset.Get();
		if (Asset->Languages.Num() == 0)
		{
			Asset->GetOrAddLanguageData(Asset->DefaultLanguage.IsEmpty() ? TEXT("EN") : Asset->DefaultLanguage);
		}

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString Url = (Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983")) + TEXT("/extract");
		float TimeoutSecs = Settings && Settings->RequestTimeout > 0.0f ? Settings->RequestTimeout : 120.0f;

		int32 ProcessedLangs = 0;
		TArray<FCharacterLanguageData*> ConfiguredLanguages;
		for (FCharacterLanguageData& LangData : Asset->Languages)
		{
			bool bHasRefAudioConfigured = (LangData.ReferenceAudioFiles.Num() > 0 || !LangData.ReferenceAudioFolder.Path.IsEmpty());
			if (bHasRefAudioConfigured)
			{
				ConfiguredLanguages.Add(&LangData);
				ProcessedLangs++;
			}
		}

		if (ProcessedLangs == 0)
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: No reference audio files or folders specified."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Please specify Reference Audio Files or Reference Audio Folder for at least one language."));
			return;
		}

		int32 TotalTasksCount = ConfiguredLanguages.Num();
		TSharedPtr<int32> CompletedTasks = MakeShared<int32>(0);
		TSharedPtr<int32> FailedTasks = MakeShared<int32>(0);

		FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Extracting Model for {0} languages..."), FText::AsNumber(TotalTasksCount)));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseThrobber = true;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.FadeOutDuration = 0.5f;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}

		auto StepTaskProgress = [NotificationItem, CompletedTasks, FailedTasks, TotalTasksCount]()
		{
			(*CompletedTasks)++;
			if (NotificationItem.IsValid())
			{
				FText Msg = FText::Format(FText::FromString("PlayVoice: Model extraction ({0}/{1})..."), FText::AsNumber(*CompletedTasks), FText::AsNumber(TotalTasksCount));
				NotificationItem->SetText(Msg);

				if (*CompletedTasks >= TotalTasksCount)
				{
					if (*FailedTasks > 0)
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
						NotificationItem->SetText(FText::Format(FText::FromString("PlayVoice: Model extraction finished with {0} errors."), FText::AsNumber(*FailedTasks)));
						UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnGenerateModelClicked: Model extraction completed with %d errors."), *FailedTasks);
					}
					else
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
						NotificationItem->SetText(FText::FromString("PlayVoice: OpenVoice model extraction complete!"));
						UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateModelClicked: OpenVoice model extraction completed successfully for all languages."));
					}
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
			}
		};

		for (FCharacterLanguageData* LangDataPtr : ConfiguredLanguages)
		{
			FCharacterLanguageData& LangData = *LangDataPtr;
			TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFilesForLanguage(LangData.LanguageCode);

			TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
			JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
			JsonObj->SetStringField(TEXT("language"), LangData.LanguageCode);

			TArray<TSharedPtr<FJsonValue>> AudioPathValues;
			for (const FString& Path : RefAudioFiles)
			{
				AudioPathValues.Add(MakeShared<FJsonValueString>(Path));
			}
			JsonObj->SetArrayField(TEXT("reference_audio_files"), AudioPathValues);

			FString PayloadStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
			FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
			HttpRequest->SetURL(Url);
			HttpRequest->SetVerb(TEXT("POST"));
			HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			HttpRequest->SetContentAsString(PayloadStr);
			HttpRequest->SetTimeout(TimeoutSecs);

			FString CurrentLangCode = LangData.LanguageCode;

			HttpRequest->OnProcessRequestComplete().BindLambda([WeakTargetAsset, CurrentLangCode, StepTaskProgress, FailedTasks](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
			{
				bool bSuccess = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
				FString ErrorMessage;

				if (Response.IsValid())
				{
					TSharedPtr<FJsonObject> ResponseObj;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
					if (FJsonSerializer::Deserialize(Reader, ResponseObj) && ResponseObj.IsValid())
					{
						FString Status = ResponseObj->GetStringField(TEXT("status"));
						if (Status.Equals(TEXT("success"), ESearchCase::IgnoreCase))
						{
							if (WeakTargetAsset.IsValid())
							{
								FCharacterLanguageData* TargetLangData = WeakTargetAsset->FindLanguageData(CurrentLangCode);
								if (TargetLangData)
								{
									TargetLangData->ToneColorEmbeddingData = ResponseObj->GetStringField(TEXT("embedding_data"));
									TargetLangData->bIsModelGenerated = !TargetLangData->ToneColorEmbeddingData.IsEmpty();
									WeakTargetAsset->SaveModelToFile(TEXT(""), CurrentLangCode);
									WeakTargetAsset->MarkPackageDirty();
									UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnGenerateModelClicked: Model saved for language '%s'."), *CurrentLangCode);
								}
							}
						}
						else
						{
							ErrorMessage = ResponseObj->HasField(TEXT("message")) ? ResponseObj->GetStringField(TEXT("message")) : TEXT("Unknown service error.");
						}
					}
					else if (!bSuccess)
					{
						ErrorMessage = FString::Printf(TEXT("HTTP Request failed with status code %d."), Response->GetResponseCode());
					}
				}
				else
				{
					ErrorMessage = TEXT("Could not connect to service endpoint.");
				}

				if (!ErrorMessage.IsEmpty())
				{
					(*FailedTasks)++;
					UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnGenerateModelClicked: Model extraction error for language '%s': %s"), *CurrentLangCode, *ErrorMessage);
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Model extraction error for language '%s':\n%s"), *CurrentLangCode, *ErrorMessage)));
				}

				StepTaskProgress();
			});

			HttpRequest->ProcessRequest();
		}
	});

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnCleanPrecachedSoundWavesClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnCleanPrecachedSoundWavesClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UCharacterVoiceAsset* Asset = TargetVoiceAsset.Get();
	int32 RemovedCount = Asset->PrecachedSoundWaves.Num();
	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnCleanPrecachedSoundWavesClicked: Initiating precached sound wave cleanup for asset '%s' (%d entries)"), *Asset->GetName(), RemovedCount);

	TArray<USoundWave*> SoundWavesToDelete;
	TArray<FString> FilePathsToDelete;

	for (auto& KVP : Asset->PrecachedSoundWaves)
	{
		if (USoundWave* SoundWave = KVP.Value.Get())
		{
			SoundWavesToDelete.AddUnique(SoundWave);
			UPackage* Pkg = SoundWave->GetOutermost();
			if (Pkg && Pkg != GetTransientPackage())
			{
				FString PkgFilename;
				if (FPackageName::DoesPackageExist(Pkg->GetName(), &PkgFilename))
				{
					FilePathsToDelete.AddUnique(PkgFilename);
				}
			}
		}
	}

	Asset->ClearPrecachedVoiceLines();
	Asset->MarkPackageDirty();

	if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::Get().LoadModule("AssetRegistry");
	}

	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		for (USoundWave* SoundWave : SoundWavesToDelete)
		{
			if (SoundWave)
			{
				AssetRegistryModule.AssetDeleted(SoundWave);
			}
		}
	}

	// Detach objects and outer packages to transient package so open file locks are released
	for (USoundWave* SoundWave : SoundWavesToDelete)
	{
		if (SoundWave)
		{
			UPackage* Pkg = SoundWave->GetOutermost();
			SoundWave->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			SoundWave->MarkAsGarbage();
			if (Pkg && Pkg != GetTransientPackage())
			{
				Pkg->MarkAsGarbage();
			}
		}
	}

	CollectGarbage(RF_NoFlags);

	for (FString& FilePath : FilePathsToDelete)
	{
		if (IFileManager::Get().FileExists(*FilePath))
		{
			bool bDeleted = IFileManager::Get().Delete(*FilePath);
			UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnCleanPrecachedSoundWavesClicked: Deleted package file '%s' (Success: %d)"), *FilePath, bDeleted);
		}
	}

	FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Cleaned {0} precached sound wave assets."), FText::AsNumber(RemovedCount)));
	NotificationInfo.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnCleanPrecachedSoundWavesClicked: Cleaned %d precached sound wave entries."), RemovedCount);

	return FReply::Handled();
}

FReply FCharacterVoiceAssetCustomization::OnPrecacheLinesClicked()
{
	if (!TargetVoiceAsset.IsValid())
	{
		UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnPrecacheLinesClicked: Target voice asset is invalid."));
		return FReply::Handled();
	}

	UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnPrecacheLinesClicked: Initiating non-blocking line pre-processing for asset '%s'"), *TargetVoiceAsset->GetName());
	TWeakObjectPtr<UCharacterVoiceAsset> WeakTargetAsset = TargetVoiceAsset;

	EnsureServiceReadyAndExecute([WeakTargetAsset](bool bServiceReady)
	{
		if (!bServiceReady)
		{
			UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnPrecacheLinesClicked: Service health check failed. Could not connect to OpenVoice REST backend."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to connect to OpenVoice REST Service. Please verify Python executable and service setup in Project Settings."));
			return;
		}

		if (!WeakTargetAsset.IsValid())
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnPrecacheLinesClicked: Target voice asset is invalid or garbage collected."));
			return;
		}

		UCharacterVoiceAsset* Asset = WeakTargetAsset.Get();
		TArray<FString> DiscoveredBlueprintLines = RetrieveVoiceLinesFromProjectBlueprints(Asset);
		UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnPrecacheLinesClicked: Discovered %d dialogue lines in Blueprint graph nodes."), DiscoveredBlueprintLines.Num());

		if (DiscoveredBlueprintLines.Num() == 0)
		{
			UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnPrecacheLinesClicked: No dialogue lines found in Blueprint graph nodes referencing this asset."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("No dialogue lines found in Blueprint graph nodes referencing this asset."));
			return;
		}

		const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
		FString BaseUrl = Settings ? Settings->ServiceUrl : TEXT("http://127.0.0.1:1983");
		float TimeoutSecs = Settings && Settings->RequestTimeout > 0.0f ? Settings->RequestTimeout : 120.0f;

		UPackage* OuterPackage = Asset->GetOutermost();
		FString AssetFolderPath = FPaths::GetPath(OuterPackage->GetName());

		int32 TotalTasksCount = Asset->Languages.Num() * DiscoveredBlueprintLines.Num();
		TSharedPtr<int32> CompletedTasks = MakeShared<int32>(0);
		TSharedPtr<int32> FailedTasks = MakeShared<int32>(0);

		FNotificationInfo NotificationInfo(FText::Format(FText::FromString("PlayVoice: Pre-processing {0} dialogue lines..."), FText::AsNumber(DiscoveredBlueprintLines.Num())));
		NotificationInfo.bFireAndForget = false;
		NotificationInfo.bUseThrobber = true;
		NotificationInfo.bUseLargeFont = false;
		NotificationInfo.bUseSuccessFailIcons = true;
		NotificationInfo.FadeOutDuration = 0.5f;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}

		auto StepTaskProgress = [NotificationItem, CompletedTasks, FailedTasks, TotalTasksCount]()
		{
			(*CompletedTasks)++;
			if (NotificationItem.IsValid())
			{
				FText Msg = FText::Format(FText::FromString("PlayVoice: Pre-processing dialogue lines ({0}/{1})..."), FText::AsNumber(*CompletedTasks), FText::AsNumber(TotalTasksCount));
				NotificationItem->SetText(Msg);

				if (*CompletedTasks >= TotalTasksCount)
				{
					if (*FailedTasks > 0)
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
						NotificationItem->SetText(FText::Format(FText::FromString("PlayVoice: Voice line pre-processing finished with {0} errors."), FText::AsNumber(*FailedTasks)));
						UE_LOG(LogCharacterVoiceCustomization, Warning, TEXT("OnPrecacheLinesClicked: Completed line pre-processing with %d errors."), *FailedTasks);
					}
					else
					{
						NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
						NotificationItem->SetText(FText::FromString("PlayVoice: Voice line pre-processing complete!"));
						UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnPrecacheLinesClicked: Pre-processed all dialogue lines successfully!"));
					}
					NotificationItem->SetExpireDuration(4.0f);
					NotificationItem->ExpireAndFadeout();
				}
			}
		};

		for (const FCharacterLanguageData& LangData : Asset->Languages)
		{
			FString CurrentLangCode = LangData.LanguageCode;
			float CurrentSpeed = LangData.Speed;
			FString EmbeddingData = LangData.ToneColorEmbeddingData;

			for (const FString& LineText : DiscoveredBlueprintLines)
			{
				if (!Asset->bRegenerateExistingVoiceLines && Asset->HasPrecachedVoiceLine(LineText, CurrentLangCode))
				{
					UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnPrecacheLinesClicked: Skipping line '%s' (already precached)."), *LineText);
					StepTaskProgress();
					continue;
				}

				TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
				JsonObj->SetStringField(TEXT("character_name"), Asset->CharacterName.ToString());
				JsonObj->SetStringField(TEXT("text"), LineText);
				JsonObj->SetStringField(TEXT("language"), CurrentLangCode);
				JsonObj->SetNumberField(TEXT("speed"), CurrentSpeed);
				JsonObj->SetStringField(TEXT("embedding_data"), EmbeddingData);

				TArray<FString> RefAudioFiles = Asset->GetResolvedReferenceAudioFilesForLanguage(CurrentLangCode);
				TArray<TSharedPtr<FJsonValue>> RefPathValues;
				for (const FString& RefPath : RefAudioFiles)
				{
					RefPathValues.Add(MakeShared<FJsonValueString>(RefPath));
				}
				JsonObj->SetArrayField(TEXT("reference_audio_files"), RefPathValues);

				const FVoiceLineGuideTrack* GuideTrack = Asset->FindGuideTrackForLine(LineText, CurrentLangCode);
				FString GuideFile = Asset->GetResolvedGuideAudioFileForLine(LineText, CurrentLangCode);
				if (!GuideFile.IsEmpty())
				{
					JsonObj->SetStringField(TEXT("guide_audio_file"), GuideFile);
				}
				if (GuideTrack)
				{
					if (!GuideTrack->Emotion.IsEmpty())
					{
						JsonObj->SetStringField(TEXT("emotion"), GuideTrack->Emotion);
					}
					if (FMath::Abs(GuideTrack->Speed - 1.0f) > 0.01f)
					{
						JsonObj->SetNumberField(TEXT("speed"), CurrentSpeed * GuideTrack->Speed);
					}
				}

				FString PayloadStr;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadStr);
				FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
				HttpRequest->SetURL(BaseUrl + TEXT("/synthesize"));
				HttpRequest->SetVerb(TEXT("POST"));
				HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				HttpRequest->SetContentAsString(PayloadStr);
				HttpRequest->SetTimeout(TimeoutSecs);

				HttpRequest->OnProcessRequestComplete().BindLambda([WeakTargetAsset, LineText, CurrentLangCode, AssetFolderPath, StepTaskProgress, FailedTasks](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
				{
					bool bSynthOk = bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
					if (!bSynthOk)
					{
						(*FailedTasks)++;
						UE_LOG(LogCharacterVoiceCustomization, Error, TEXT("OnPrecacheLinesClicked: Failed to synthesize line '%s' for language '%s'."), *LineText, *CurrentLangCode);
					}

					if (bSynthOk && WeakTargetAsset.IsValid())
					{
						FString LineSanitized = FString::Printf(TEXT("SW_%s_%s_%u"), *WeakTargetAsset->CharacterName.ToString(), *CurrentLangCode, GetTypeHash(LineText));
						FString SoundWavePackagePath = AssetFolderPath / LineSanitized;

						UPackage* SoundWavePackage = CreatePackage(*SoundWavePackagePath);
						USoundWave* SoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(Response->GetContent(), SoundWavePackage, FName(*LineSanitized));

						if (SoundWave)
						{
							WeakTargetAsset->CacheVoiceLine(LineText, SoundWave, CurrentLangCode);
							SoundWave->MarkPackageDirty();
							WeakTargetAsset->MarkPackageDirty();

							if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
							{
								FAssetRegistryModule::AssetCreated(SoundWave);
							}
							UE_LOG(LogCharacterVoiceCustomization, Log, TEXT("OnPrecacheLinesClicked: Pre-rendered sound wave asset '%s' successfully."), *LineSanitized);
						}
					}

					StepTaskProgress();
				});

				HttpRequest->ProcessRequest();
			}
		}
	});

	return FReply::Handled();
}
