// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacterInputActions.h"

#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "StellarEraInputActionBuilder.h"

void FPlanetCharacterInputActions::Build(UObject* Outer, UInputMappingContext* Context)
{
	if (Move || !Context)
	{
		return;
	}

	Move = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_Move"), EInputActionValueType::Axis2D);
	Look = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_Look"), EInputActionValueType::Axis2D);
	LookActivate = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_LookActivate"), EInputActionValueType::Boolean);
	Jump = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_Jump"), EInputActionValueType::Boolean);
	Sprint = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_Sprint"), EInputActionValueType::Boolean);
	Zoom = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_Zoom"), EInputActionValueType::Axis1D);
	EditModeToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_EditModeToggle"), EInputActionValueType::Boolean);
	BrushSize = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_BrushSize"), EInputActionValueType::Axis1D);
	EditPrimary = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_EditPrimary"), EInputActionValueType::Boolean);
	Interact = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_Interact"), EInputActionValueType::Boolean);
	StarMapToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_Character_StarMapToggle"), EInputActionValueType::Boolean);

	// WASD 合成一个二维向量：W/S 走 X 轴，A/D 先用 Swizzle 搬到 Y 轴再取反。
	// 修饰器按数组顺序依次作用，所以 A 是“先 Swizzle 再 Negate”。
	StellarEraInput::MapKey(Context, Move, EKeys::W);
	StellarEraInput::MapKey(Context, Move, EKeys::S, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, Move, EKeys::D, { StellarEraInput::MakeSwizzleYXZ(Outer) });
	StellarEraInput::MapKey(Context, Move, EKeys::A, { StellarEraInput::MakeSwizzleYXZ(Outer), StellarEraInput::MakeNegate(Outer) });

	// 鼠标位移本身一直有值，能不能用由角色自己按按住的键分派：右键 = 转头。
	StellarEraInput::MapKey(Context, Look, EKeys::Mouse2D);
	StellarEraInput::MapKey(Context, LookActivate, EKeys::RightMouseButton);

	StellarEraInput::MapKey(Context, Jump, EKeys::SpaceBar);
	StellarEraInput::MapKey(Context, Sprint, EKeys::LeftShift);

	// 滚轮是轴键（每格 ±1）；方向约定（上滚 = 拉近）由 APlanetCharacter::AddZoom 负责。
	StellarEraInput::MapKey(Context, Zoom, EKeys::MouseWheelAxis);

	StellarEraInput::MapKey(Context, EditModeToggle, EKeys::G);

	// 笔刷尺寸另占几对键，两个模式都能用：`]` / `=` / 小键盘 `+` 大一档，`[` / `-` 小一档
	// （负值三行接 Negate）；`=` 与 `-` 取物理键，所以 Shift+`=` 也生效。
	StellarEraInput::MapKey(Context, BrushSize, EKeys::RightBracket);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Equals);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Add);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::LeftBracket, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Hyphen, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Subtract, { StellarEraInput::MakeNegate(Outer) });

	// 左键 / E / M 在别的表里也有同名动作，同一 IMC 里互不吞输入，谁被 Possess 谁响应。
	// 左键在角色这里只服务编辑：点笔刷按钮、挖掘。
	StellarEraInput::MapKey(Context, EditPrimary, EKeys::LeftMouseButton);
	StellarEraInput::MapKey(Context, Interact, EKeys::E);
	StellarEraInput::MapKey(Context, StarMapToggle, EKeys::M);
}
