// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipInputActions.h"

#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "StellarEraInputActionBuilder.h"

void FSpaceshipInputActions::Build(UObject* Outer, UInputMappingContext* Context)
{
	if (Look || !Context)
	{
		return;
	}

	Look = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Look"), EInputActionValueType::Axis2D);
	Thrust = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Thrust"), EInputActionValueType::Axis1D);
	Roll = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Roll"), EInputActionValueType::Axis1D);
	Vertical = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Vertical"), EInputActionValueType::Axis1D);
	Boost = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Boost"), EInputActionValueType::Boolean);
	Cruise = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Cruise"), EInputActionValueType::Boolean);
	DebugViewToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_DebugViewToggle"), EInputActionValueType::Boolean);
	Zoom = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Zoom"), EInputActionValueType::Axis1D);
	Interact = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_Interact"), EInputActionValueType::Boolean);
	StarMapToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_Ship_StarMapToggle"), EInputActionValueType::Boolean);

	// 鼠标位移本身一直有值，能不能用、拿去干什么由各 Pawn 自己按按住的键分派。
	StellarEraInput::MapKey(Context, Look, EKeys::Mouse2D);

	// 飞船：W 推力 / S 制动，D/A 横滚，Space/LeftControl 升降，LeftShift 加速，Tab 巡航。
	StellarEraInput::MapKey(Context, Thrust, EKeys::W);
	StellarEraInput::MapKey(Context, Thrust, EKeys::S, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, Roll, EKeys::D);
	StellarEraInput::MapKey(Context, Roll, EKeys::A, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, Vertical, EKeys::SpaceBar);
	StellarEraInput::MapKey(Context, Vertical, EKeys::LeftControl, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, Boost, EKeys::LeftShift);
	StellarEraInput::MapKey(Context, Cruise, EKeys::Tab);

	// B 键：飞船调试视角（落地检测球 + 引力合力箭头）。
	StellarEraInput::MapKey(Context, DebugViewToggle, EKeys::B);

	// 滚轮是轴键（每格 ±1）；方向约定（上滚 = 拉近）由 AddZoom 负责，不需要修饰器。
	StellarEraInput::MapKey(Context, Zoom, EKeys::MouseWheelAxis);

	// E / M 在角色与星图相机表里也各有一条；真正响应的是被 Possess 那个 Pawn 绑的。
	StellarEraInput::MapKey(Context, Interact, EKeys::E);
	StellarEraInput::MapKey(Context, StarMapToggle, EKeys::M);
}
