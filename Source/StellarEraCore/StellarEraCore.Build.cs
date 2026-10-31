// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

// 底座层：单位系统、N 体求解器，以及跨模块共用的接口。
//
// Engine 依赖**不能去掉**。NBodySimulator.h 持有 TWeakObjectPtr<AActor>，
// NBodySimulator.cpp 定义了 FindBodyByActor(const AActor*)，但两处既没有
// include "GameFramework/Actor.h" 也没有前向声明 AActor —— 它们现在编得过，
// 靠的是模块依赖 Engine 时 UBT 选用的 SharedPCH.Engine.h，它经
// EngineMinimal.h:26 带进 GameFramework/Actor.h。
// 把 Engine 拿掉会退到 SharedPCH.Core.h，上述两处立刻报未定义类型。
//
// .h 与 .cpp 同目录并列在 Src/ 下。
//
// Src/Interfaces 里是 IVoxelViewpointInvokers：实现方是 Pawns 的两个 Pawn，
// 使用方是主模块的控制器，两边都不该为这一个头互相依赖，所以放在共同下游。
public class StellarEraCore : ModuleRules
{
	public StellarEraCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 模块入口 StellarEraCore.h/.cpp 与 Build.cs 并列在模块根，不放进 Src/。
		PublicIncludePaths.Add(ModuleDirectory);

		string SrcDir = Path.Combine(ModuleDirectory, "Src");
		PublicIncludePaths.Add(SrcDir);
		foreach (string SubDir in Directory.GetDirectories(SrcDir, "*", SearchOption.AllDirectories))
		{
			PublicIncludePaths.Add(SubDir);
		}

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
