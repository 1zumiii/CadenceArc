using UnrealBuildTool;

// Optional Enhanced Input adapter: binds Input Actions to UCadenceArcComponent press/release/cancel calls.
// The CadenceArc runtime module must never depend on this module or on EnhancedInput.
public class CadenceArcEnhancedInput : ModuleRules
{
	public CadenceArcEnhancedInput(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			[
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"EnhancedInput",
				"CadenceArc"
			]
		);
	}
}
