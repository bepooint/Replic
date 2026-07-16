using UnrealBuildTool;

public class ReplicEditor : ModuleRules
{
	public ReplicEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Kismet",
				"PropertyEditor",
				"Replic",
				"Slate",
				"SlateCore",
				"UMG",
				"UnrealEd"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"BlueprintGraph",
				"CQTest",
				"GraphEditor",
				"KismetCompiler",
				"NetCore",
				"Projects"
			}
		);
	}
}
