// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraInputActions.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	/** 造一个运行时输入动作。ValueType 决定回调里 Get<T>() 的模板参数。 */
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	/** 把一个按键映射到动作，并按顺序挂上修饰器。 */
	void MapKey(
		UInputMappingContext* Context,
		UInputAction* Action,
		const FKey& Key,
		const TArray<UInputModifier*>& Modifiers = TArray<UInputModifier*>())
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		for (UInputModifier* Modifier : Modifiers)
		{
			Mapping.Modifiers.Add(Modifier);
		}
	}

	UInputModifier* MakeNegate(UObject* Outer)
	{
		return NewObject<UInputModifierNegate>(Outer);
	}

	/** Swizzle(YXZ) 把按键的 X 轴值搬到 Y 轴，用于把 WASD 合成一个二维向量。 */
	UInputModifier* MakeSwizzleYXZ(UObject* Outer)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	}
}

void FStellarEraInputActions::Build(UObject* Outer)
{
	if (MappingContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(Outer, TEXT("IMC_StellarEra"));

	Move = MakeAction(Outer, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	Look = MakeAction(Outer, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	LookActivate = MakeAction(Outer, TEXT("IA_LookActivate"), EInputActionValueType::Boolean);
	LookMiddle = MakeAction(Outer, TEXT("IA_LookMiddle"), EInputActionValueType::Boolean);
	Jump = MakeAction(Outer, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	Sprint = MakeAction(Outer, TEXT("IA_Sprint"), EInputActionValueType::Boolean);

	Thrust = MakeAction(Outer, TEXT("IA_Thrust"), EInputActionValueType::Axis1D);
	Roll = MakeAction(Outer, TEXT("IA_Roll"), EInputActionValueType::Axis1D);
	Vertical = MakeAction(Outer, TEXT("IA_Vertical"), EInputActionValueType::Axis1D);
	Boost = MakeAction(Outer, TEXT("IA_Boost"), EInputActionValueType::Boolean);
	Cruise = MakeAction(Outer, TEXT("IA_Cruise"), EInputActionValueType::Boolean);
	DebugViewToggle = MakeAction(Outer, TEXT("IA_DebugViewToggle"), EInputActionValueType::Boolean);
	Zoom = MakeAction(Outer, TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	Interact = MakeAction(Outer, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	StarMapToggle = MakeAction(Outer, TEXT("IA_StarMapToggle"), EInputActionValueType::Boolean);
	StarMapSelect = MakeAction(Outer, TEXT("IA_StarMapSelect"), EInputActionValueType::Boolean);
	EditModeToggle = MakeAction(Outer, TEXT("IA_EditModeToggle"), EInputActionValueType::Boolean);
	MineralViewToggle = MakeAction(Outer, TEXT("IA_MineralViewToggle"), EInputActionValueType::Boolean);
	MineralShowAll = MakeAction(Outer, TEXT("IA_MineralShowAll"), EInputActionValueType::Boolean);
	MineralFilter.Reset();
	for (int32 Slot = 1; Slot <= 9; ++Slot)
	{
		MineralFilter.Add(MakeAction(Outer,
			*FString::Printf(TEXT("IA_MineralFilter%d"), Slot),
			EInputActionValueType::Boolean));
	}
	BrushSize = MakeAction(Outer, TEXT("IA_BrushSize"), EInputActionValueType::Axis1D);

	// WASD 合成一个二维向量：W/S 走 X 轴，A/D 先用 Swizzle 搬到 Y 轴再取反。
	// 修饰器按数组顺序依次作用，所以 A 是“先 Swizzle 再 Negate”。
	MapKey(MappingContext, Move, EKeys::W);
	MapKey(MappingContext, Move, EKeys::S, { MakeNegate(Outer) });
	MapKey(MappingContext, Move, EKeys::D, { MakeSwizzleYXZ(Outer) });
	MapKey(MappingContext, Move, EKeys::A, { MakeSwizzleYXZ(Outer), MakeNegate(Outer) });

	// 鼠标位移本身一直有值，能不能用、拿去干什么由各 Pawn 自己按按住的键分派：
	// 右键 = 转头（Aim / 角色视角），左键 = 星图绕行，中键 = 绕行（编辑 / 矿物透视下
	// 左键被借走时用）。三个键都要分别映射成布尔动作 —— Look 只有位移，分不清按的
	// 是哪个键。
	MapKey(MappingContext, Look, EKeys::Mouse2D);
	MapKey(MappingContext, LookActivate, EKeys::RightMouseButton);
	MapKey(MappingContext, LookMiddle, EKeys::MiddleMouseButton);

	MapKey(MappingContext, Jump, EKeys::SpaceBar);
	MapKey(MappingContext, Sprint, EKeys::LeftShift);

	// 飞船：W 推力 / S 制动，D/A 横滚，Space/LeftControl 升降，LeftShift 加速，Tab 巡航。
	// 与角色的 WASD、LeftShift 共用按键没问题——同一个 IMC 里的动作互不吞输入，
	// 分派由 Pawn 类型决定（Sprint 只认 APlanetCharacter，Boost 只认飞船）。
	MapKey(MappingContext, Thrust, EKeys::W);
	MapKey(MappingContext, Thrust, EKeys::S, { MakeNegate(Outer) });
	MapKey(MappingContext, Roll, EKeys::D);
	MapKey(MappingContext, Roll, EKeys::A, { MakeNegate(Outer) });
	MapKey(MappingContext, Vertical, EKeys::SpaceBar);
	MapKey(MappingContext, Vertical, EKeys::LeftControl, { MakeNegate(Outer) });
	MapKey(MappingContext, Boost, EKeys::LeftShift);
	MapKey(MappingContext, Cruise, EKeys::Tab);

	// B 键：飞船调试视角（落地检测球 + 引力合力箭头）。角色 / 星图相机上
	// 没有对应处理，按下无反应。
	MapKey(MappingContext, DebugViewToggle, EKeys::B);

	// 滚轮是轴键（每格 ±1），不是按键，所以映射到 Axis1D 的动作上直接取值。
	// 不需要任何修饰器：方向约定（上滚 = 拉近）由各 Pawn 的 AddZoom 负责。
	MapKey(MappingContext, Zoom, EKeys::MouseWheelAxis);

	MapKey(MappingContext, Interact, EKeys::E);
	MapKey(MappingContext, StarMapToggle, EKeys::M);
	MapKey(MappingContext, StarMapSelect, EKeys::LeftMouseButton);
	MapKey(MappingContext, EditModeToggle, EKeys::G);

	// 矿物透视：H 进出；数字键 1..9 切换前 9 个元素，0 全部打开。
	MapKey(MappingContext, MineralViewToggle, EKeys::H);

	const FKey DigitKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	for (int32 Slot = 0; Slot < MineralFilter.Num(); ++Slot)
	{
		MapKey(MappingContext, MineralFilter[Slot], DigitKeys[Slot]);
	}
	MapKey(MappingContext, MineralShowAll, EKeys::Zero);

	// 笔刷尺寸：星图里滚轮已归缩放（角色编辑模式下滚轮同样调笔刷），尺寸因此另占
	// 几对键，两个模式都能用。`]` / `=` / 小键盘 `+` = 大一档；
	// `[` / `-` / 小键盘 `-` = 小一档。负值那三行接 Negate，同一组映射直接出 −1。
	// `=` 与 `-` 取的是物理键，所以 Shift+`=`（即 `+`）同样生效。
	MapKey(MappingContext, BrushSize, EKeys::RightBracket);
	MapKey(MappingContext, BrushSize, EKeys::Equals);
	MapKey(MappingContext, BrushSize, EKeys::Add);
	MapKey(MappingContext, BrushSize, EKeys::LeftBracket, { MakeNegate(Outer) });
	MapKey(MappingContext, BrushSize, EKeys::Hyphen, { MakeNegate(Outer) });
	MapKey(MappingContext, BrushSize, EKeys::Subtract, { MakeNegate(Outer) });
}
