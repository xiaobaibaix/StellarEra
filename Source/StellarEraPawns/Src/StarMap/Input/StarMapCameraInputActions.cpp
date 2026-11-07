// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraInputActions.h"

#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "StellarEraInputActionBuilder.h"

void FStarMapCameraInputActions::Build(UObject* Outer, UInputMappingContext* Context)
{
	if (Look || !Context)
	{
		return;
	}

	Look = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_Look"), EInputActionValueType::Axis2D);
	LookActivate = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_LookActivate"), EInputActionValueType::Boolean);
	LookMiddle = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_LookMiddle"), EInputActionValueType::Boolean);
	Zoom = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_Zoom"), EInputActionValueType::Axis1D);
	EditModeToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_EditModeToggle"), EInputActionValueType::Boolean);
	BrushSize = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_BrushSize"), EInputActionValueType::Axis1D);
	MineralViewToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_MineralViewToggle"), EInputActionValueType::Boolean);
	MineralShowAll = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_MineralShowAll"), EInputActionValueType::Boolean);
	StarMapSelect = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_Select"), EInputActionValueType::Boolean);
	StarMapToggle = StellarEraInput::MakeAction(Outer, TEXT("IA_StarMap_StarMapToggle"), EInputActionValueType::Boolean);
	MineralFilter.Reset();
	for (int32 Slot = 1; Slot <= 9; ++Slot)
	{
		MineralFilter.Add(StellarEraInput::MakeAction(Outer,
			*FString::Printf(TEXT("IA_StarMap_MineralFilter%d"), Slot),
			EInputActionValueType::Boolean));
	}

	// 右键 = 转头，中键 = 绕行。两个键都要各映射成布尔动作 —— Look 只有位移，
	// 分不清按的是哪个键。
	StellarEraInput::MapKey(Context, Look, EKeys::Mouse2D);
	StellarEraInput::MapKey(Context, LookActivate, EKeys::RightMouseButton);
	StellarEraInput::MapKey(Context, LookMiddle, EKeys::MiddleMouseButton);

	// 滚轮是轴键（每格 ±1），不是按键，所以映射到 Axis1D 的动作上直接取值。
	// 不需要任何修饰器：方向约定（上滚 = 拉近）由星图相机自己的 AddZoom 负责。
	StellarEraInput::MapKey(Context, Zoom, EKeys::MouseWheelAxis);

	StellarEraInput::MapKey(Context, EditModeToggle, EKeys::G);

	// 笔刷尺寸：滚轮已归缩放，尺寸另占几对键。`]` / `=` / 小键盘 `+` 大一档，
	// `[` / `-` / 小键盘 `-` 小一档（接 Negate 出 −1）。取物理键，Shift+`=` 也生效。
	StellarEraInput::MapKey(Context, BrushSize, EKeys::RightBracket);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Equals);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Add);
	StellarEraInput::MapKey(Context, BrushSize, EKeys::LeftBracket, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Hyphen, { StellarEraInput::MakeNegate(Outer) });
	StellarEraInput::MapKey(Context, BrushSize, EKeys::Subtract, { StellarEraInput::MakeNegate(Outer) });

	// 矿物透视：H 进出；数字键 1..9 切换前 9 个元素，0 全部打开。
	StellarEraInput::MapKey(Context, MineralViewToggle, EKeys::H);

	const FKey DigitKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	for (int32 Slot = 0; Slot < MineralFilter.Num(); ++Slot)
	{
		StellarEraInput::MapKey(Context, MineralFilter[Slot], DigitKeys[Slot]);
	}
	StellarEraInput::MapKey(Context, MineralShowAll, EKeys::Zero);

	// 左键选择 / M 在角色 / 飞船表里也各有一份（进星图的入口）；同一 IMC 里谁被
	// Possess 谁那条生效。
	StellarEraInput::MapKey(Context, StarMapSelect, EKeys::LeftMouseButton);
	StellarEraInput::MapKey(Context, StarMapToggle, EKeys::M);
}
