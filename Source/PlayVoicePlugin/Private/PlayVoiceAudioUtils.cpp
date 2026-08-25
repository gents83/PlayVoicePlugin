// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "PlayVoiceAudioUtils.h"
#include "Sound/SoundWave.h"

USoundWave* UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(const TArray<uint8>& PCMData, int32 SampleRate, int32 NumChannels)
{
	if (PCMData.Num() == 0 || SampleRate <= 0 || NumChannels <= 0)
	{
		return nullptr;
	}

	// Parent to GetTransientPackage() to ensure the object is properly rooted until GC or asset caching
	USoundWave* SoundWave = NewObject<USoundWave>(GetTransientPackage(), NAME_None, RF_Transient);
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

#if WITH_EDITORONLY_DATA
	SoundWave->RawData.Lock(LOCK_READ_WRITE);
	void* BufferData = SoundWave->RawData.Realloc(PCMData.Num());
	FMemory::Memcpy(BufferData, PCMData.GetData(), PCMData.Num());
	SoundWave->RawData.Unlock();
#endif

	// Populate RawPCMData for runtime sound wave playback
	SoundWave->RawPCMDataSize = PCMData.Num();
	SoundWave->RawPCMData = (uint8*)FMemory::Malloc(PCMData.Num());
	FMemory::Memcpy(SoundWave->RawPCMData, PCMData.GetData(), PCMData.Num());

	return SoundWave;
}

USoundWave* UPlayVoiceAudioUtils::CreateSoundWaveFromWAVBuffer(const TArray<uint8>& WAVData)
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

	return CreateSoundWaveFromPCM(PCMData, SampleRate, NumChannels);
}
