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

	// 滚轮是轴键（每格 ±1），不是按键，所以映射到 Axis1D 的动作上直接取值。
	// 不需要任何修饰器：方向约定（上滚 = 拉近）由 APlanetCharacter::AddZoom 负责。
	StellarEraInput::MapKey(Context, Zoom, EKeys::MouseWheelAxis);

	StellarEraInput::MapKey(Context, EditModeToggle, EKeys::G);

	// 笔刷尺寸：星图里滚轮已归缩放（角色编辑模式下滚轮同样调笔刷），尺寸因此另占
	// 几对键，两个模式都能用。`]` / `=` / 小键盘 `+` = 大一档；
	// `[` / `-` / 小键盘 `-` = 小一档。负值那三行接 Negate，同一组映射直接出 −1。
	// `=` 与 `-` 取的是物理键，所以 Shift+`=`（即 `+`）同样生效。
	StellarEraInput::MapKey(Context, BrushSize, EKeys::RightBracket);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Equals);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Add);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::LeftBracket, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Hyphen, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Subtract, { StellarEraInput::MakeNegate(Outer) });
}
