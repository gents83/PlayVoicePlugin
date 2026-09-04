// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceAudioUtils.h"
#include "Sound/SoundWave.h"
#include "Memory/SharedBuffer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

static TArray<uint8> BuildWAVHeaderAndPCMBuffer(const TArray<uint8>& PCMData, int32 SampleRate, int32 NumChannels)
{
	if (PCMData.Num() == 0 || SampleRate <= 0 || NumChannels <= 0 || (PCMData.Num() % (NumChannels * static_cast<int32>(sizeof(int16)))) != 0)
	{
		return TArray<uint8>();
	}

	uint32 DataSize = PCMData.Num();
	uint32 ChunkSize = 36 + DataSize;
	uint16 AudioFormat = 1; // Uncompressed PCM
	uint16 Channels = (uint16)FMath::Max(1, NumChannels);
	uint32 SampleRateU32 = (uint32)FMath::Max(1, SampleRate);
	uint16 BitsPerSample = 16;
	uint16 BlockAlign = Channels * (BitsPerSample / 8);
	uint32 ByteRate = SampleRateU32 * BlockAlign;

	TArray<uint8> WAVBuffer;
	WAVBuffer.SetNumUninitialized(44 + DataSize);

	uint8* Header = WAVBuffer.GetData();
	FMemory::Memcpy(Header, "RIFF", 4);
	FMemory::Memcpy(Header + 4, &ChunkSize, 4);
	FMemory::Memcpy(Header + 8, "WAVE", 4);
	FMemory::Memcpy(Header + 12, "fmt ", 4);
	uint32 Subchunk1Size = 16;
	FMemory::Memcpy(Header + 16, &Subchunk1Size, 4);
	FMemory::Memcpy(Header + 20, &AudioFormat, 2);
	FMemory::Memcpy(Header + 22, &Channels, 2);
	FMemory::Memcpy(Header + 24, &SampleRateU32, 4);
	FMemory::Memcpy(Header + 28, &ByteRate, 4);
	FMemory::Memcpy(Header + 32, &BlockAlign, 2);
	FMemory::Memcpy(Header + 34, &BitsPerSample, 2);
	FMemory::Memcpy(Header + 36, "data", 4);
	FMemory::Memcpy(Header + 40, &DataSize, 4);

	FMemory::Memcpy(Header + 44, PCMData.GetData(), DataSize);
	return WAVBuffer;
}

TArray<uint8> UPlayVoiceAudioUtils::CreateWAVBufferFromPCM(const TArray<uint8>& PCMData, int32 SampleRate, int32 NumChannels)
{
	return BuildWAVHeaderAndPCMBuffer(PCMData, SampleRate, NumChannels);
}

int32 UPlayVoiceAudioUtils::GetPCM16Peak(const TArray<uint8>& PCMData)
{
	const int32 SampleCount = PCMData.Num() / sizeof(int16);
	int32 Peak = 0;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		int16 Sample = 0;
		FMemory::Memcpy(&Sample, PCMData.GetData() + SampleIndex * sizeof(int16), sizeof(int16));
		Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Sample)));
	}

	return Peak;
}

bool UPlayVoiceAudioUtils::NormalizePCM16ToPeak(TArray<uint8>& PCMData, int32 TargetPeak)
{
	if (PCMData.Num() == 0 || (PCMData.Num() % sizeof(int16)) != 0)
	{
		return false;
	}

	const int32 SourcePeak = GetPCM16Peak(PCMData);
	if (SourcePeak <= 0 || TargetPeak <= 0)
	{
		return false;
	}

	const float Scale = static_cast<float>(FMath::Min(TargetPeak, 32767)) / static_cast<float>(SourcePeak);
	const int32 SampleCount = PCMData.Num() / sizeof(int16);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		int16 Sample = 0;
		FMemory::Memcpy(&Sample, PCMData.GetData() + SampleIndex * sizeof(int16), sizeof(int16));
		const int32 ScaledSample = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Sample) * Scale), -32768, 32767);
		const int16 OutputSample = static_cast<int16>(ScaledSample);
		FMemory::Memcpy(PCMData.GetData() + SampleIndex * sizeof(int16), &OutputSample, sizeof(int16));
	}

	return true;
}

USoundWave* UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(const TArray<uint8>& PCMData, int32 SampleRate, int32 NumChannels, UObject* Outer, FName Name)
{
	if (PCMData.Num() == 0 || SampleRate <= 0 || NumChannels <= 0 || (PCMData.Num() % (NumChannels * static_cast<int32>(sizeof(int16)))) != 0)
	{
		return nullptr;
	}

	UObject* SoundOuter = Outer ? Outer : GetTransientPackage();
	EObjectFlags ObjectFlags = Outer && Outer != GetTransientPackage() ? (RF_Public | RF_Standalone) : RF_Transient;
	FName SoundName = Name.IsNone() ? NAME_None : Name;

	USoundWave* SoundWave = nullptr;
	if (SoundOuter && !SoundName.IsNone())
	{
		UObject* ExistingObj = FindObject<UObject>(SoundOuter, *SoundName.ToString());
		if (ExistingObj)
		{
			SoundWave = Cast<USoundWave>(ExistingObj);
			if (!SoundWave)
			{
				// Existing object is of a different class (e.g., UObjectRedirector). Rename out of the way.
				ExistingObj->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				ExistingObj->MarkAsGarbage();
			}
		}
	}

	if (!SoundWave)
	{
		SoundWave = NewObject<USoundWave>(SoundOuter, SoundName, ObjectFlags);
	}

	if (!SoundWave)
	{
		return nullptr;
	}

	SoundWave->SetSampleRate(SampleRate);
	SoundWave->NumChannels = NumChannels;
	SoundWave->Duration = (float)PCMData.Num() / (float)(SampleRate * NumChannels * sizeof(int16));
	SoundWave->TotalSamples = PCMData.Num() / (NumChannels * sizeof(int16));
	SoundWave->bProcedural = false;
	SoundWave->bStreaming = false;

	TArray<uint8> FullWAVBuffer = BuildWAVHeaderAndPCMBuffer(PCMData, SampleRate, NumChannels);

#if WITH_EDITORONLY_DATA
	const FSharedBuffer UpdatedBuffer = FSharedBuffer::Clone(FullWAVBuffer.GetData(), FullWAVBuffer.Num());
	SoundWave->RawData.UpdatePayload(UpdatedBuffer);
#endif

	// Populate RawPCMData for runtime sound wave playback
	if (SoundWave->RawPCMData)
	{
		FMemory::Free(SoundWave->RawPCMData);
		SoundWave->RawPCMData = nullptr;
		SoundWave->RawPCMDataSize = 0;
	}
	SoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(PCMData.Num()));
	if (!SoundWave->RawPCMData)
	{
		return nullptr;
	}
	SoundWave->RawPCMDataSize = PCMData.Num();
	FMemory::Memcpy(SoundWave->RawPCMData, PCMData.GetData(), PCMData.Num());

	return SoundWave;
}

USoundWave* UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(const TArray<uint8>& WAVData, UObject* Outer, FName Name)
{
	if (WAVData.Num() < 12 || FMemory::Memcmp(WAVData.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(WAVData.GetData() + 8, "WAVE", 4) != 0)
	{
		return nullptr;
	}

	uint16 AudioFormat = 0;
	uint16 NumChannels = 0;
	uint16 BitsPerSample = 0;
	uint32 SampleRate = 0;
	int32 DataOffset = INDEX_NONE;
	uint32 DataSize = 0;
	int32 Offset = 12;

	while (Offset <= WAVData.Num() - 8)
	{
		uint32 ChunkSize = 0;
		FMemory::Memcpy(&ChunkSize, WAVData.GetData() + Offset + 4, sizeof(ChunkSize));
		const int64 ChunkDataStart = static_cast<int64>(Offset) + 8;
		const int64 ChunkDataEnd = ChunkDataStart + ChunkSize;
		if (ChunkDataEnd > WAVData.Num())
		{
			return nullptr;
		}

		const uint8* ChunkId = WAVData.GetData() + Offset;
		if (FMemory::Memcmp(ChunkId, "fmt ", 4) == 0)
		{
			if (ChunkSize < 16)
			{
				return nullptr;
			}
			FMemory::Memcpy(&AudioFormat, WAVData.GetData() + ChunkDataStart, sizeof(AudioFormat));
			FMemory::Memcpy(&NumChannels, WAVData.GetData() + ChunkDataStart + 2, sizeof(NumChannels));
			FMemory::Memcpy(&SampleRate, WAVData.GetData() + ChunkDataStart + 4, sizeof(SampleRate));
			FMemory::Memcpy(&BitsPerSample, WAVData.GetData() + ChunkDataStart + 14, sizeof(BitsPerSample));
		}
		else if (FMemory::Memcmp(ChunkId, "data", 4) == 0 && DataOffset == INDEX_NONE)
		{
			DataOffset = static_cast<int32>(ChunkDataStart);
			DataSize = ChunkSize;
		}

		const int64 NextOffset = ChunkDataEnd + (ChunkSize & 1u);
		if (NextOffset > MAX_int32)
		{
			return nullptr;
		}
		Offset = static_cast<int32>(NextOffset);
	}

	if (AudioFormat != 1 || NumChannels == 0 || SampleRate == 0 || BitsPerSample != 16 || DataOffset == INDEX_NONE || DataSize == 0 || (DataSize % (NumChannels * sizeof(int16))) != 0)
	{
		return nullptr;
	}

	UObject* SoundOuter = Outer ? Outer : GetTransientPackage();
	EObjectFlags ObjectFlags = Outer && Outer != GetTransientPackage() ? (RF_Public | RF_Standalone) : RF_Transient;
	FName SoundName = Name.IsNone() ? NAME_None : Name;

	USoundWave* SoundWave = nullptr;
	if (SoundOuter && !SoundName.IsNone())
	{
		UObject* ExistingObj = FindObject<UObject>(SoundOuter, *SoundName.ToString());
		if (ExistingObj)
		{
			SoundWave = Cast<USoundWave>(ExistingObj);
			if (!SoundWave)
			{
				ExistingObj->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				ExistingObj->MarkAsGarbage();
			}
		}
	}

	if (!SoundWave)
	{
		SoundWave = NewObject<USoundWave>(SoundOuter, SoundName, ObjectFlags);
	}
	if (!SoundWave)
	{
		return nullptr;
	}

	if (SoundWave->RawPCMData)
	{
		FMemory::Free(SoundWave->RawPCMData);
		SoundWave->RawPCMData = nullptr;
		SoundWave->RawPCMDataSize = 0;
	}

	SoundWave->SetSampleRate(static_cast<int32>(SampleRate));
	SoundWave->NumChannels = NumChannels;
	SoundWave->Duration = static_cast<float>(DataSize) / static_cast<float>(SampleRate * NumChannels * sizeof(int16));
	SoundWave->TotalSamples = DataSize / (NumChannels * sizeof(int16));
	SoundWave->bProcedural = false;
	SoundWave->bStreaming = false;

#if WITH_EDITORONLY_DATA
	const FSharedBuffer UpdatedBuffer = FSharedBuffer::Clone(WAVData.GetData(), WAVData.Num());
	SoundWave->RawData.UpdatePayload(UpdatedBuffer);
#endif

	SoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(DataSize));
	if (!SoundWave->RawPCMData)
	{
		return nullptr;
	}
	SoundWave->RawPCMDataSize = DataSize;
	FMemory::Memcpy(SoundWave->RawPCMData, WAVData.GetData() + DataOffset, DataSize);

	return SoundWave;
}

FString UPlayVoiceAudioUtils::ExportSoundWaveToTempWAVFile(USoundWave* SoundWave)
{
	if (!SoundWave)
	{
		return FString();
	}

#if WITH_EDITORONLY_DATA
	if (SoundWave->AssetImportData)
	{
		FString ImportedFile = SoundWave->AssetImportData->GetFirstFilename();
		if (!ImportedFile.IsEmpty() && IFileManager::Get().FileExists(*ImportedFile))
		{
			return FPaths::ConvertRelativePathToFull(ImportedFile);
		}
	}
#endif

	TArray<uint8> WAVBytes;

#if WITH_EDITORONLY_DATA
	FSharedBuffer Payload = SoundWave->RawData.GetPayload().Get();
	if (Payload.GetSize() >= 44)
	{
		const uint8* Data = static_cast<const uint8*>(Payload.GetData());
		bool bIsRIFF = (Data[0] == 'R' && Data[1] == 'I' && Data[2] == 'F' && Data[3] == 'F');
		bool bHasDataChunk = false;
		for (size_t i = 12; i < Payload.GetSize() - 8; ++i)
		{
			if (Data[i] == 'd' && Data[i + 1] == 'a' && Data[i + 2] == 't' && Data[i + 3] == 'a')
			{
				bHasDataChunk = true;
				break;
			}
		}

		if (bIsRIFF && bHasDataChunk)
		{
			WAVBytes.SetNumUninitialized(Payload.GetSize());
			FMemory::Memcpy(WAVBytes.GetData(), Payload.GetData(), Payload.GetSize());
		}
		else
		{
			int32 SR = FMath::Max(1, (int32)SoundWave->GetSampleRateForCurrentPlatform());
			int32 Ch = FMath::Max(1, (int32)SoundWave->NumChannels);
			TArray<uint8> RawPCM;
			RawPCM.SetNumUninitialized(Payload.GetSize());
			FMemory::Memcpy(RawPCM.GetData(), Payload.GetData(), Payload.GetSize());
			WAVBytes = BuildWAVHeaderAndPCMBuffer(RawPCM, SR, Ch);
		}
	}
#endif

	if (WAVBytes.Num() < 44 && SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
	{
		TArray<uint8> PCMData;
		PCMData.SetNumUninitialized(SoundWave->RawPCMDataSize);
		FMemory::Memcpy(PCMData.GetData(), SoundWave->RawPCMData, SoundWave->RawPCMDataSize);
		int32 SR = FMath::Max(1, (int32)SoundWave->GetSampleRateForCurrentPlatform());
		int32 Ch = FMath::Max(1, (int32)SoundWave->NumChannels);
		WAVBytes = BuildWAVHeaderAndPCMBuffer(PCMData, SR, Ch);
	}

	if (WAVBytes.Num() < 44)
	{
		return FString();
	}

	FString TempDir = FPaths::ProjectSavedDir() / TEXT("PlayVoiceTemp");
	IFileManager::Get().MakeDirectory(*TempDir, true);

	FString TempFilePath = TempDir / FString::Printf(TEXT("%s_%s.wav"), *SoundWave->GetName(), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (FFileHelper::SaveArrayToFile(WAVBytes, *TempFilePath))
	{
		return FPaths::ConvertRelativePathToFull(TempFilePath);
	}

	return FString();
}
