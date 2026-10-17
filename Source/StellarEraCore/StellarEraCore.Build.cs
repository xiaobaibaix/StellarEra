// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 天体力学纯模拟层：单位系统与 N 体求解器。
//
// Engine 依赖**不能去掉**。NBodySimulator.h 持有 TWeakObjectPtr<AActor>，
// NBodySimulator.cpp 定义了 FindBodyByActor(const AActor*)，但两处既没有
// include "GameFramework/Actor.h" 也没有前向声明 AActor —— 它们现在编得过，
// 靠的是模块依赖 Engine 时 UBT 选用的 SharedPCH.Engine.h，它经
// EngineMinimal.h:26 带进 GameFramework/Actor.h。
// 把 Engine 拿掉会退到 SharedPCH.Core.h，上述两处立刻报未定义类型。
public class StellarEraCore : ModuleRules
{
	public StellarEraCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
