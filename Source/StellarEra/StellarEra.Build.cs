// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StellarEra : ModuleRules
{
	public StellarEra(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Voxel" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// 如需使用 Slate UI，请取消下一行注释
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// 如需使用在线功能，请取消下一行注释
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// 若要引入 OnlineSubsystemSteam，需在 uproject 文件的 plugins 小节中添加该项，并把 Enabled 属性设为 true
	}
}
