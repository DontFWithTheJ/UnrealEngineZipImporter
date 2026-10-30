using UnrealBuildTool;

public class ZipImporter : ModuleRules
{
	public ZipImporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
				"AssetTools",
				"DesktopPlatform",
				"Projects",
				"FileUtilities",
				"ContentBrowser"
			});
	}
}