// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class StellarEraTarget : TargetRules
{
	public StellarEraTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[]
		{
			"StellarEra",          // 框架（primary game module）
			"StellarEraCosmos",    // 万有引力
			"StellarEraCore",      // 天体力学
			"StellarEraPlanet",    // 体素星球
			"StellarEraGeology",   // 地质
			"StellarEraSpaceship", // 飞船
			"StellarEraUI",    // 显示与控制
			"StellarEraPlanetEdit",  // 行星编辑
			"StellarEraCharacter", // 角色控制
		});
	}
}
