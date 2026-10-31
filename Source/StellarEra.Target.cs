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
			"StellarEra",         // 装配：GameMode / PlayerController（primary game module）
			"StellarEraCore",     // 底座：N 体求解器、物理单位、通用接口
			"StellarEraGeology",  // 底座：元素表与地质场
			"StellarEraPlanet",   // 世界：地形 / 大气 / 矿物 / 编辑
			"StellarEraPawns",    // 受控实体：角色 / 飞船 / 星图相机
			"StellarEraCosmos",   // 天体：N 体编排与轨迹
			"StellarEraUI",       // 界面：HUD / 星图面板 / 编辑 UI
		});
	}
}
