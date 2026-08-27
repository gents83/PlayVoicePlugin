// Copyright (c) 2025 PlayVoice Team. All Rights Reserved.

using UnrealBuildTool;

public class PlayVoicePluginEditor : ModuleRules
{
	public PlayVoicePluginEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"PlayVoicePlugin"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UnrealEd",
				"AssetTools",
				"DetailCustomizations",
				"PropertyEditor",
				"EditorSubsystem",
				"HTTP",
				"Json",
				"JsonUtilities",
				"DeveloperSettings",
				"BlueprintGraph",
				"AssetRegistry",
				"Projects"
			}
		);
	}
}
