// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "CharacterVoiceAssetCustomization.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterVoiceGeneratedSoundWaveNameTest, "PlayVoice.UnitTests.CharacterVoiceGeneratedSoundWaveName", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCharacterVoiceGeneratedSoundWaveNameTest::RunTest(const FString& Parameters)
{
	const FString Identity = TEXT("dialogue|greeting|EN");
	const FString OldName = FString::Printf(TEXT("SW_OldHero_EN_Dialogue_Greeting_%08X"), FCrc::StrCrc32(*Identity));
	TestTrue(
		TEXT("A generated SoundWave with a prior character name remains recognizable for rename cleanup"),
		FCharacterVoiceAssetCustomization::IsGeneratedSoundWaveNameForVoiceLine(OldName, FName(TEXT("Dialogue")), FName(TEXT("Greeting")), TEXT("EN")));
	TestFalse(
		TEXT("A SoundWave with a different identity hash is not recognized as generated"),
		FCharacterVoiceAssetCustomization::IsGeneratedSoundWaveNameForVoiceLine(TEXT("SW_OldHero_EN_Dialogue_Greeting_00000000"), FName(TEXT("Dialogue")), FName(TEXT("Greeting")), TEXT("EN")));
	return true;
}

#endif
