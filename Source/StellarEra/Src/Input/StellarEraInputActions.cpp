// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraInputActions.h"

#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "StellarEraInputActionBuilder.h"

void FStellarEraInputActions::Build(UObject* Outer)
{
	if (MappingContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(Outer, TEXT("IMC_StellarEra"));

	// 三张表往同一个 IMC 里注册映射。顺序无所谓 —— 引擎按动作分派，同一个键上挂
	// 几个不同动作互不影响，真正响应的是被 Possess 那个 Pawn 的绑定。
	// E / M / 左键各自在需要它的表里各有一条（语义不同：角色登船、飞船离船）。
	Character.Build(Outer, MappingContext);
	Ship.Build(Outer, MappingContext);
	StarMap.Build(Outer, MappingContext);
}
