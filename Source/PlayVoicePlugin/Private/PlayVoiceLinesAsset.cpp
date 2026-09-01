// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceLinesAsset.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"

UPlayVoiceLinesAsset::UPlayVoiceLinesAsset()
{
}

const FPlayVoiceLineEntry* UPlayVoiceLinesAsset::FindLineByTag(const FGameplayTag& InTag) const
{
	if (!InTag.IsValid())
	{
		return nullptr;
	}

	for (const FPlayVoiceLineEntry& Entry : Lines)
	{
		if (Entry.VoiceTag == InTag)
		{
			return &Entry;
		}
	}
	return nullptr;
}

bool UPlayVoiceLinesAsset::HasLineForTag(const FGameplayTag& InTag) const
{
	return FindLineByTag(InTag) != nullptr;
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
