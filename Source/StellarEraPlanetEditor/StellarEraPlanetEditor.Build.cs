using System.IO;
using UnrealBuildTool;

// 编辑器专用：地质查看器面板（Slate）。图层、取值、出图都在 StellarEraPlanet 的 Geo/Debug，这里只放 UI。
// 可停靠面板要用 UnrealEd / Slate 的编辑器 API，不能进运行时模块。
public class StellarEraPlanetEditor : ModuleRules
{
	public StellarEraPlanetEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 与其它模块同一套目录规则：根目录放模块入口，Src/ 及全部子目录进搜索路径。
		PublicIncludePaths.Add(ModuleDirectory);
		string SrcDir = Path.Combine(ModuleDirectory, "Src");
		PublicIncludePaths.Add(SrcDir);
		foreach (string SubDir in Directory.GetDirectories(SrcDir, "*", SearchOption.AllDirectories))
		{
			PublicIncludePaths.Add(SubDir);
		}

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "StellarEraPlanet" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "InputCore", "UnrealEd", "WorkspaceMenuStructure" });
	}
}
