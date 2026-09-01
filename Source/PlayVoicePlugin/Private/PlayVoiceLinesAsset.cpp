// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceLinesAsset.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"

UPlayVoiceLinesAsset::UPlayVoiceLinesAsset()
{
}

const FPlayVoiceLineEntry* UPlayVoiceLinesAsset::FindLineByKey(FName InKey) const
{
	if (InKey.IsNone())
	{
		return nullptr;
	}

	for (const FPlayVoiceLineEntry& Entry : Lines)
	{
		if (Entry.Key == InKey)
		{
			return &Entry;
		}
	}
	return nullptr;
}

bool UPlayVoiceLinesAsset::HasLineForKey(FName InKey) const
{
	return FindLineByKey(InKey) != nullptr;
}

FString UPlayVoiceLinesAsset::GetResolvedTextLineForEntry(const FPlayVoiceLineEntry& Entry) const
{
	if (!Entry.TextLine.TrimStartAndEnd().IsEmpty())
	{
		return Entry.TextLine;
	}

	UStringTable* TargetTable = Entry.StringTableOverride ? Entry.StringTableOverride.Get() : StringTable.Get();
	if (TargetTable && !Entry.Key.IsNone())
	{
		FString TableId = TargetTable->GetStringTableId().ToString();
		FText ResolvedText = FText::FromStringTable(FName(*TableId), Entry.Key.ToString());
		if (!ResolvedText.IsEmpty())
		{
			return ResolvedText.ToString();
		}
	}

	return Entry.Key.IsNone() ? FString() : Entry.Key.ToString();
}

FString UPlayVoiceLinesAsset::GetVoiceRecordingFolderOnDisk() const
{
	UPackage* OuterPackage = GetOutermost();
	if (!OuterPackage)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("VoiceRecording"));
	}

	FString PackageName = OuterPackage->GetName();
	FString AssetPathName = FPaths::GetPath(PackageName);

	FString DiskFolder = FPaths::ProjectContentDir();
	if (AssetPathName.StartsWith(TEXT("/Game/")))
	{
		FString RelativeContentPath = AssetPathName.RightChop(6); // Remove "/Game/"
		DiskFolder = FPaths::Combine(FPaths::ProjectContentDir(), RelativeContentPath);
	}

	FString VoiceRecordingFolder = FPaths::Combine(DiskFolder, TEXT("VoiceRecording"));
	IFileManager::Get().MakeDirectory(*VoiceRecordingFolder, true);

	return FPaths::ConvertRelativePathToFull(VoiceRecordingFolder);
}
