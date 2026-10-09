using UnrealBuildTool;

public class CadenceArcAnimation : ModuleRules
{
	public CadenceArcAnimation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(["Core", "CoreUObject", "Engine", "GameplayTags", "CadenceArc"]);
	}
}
