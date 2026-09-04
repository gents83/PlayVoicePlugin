// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CharacterVoiceAsset.h"
#include "PlayVoiceAudioUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectGlobals.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif
#include "Misc/Crc.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharacterVoiceAsset, Log, All);

static FString MakeSafePackageComponent(const FString& Value)
{
	FString SafeValue;
	for (const TCHAR Character : Value.TrimStartAndEnd())
	{
		SafeValue.AppendChar(FChar::IsAlnum(Character) || Character == TCHAR('_') ? Character : TCHAR('_'));
	}
	return SafeValue.IsEmpty() ? TEXT("Voice") : SafeValue;
}

static bool IsPathUnderDirectory(const FString& Path, const FString& Directory)
{
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(Path);
	FString NormalizedDirectory = FPaths::ConvertRelativePathToFull(Directory);
	FPaths::NormalizeFilename(NormalizedPath);
	FPaths::NormalizeFilename(NormalizedDirectory);
	if (!NormalizedDirectory.EndsWith(TEXT("/")))
	{
		NormalizedDirectory += TEXT("/");
	}
	return NormalizedPath.StartsWith(NormalizedDirectory, ESearchCase::IgnoreCase);
}

static uint32 MakeVoiceLineIdentityHash(FName StringTableId, FName Key, const FString& LanguageCode)
{
	const FString Identity = StringTableId.ToString().ToLower() + TEXT("|") + Key.ToString().ToLower() + TEXT("|") + LanguageCode.ToUpper();
	return FCrc::StrCrc32(*Identity);
}

static FString NormalizeLanguageCode(const FString& LanguageCode, const FString& DefaultLanguage = TEXT("EN"))
{
	FString Normalized = LanguageCode.TrimStartAndEnd().ToUpper();
	if (Normalized.IsEmpty())
	{
		Normalized = DefaultLanguage.TrimStartAndEnd().ToUpper();
	}
	return Normalized.IsEmpty() ? TEXT("EN") : Normalized;
}

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

	const FPlayVoiceLineEntry* MatchingEntry = nullptr;
	for (const FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		if (Entry.Key == InKey)
		{
			if (MatchingEntry)
			{
				return nullptr;
			}
			MatchingEntry = &Entry;
		}
	}
	return MatchingEntry;
}

const FPlayVoiceLineEntry* UCharacterVoiceAsset::FindVoiceLineByStringTableIdAndKey(FName StringTableId, const FString& Key) const
{
	if (StringTableId.IsNone() || Key.IsEmpty())
	{
		return nullptr;
	}

	const FPlayVoiceLineEntry* MatchingEntry = nullptr;
	for (const FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		const FName EntryTableId = Entry.StringTable ? Entry.StringTable->GetStringTableId() : Entry.StringTableId;
		if (EntryTableId == StringTableId && Entry.Key.ToString() == Key)
		{
			if (MatchingEntry)
			{
				return nullptr;
			}
			MatchingEntry = &Entry;
		}
	}
	return MatchingEntry;
}

USoundWave* UCharacterVoiceAsset::GetPrecachedVoiceLineForStringTableIdAndKey(FName StringTableId, const FString& Key, const FString& LanguageCode) const
{
	const FPlayVoiceLineEntry* Entry = FindVoiceLineByStringTableIdAndKey(StringTableId, Key);
	if (!Entry)
	{
		return nullptr;
	}

	const FString NormalizedLanguage = NormalizeLanguageCode(LanguageCode, DefaultLanguage);
	if (const TObjectPtr<USoundWave>* CachedSound = Entry->PrecachedSoundWavesByLanguage.Find(NormalizedLanguage))
	{
		return CachedSound->Get();
	}
	if (NormalizedLanguage == NormalizeLanguageCode(DefaultLanguage))
	{
		return Entry->PrecachedSoundWave.Get();
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
	return FPaths::Combine(AssetFolder, TEXT("VoiceRecording"));
}

void UCharacterVoiceAsset::PostLoad()
{
	Super::PostLoad();
}

void UCharacterVoiceAsset::FixupVoiceLineAudioReferences()
{
#if !WITH_EDITOR
	return;
#else
	const FString TargetRecordingFolder = GetVoiceRecordingFolderOnDisk();
	IFileManager::Get().MakeDirectory(*TargetRecordingFolder, true);

	for (FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		if (Entry.StringTableId.IsNone() && Entry.StringTable)
		{
			Entry.StringTableId = Entry.StringTable->GetStringTableId();
		}

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

		// Migrate the legacy pointer to the default language cache entry.
		if (Entry.PrecachedSoundWave)
		{
			const FString DefaultLang = NormalizeLanguageCode(DefaultLanguage);
			if (!Entry.PrecachedSoundWavesByLanguage.Contains(DefaultLang))
			{
				Entry.PrecachedSoundWavesByLanguage.Add(DefaultLang, Entry.PrecachedSoundWave);
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
					else if (!IFileManager::Get().FileExists(*RawPath))
					{
						const int32 TimestampSeparator = CleanFilename.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
						const FString RecordingPrefix = TimestampSeparator != INDEX_NONE ? CleanFilename.Left(TimestampSeparator + 1) : CleanFilename;
						TArray<FString> CandidateFiles;
						IFileManager::Get().FindFilesRecursive(CandidateFiles, *TargetRecordingFolder, TEXT("*.wav"), true, false, false);

						FString LatestCandidate;
						FDateTime LatestTimestamp = FDateTime::MinValue();
						for (const FString& CandidateFile : CandidateFiles)
						{
							const FString CandidateFilename = FPaths::GetCleanFilename(CandidateFile);
							const FDateTime CandidateTimestamp = IFileManager::Get().GetTimeStamp(*CandidateFile);
							if (CandidateFilename.StartsWith(RecordingPrefix) && CandidateTimestamp > LatestTimestamp)
							{
								LatestCandidate = CandidateFile;
								LatestTimestamp = CandidateTimestamp;
							}
						}

						if (!LatestCandidate.IsEmpty())
						{
							Entry.AudioFile.FilePath = LatestCandidate;
							UE_LOG(LogCharacterVoiceAsset, Log, TEXT("Recovered missing guide track reference '%s' as '%s'."), *RawPath, *LatestCandidate);
						}
					}
				}
			}
		}
	}

	AutoLinkPrecachedSoundWaves();
#endif
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

FString UCharacterVoiceAsset::MakeStringTableKeyCacheKey(FName StringTableId, const FString& Key, const FString& LanguageCode)
{
	FString CleanTableId = StringTableId.IsNone() ? FString() : StringTableId.ToString().ToLower();
	FString CleanKey = Key.TrimStartAndEnd().ToLower();
	FString CleanLang = LanguageCode.TrimStartAndEnd().ToUpper();
	if (CleanLang.IsEmpty())
	{
		CleanLang = TEXT("EN");
	}
	return FString::Printf(TEXT("%s:%s:%s"), *CleanLang, *CleanTableId, *CleanKey);
}

void UCharacterVoiceAsset::ClearPrecachedVoiceLines()
{
	for (FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		Entry.PrecachedSoundWave = nullptr;
		Entry.PrecachedSoundWavesByLanguage.Empty();
	}
}

void UCharacterVoiceAsset::CacheVoiceLineForKey(FName Key, USoundWave* InSoundWave, const FString& LanguageCode)
{
	if (Key.IsNone() || !InSoundWave)
	{
		return;
	}

	FPlayVoiceLineEntry* MatchingEntry = nullptr;
	for (FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		if (Entry.Key == Key)
		{
			if (MatchingEntry)
			{
				return;
			}
			MatchingEntry = &Entry;
		}
	}

	if (MatchingEntry)
	{
		const FString NormalizedLanguage = NormalizeLanguageCode(LanguageCode, DefaultLanguage);
		MatchingEntry->PrecachedSoundWavesByLanguage.Add(NormalizedLanguage, InSoundWave);
		if (NormalizedLanguage == NormalizeLanguageCode(DefaultLanguage))
		{
			MatchingEntry->PrecachedSoundWave = InSoundWave;
		}
	}
}

void UCharacterVoiceAsset::CacheVoiceLineForStringTableIdAndKey(FName StringTableId, const FString& Key, USoundWave* InSoundWave, const FString& LanguageCode)
{
	if (StringTableId.IsNone() || Key.IsEmpty() || !InSoundWave)
	{
		return;
	}

	FPlayVoiceLineEntry* MatchingEntry = nullptr;
	for (FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		const FName EntryTableId = Entry.StringTable ? Entry.StringTable->GetStringTableId() : Entry.StringTableId;
		if (EntryTableId == StringTableId && Entry.Key.ToString() == Key)
		{
			if (MatchingEntry)
			{
				return;
			}
			MatchingEntry = &Entry;
		}
	}

	if (MatchingEntry)
	{
		const FString NormalizedLanguage = NormalizeLanguageCode(LanguageCode, DefaultLanguage);
		MatchingEntry->PrecachedSoundWavesByLanguage.Add(NormalizedLanguage, InSoundWave);
		if (NormalizedLanguage == NormalizeLanguageCode(DefaultLanguage))
		{
			MatchingEntry->PrecachedSoundWave = InSoundWave;
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
		const FString NormalizedLanguage = NormalizeLanguageCode(LanguageCode, DefaultLanguage);
		if (const TObjectPtr<USoundWave>* CachedSound = Entry->PrecachedSoundWavesByLanguage.Find(NormalizedLanguage))
		{
			return CachedSound->Get();
		}
		if (NormalizedLanguage == NormalizeLanguageCode(DefaultLanguage))
		{
			return Entry->PrecachedSoundWave.Get();
		}
	}
	return nullptr;
}

bool UCharacterVoiceAsset::HasPrecachedVoiceLineForKey(FName Key, const FString& LanguageCode) const
{
	return GetPrecachedVoiceLineForKey(Key, LanguageCode) != nullptr;
}

void UCharacterVoiceAsset::CacheVoiceLine(const FString& TextLine, USoundWave* InSoundWave, const FString& LanguageCode)
{
	if (TextLine.IsEmpty() || !InSoundWave)
	{
		return;
	}

	const FString TargetText = TextLine.TrimStartAndEnd();
	FPlayVoiceLineEntry* MatchingEntry = nullptr;
	for (FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		if (GetResolvedTextLineForEntry(Entry).Equals(TargetText, ESearchCase::IgnoreCase))
		{
			if (MatchingEntry)
			{
				return;
			}
			MatchingEntry = &Entry;
		}
	}

	if (MatchingEntry)
	{
		const FString NormalizedLanguage = NormalizeLanguageCode(LanguageCode, DefaultLanguage);
		MatchingEntry->PrecachedSoundWavesByLanguage.Add(NormalizedLanguage, InSoundWave);
		if (NormalizedLanguage == NormalizeLanguageCode(DefaultLanguage))
		{
			MatchingEntry->PrecachedSoundWave = InSoundWave;
		}
	}
}

USoundWave* UCharacterVoiceAsset::GetPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode) const
{
	const FString TargetText = TextLine.TrimStartAndEnd();
	if (TargetText.IsEmpty())
	{
		return nullptr;
	}

	const FPlayVoiceLineEntry* MatchingEntry = nullptr;
	for (const FPlayVoiceLineEntry& Entry : VoiceLines)
	{
		if (GetResolvedTextLineForEntry(Entry).Equals(TargetText, ESearchCase::IgnoreCase))
		{
			if (MatchingEntry)
			{
				return nullptr;
			}
			MatchingEntry = &Entry;
		}
	}

	if (!MatchingEntry)
	{
		return nullptr;
	}

	const FString NormalizedLanguage = NormalizeLanguageCode(LanguageCode, DefaultLanguage);
	if (const TObjectPtr<USoundWave>* CachedSound = MatchingEntry->PrecachedSoundWavesByLanguage.Find(NormalizedLanguage))
	{
		return CachedSound->Get();
	}
	if (NormalizedLanguage == NormalizeLanguageCode(DefaultLanguage))
	{
		return MatchingEntry->PrecachedSoundWave.Get();
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
		const FString PackagePath = Package->GetName();
		const FString AssetFolder = FPaths::GetPath(PackagePath);
		const FString DiskFolder = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(AssetFolder, TEXT("")));
		if (!DiskFolder.IsEmpty())
		{
			return DiskFolder;
		}
	}
	return FPaths::ProjectSavedDir() / TEXT("PlayVoice");
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

FString UCharacterVoiceAsset::ResolveAudioFilePath(const FString& InFilePath)
{
#if !WITH_EDITOR
	return FString();
#else
	FString CleanPath = InFilePath.TrimStartAndEnd();
	if (CleanPath.IsEmpty())
	{
		return FString();
	}

	if (CleanPath.StartsWith(TEXT("/Game/")) || CleanPath.StartsWith(TEXT("/Engine/")))
	{
		if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
		{
			FModuleManager::Get().LoadModule("AssetRegistry");
		}

		const FString PackageName = FPackageName::ObjectPathToPackageName(CleanPath);
		if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> AssetList;
			AssetRegistryModule.Get().GetAssetsByPackageName(FName(*PackageName), AssetList);
			for (const FAssetData& AssetData : AssetList)
			{
				if (AssetData.AssetClassPath.GetAssetName() == FName(TEXT("SoundWave")))
				{
					if (USoundWave* SoundWave = Cast<USoundWave>(AssetData.GetAsset()))
					{
						return UPlayVoiceAudioUtils::ExportSoundWaveToTempWAVFile(SoundWave);
					}
				}
			}
		}
		return FString();
	}

	if (!FPaths::IsRelative(CleanPath))
	{
		return FPaths::ConvertRelativePathToFull(CleanPath);
	}

	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), CleanPath);
#endif
}

TArray<FString> UCharacterVoiceAsset::ResolveAudioFilesFromFolderAndFiles(const TArray<FFilePath>& FilePaths, const FDirectoryPath& FolderPath)
{
#if !WITH_EDITOR
	return TArray<FString>();
#else
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
#endif
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
#if !WITH_EDITOR
	return false;
#else
	FCharacterLanguageData* LangData = FindLanguageData(LanguageCode);
	if (!LangData)
	{
		return false;
	}

	TSharedPtr<FJsonObject> EmbeddingObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LangData->ToneColorEmbeddingData);
	if (!FJsonSerializer::Deserialize(Reader, EmbeddingObject) || !EmbeddingObject.IsValid()
		|| !EmbeddingObject->HasTypedField<EJson::Array>(TEXT("target_se"))
		|| EmbeddingObject->GetArrayField(TEXT("target_se")).Num() == 0)
	{
		UE_LOG(LogCharacterVoiceAsset, Warning, TEXT("SaveModelToFile: No valid target_se embedding is available for language '%s'."), *LanguageCode);
		return false;
	}

	FString TargetPath = InFilePath;
	if (TargetPath.IsEmpty())
	{
		FString TargetDir = GetAssetDiskFolder();
		TargetPath = TargetDir / FString::Printf(TEXT("%s_%s_se.json"), *CharacterName.ToString(), *LangData->LanguageCode);
	}

	TargetPath = FPaths::ConvertRelativePathToFull(TargetPath);
	const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!IsPathUnderDirectory(TargetPath, ProjectDirectory))
	{
		UE_LOG(LogCharacterVoiceAsset, Warning, TEXT("SaveModelToFile: Refusing to write outside the project directory: '%s'."), *TargetPath);
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TargetPath), true);
	if (FFileHelper::SaveStringToFile(LangData->ToneColorEmbeddingData, *TargetPath))
	{
		LangData->ModelCheckpointPath = FPaths::ConvertRelativePathToFull(TargetPath);
		MarkPackageDirty();
		return true;
	}
	return false;
#endif
}

bool UCharacterVoiceAsset::LoadModelFromFile(const FString& InFilePath, const FString& LanguageCode)
{
#if !WITH_EDITOR
	return false;
#else
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
	TargetPath = FPaths::ConvertRelativePathToFull(TargetPath);
	const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!IsPathUnderDirectory(TargetPath, ProjectDirectory))
	{
		UE_LOG(LogCharacterVoiceAsset, Warning, TEXT("LoadModelFromFile: Refusing to read outside the project directory: '%s'."), *TargetPath);
		return false;
	}

	FString LoadedData;
	if (FFileHelper::LoadFileToString(LoadedData, *TargetPath))
	{
		TSharedPtr<FJsonObject> EmbeddingObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LoadedData);
		if (!FJsonSerializer::Deserialize(Reader, EmbeddingObject) || !EmbeddingObject.IsValid()
			|| !EmbeddingObject->HasTypedField<EJson::Array>(TEXT("target_se"))
			|| EmbeddingObject->GetArrayField(TEXT("target_se")).Num() == 0)
		{
			return false;
		}
		LangData->ToneColorEmbeddingData = LoadedData;
		LangData->ModelCheckpointPath = FPaths::ConvertRelativePathToFull(TargetPath);
		LangData->bIsModelGenerated = true;
		MarkPackageDirty();
		return true;
	}
	return false;
#endif
}

void UCharacterVoiceAsset::AutoLinkPrecachedSoundWaves()
{
#if !WITH_EDITOR
	return;
#else
	UPackage* Package = GetOutermost();
	if (!Package || Package == GetTransientPackage())
	{
		return;
	}

	auto LinkSoundWave = [this](USoundWave* SoundWave)
	{
		if (!SoundWave)
		{
			return;
		}

		const FString SoundName = SoundWave->GetName();
		for (FPlayVoiceLineEntry& Entry : VoiceLines)
		{
			if (Entry.Key.IsNone())
			{
				continue;
			}

			const FString SanitizedKey = MakeSafePackageComponent(Entry.Key.ToString());
			const FString SanitizedCharacter = MakeSafePackageComponent(CharacterName.ToString());
			const FName EntryTableId = Entry.StringTable ? Entry.StringTable->GetStringTableId() : Entry.StringTableId;
			bool bLegacyKeyIsUnique = true;
			for (const FPlayVoiceLineEntry& OtherEntry : VoiceLines)
			{
				if (&OtherEntry != &Entry && OtherEntry.Key == Entry.Key)
				{
					bLegacyKeyIsUnique = false;
					break;
				}
			}
			for (const FCharacterLanguageData& LanguageData : Languages)
			{
				const FString NormalizedLanguage = NormalizeLanguageCode(LanguageData.LanguageCode, DefaultLanguage);
				const FString NewExpectedName = FString::Printf(TEXT("SW_%s_%s_%s_%s_%08X"), *SanitizedCharacter, *NormalizedLanguage, *MakeSafePackageComponent(EntryTableId.ToString()), *SanitizedKey, MakeVoiceLineIdentityHash(EntryTableId, Entry.Key, NormalizedLanguage));
				const FString LegacyExpectedName = FString::Printf(TEXT("SW_%s_%s_%s"), *CharacterName.ToString(), *NormalizedLanguage, *SanitizedKey);
				if (SoundName == NewExpectedName || (bLegacyKeyIsUnique && SoundName == LegacyExpectedName))
				{
					Entry.PrecachedSoundWavesByLanguage.Add(NormalizedLanguage, SoundWave);
					if (NormalizedLanguage == NormalizeLanguageCode(DefaultLanguage))
					{
						Entry.PrecachedSoundWave = SoundWave;
					}
					return;
				}
			}
		}
	};

	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(Package, SubObjects, EGetObjectsFlags::None);
	for (UObject* Obj : SubObjects)
	{
		LinkSoundWave(Cast<USoundWave>(Obj));
	}

	if (!FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::Get().LoadModule("AssetRegistry");
	}
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> AssetList;
		const FString AssetFolder = FPaths::GetPath(Package->GetName());
		AssetRegistryModule.Get().GetAssetsByPath(FName(*AssetFolder), AssetList, true);
		for (const FAssetData& AssetData : AssetList)
		{
			if (AssetData.AssetClassPath.GetAssetName() == FName(TEXT("SoundWave")))
			{
				LinkSoundWave(Cast<USoundWave>(AssetData.GetAsset()));
			}
		}
	}
}
#endif

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
