// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAsset.h"
#include "PlayVoiceAudioUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectGlobals.h"
#include "AssetRegistry/AssetRegistryModule.h"

UCharacterVoiceAsset::UCharacterVoiceAsset()
	: CharacterName(TEXT("NewCharacter"))
	, DefaultLanguage(TEXT("EN"))
{
	// Ensure at least one default language configuration exists
	FCharacterLanguageData DefaultData;
	DefaultData.LanguageCode = TEXT("EN");
	DefaultData.Speed = 1.0f;
	Languages.Add(DefaultData);
}

FCharacterLanguageData* UCharacterVoiceAsset::FindLanguageData(const FString& InLanguageCode)
{
	FString TargetLang = InLanguageCode.TrimStartAndEnd().ToUpper();
	if (TargetLang.IsEmpty())
	{
		TargetLang = DefaultLanguage.TrimStartAndEnd().ToUpper();
	}
	if (TargetLang.IsEmpty())
	{
		TargetLang = TEXT("EN");
	}

	for (FCharacterLanguageData& LangData : Languages)
	{
		if (LangData.LanguageCode.Equals(TargetLang, ESearchCase::IgnoreCase))
		{
			return &LangData;
		}
	}

	if (Languages.Num() > 0)
	{
		return &Languages[0];
	}

	return nullptr;
}

const FCharacterLanguageData* UCharacterVoiceAsset::FindLanguageData(const FString& InLanguageCode) const
{
	FString TargetLang = InLanguageCode.TrimStartAndEnd().ToUpper();
	if (TargetLang.IsEmpty())
	{
		TargetLang = DefaultLanguage.TrimStartAndEnd().ToUpper();
	}
	if (TargetLang.IsEmpty())
	{
		TargetLang = TEXT("EN");
	}

	for (const FCharacterLanguageData& LangData : Languages)
	{
		if (LangData.LanguageCode.Equals(TargetLang, ESearchCase::IgnoreCase))
		{
			return &LangData;
		}
	}

	if (Languages.Num() > 0)
	{
		return &Languages[0];
	}

	return nullptr;
}

FCharacterLanguageData& UCharacterVoiceAsset::GetOrAddLanguageData(const FString& InLanguageCode)
{
	FString TargetLang = InLanguageCode.TrimStartAndEnd().ToUpper();
	if (TargetLang.IsEmpty())
	{
		TargetLang = DefaultLanguage.TrimStartAndEnd().ToUpper();
	}
	if (TargetLang.IsEmpty())
	{
		TargetLang = TEXT("EN");
	}

	for (FCharacterLanguageData& LangData : Languages)
	{
		if (LangData.LanguageCode.Equals(TargetLang, ESearchCase::IgnoreCase))
		{
			return LangData;
		}
	}

	FCharacterLanguageData NewLangData;
	NewLangData.LanguageCode = TargetLang;
	NewLangData.Speed = 1.0f;
	int32 Index = Languages.Add(NewLangData);
	return Languages[Index];
}

FString UCharacterVoiceAsset::GetResolvedTextLineForEntry(const FPlayVoiceLineEntry& Entry) const
{
	if (Entry.StringTable && !Entry.Key.IsNone())
	{
		FStringTableConstRef TableRef = Entry.StringTable->GetStringTable();
		FStringTableEntryConstPtr TableEntry = TableRef->FindEntry(FTextKey(Entry.Key.ToString()));
		if (TableEntry.IsValid())
		{
			return TableEntry->GetSourceString();
		}
	}

	if (!Entry.TextLine.IsEmpty())
	{
		return Entry.TextLine;
	}

	return Entry.Key.IsNone() ? FString() : Entry.Key.ToString();
}

const FPlayVoiceLineEntry* UCharacterVoiceAsset::FindVoiceLineByKey(FName InKey) const
{
	if (InKey.IsNone())
	{
		return nullptr;
	}

	for (const FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		if (Entry.Key == InKey)
		{
			return &Entry;
		}
	}
	return nullptr;
}

bool UCharacterVoiceAsset::HasVoiceLineForKey(FName InKey) const
{
	return FindVoiceLineByKey(InKey) != nullptr;
}

FString UCharacterVoiceAsset::GetVoiceRecordingFolderOnDisk() const
{
	FString AssetFolder = GetAssetDiskFolder();
	FString RecordingFolder = FPaths::Combine(AssetFolder, TEXT("VoiceRecording"));
	IFileManager::Get().MakeDirectory(*RecordingFolder, true);
	return RecordingFolder;
}

void UCharacterVoiceAsset::PostLoad()
{
	Super::PostLoad();
	FixupVoiceLineAudioReferences();
}

void UCharacterVoiceAsset::FixupVoiceLineAudioReferences()
{
	FString TargetRecordingFolder = GetVoiceRecordingFolderOnDisk();

	for (FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		// 1. Resolve TextLine from String Table if not set
		if (Entry.StringTable && !Entry.Key.IsNone())
		{
			FStringTableConstRef TableRef = Entry.StringTable->GetStringTable();
			FStringTableEntryConstPtr TableEntry = TableRef->FindEntry(FTextKey(Entry.Key.ToString()));
			if (TableEntry.IsValid())
			{
				Entry.TextLine = TableEntry->GetSourceString();
			}
		}

		// 2. Fixup Guide Track audio file paths if recorded or moved
		FString RawPath = Entry.AudioFile.FilePath.TrimStartAndEnd();
		if (!RawPath.IsEmpty())
		{
			FString CleanFilename = FPaths::GetCleanFilename(RawPath);
			if (CleanFilename.StartsWith(TEXT("REC_")))
			{
				FString DesiredPath = FPaths::Combine(TargetRecordingFolder, CleanFilename);
				if (RawPath != DesiredPath)
				{
					if (IFileManager::Get().FileExists(*RawPath) && !IFileManager::Get().FileExists(*DesiredPath))
					{
						IFileManager::Get().Move(*DesiredPath, *RawPath);
					}

					if (IFileManager::Get().FileExists(*DesiredPath))
					{
						Entry.AudioFile.FilePath = DesiredPath;
					}
				}
			}
		}
	}

	AutoLinkPrecachedSoundWaves();
}

#if WITH_EDITOR
void UCharacterVoiceAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FixupVoiceLineAudioReferences();
}
#endif

static FString CleanVoiceLineTextForCache(const FString& InRaw)
{
	FString S = InRaw.TrimStartAndEnd();
	if (S.IsEmpty())
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
				S = S.Mid(StartQuote + 1, EndQuote - StartQuote - 1).TrimStartAndEnd();
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
				S = S.Mid(LastQuoteStart + 1, LastQuoteEnd - LastQuoteStart - 1).TrimStartAndEnd();
			}
		}
	}

	// Strip enclosing double quotes
	if (S.StartsWith(TEXT("\"")) && S.EndsWith(TEXT("\"")) && S.Len() >= 2)
	{
		S = S.Mid(1, S.Len() - 2);
	}

	return S.TrimStartAndEnd().ToLower();
}

FString UCharacterVoiceAsset::MakeCacheKey(const FString& TextLine, const FString& LanguageCode)
{
	FString CleanText = CleanVoiceLineTextForCache(TextLine);
	FString CleanLang = LanguageCode.TrimStartAndEnd().ToUpper();
	if (CleanLang.IsEmpty())
	{
		CleanLang = TEXT("EN");
	}
	return FString::Printf(TEXT("%s:%s"), *CleanLang, *CleanText);
}

FString UCharacterVoiceAsset::MakeKeyCacheKey(FName Key, const FString& LanguageCode)
{
	FString CleanKey = Key.IsNone() ? FString() : Key.ToString().ToLower();
	FString CleanLang = LanguageCode.TrimStartAndEnd().ToUpper();
	if (CleanLang.IsEmpty())
	{
		CleanLang = TEXT("EN");
	}
	return FString::Printf(TEXT("%s:KEY:%s"), *CleanLang, *CleanKey);
}

void UCharacterVoiceAsset::ClearPrecachedVoiceLines()
{
	for (FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		Entry.PrecachedSoundWave = nullptr;
	}
}

void UCharacterVoiceAsset::CacheVoiceLineForKey(FName Key, USoundWave* InSoundWave, const FString& LanguageCode)
{
	if (!Key.IsNone() && InSoundWave)
	{
		for (FPlayVoiceLineEntry& Entry : VoiceLines)
		{
			if (Entry.Key == Key)
			{
				Entry.PrecachedSoundWave = InSoundWave;
				return;
			}
		}
	}
}

USoundWave* UCharacterVoiceAsset::GetPrecachedVoiceLineForKey(FName Key, const FString& LanguageCode) const
{
	if (Key.IsNone())
	{
		return nullptr;
	}

	if (const FPlayVoiceLineEntry* Entry = FindVoiceLineByKey(Key))
	{
		return Entry->PrecachedSoundWave.Get();
	}
	return nullptr;
}

bool UCharacterVoiceAsset::HasPrecachedVoiceLineForKey(FName Key, const FString& LanguageCode) const
{
	return GetPrecachedVoiceLineForKey(Key, LanguageCode) != nullptr;
}

void UCharacterVoiceAsset::CacheVoiceLine(const FString& TextLine, USoundWave* InSoundWave, const FString& LanguageCode)
{
	if (!TextLine.IsEmpty() && InSoundWave)
	{
		FString TargetText = TextLine.TrimStartAndEnd();
		for (FPlayVoiceLineEntry& Entry : VoiceLines)
		{
			FString EntryText = GetResolvedTextLineForEntry(Entry);
			if (EntryText.Equals(TargetText, ESearchCase::IgnoreCase))
			{
				Entry.PrecachedSoundWave = InSoundWave;
				return;
			}
		}
	}
}

USoundWave* UCharacterVoiceAsset::GetPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode) const
{
	FString TargetText = TextLine.TrimStartAndEnd();
	if (TargetText.IsEmpty())
	{
		return nullptr;
	}

	for (const FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		FString EntryText = GetResolvedTextLineForEntry(Entry);
		if (EntryText.Equals(TargetText, ESearchCase::IgnoreCase))
		{
			return Entry.PrecachedSoundWave.Get();
		}
	}

	return nullptr;
}

bool UCharacterVoiceAsset::HasPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode) const
{
	return GetPrecachedVoiceLine(TextLine, LanguageCode) != nullptr;
}

FString UCharacterVoiceAsset::GetAssetDiskFolder() const
{
	UPackage* Package = GetOutermost();
	if (Package && Package != GetTransientPackage())
	{
		FString PackagePath = Package->GetName();
		FString AssetFolder = FPaths::GetPath(PackagePath);
		FString DiskFolder = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(AssetFolder, TEXT("")));
		if (!DiskFolder.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*DiskFolder, true);
			return DiskFolder;
		}
	}
	FString SavedDir = FPaths::ProjectSavedDir() / TEXT("PlayVoice");
	IFileManager::Get().MakeDirectory(*SavedDir, true);
	return SavedDir;
}

FString UCharacterVoiceAsset::ResolveFolderPathToDisk(const FString& InFolderPath)
{
	FString CleanFolder = InFolderPath.TrimStartAndEnd();
	if (CleanFolder.IsEmpty())
	{
		return FString();
	}

	if (CleanFolder.StartsWith(TEXT("/Game/")))
	{
		FString RelPath = CleanFolder.RightChop(6);
		FString FullPath = FPaths::Combine(FPaths::ProjectContentDir(), RelPath);
		return FPaths::ConvertRelativePathToFull(FullPath);
	}
	if (CleanFolder.StartsWith(TEXT("/Engine/")))
	{
		FString RelPath = CleanFolder.RightChop(8);
		FString FullPath = FPaths::Combine(FPaths::EngineContentDir(), RelPath);
		return FPaths::ConvertRelativePathToFull(FullPath);
	}

	if (!FPaths::IsRelative(CleanFolder))
	{
		return FPaths::ConvertRelativePathToFull(CleanFolder);
	}

	if (CleanFolder.StartsWith(TEXT("Content/")) || CleanFolder.StartsWith(TEXT("Content\\")))
	{
		FString RelPath = CleanFolder.RightChop(8);
		FString FullPath = FPaths::Combine(FPaths::ProjectContentDir(), RelPath);
		return FPaths::ConvertRelativePathToFull(FullPath);
	}

	FString ContentRelativePath = FPaths::Combine(FPaths::ProjectContentDir(), CleanFolder);
	if (IFileManager::Get().DirectoryExists(*ContentRelativePath))
	{
		return FPaths::ConvertRelativePathToFull(ContentRelativePath);
	}

	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), CleanFolder);
}

TArray<FString> UCharacterVoiceAsset::ResolveAudioFilesFromFolderAndFiles(const TArray<FFilePath>& FilePaths, const FDirectoryPath& FolderPath)
{
	TArray<FString> ResolvedFiles;

	if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::Get().LoadModule("AssetRegistry");
	}

	for (const FFilePath& FilePath : FilePaths)
	{
		if (!FilePath.FilePath.IsEmpty())
		{
			FString FullPath = FilePath.FilePath.TrimStartAndEnd();
			if (FPaths::IsRelative(FullPath) && !FullPath.StartsWith(TEXT("/Game/")) && !FullPath.StartsWith(TEXT("/Engine/")))
			{
				FullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FullPath);
			}

			if (IFileManager::Get().FileExists(*FullPath))
			{
				ResolvedFiles.AddUnique(FullPath);
			}
			else if (FullPath.StartsWith(TEXT("/Game/")) || FullPath.StartsWith(TEXT("/Engine/")))
			{
				FString PkgName = FPackageName::ObjectPathToPackageName(FullPath);
				if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
				{
					FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					TArray<FAssetData> AssetList;
					AssetRegistryModule.Get().GetAssetsByPackageName(FName(*PkgName), AssetList);
					for (const FAssetData& AssetData : AssetList)
					{
						if (AssetData.AssetClassPath.GetAssetName() == FName(TEXT("SoundWave")))
						{
							USoundWave* LoadedWave = Cast<USoundWave>(AssetData.GetAsset());
							if (LoadedWave)
							{
								FString ExportedWav = UPlayVoiceAudioUtils::ExportSoundWaveToTempWAVFile(LoadedWave);
								if (!ExportedWav.IsEmpty())
								{
									ResolvedFiles.AddUnique(ExportedWav);
								}
							}
						}
					}
				}
			}
		}
	}

	if (!FolderPath.Path.IsEmpty())
	{
		FString FolderFullPath = ResolveFolderPathToDisk(FolderPath.Path);
		if (!FolderFullPath.IsEmpty() && IFileManager::Get().DirectoryExists(*FolderFullPath))
		{
			TArray<FString> FoundFiles;
			// Pass false for bClearFileResults (6th parameter) to accumulate files
			IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.wav"), true, false, false);
			IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.mp3"), true, false, false);
			IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.flac"), true, false, false);
			IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.ogg"), true, false, false);
			IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.aiff"), true, false, false);
			IFileManager::Get().FindFilesRecursive(FoundFiles, *FolderFullPath, TEXT("*.wma"), true, false, false);

			for (const FString& FoundFile : FoundFiles)
			{
				FString CleanFilename = FPaths::GetCleanFilename(FoundFile);
				FString CleanPath = FPaths::ConvertRelativePathToFull(FoundFile);

				// Exclude guide tracks (REC_... or VoiceRecording folder) and precached sound wave files (SW_...)
				if (CleanFilename.StartsWith(TEXT("REC_")) || CleanFilename.StartsWith(TEXT("SW_")) || CleanPath.Contains(TEXT("VoiceRecording")))
				{
					continue;
				}

				if (IFileManager::Get().FileExists(*FoundFile))
				{
					ResolvedFiles.AddUnique(FoundFile);
				}
			}

			// Search for imported Unreal Engine .uasset files in the directory by checking AssetRegistry class name first
			TArray<FString> FoundUAssets;
			IFileManager::Get().FindFilesRecursive(FoundUAssets, *FolderFullPath, TEXT("*.uasset"), true, false, false);
			for (const FString& UAssetFile : FoundUAssets)
			{
				FString CleanFilename = FPaths::GetCleanFilename(UAssetFile);
				FString CleanPath = FPaths::ConvertRelativePathToFull(UAssetFile);

				// Exclude guide tracks (REC_... or VoiceRecording folder) and precached sound wave files (SW_...)
				if (CleanFilename.StartsWith(TEXT("REC_")) || CleanFilename.StartsWith(TEXT("SW_")) || CleanPath.Contains(TEXT("VoiceRecording")))
				{
					continue;
				}

				FString PackageName;
				if (FPackageName::TryConvertFilenameToLongPackageName(UAssetFile, PackageName))
				{
					if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
					{
						FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
						TArray<FAssetData> AssetList;
						AssetRegistryModule.Get().GetAssetsByPackageName(FName(*PackageName), AssetList);
						for (const FAssetData& AssetData : AssetList)
						{
							if (AssetData.AssetClassPath.GetAssetName() == FName(TEXT("SoundWave")))
							{
								USoundWave* LoadedWave = Cast<USoundWave>(AssetData.GetAsset());
								if (LoadedWave)
								{
									FString ExportedWav = UPlayVoiceAudioUtils::ExportSoundWaveToTempWAVFile(LoadedWave);
									if (!ExportedWav.IsEmpty())
									{
										ResolvedFiles.AddUnique(ExportedWav);
									}
								}
							}
						}
					}
				}
			}
		}

		// Also check AssetRegistry for USoundWave assets if FolderPath is a /Game/ package path
		FString CleanPath = FolderPath.Path.TrimStartAndEnd();
		if (CleanPath.StartsWith(TEXT("/Game")))
		{
			if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				TArray<FAssetData> AssetList;
				AssetRegistryModule.Get().GetAssetsByPath(FName(*CleanPath), AssetList, true);

				for (const FAssetData& AssetData : AssetList)
				{
					if (AssetData.AssetClassPath.GetAssetName() == FName(TEXT("SoundWave")))
					{
						USoundWave* LoadedWave = Cast<USoundWave>(AssetData.GetAsset());
						if (LoadedWave)
						{
							FString ExportedWav = UPlayVoiceAudioUtils::ExportSoundWaveToTempWAVFile(LoadedWave);
							if (!ExportedWav.IsEmpty())
							{
								ResolvedFiles.AddUnique(ExportedWav);
							}
						}
					}
				}
			}
		}
	}

	return ResolvedFiles;
}


TArray<FString> UCharacterVoiceAsset::GetResolvedReferenceAudioFilesForLanguage(const FString& LanguageCode) const
{
	const FCharacterLanguageData* LangData = FindLanguageData(LanguageCode);
	if (LangData)
	{
		return ResolveAudioFilesFromFolderAndFiles(LangData->ReferenceAudioFiles, LangData->ReferenceAudioFolder);
	}
	return TArray<FString>();
}

TArray<FString> UCharacterVoiceAsset::GetResolvedReferenceAudioFiles() const
{
	TArray<FString> AllResolvedFiles;
	for (const FCharacterLanguageData& LangData : Languages)
	{
		TArray<FString> FilesForLang = ResolveAudioFilesFromFolderAndFiles(LangData.ReferenceAudioFiles, LangData.ReferenceAudioFolder);
		for (const FString& FilePath : FilesForLang)
		{
			AllResolvedFiles.AddUnique(FilePath);
		}
	}
	return AllResolvedFiles;
}

bool UCharacterVoiceAsset::SaveModelToFile(const FString& InFilePath, const FString& LanguageCode)
{
	FCharacterLanguageData* LangData = FindLanguageData(LanguageCode);
	if (!LangData)
	{
		return false;
	}

	FString TargetPath = InFilePath;
	if (TargetPath.IsEmpty())
	{
		FString TargetDir = GetAssetDiskFolder();
		TargetPath = TargetDir / FString::Printf(TEXT("%s_%s_se.json"), *CharacterName.ToString(), *LangData->LanguageCode);
	}

	if (FFileHelper::SaveStringToFile(LangData->ToneColorEmbeddingData, *TargetPath))
	{
		LangData->ModelCheckpointPath = TargetPath;
		return true;
	}
	return false;
}

bool UCharacterVoiceAsset::LoadModelFromFile(const FString& InFilePath, const FString& LanguageCode)
{
	FCharacterLanguageData* LangData = FindLanguageData(LanguageCode);
	if (!LangData)
	{
		return false;
	}

	FString TargetPath = InFilePath.IsEmpty() ? LangData->ModelCheckpointPath : InFilePath;
	if (TargetPath.IsEmpty())
	{
		return false;
	}

	FString LoadedData;
	if (FFileHelper::LoadFileToString(LoadedData, *TargetPath))
	{
		LangData->ToneColorEmbeddingData = LoadedData;
		LangData->ModelCheckpointPath = TargetPath;
		LangData->bIsModelGenerated = !LangData->ToneColorEmbeddingData.IsEmpty();
		return true;
	}
	return false;
}

void UCharacterVoiceAsset::AutoLinkPrecachedSoundWaves()
{
	UPackage* Package = GetOutermost();
	if (!Package || Package == GetTransientPackage())
	{
		return;
	}

	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(Package, SubObjects, EGetObjectsFlags::None);

	for (UObject* Obj : SubObjects)
	{
		if (USoundWave* SoundWave = Cast<USoundWave>(Obj))
		{
			FString SoundName = SoundWave->GetName();
			for (FPlayVoiceLineEntry& Entry : VoiceLines)
			{
				if (Entry.PrecachedSoundWave == nullptr && !Entry.Key.IsNone())
				{
					if (SoundName.Contains(Entry.Key.ToString()))
					{
						Entry.PrecachedSoundWave = SoundWave;
						break;
					}
				}
			}
		}
	}
}

FString UCharacterVoiceAsset::GetLanguage() const
{
	const FCharacterLanguageData* LangData = FindLanguageData();
	return LangData ? LangData->LanguageCode : DefaultLanguage;
}

float UCharacterVoiceAsset::GetSpeed(const FString& LanguageCode) const
{
	const FCharacterLanguageData* LangData = FindLanguageData(LanguageCode);
	return LangData ? LangData->Speed : 1.0f;
}

bool UCharacterVoiceAsset::IsModelGenerated(const FString& LanguageCode) const
{
	const FCharacterLanguageData* LangData = FindLanguageData(LanguageCode);
	return LangData ? LangData->bIsModelGenerated : false;
}
