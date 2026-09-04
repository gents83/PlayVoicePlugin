// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

using UnrealBuildTool;

public class PlayVoicePlugin : ModuleRules
{
	public PlayVoicePlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
				"GameplayTags"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"AssetRegistry"
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HTTP",
					"Json",
					"JsonUtilities"
				}
			);
		}
	}
}
