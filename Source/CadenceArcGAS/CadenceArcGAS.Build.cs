using UnrealBuildTool;

// Optional Gameplay Ability System executor: activates the granted ability whose asset tags contain the requested action tag,
// and reports its activation and end back to UCadenceArcComponent.
// The CadenceArc runtime module must never depend on this module or on GameplayAbilities.
public class CadenceArcGAS : ModuleRules
{
	public CadenceArcGAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			[
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"GameplayAbilities",
				"CadenceArc"
			]
		);

		PrivateDependencyModuleNames.AddRange(
			[
				"GameplayTasks"
			]
		);
	}
}
