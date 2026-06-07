using UnrealBuildTool;

public class Replic : ModuleRules
{
	public Replic(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrecompileForTargets = PrecompileTargetsType.Any;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"NetCore",
				"UMG"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"EngineSettings"
			}
		);
	}
}
