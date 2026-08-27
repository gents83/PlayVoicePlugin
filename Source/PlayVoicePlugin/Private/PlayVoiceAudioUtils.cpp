// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceAudioUtils.h"
#include "Sound/SoundWave.h"
#include "Memory/SharedBuffer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

static TArray<uint8> BuildWAVHeaderAndPCMBuffer(const TArray<uint8>& PCMData, int32 SampleRate, int32 NumChannels)
{
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

USoundWave* UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(const TArray<uint8>& PCMData, int32 SampleRate, int32 NumChannels, UObject* Outer, FName Name)
{
	if (PCMData.Num() == 0 || SampleRate <= 0 || NumChannels <= 0)
	{
		return nullptr;
	}

	UObject* SoundOuter = Outer ? Outer : GetTransientPackage();
	EObjectFlags ObjectFlags = Outer ? (RF_Public | RF_Standalone) : RF_Transient;
	FName SoundName = Name.IsNone() ? NAME_None : Name;

	USoundWave* SoundWave = NewObject<USoundWave>(SoundOuter, SoundName, ObjectFlags);
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
	SoundWave->RawPCMDataSize = PCMData.Num();
	SoundWave->RawPCMData = (uint8*)FMemory::Malloc(PCMData.Num());
	FMemory::Memcpy(SoundWave->RawPCMData, PCMData.GetData(), PCMData.Num());

	return SoundWave;
}

USoundWave* UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(const TArray<uint8>& WAVData, UObject* Outer, FName Name)
{
	if (WAVData.Num() < 44) // Basic WAV header size
	{
		return nullptr;
	}

	// Parse basic WAV header
	uint16 NumChannels = *reinterpret_cast<const uint16*>(&WAVData[22]);
	uint32 SampleRate = *reinterpret_cast<const uint32*>(&WAVData[24]);

	int32 DataOffset = 44;
	for (int32 i = 12; i < WAVData.Num() - 8; ++i)
	{
		if (WAVData[i] == 'd' && WAVData[i + 1] == 'a' && WAVData[i + 2] == 't' && WAVData[i + 3] == 'a')
		{
			DataOffset = i + 8;
			break;
		}
	}

	if (DataOffset >= WAVData.Num())
	{
		return nullptr;
	}

	int32 DataSize = WAVData.Num() - DataOffset;
	TArray<uint8> PCMData;
	PCMData.SetNumUninitialized(DataSize);
	FMemory::Memcpy(PCMData.GetData(), WAVData.GetData() + DataOffset, DataSize);

	UObject* SoundOuter = Outer ? Outer : GetTransientPackage();
	EObjectFlags ObjectFlags = Outer ? (RF_Public | RF_Standalone) : RF_Transient;
	FName SoundName = Name.IsNone() ? NAME_None : Name;

	USoundWave* SoundWave = NewObject<USoundWave>(SoundOuter, SoundName, ObjectFlags);
	if (!SoundWave)
	{
		return nullptr;
	}

	SoundWave->SetSampleRate(SampleRate);
	SoundWave->NumChannels = NumChannels;
	SoundWave->Duration = (float)DataSize / (float)(SampleRate * NumChannels * sizeof(int16));
	SoundWave->TotalSamples = DataSize / (NumChannels * sizeof(int16));
	SoundWave->bProcedural = false;
	SoundWave->bStreaming = false;

#if WITH_EDITORONLY_DATA
	// Store complete WAV payload (including RIFF header) in RawData
	const FSharedBuffer UpdatedBuffer = FSharedBuffer::Clone(WAVData.GetData(), WAVData.Num());
	SoundWave->RawData.UpdatePayload(UpdatedBuffer);
#endif

	// Populate RawPCMData for runtime sound wave playback
	SoundWave->RawPCMDataSize = DataSize;
	SoundWave->RawPCMData = (uint8*)FMemory::Malloc(DataSize);
	FMemory::Memcpy(SoundWave->RawPCMData, PCMData.GetData(), DataSize);

	return SoundWave;
}

FString UPlayVoiceAudioUtils::ExportSoundWaveToTempWAVFile(USoundWave* SoundWave)
{
	if (!SoundWave)
	{
		return FString();
	}

	TArray<uint8> WAVBytes;

#if WITH_EDITORONLY_DATA
	if (SoundWave->RawData.HasPayloadData())
	{
		FSharedBuffer Payload = SoundWave->RawData.GetPayload();
		if (Payload.GetSize() >= 44)
		{
			WAVBytes.SetNumUninitialized(Payload.GetSize());
			FMemory::Memcpy(WAVBytes.GetData(), Payload.GetData(), Payload.GetSize());
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

	FString TempFilePath = TempDir / FString::Printf(TEXT("%s.wav"), *SoundWave->GetName());
	if (FFileHelper::SaveArrayToFile(WAVBytes, *TempFilePath))
	{
		return FPaths::ConvertRelativePathToFull(TempFilePath);
	}

	return FString();
}
