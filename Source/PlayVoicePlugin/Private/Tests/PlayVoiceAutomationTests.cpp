// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoiceAudioUtils.h"
#include "PlayVoiceSettings.h"
#include "Sound/SoundWave.h"

#if WITH_DEV_AUTOMATION_TESTS

// 1. Test UCharacterVoiceAsset caching & lookup functionality
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterVoiceAssetCachingTest, "PlayVoice.UnitTests.CharacterVoiceAssetCaching", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCharacterVoiceAssetCachingTest::RunTest(const FString& Parameters)
{
	UCharacterVoiceAsset* VoiceAsset = NewObject<UCharacterVoiceAsset>();
	TestNotNull(TEXT("VoiceAsset should be successfully instantiated"), VoiceAsset);

	if (!VoiceAsset)
	{
		return false;
	}

	FString TestLine = TEXT("Hello world, this is a test line.");
	TestFalse(TEXT("Voice line should initially not be cached"), VoiceAsset->HasPrecachedVoiceLine(TestLine));

	// Create dynamic dummy sound wave
	USoundWave* DummySoundWave = NewObject<USoundWave>();
	VoiceAsset->CacheVoiceLine(TestLine, DummySoundWave);

	TestTrue(TEXT("Voice line should be cached after CacheVoiceLine"), VoiceAsset->HasPrecachedVoiceLine(TestLine));
	TestEqual(TEXT("Retrieved sound wave should match cached sound wave"), VoiceAsset->GetPrecachedVoiceLine(TestLine), DummySoundWave);

	// Case-insensitivity & whitespace trimming test
	FString MessyLine = TEXT("  HELLO WORLD, THIS IS A TEST LINE.  ");
	TestTrue(TEXT("Voice line lookup should be case and whitespace insensitive"), VoiceAsset->HasPrecachedVoiceLine(MessyLine));

	return true;
}

// 2. Test UPlayVoiceAudioUtils PCM & WAV parser
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayVoiceAudioUtilsTest, "PlayVoice.UnitTests.AudioUtilsPCMAndWAVParsing", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayVoiceAudioUtilsTest::RunTest(const FString& Parameters)
{
	// 2a. Test PCM creation
	TArray<uint8> PCMData;
	PCMData.SetNumZeroed(4800); // 0.1s of 24kHz 16-bit mono PCM (4800 bytes)

	USoundWave* PCMSoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(PCMData, 24000, 1);
	TestNotNull(TEXT("Created SoundWave from PCM should not be null"), PCMSoundWave);

	if (PCMSoundWave)
	{
		TestEqual(TEXT("Sample rate should be 24000"), PCMSoundWave->GetSampleRateForCurrentPlatform(), 24000);
		TestEqual(TEXT("Channels count should be 1"), PCMSoundWave->NumChannels, 1);
		TestNearlyEqual(TEXT("Duration should be approximately 0.1s"), PCMSoundWave->Duration, 0.1f, 0.01f);
	}

	// 2b. Test null/invalid input handling
	TArray<uint8> EmptyData;
	USoundWave* InvalidSoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(EmptyData, 24000, 1);
	TestNull(TEXT("Empty PCM data should return null SoundWave"), InvalidSoundWave);

	return true;
}

// 3. Test UPlayVoiceSettings default values
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayVoiceSettingsTest, "PlayVoice.UnitTests.PlayVoiceSettingsDefaults", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPlayVoiceSettingsTest::RunTest(const FString& Parameters)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	TestNotNull(TEXT("PlayVoiceSettings default object should exist"), Settings);

	if (Settings)
	{
		TestEqual(TEXT("Default service URL should be http://127.0.0.1:8000"), Settings->ServiceUrl, TEXT("http://127.0.0.1:8000"));
		TestEqual(TEXT("Default sample rate should be 24000"), Settings->DefaultSampleRate, 24000);
		TestTrue(TEXT("Default AutoPrecacheOnStartup should be true"), Settings->bAutoPrecacheOnStartup);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
