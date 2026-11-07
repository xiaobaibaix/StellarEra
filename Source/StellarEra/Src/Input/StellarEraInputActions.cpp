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

	// 顺序无所谓：引擎按动作分派，同键上的多个动作互不影响，
	// 真正响应的是被 Possess 那个 Pawn 的绑定。
	Character.Build(Outer, MappingContext);
	Ship.Build(Outer, MappingContext);
	StarMap.Build(Outer, MappingContext);
}
