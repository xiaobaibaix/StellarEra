// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraInputActions.h"

#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "StellarEraInputActionBuilder.h"

void FControllerInputActions::Build(UObject* Outer, UInputMappingContext* Context)
{
	if (Interact || !Context)
	{
		return;
	}

	Interact = StellarEraInput::MakeAction(Outer, TEXT("IA_Controller_Interact"), EInputActionValueType::Boolean);
	StarMapToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_Controller_StarMapToggle"), EInputActionValueType::Boolean);
	StarMapSelect = StellarEraInput::MakeAction(Outer, TEXT("IA_Controller_StarMapSelect"), EInputActionValueType::Boolean);

	StellarEraInput::MapKey(Context, Interact, EKeys::E);
	StellarEraInput::MapKey(Context, StarMapToggle, EKeys::M);
	StellarEraInput::MapKey(Context, StarMapSelect, EKeys::LeftMouseButton);
}

void FStellarEraInputActions::Build(UObject* Outer)
{
	if (MappingContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(Outer, TEXT("IMC_StellarEra"));

	// 四张表往同一个 IMC 里注册映射。顺序无所谓 —— 引擎按动作分派，
	// 同一个键上挂几个不同动作互不影响，真正响应的是被 Possess 那个 Pawn 的绑定。
	Character.Build(Outer, MappingContext);
	Ship.Build(Outer, MappingContext);
	StarMap.Build(Outer, MappingContext);
	Controller.Build(Outer, MappingContext);
}
