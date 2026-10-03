
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
			]
		);
	}
}