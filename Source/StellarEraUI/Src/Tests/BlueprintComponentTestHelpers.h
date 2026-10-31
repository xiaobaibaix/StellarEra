// 蓝图 SCS 的测试辅助：从蓝图资产里取某个组件的「类默认值」模板。
//
// 为什么不能查 CDO：蓝图 SCS 里挂的组件**不在 CDO 的 OwnedComponents 里** ——
// 它们要等 Actor 构造（ExecuteConstruction）时才实例化，CDO 上只有 C++ 构造函数
// 造出来的那几个。要验证「蓝图里挂了这个组件、参数是这些值」，得看 SCS 节点的
// ComponentTemplate —— 那才是运行时被复制给实例的那份模板。

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace StellarEraTest
{
	/**
	 * 在蓝图的 SCS 里找指定组件类的模板。
	 *
	 * @param BlueprintObjectPath 蓝图资产路径，形如 "/Game/Code/BP_VoxelPlanet.BP_VoxelPlanet"
	 * @param ComponentClass      目标组件类，子类也算命中
	 * @return 组件模板；蓝图没加载上、没有 SCS、或没挂这类组件时返回 nullptr
	 */
	inline UActorComponent* FindBlueprintComponentTemplate(
		const TCHAR* BlueprintObjectPath,
		const TSubclassOf<UActorComponent>& ComponentClass)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BlueprintObjectPath);
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			return nullptr;
		}

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate || !Node->ComponentClass)
			{
				continue;
			}
			if (Node->ComponentClass->IsChildOf(ComponentClass))
			{
				return Node->ComponentTemplate;
			}
		}

		return nullptr;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
