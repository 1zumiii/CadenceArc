using UnrealBuildTool;

// Editor-only tooling for CadenceArc: the runtime graph debugger (Arc Debugger and Arc History).
// The runtime module must never depend on this module.
public class CadenceArcEditor : ModuleRules
{
	public CadenceArcEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			[
				"Core",
				"CoreUObject",
				"Engine",
				"CadenceArc"
			]
		);

		PrivateDependencyModuleNames.AddRange(
			[
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"WorkspaceMenuStructure",
				"GameplayTags"
			]
		);
	}
}