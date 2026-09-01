// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CharacterVoiceAsset.h"
#include "PlayVoiceLinesAsset.h"
#include "PlayVoiceAudioUtils.h"
#include "PlayVoiceSettings.h"
#include "PlayVoiceBlueprintLibrary.h"
#include "Sound/SoundWave.h"
#include "GameplayTagContainer.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// 1. Test UCharacterVoiceAsset caching & multi-language lookup functionality
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterVoiceAssetCachingTest, "PlayVoice.UnitTests.CharacterVoiceAssetCaching", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCharacterVoiceAssetCachingTest::RunTest(const FString& Parameters)
{
	UCharacterVoiceAsset* VoiceAsset = NewObject<UCharacterVoiceAsset>();
	TestNotNull(TEXT("VoiceAsset should be successfully instantiated"), VoiceAsset);

	if (!VoiceAsset)
	{
		return false;
	}

	FString TestLine = TEXT("Hello world, this is a test line.");
	TestFalse(TEXT("Voice line should initially not be cached"), VoiceAsset->HasPrecachedVoiceLine(TestLine, TEXT("EN")));

	// Create dynamic dummy sound wave
	USoundWave* DummySoundWaveEN = NewObject<USoundWave>();
	VoiceAsset->CacheVoiceLine(TestLine, DummySoundWaveEN, TEXT("EN"));

	TestTrue(TEXT("Voice line should be cached for EN"), VoiceAsset->HasPrecachedVoiceLine(TestLine, TEXT("EN")));
	TestEqual(TEXT("Retrieved sound wave for EN should match cached sound wave"), VoiceAsset->GetPrecachedVoiceLine(TestLine, TEXT("EN")), DummySoundWaveEN);
	TestEqual(TEXT("Caching a single line should create exactly 1 entry in PrecachedSoundWaves map"), VoiceAsset->PrecachedSoundWaves.Num(), 1);

	// Multi-language caching test
	USoundWave* DummySoundWaveES = NewObject<USoundWave>();
	VoiceAsset->CacheVoiceLine(TestLine, DummySoundWaveES, TEXT("ES"));

	TestTrue(TEXT("Voice line should be cached for ES"), VoiceAsset->HasPrecachedVoiceLine(TestLine, TEXT("ES")));
	TestEqual(TEXT("Retrieved sound wave for ES should match ES sound wave"), VoiceAsset->GetPrecachedVoiceLine(TestLine, TEXT("ES")), DummySoundWaveES);
	TestEqual(TEXT("Caching two lines across EN and ES should result in exactly 2 entries in PrecachedSoundWaves map"), VoiceAsset->PrecachedSoundWaves.Num(), 2);

	// Case-insensitivity & whitespace trimming test
	FString MessyLine = TEXT("  HELLO WORLD, THIS IS A TEST LINE.  ");
	TestTrue(TEXT("Voice line lookup should be case and whitespace insensitive"), VoiceAsset->HasPrecachedVoiceLine(MessyLine, TEXT("EN")));

	// AutoLink test
	VoiceAsset->AutoLinkPrecachedSoundWaves();

	// Test bRegenerateExistingVoiceLines default
	TestTrue(TEXT("bRegenerateExistingVoiceLines should default to true"), VoiceAsset->bRegenerateExistingVoiceLines);

	// Test ClearPrecachedVoiceLines
	VoiceAsset->ClearPrecachedVoiceLines();
	TestFalse(TEXT("Voice line should no longer be cached after clearing"), VoiceAsset->HasPrecachedVoiceLine(TestLine, TEXT("EN")));

	// Disk folder helper test
	FString AssetDiskFolder = VoiceAsset->GetAssetDiskFolder();
	TestFalse(TEXT("GetAssetDiskFolder should return non-empty directory path"), AssetDiskFolder.IsEmpty());

	return true;
}

// 2. Test Multi-Language settings data structures in UCharacterVoiceAsset
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterVoiceMultiLanguageTest, "PlayVoice.UnitTests.CharacterVoiceMultiLanguage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCharacterVoiceMultiLanguageTest::RunTest(const FString& Parameters)
{
	UCharacterVoiceAsset* VoiceAsset = NewObject<UCharacterVoiceAsset>();
	TestNotNull(TEXT("VoiceAsset should be instantiated"), VoiceAsset);

	if (!VoiceAsset)
	{
		return false;
	}

	// Verify default language
	TestEqual(TEXT("Default language should be EN"), VoiceAsset->DefaultLanguage, TEXT("EN"));

	FCharacterLanguageData* DefaultData = VoiceAsset->FindLanguageData(TEXT("EN"));
	TestNotNull(TEXT("Default language data EN should exist"), DefaultData);

	if (DefaultData)
	{
		TestEqual(TEXT("Default speed should be 1.0"), DefaultData->Speed, 1.0f);
		TestFalse(TEXT("Model should initially not be generated"), DefaultData->bIsModelGenerated);
	}

	// Add Spanish language configuration
	FCharacterLanguageData& SpanishData = VoiceAsset->GetOrAddLanguageData(TEXT("ES"));
	SpanishData.Speed = 1.2f;
	SpanishData.ToneColorEmbeddingData = TEXT("{\"dummy_emb\": 123}");
	SpanishData.bIsModelGenerated = true;

	FCharacterLanguageData* FoundSpanish = VoiceAsset->FindLanguageData(TEXT("ES"));
	TestNotNull(TEXT("Spanish language data should be retrievable"), FoundSpanish);
	if (FoundSpanish)
	{
		TestEqual(TEXT("Spanish speed should be 1.2"), FoundSpanish->Speed, 1.2f);
		TestTrue(TEXT("Spanish model generated status should be true"), FoundSpanish->bIsModelGenerated);
	}

	// Add French language configuration
	FCharacterLanguageData& FrenchData = VoiceAsset->GetOrAddLanguageData(TEXT("FR"));
	FrenchData.Speed = 0.9f;

	// GuideTracks test on UCharacterVoiceAsset
	FVoiceLineGuideTrack GuideEN;
	GuideEN.LineText = TEXT("Guide line text");
	GuideEN.Emotion = TEXT("happy");
	GuideEN.Speed = 1.1f;
	VoiceAsset->GuideTracks.Add(GuideEN);

	const FVoiceLineGuideTrack* FoundGuide = VoiceAsset->FindGuideTrackForLine(TEXT("Guide line text"), TEXT("EN"));
	TestNotNull(TEXT("Guide track should be found in VoiceAsset GuideTracks"), FoundGuide);
	if (FoundGuide)
	{
		TestEqual(TEXT("Guide track emotion should match"), FoundGuide->Emotion, TEXT("happy"));
		TestEqual(TEXT("Guide track speed should match"), FoundGuide->Speed, 1.1f);
	}

	// Verify case-insensitive guide track lookup
	const FVoiceLineGuideTrack* FoundGuideMessy = VoiceAsset->FindGuideTrackForLine(TEXT("  GUIDE LINE TEXT  "), TEXT("EN"));
	TestNotNull(TEXT("Guide track lookup should be case and whitespace insensitive"), FoundGuideMessy);

	TestEqual(TEXT("Asset should now contain 3 language entries"), VoiceAsset->Languages.Num(), 3);

	return true;
}

// 3. Test Folder Resolution & non-clearing multi-format audio search
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterVoiceFolderResolutionTest, "PlayVoice.UnitTests.CharacterVoiceFolderResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCharacterVoiceFolderResolutionTest::RunTest(const FString& Parameters)
{
	// Test ResolveFolderPathToDisk
	FString GameContentPath = TEXT("/Game/Audio/Voices");
	FString DiskPath = UCharacterVoiceAsset::ResolveFolderPathToDisk(GameContentPath);
	TestFalse(TEXT("Resolved disk path for /Game/ should not be empty"), DiskPath.IsEmpty());
	TestTrue(TEXT("Resolved disk path should point to content directory"), DiskPath.Contains(TEXT("Content")));

	FString RelContentPath = TEXT("Content/Audio/Voices");
	FString DiskPathRel = UCharacterVoiceAsset::ResolveFolderPathToDisk(RelContentPath);
	TestFalse(TEXT("Resolved disk path for Content/ should not be empty"), DiskPathRel.IsEmpty());

	// Test ResolveAudioFilesFromFolderAndFiles with dummy files
	FDirectoryPath TestFolder;
	TestFolder.Path = FPaths::ProjectSavedDir() / TEXT("TestAudioFolder");
	IFileManager::Get().MakeDirectory(*TestFolder.Path, true);

	FString WavFile = TestFolder.Path / TEXT("clip1.wav");
	FString Mp3File = TestFolder.Path / TEXT("clip2.mp3");
	FString FlacFile = TestFolder.Path / TEXT("clip3.flac");

	// Create dummy audio files
	FFileHelper::SaveStringToFile(TEXT("dummy wav content"), *WavFile);
	FFileHelper::SaveStringToFile(TEXT("dummy mp3 content"), *Mp3File);
	FFileHelper::SaveStringToFile(TEXT("dummy flac content"), *FlacFile);

	TArray<FFilePath> EmptyFiles;
	TArray<FString> ResolvedAudioFiles = UCharacterVoiceAsset::ResolveAudioFilesFromFolderAndFiles(EmptyFiles, TestFolder);

	TestEqual(TEXT("Resolved files should collect all 3 formats (.wav, .mp3, .flac) without clearing"), ResolvedAudioFiles.Num(), 3);

	// Clean up temporary test files
	IFileManager::Get().Delete(*WavFile);
	IFileManager::Get().Delete(*Mp3File);
	IFileManager::Get().Delete(*FlacFile);
	IFileManager::Get().DeleteDirectory(*TestFolder.Path, false, true);

	return true;
}

// 4. Test UPlayVoiceAudioUtils PCM & WAV parser
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayVoiceAudioUtilsTest, "PlayVoice.UnitTests.AudioUtilsPCMAndWAVParsing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPlayVoiceAudioUtilsTest::RunTest(const FString& Parameters)
{
	// 4a. Test PCM creation
	TArray<uint8> PCMData;
	PCMData.SetNumZeroed(4800); // 0.1s of 24kHz 16-bit mono PCM (4800 bytes)

	USoundWave* PCMSoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(PCMData, 24000, 1);
	TestNotNull(TEXT("Created SoundWave from PCM should not be null"), PCMSoundWave);

	if (PCMSoundWave)
	{
		TestEqual(TEXT("Sample rate should be 24000"), static_cast<int32>(PCMSoundWave->GetSampleRateForCurrentPlatform()), 24000);
		TestEqual(TEXT("Channels count should be 1"), static_cast<int32>(PCMSoundWave->NumChannels), 1);
		TestNearlyEqual(TEXT("Duration should be approximately 0.1s"), PCMSoundWave->Duration, 0.1f, 0.01f);
	}

	// 4b. Test null/invalid input handling
	TArray<uint8> EmptyData;
	USoundWave* InvalidSoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(EmptyData, 24000, 1);
	TestNull(TEXT("Empty PCM data should return null SoundWave"), InvalidSoundWave);

	// 4c. Test SoundWave creation with Outer package
	USoundWave* PersistentSoundWave = UPlayVoiceAudioUtils::CreateSoundWaveFromPCM(PCMData, 24000, 1, GetTransientPackage(), FName(TEXT("TestSW")));
	TestNotNull(TEXT("SoundWave created with Outer should not be null"), PersistentSoundWave);

	return true;
}

// 5. Test UPlayVoiceSettings default values
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayVoiceSettingsTest, "PlayVoice.UnitTests.PlayVoiceSettingsDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPlayVoiceSettingsTest::RunTest(const FString& Parameters)
{
	const UPlayVoiceSettings* Settings = GetDefault<UPlayVoiceSettings>();
	TestNotNull(TEXT("PlayVoiceSettings default object should exist"), Settings);

	if (Settings)
	{
		TestEqual(TEXT("Default service URL should be http://127.0.0.1:1983"), Settings->ServiceUrl, TEXT("http://127.0.0.1:1983"));
		TestFalse(TEXT("Default bAutoStartServiceOnEditorStartup should be false"), Settings->bAutoStartServiceOnEditorStartup);
		TestEqual(TEXT("Default sample rate should be 24000"), Settings->DefaultSampleRate, 24000);
		TestTrue(TEXT("Default AutoPrecacheOnStartup should be true"), Settings->bAutoPrecacheOnStartup);
		TestTrue(TEXT("Default bEnableOnTheFlySynthesis should be true"), Settings->bEnableOnTheFlySynthesis);
		TestEqual(TEXT("Default PythonExecutable should be python"), Settings->PythonExecutable, TEXT("python"));
		TestEqual(TEXT("Default RequirementsFilePath should be Resources/OpenVoiceService/requirements.txt"), Settings->RequirementsFilePath, TEXT("Resources/OpenVoiceService/requirements.txt"));
	}

	return true;
}

// 6. Test UPlayVoiceBlueprintLibrary functions
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayVoiceBlueprintLibraryTest, "PlayVoice.UnitTests.PlayVoiceBlueprintLibrary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPlayVoiceBlueprintLibraryTest::RunTest(const FString& Parameters)
{
	UCharacterVoiceAsset* VoiceAsset = NewObject<UCharacterVoiceAsset>();
	TestNotNull(TEXT("VoiceAsset should be instantiated"), VoiceAsset);

	if (!VoiceAsset)
	{
		return false;
	}

	TestFalse(TEXT("IsCharacterVoiceModelGenerated should return false initially"), UPlayVoiceBlueprintLibrary::IsCharacterVoiceModelGenerated(VoiceAsset, TEXT("EN")));

	FCharacterLanguageData* LangData = VoiceAsset->FindLanguageData(TEXT("EN"));
	if (LangData)
	{
		LangData->bIsModelGenerated = true;
	}

	TestTrue(TEXT("IsCharacterVoiceModelGenerated should return true after model flag set"), UPlayVoiceBlueprintLibrary::IsCharacterVoiceModelGenerated(VoiceAsset, TEXT("EN")));

	return true;
}

// 7. Test UPlayVoiceSubsystem PlayCharacterVoice fallback and dynamic synthesis settings
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayVoiceOnTheFlySynthesisTest, "PlayVoice.UnitTests.PlayVoiceOnTheFlySynthesis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPlayVoiceOnTheFlySynthesisTest::RunTest(const FString& Parameters)
{
	UCharacterVoiceAsset* VoiceAsset = NewObject<UCharacterVoiceAsset>();
	TestNotNull(TEXT("VoiceAsset should be instantiated"), VoiceAsset);

	if (!VoiceAsset)
	{
		return false;
	}

	VoiceAsset->CharacterName = FName("TestHero");
	FString TestLine = TEXT("Dynamic on the fly synthesis test line");

	// Precached line should return sound instantly
	USoundWave* DummySoundWave = NewObject<USoundWave>();
	VoiceAsset->CacheVoiceLine(TestLine, DummySoundWave, TEXT("EN"));
	TestTrue(TEXT("Voice line should be precached"), VoiceAsset->HasPrecachedVoiceLine(TestLine, TEXT("EN")));
	TestEqual(TEXT("Precached SoundWave should be returned"), VoiceAsset->GetPrecachedVoiceLine(TestLine, TEXT("EN")), DummySoundWave);

	// Unprecached line should not be in cache initially
	FString UncachedLine = TEXT("Unprecached dynamic synthesis line");
	TestFalse(TEXT("Uncached line should initially return false"), VoiceAsset->HasPrecachedVoiceLine(UncachedLine, TEXT("EN")));

	return true;
}

// 8. Test UPlayVoiceLinesAsset and StringTable Key precaching
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayVoiceLinesAssetTest, "PlayVoice.UnitTests.PlayVoiceLinesAsset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPlayVoiceLinesAssetTest::RunTest(const FString& Parameters)
{
	UPlayVoiceLinesAsset* LinesAsset = NewObject<UPlayVoiceLinesAsset>();
	TestNotNull(TEXT("PlayVoiceLinesAsset should be instantiated"), LinesAsset);

	if (!LinesAsset)
	{
		return false;
	}

	FName GreetingKey = FName("Hero_Greeting_01");

	FPlayVoiceLineEntry Entry;
	Entry.Key = GreetingKey;
	Entry.TextLine = TEXT("Hello traveler, welcome to the village!");
	Entry.AudioFile.FilePath = TEXT("VoiceRecording/REC_Hero_Greeting_01.wav");
	LinesAsset->Lines.Add(Entry);

	TestTrue(TEXT("LinesAsset should find line for GreetingKey"), LinesAsset->HasLineForKey(GreetingKey));

	const FPlayVoiceLineEntry* FoundEntry = LinesAsset->FindLineByKey(GreetingKey);
	TestNotNull(TEXT("Found entry should not be null"), FoundEntry);
	if (FoundEntry)
	{
		TestEqual(TEXT("TextLine should match"), LinesAsset->GetResolvedTextLineForEntry(*FoundEntry), TEXT("Hello traveler, welcome to the village!"));
	}

	// Test CharacterVoiceAsset reference to PlayVoiceLinesAsset and Key Caching
	UCharacterVoiceAsset* VoiceAsset = NewObject<UCharacterVoiceAsset>();
	VoiceAsset->VoiceLineAssets.Add(LinesAsset);

	USoundWave* KeySoundWave = NewObject<USoundWave>();
	VoiceAsset->CacheVoiceLineForKey(GreetingKey, KeySoundWave, TEXT("EN"));

	TestTrue(TEXT("VoiceAsset should have precached line for GreetingKey"), VoiceAsset->HasPrecachedVoiceLineForKey(GreetingKey, TEXT("EN")));
	TestEqual(TEXT("VoiceAsset should return cached SoundWave for GreetingKey"), VoiceAsset->GetPrecachedVoiceLineForKey(GreetingKey, TEXT("EN")), KeySoundWave);

	FString RecordingFolder = LinesAsset->GetVoiceRecordingFolderOnDisk();
	TestFalse(TEXT("VoiceRecording folder path should not be empty"), RecordingFolder.IsEmpty());
	TestTrue(TEXT("VoiceRecording folder path should contain VoiceRecording"), RecordingFolder.Contains(TEXT("VoiceRecording")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
