// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CadenceArc : ModuleRules
{
	public CadenceArc(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			[
				"CoreUObject",
				"Engine",
				"Core",
				"GameplayTags"
				// ... add other public dependencies that you statically link with here ...
			]
		);
	}
}