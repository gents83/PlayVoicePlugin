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

void UCharacterVoiceAsset::ClearPrecachedVoiceLines()
{
	PrecachedSoundWaves.Empty();
}

void UCharacterVoiceAsset::CacheVoiceLine(const FString& TextLine, USoundWave* InSoundWave, const FString& LanguageCode)
{
	if (!TextLine.IsEmpty() && InSoundWave)
	{
		FString TargetLang = LanguageCode.TrimStartAndEnd().ToUpper();
		if (TargetLang.IsEmpty())
		{
			TargetLang = DefaultLanguage.TrimStartAndEnd().ToUpper();
		}
		if (TargetLang.IsEmpty())
		{
			TargetLang = TEXT("EN");
		}
		FString KeyWithLang = MakeCacheKey(TextLine, TargetLang);
		PrecachedSoundWaves.Add(KeyWithLang, InSoundWave);
	}
}

USoundWave* UCharacterVoiceAsset::GetPrecachedVoiceLine(const FString& TextLine, const FString& LanguageCode) const
{
	FString TargetLang = LanguageCode.TrimStartAndEnd().ToUpper();
	if (TargetLang.IsEmpty())
	{
		TargetLang = DefaultLanguage.TrimStartAndEnd().ToUpper();
	}
	if (TargetLang.IsEmpty())
	{
		TargetLang = TEXT("EN");
	}

	FString KeyWithLang = MakeCacheKey(TextLine, TargetLang);
	if (const TObjectPtr<USoundWave>* FoundSound = PrecachedSoundWaves.Find(KeyWithLang))
	{
		if (*FoundSound)
		{
			return *FoundSound;
		}
	}

	// Legacy fallback lookup for un-prefixed keys in older saved assets
	FString PlainKey = TextLine.TrimStartAndEnd().ToLower();
	if (const TObjectPtr<USoundWave>* FoundSound = PrecachedSoundWaves.Find(PlainKey))
	{
		if (*FoundSound)
		{
			return *FoundSound;
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

const FVoiceLineGuideTrack* UCharacterVoiceAsset::FindGuideTrackForLine(const FString& TextLine, const FString& LanguageCode) const
{
	FString CleanText = TextLine.TrimStartAndEnd().ToLower();
	if (CleanText.IsEmpty())
	{
		return nullptr;
	}

	FString TargetLang = LanguageCode.TrimStartAndEnd().ToUpper();
	if (TargetLang.IsEmpty())
	{
		TargetLang = DefaultLanguage.TrimStartAndEnd().ToUpper();
	}
	if (TargetLang.IsEmpty())
	{
		TargetLang = TEXT("EN");
	}

	const FCharacterLanguageData* LangData = FindLanguageData(TargetLang);
	if (LangData)
	{
		for (const FVoiceLineGuideTrack& GuideTrack : LangData->GuideTracks)
		{
			if (GuideTrack.LineText.TrimStartAndEnd().ToLower().Equals(CleanText))
			{
				return &GuideTrack;
			}
		}
	}
	return nullptr;
}

FString UCharacterVoiceAsset::GetResolvedGuideAudioFileForLine(const FString& TextLine, const FString& LanguageCode) const
{
	const FVoiceLineGuideTrack* GuideTrack = FindGuideTrackForLine(TextLine, LanguageCode);
	if (GuideTrack)
	{
		FString PathStr = GuideTrack->GuideAudioFile.FilePath.TrimStartAndEnd();
		if (!PathStr.IsEmpty())
		{
			if (FPaths::IsRelative(PathStr))
			{
				PathStr = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), PathStr);
			}
			if (IFileManager::Get().FileExists(*PathStr))
			{
				return PathStr;
			}
		}
	}
	return FString();
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

	// Link any inner USoundWave objects in this package
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(Package, SubObjects, EGetObjectsFlags::None);

	for (UObject* Obj : SubObjects)
	{
		if (USoundWave* SoundWave = Cast<USoundWave>(Obj))
		{
			// Check if already linked as a value
			bool bAlreadyLinked = false;
			for (const auto& KVP : PrecachedSoundWaves)
			{
				if (KVP.Value == SoundWave)
				{
					bAlreadyLinked = true;
					break;
				}
			}

			if (!bAlreadyLinked)
			{
				FString TargetLang = DefaultLanguage.IsEmpty() ? TEXT("EN") : DefaultLanguage;
				PrecachedSoundWaves.Add(MakeCacheKey(SoundWave->GetName(), TargetLang), SoundWave);
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
