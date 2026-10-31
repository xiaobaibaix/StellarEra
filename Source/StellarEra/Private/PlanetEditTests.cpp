// 行星编辑模式的纯函数测试。
//
// 这里只放不需要世界、不需要 PIE 的断言：笔刷几何、按钮布局、网格几何。
// 需要真实体素数据的部分（密度采样、保存）只能 PIE 手验，见计划任务 8。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "PlanetEditBrush.h"
#include "PlanetEditUI.h"
#include "PlanetEditGridGeometry.h"
#include "VoxelPlanetActor.h"
#include "PlanetSurfaceGridComponent.h"
#include "PlanetEditComponent.h"
#include "PlanetCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushSphereCenterTest,
	"StellarEra.Planet.EditBrush.SphereCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushSphereCenterTest::RunTest(const FString& Parameters)
{
	const FVector Hit(100.f, 200.f, 300.f);
	const FVector Normal(0.f, 0.f, 1.f);

	TestTrue(TEXT("挖：球心就是命中点"),
		PlanetEditBrush::GetEditCenterWorld(Hit, Normal, 2, 100.f, false).Equals(Hit));

	const FVector Expected = Hit + FVector(0.f, 0.f, 100.f);
	TestTrue(TEXT("放：球心沿法线外移一个半径"),
		PlanetEditBrush::GetEditCenterWorld(Hit, Normal, 2, 100.f, true).Equals(Expected));

	TestTrue(TEXT("放：退化法线不偏移"),
		PlanetEditBrush::GetEditCenterWorld(Hit, FVector::ZeroVector, 2, 100.f, true).Equals(Hit));

	TestEqual(TEXT("尺寸 2、体素 100cm 的半边长"),
		PlanetEditBrush::GetHalfExtentCm(2, 100.f), 100.f);
	TestEqual(TEXT("尺寸 1、体素 100cm 的半边长"),
		PlanetEditBrush::GetHalfExtentCm(1, 100.f), 50.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushBoxBoundsTest,
	"StellarEra.Planet.EditBrush.BoxBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushBoxBoundsTest::RunTest(const FString& Parameters)
{
	const FPlanetEditBox One = PlanetEditBrush::MakeBox(FVector(10.4f, 10.4f, 10.4f), 1);
	TestEqual(TEXT("尺寸 1 Min.X"), One.Min.X, 10);
	TestEqual(TEXT("尺寸 1 Max.X"), One.Max.X, 10);

	const FPlanetEditBox Two = PlanetEditBrush::MakeBox(FVector(10.4f, -3.6f, 0.4f), 2);
	TestEqual(TEXT("尺寸 2 Min.X"), Two.Min.X, 10);
	TestEqual(TEXT("尺寸 2 Max.X"), Two.Max.X, 11);
	TestEqual(TEXT("尺寸 2 Min.Y"), Two.Min.Y, -4);
	TestEqual(TEXT("尺寸 2 Max.Y"), Two.Max.Y, -3);
	TestEqual(TEXT("尺寸 2 X 方向宽度（含端点）"), Two.Max.X - Two.Min.X + 1, 2);

	const FPlanetEditBox Eight = PlanetEditBrush::MakeBox(FVector(100.f, 100.f, 100.f), 8);
	TestEqual(TEXT("尺寸 8 Min.X"), Eight.Min.X, 97);
	TestEqual(TEXT("尺寸 8 Max.X"), Eight.Max.X, 104);
	TestEqual(TEXT("尺寸 8 X 方向宽度（含端点）"), Eight.Max.X - Eight.Min.X + 1, 8);

	return true;
}

// 含端点 → 开区间的换算回归护栏。
//
// 这一步（FVoxelIntBox 的 Max 是开区间，所以 Max 要 +1）在设计与计划里反复
// 写错过；只靠注释保护的话，一次「清理掉多余的 +1」的改动能编译、能过其余
// 全部测试，却让每个方块笔刷悄悄少挖一格。这里把换算钉成纯函数并断言格数。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushVoxelBoxTest,
	"StellarEra.Planet.EditBrush.VoxelBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushVoxelBoxTest::RunTest(const FString& Parameters)
{
	// 中心故意选非对称值，避免「把 X/Y 写反」之类的错误恰好通过。
	const FVector Center(3.2, -7.6, 0.4);

	for (const int32 Size : {1, 2, 8})
	{
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(Center, Size);
		const FPlanetEditVoxelBox VoxelBox = PlanetEditBrush::MakeVoxelBox(Box);

		const FIntVector ExpectedSize(Size);
		TestTrue(*FString::Printf(TEXT("尺寸 %d：格数 == MaxExclusive - Min"), Size),
			VoxelBox.GetSize() == ExpectedSize);
		TestTrue(*FString::Printf(TEXT("尺寸 %d：MaxExclusive - Min 每轴都等于 Size"), Size),
			VoxelBox.MaxExclusive - VoxelBox.Min == ExpectedSize);
		TestTrue(*FString::Printf(TEXT("尺寸 %d：MaxExclusive == Max + 1"), Size),
			VoxelBox.MaxExclusive == Box.Max + FIntVector(1));
		TestTrue(*FString::Printf(TEXT("尺寸 %d：Min 原样透传"), Size),
			VoxelBox.Min == Box.Min);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushUnionVoxelBoxTest,
	"StellarEra.Planet.EditBrush.UnionVoxelBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushUnionVoxelBoxTest::RunTest(const FString& Parameters)
{
	// FPlanetEditVoxelBox 的默认值就是「无范围」：连挖期间 PendingRenderBounds
	// 从它起步，所以「无效盒参与并集」这条必须有明确行为，不能碰运气。
	const FPlanetEditVoxelBox Invalid;

	TestFalse(TEXT("默认值无效"), PlanetEditBrush::IsValidVoxelBox(Invalid));

	// 某一轴退化成零宽也算无效（判据是三轴都 > 0，不是任意一轴）。
	FPlanetEditVoxelBox Flat;
	Flat.Min = FIntVector(1, 2, 3);
	Flat.MaxExclusive = FIntVector(5, 2, 9);
	TestFalse(TEXT("Y 轴零宽 → 无效"), PlanetEditBrush::IsValidVoxelBox(Flat));

	FPlanetEditVoxelBox A;
	A.Min = FIntVector(-10, 4, 0);
	A.MaxExclusive = FIntVector(2, 30, 8);
	TestTrue(TEXT("正常盒有效"), PlanetEditBrush::IsValidVoxelBox(A));

	// 无效 ⊕ 有效 = 有效（就是那个有效的）。两个都无效 = 无效。
	const FPlanetEditVoxelBox FromInvalid = PlanetEditBrush::UnionVoxelBox(Invalid, A);
	TestTrue(TEXT("无效 ⊕ A == A"), FromInvalid.Min == A.Min && FromInvalid.MaxExclusive == A.MaxExclusive);
	const FPlanetEditVoxelBox ToInvalid = PlanetEditBrush::UnionVoxelBox(A, Invalid);
	TestTrue(TEXT("A ⊕ 无效 == A"), ToInvalid.Min == A.Min && ToInvalid.MaxExclusive == A.MaxExclusive);
	TestFalse(TEXT("无效 ⊕ 无效 仍然无效"),
		PlanetEditBrush::IsValidVoxelBox(PlanetEditBrush::UnionVoxelBox(Invalid, Invalid)));

	// 两笔真正相交的挖掘：各轴取 min(Min) / max(MaxExclusive)。
	FPlanetEditVoxelBox B;
	B.Min = FIntVector(6, -3, 5);
	B.MaxExclusive = FIntVector(9, 12, 6);
	const FPlanetEditVoxelBox Union = PlanetEditBrush::UnionVoxelBox(A, B);
	TestTrue(TEXT("并集 Min 取各轴较小者"), Union.Min == FIntVector(-10, -3, 0));
	TestTrue(TEXT("并集 MaxExclusive 取各轴较大者"), Union.MaxExclusive == FIntVector(9, 30, 8));

	// 并集必须同时包住两个输入（逐轴包含），这是它被用来刷渲染的唯一要求。
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		TestTrue(*FString::Printf(TEXT("轴 %d：并集下界 ≤ A 与 B"), Axis),
			Union.Min[Axis] <= A.Min[Axis] && Union.Min[Axis] <= B.Min[Axis]);
		TestTrue(*FString::Printf(TEXT("轴 %d：并集上界 ≥ A 与 B"), Axis),
			Union.MaxExclusive[Axis] >= A.MaxExclusive[Axis]
			&& Union.MaxExclusive[Axis] >= B.MaxExclusive[Axis]);
	}

	// 包含关系（含端点）：完整包含一个盒之后，并集等于较大的那个。
	const FPlanetEditVoxelBox Nested = PlanetEditBrush::UnionVoxelBox(A, FromInvalid);
	TestTrue(TEXT("与自身子集求并返回自身"),
		Nested.Min == A.Min && Nested.MaxExclusive == A.MaxExclusive);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushVoxelBoxesIntersectTest,
	"StellarEra.Planet.Editor.Brush.VoxelBoxesIntersect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushVoxelBoxesIntersectTest::RunTest(const FString& Parameters)
{
	// FPlanetEditVoxelBox 没有构造函数，用局部小工具拼，别为测试去改公开结构体。
	auto Box = [](const FIntVector& Mn, const FIntVector& MaxEx)
	{
		FPlanetEditVoxelBox Out;
		Out.Min = Mn;
		Out.MaxExclusive = MaxEx;
		return Out;
	};

	const FPlanetEditVoxelBox A = Box(FIntVector(0, 0, 0), FIntVector(10, 10, 10));

	TestTrue(TEXT("自己与自己相交"), PlanetEditBrush::VoxelBoxesIntersect(A, A));
	TestTrue(TEXT("部分重叠"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(5, 5, 5), FIntVector(15, 15, 15))));
	TestTrue(TEXT("完全包含"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(2, 2, 2), FIntVector(4, 4, 4))));

	// 右端开：贴边相邻没有公共格。
	TestFalse(TEXT("X 轴贴边不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(10, 0, 0), FIntVector(20, 10, 10))));
	TestFalse(TEXT("Y 轴贴边不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(0, 10, 0), FIntVector(10, 20, 10))));
	TestFalse(TEXT("Z 轴贴边不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(0, 0, 10), FIntVector(10, 10, 20))));
	TestFalse(TEXT("离得远不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(100, 0, 0), FIntVector(110, 10, 10))));

	// 无效盒子一律 false。
	TestFalse(TEXT("无效盒子不相交"),
		PlanetEditBrush::VoxelBoxesIntersect(A, FPlanetEditVoxelBox()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushClampSizeTest,
	"StellarEra.Planet.EditBrush.ClampSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushClampSizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("-3 夹到 1"), PlanetEditBrush::ClampSize(-3), 1);
	TestEqual(TEXT("0 夹到 1"), PlanetEditBrush::ClampSize(0), 1);
	TestEqual(TEXT("1 不变"), PlanetEditBrush::ClampSize(1), 1);
	TestEqual(TEXT("8 不变"), PlanetEditBrush::ClampSize(8), 8);
	TestEqual(TEXT("9 夹到 8"), PlanetEditBrush::ClampSize(9), 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushButtonsTest,
	"StellarEra.UI.EditBrushButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushButtonsTest::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(1920.f, 1080.f);
	FVector2D Sphere = FVector2D::ZeroVector;
	FVector2D Cube = FVector2D::ZeroVector;
	PlanetEditUI::GetBrushButtonRects(Viewport, Sphere, Cube);

	// FVector2D 的分量在 UE5 里是 double，显式转 float 再比，
	// 免得 TestEqual 的 float / double 重载挑不出来。
	TestEqual(TEXT("球体按钮 X"), static_cast<float>(Sphere.X), 908.f, 0.01f);
	TestEqual(TEXT("按钮顶边 Y"), static_cast<float>(Sphere.Y), 992.f, 0.01f);
	TestEqual(TEXT("两个按钮同高"),
		static_cast<float>(Sphere.Y), static_cast<float>(Cube.Y), 0.01f);
	TestEqual(TEXT("方块按钮紧跟球体按钮"),
		static_cast<float>(Cube.X),
		static_cast<float>(Sphere.X) + PlanetEditUI::ButtonSize + PlanetEditUI::ButtonGap,
		0.01f);
	TestTrue(TEXT("两个按钮不重叠"),
		Cube.X >= Sphere.X + PlanetEditUI::ButtonSize);

	int32 Index = INDEX_NONE;
	TestTrue(TEXT("球体中心命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + 24.f, Sphere.Y + 24.f), Index));
	TestEqual(TEXT("球体中心下标 = 0"), Index, 0);

	Index = INDEX_NONE;
	TestTrue(TEXT("正方体中心命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Cube.X + 24.f, Cube.Y + 24.f), Index));
	TestEqual(TEXT("正方体中心下标 = 1"), Index, 1);

	Index = INDEX_NONE;
	TestFalse(TEXT("两个按钮之间的空隙不命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + PlanetEditUI::ButtonSize + PlanetEditUI::ButtonGap * 0.5f,
			Sphere.Y + 24.f), Index));

	Index = INDEX_NONE;
	TestFalse(TEXT("按钮上方不命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + 24.f, Sphere.Y - 1.f), Index));

	// 按钮覆盖 [X, X+Size)：右/下边界不在按钮内，与 DrawRect 的像素一致。
	Index = INDEX_NONE;
	TestFalse(TEXT("半开区间：球体按钮右边缘不命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + PlanetEditUI::ButtonSize, Sphere.Y + 24.f), Index));
	TestEqual(TEXT("右边缘不写下标"), Index, INDEX_NONE);

	Index = INDEX_NONE;
	TestFalse(TEXT("半开区间：球体按钮下边缘不命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X, Sphere.Y + PlanetEditUI::ButtonSize), Index));
	TestEqual(TEXT("下边缘不写下标"), Index, INDEX_NONE);

	Index = INDEX_NONE;
	TestFalse(TEXT("下边缘再往下 1 像素不命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X, Sphere.Y + PlanetEditUI::ButtonSize + 1.f), Index));
	TestEqual(TEXT("未命中时不写下标"), Index, INDEX_NONE);

	FVector2D NarrowSphere = FVector2D::ZeroVector;
	FVector2D NarrowCube = FVector2D::ZeroVector;
	PlanetEditUI::GetBrushButtonRects(FVector2D(100.f, 100.f), NarrowSphere, NarrowCube);
	TestEqual(TEXT("视口过窄时球体按钮贴左边"),
		static_cast<float>(NarrowSphere.X), 0.f, 0.01f);
	TestEqual(TEXT("方块按钮紧贴球体按钮（窄视口）"),
		static_cast<float>(NarrowCube.X),
		PlanetEditUI::ButtonSize + PlanetEditUI::ButtonGap, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditGridGeometryTest,
	"StellarEra.Planet.EditGrid.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditGridGeometryTest::RunTest(const FString& Parameters)
{
	TArray<FPlanetEditGridLine> Lines;
	PlanetEditGrid::BuildLines(5.f, Lines);

	// 5°：72 条经线 + 35 条纬线（两极不生成退化纬线）。
	// 后面按下标取首尾线，条数不对就先失败退出，避免越界。
	if (!TestEqual(TEXT("5° 网格线总数"), Lines.Num(), 107))
	{
		return false;
	}

	int32 ClosedCount = 0;
	int32 MeridianPoints = 0;
	int32 ParallelPoints = 0;
	for (const FPlanetEditGridLine& Line : Lines)
	{
		if (Line.bClosed)
		{
			++ClosedCount;
			ParallelPoints += Line.Directions.Num();
		}
		else
		{
			MeridianPoints += Line.Directions.Num();
		}
	}
	TestEqual(TEXT("闭合纬线数"), ClosedCount, 35);
	TestEqual(TEXT("经线总采样点（72 × 37）"), MeridianPoints, 2664);
	TestEqual(TEXT("纬线总采样点（35 × 72）"), ParallelPoints, 2520);

	// 输出顺序固定：前 72 条是开放经线，后 35 条是闭合纬线。
	int32 OrderMismatchCount = 0;
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const bool bExpectClosed = LineIndex >= 72;
		if (Lines[LineIndex].bClosed != bExpectClosed)
		{
			++OrderMismatchCount;
		}
	}
	TestEqual(TEXT("前 72 条是开放经线、后 35 条是闭合纬线"), OrderMismatchCount, 0);

	// 经线从南极到北极：首点为 (0,0,-1)，末点为 (0,0,+1)。
	// 这条同时能抓住纬度/经度写反或 Z 轴反向。
	if (!TestEqual(TEXT("5° 首条经线采样点数"), Lines[0].Directions.Num(), 37))
	{
		return false;
	}
	TestTrue(TEXT("5° 首条经线起点 ≈ (0,0,-1)"),
		Lines[0].Directions[0].Equals(FVector(0.f, 0.f, -1.f), 1.e-3f));
	TestTrue(TEXT("5° 首条经线终点 ≈ (0,0,+1)"),
		Lines[0].Directions.Last().Equals(FVector(0.f, 0.f, 1.f), 1.e-3f));

	// 每个方向都是单位向量：采样半径按方向缩放，方向长度必须为 1。
	for (const FVector& Dir : Lines[0].Directions)
	{
		TestTrue(TEXT("经线方向是单位向量"), FMath::IsNearlyEqual(Dir.Size(), 1.f, 1.e-3f));
	}

	// 纬线也要查单位长度，并且赤道纬线的所有点必须落在 z = 0 平面上。
	// 只查经线的话，纬度被误当成经度生成时仍可能通过。
	const FPlanetEditGridLine* EquatorLine = nullptr;
	for (const FPlanetEditGridLine& Line : Lines)
	{
		if (Line.bClosed && !Line.Directions.IsEmpty() &&
			FMath::IsNearlyZero(Line.Directions[0].Z, 1.e-3))
		{
			EquatorLine = &Line;
			break;
		}
	}
	TestNotNull(TEXT("5° 网格里有赤道纬线"), EquatorLine);
	if (EquatorLine != nullptr)
	{
		TestEqual(TEXT("赤道纬线采样点数"), EquatorLine->Directions.Num(), 72);
		for (const FVector& Dir : EquatorLine->Directions)
		{
			TestTrue(TEXT("纬线方向是单位向量"), FMath::IsNearlyEqual(Dir.Size(), 1.f, 1.e-3f));
			TestTrue(TEXT("赤道纬线所有点 |Z| < 1e-3"), FMath::IsNearlyZero(Dir.Z, 1.e-3));
		}
		TestTrue(TEXT("赤道首点 ≈ (-1,0,0)"),
			EquatorLine->Directions[0].Equals(FVector(-1.f, 0.f, 0.f), 1.e-3f));
	}

	TArray<FPlanetEditGridLine> Coarse;
	PlanetEditGrid::BuildLines(90.f, Coarse);
	if (!TestEqual(TEXT("90° 网格线总数（4 经线 + 1 纬线）"), Coarse.Num(), 5))
	{
		return false;
	}
	TestEqual(TEXT("90° 经线各 3 点"), Coarse[0].Directions.Num(), 3);
	TestTrue(TEXT("90° 纬线闭合"), Coarse[4].bClosed);

	TestTrue(TEXT("同向可见"),
		PlanetEditGrid::IsDirectionVisible(FVector(1.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f)));
	TestFalse(TEXT("背向不可见"),
		PlanetEditGrid::IsDirectionVisible(FVector(-1.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f)));
	TestFalse(TEXT("两端都在背面时整段剔除"),
		PlanetEditGrid::IsSegmentVisible(FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f),
			FVector(0.f, 1.f, 0.f)));
	TestTrue(TEXT("一端在正面就保留整段"),
		PlanetEditGrid::IsSegmentVisible(FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f),
			FVector(1.f, 0.f, 0.f)));

	TArray<FPlanetEditGridLine> Clamped;
	PlanetEditGrid::BuildLines(0.f, Clamped);
	TestEqual(TEXT("步长 0 被夹到 1°：360 经线 + 179 纬线"), Clamped.Num(), 539);

	TArray<FPlanetEditGridLine> ClampedNegative;
	PlanetEditGrid::BuildLines(-5.f, ClampedNegative);
	TestEqual(TEXT("步长 -5° 被夹到 1°：539 条"), ClampedNegative.Num(), 539);

	// 上界是 90°（不是 1°）：180° 夹到 90°，得到 4 经线 + 1 纬线。
	TArray<FPlanetEditGridLine> ClampedTooLarge;
	PlanetEditGrid::BuildLines(180.f, ClampedTooLarge);
	TestEqual(TEXT("步长 180° 被夹到 90°：4 经线 + 1 纬线 = 5 条"), ClampedTooLarge.Num(), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetEditGridDefaultsTest,
	"StellarEra.Planet.EditGrid.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetEditGridDefaultsTest::RunTest(const FString& Parameters)
{
	// GetDefault<> 返回 const 指针，而 GetDefaultSubobjectByName 是非 const 成员；
	// 这里只读取，用 GetMutableDefault 取到可调用的 CDO 指针。
	AVoxelPlanetActor* PlanetCDO = GetMutableDefault<AVoxelPlanetActor>();
	if (!TestNotNull(TEXT("AVoxelPlanetActor CDO"), PlanetCDO))
	{
		return false;
	}

	const UPlanetSurfaceGridComponent* Grid = Cast<UPlanetSurfaceGridComponent>(
		PlanetCDO->GetDefaultSubobjectByName(TEXT("PlanetEditGrid")));
	if (!TestNotNull(TEXT("行星 CDO 上的 PlanetEditGrid 组件"), Grid))
	{
		return false;
	}

	TestEqual(TEXT("默认网格步长"), Grid->GridStepDegrees, 5.f, 0.01f);
	TestEqual(TEXT("默认表面偏移"), Grid->SurfaceOffsetCm, 3.f, 0.01f);
	TestEqual(TEXT("默认搜索步数"), Grid->MaxSearchSteps, 32);
	TestEqual(TEXT("默认重建防抖"), Grid->RebuildDebounceSeconds, 0.3f, 0.01f);
	TestFalse(TEXT("默认不可见"), Grid->IsGridVisible());
	TestFalse(TEXT("不可见时不 Tick"), Grid->PrimaryComponentTick.bStartWithTickEnabled);

	// 保存不再有防抖计时器（2026-10-28）：存档时机是「退出编辑模式 / 退出 PIE」，
	// 由 UPlanetEditComponent::SetEditMode 与 AVoxelPlanetActor::EndPlay 触发。
	// 行星这边对应的公开入口是 RequestSaveEdits / SaveEditsToSaveObject。

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetEditComponentDefaultsTest,
	"StellarEra.Planet.EditComponent.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetEditComponentDefaultsTest::RunTest(const FString& Parameters)
{
	const UPlanetEditComponent* EditCDO = GetDefault<UPlanetEditComponent>();
	if (!TestNotNull(TEXT("UPlanetEditComponent CDO"), EditCDO))
	{
		return false;
	}

	TestFalse(TEXT("默认不在编辑模式"), EditCDO->IsEditMode());
	TestTrue(TEXT("默认笔刷是球体"),
		EditCDO->GetBrushShape() == EPlanetBrushShape::Sphere);
	TestEqual(TEXT("默认笔刷尺寸 2 体素"), EditCDO->GetBrushSizeVoxels(), 2);
	TestEqual(TEXT("默认最大编辑距离 1000 米"),
		EditCDO->MaxEditDistanceCm, 1.0e5f, 1.f);
	TestEqual(TEXT("默认连挖渲染合并间隔 0.5 秒"),
		EditCDO->ContinuousDigRenderFlushIntervalSeconds, 0.5f, 0.01f);
	TestEqual(TEXT("默认连挖间隔 0.12 秒"),
		EditCDO->ContinuousDigIntervalSeconds, 0.12f, 0.001f);

	// GetDefaultSubobjectByName 只有非 const 版本（CDO 查默认子对象必然需要
	// 非 const 入口），而 GetDefault<T>() 只返回 const 指针。这里只读取，不修改 CDO。
	APlanetCharacter* CharacterCDO = const_cast<APlanetCharacter*>(GetDefault<APlanetCharacter>());
	if (!TestNotNull(TEXT("APlanetCharacter CDO"), CharacterCDO))
	{
		return false;
	}

	const UPlanetEditComponent* CharacterEdit = Cast<UPlanetEditComponent>(
		CharacterCDO->GetDefaultSubobjectByName(TEXT("PlanetEdit")));
	TestNotNull(TEXT("角色 CDO 上挂了 PlanetEdit 组件"), CharacterEdit);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetEditComponentStateTest,
	"StellarEra.Planet.EditComponent.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetEditComponentStateTest::RunTest(const FString& Parameters)
{
	// CDO 是全局共享的类默认对象，改它会影响后续测试；这里 NewObject 一个临时实例。
	// 组件不注册、不 BeginPlay —— 纯状态接口不需要世界。
	TStrongObjectPtr<UPlanetEditComponent> Edit(
		NewObject<UPlanetEditComponent>(GetTransientPackage()));
	if (!TestNotNull(TEXT("临时编辑组件"), Edit.Get()))
	{
		return false;
	}

	TestEqual(TEXT("初始尺寸 2"), Edit->GetBrushSizeVoxels(), 2);

	Edit->AdjustBrushSize(100);
	TestEqual(TEXT("+100 夹到上限 8"), Edit->GetBrushSizeVoxels(), 8);
	Edit->AdjustBrushSize(-100);
	TestEqual(TEXT("-100 夹到下限 1"), Edit->GetBrushSizeVoxels(), 1);

	// 从下限逐步滚到上限，每一步都不能越界。
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Edit->AdjustBrushSize(1);
		const int32 Size = Edit->GetBrushSizeVoxels();
		TestTrue(TEXT("向上滚动后尺寸仍在 [1, 8]"), Size >= 1 && Size <= 8);
	}
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Edit->AdjustBrushSize(-1);
		const int32 Size = Edit->GetBrushSizeVoxels();
		TestTrue(TEXT("向下滚动后尺寸仍在 [1, 8]"), Size >= 1 && Size <= 8);
	}

	TestTrue(TEXT("默认笔刷是球体"), Edit->GetBrushShape() == EPlanetBrushShape::Sphere);
	Edit->SetBrushShape(EPlanetBrushShape::Cube);
	TestTrue(TEXT("设置方块后读回方块"), Edit->GetBrushShape() == EPlanetBrushShape::Cube);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushSpherePreviewTest,
	"StellarEra.Planet.EditBrush.SpherePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushSpherePreviewTest::RunTest(const FString& Parameters)
{
	const FVector Center(1000.f, -200.f, 50.f);
	const FVector Axis = FVector(0.3, -0.5, 0.8).GetSafeNormal();
	constexpr float Radius = 250.f;

	TArray<FPlanetEditSegment> Segments;
	PlanetEditBrush::BuildSpherePreviewSegments(Center, Axis, Radius, Segments);

	TestEqual(TEXT("纬线 + 经线的总段数"),
		Segments.Num(),
		PlanetEditBrush::PreviewParallels * PlanetEditBrush::PreviewSegmentsPerCircle
			+ PlanetEditBrush::PreviewMeridians * PlanetEditBrush::PreviewSegmentsPerMeridian);

	for (const FPlanetEditSegment& Segment : Segments)
	{
		TestTrue(TEXT("起点在球面上"),
			FMath::IsNearlyEqual(FVector::Dist(Center, Segment.Start), Radius, 0.01f));
		TestTrue(TEXT("终点在球面上"),
			FMath::IsNearlyEqual(FVector::Dist(Center, Segment.End), Radius, 0.01f));
	}

	// 极轴真的起作用：找赤道那一圈。纬线条数取奇数时它必然存在。
	// 判据要**两个端点都**落在赤道平面上 —— 经线在中点也会穿过赤道，只算单点会多算。
	int32 EquatorSegments = 0;
	for (const FPlanetEditSegment& Segment : Segments)
	{
		const bool bStartOnEquator =
			FMath::Abs(FVector::DotProduct(Segment.Start - Center, Axis)) < 0.01f;
		const bool bEndOnEquator =
			FMath::Abs(FVector::DotProduct(Segment.End - Center, Axis)) < 0.01f;
		if (bStartOnEquator && bEndOnEquator)
		{
			++EquatorSegments;
		}
	}
	TestEqual(TEXT("赤道正好一整圈"), EquatorSegments, PlanetEditBrush::PreviewSegmentsPerCircle);

	// 两极各有一条经线汇聚点。
	bool bFoundNorthPole = false;
	bool bFoundSouthPole = false;
	for (const FPlanetEditSegment& Segment : Segments)
	{
		const FVector NorthPole = Center + Axis * Radius;
		const FVector SouthPole = Center - Axis * Radius;
		bFoundNorthPole |= Segment.Start.Equals(NorthPole, 0.01f) || Segment.End.Equals(NorthPole, 0.01f);
		bFoundSouthPole |= Segment.Start.Equals(SouthPole, 0.01f) || Segment.End.Equals(SouthPole, 0.01f);
	}
	TestTrue(TEXT("经线都汇到北极"), bFoundNorthPole);
	TestTrue(TEXT("经线都汇到南极"), bFoundSouthPole);

	// 半径 <= 0 时不应产生任何线。
	TArray<FPlanetEditSegment> Empty;
	PlanetEditBrush::BuildSpherePreviewSegments(Center, Axis, 0.f, Empty);
	TestEqual(TEXT("零半径不画线"), Empty.Num(), 0);

	// 零极轴退回世界 Z，而不是画出一堆 NaN。
	TArray<FPlanetEditSegment> Degenerate;
	PlanetEditBrush::BuildSpherePreviewSegments(
		Center, FVector::ZeroVector, Radius, Degenerate);
	TestEqual(TEXT("零极轴仍画满整套线段"),
		Degenerate.Num(), Segments.Num());
	TestFalse(TEXT("零极轴不产生 NaN"),
		Degenerate[0].Start.ContainsNaN() || Degenerate[0].End.ContainsNaN());

	return true;
}

// 预览线宽：目标是在屏幕上恒定像素宽。引擎的线厚是世界单位，所以同一像素宽必须
// 随距离线性放大。这里用互逆的投影公式反验 —— 把换算出的世界厚度投回屏幕，
// 任何距离都该正好等于设定的像素宽。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushPreviewLineThicknessTest,
	"StellarEra.Planet.EditBrush.PreviewLineThickness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushPreviewLineThicknessTest::RunTest(const FString& Parameters)
{
	constexpr float FovDegrees = 90.f;
	constexpr float ViewportWidthPx = 1920.f;
	constexpr float ThicknessPx = 2.f;
	constexpr float FallbackCm = 1.5f;
	const float TanHalfFov = FMath::Tan(FMath::DegreesToRadians(FovDegrees * 0.5f));

	const auto ToPixels = [TanHalfFov](const float WorldThicknessCm, const float DistanceCm)
	{
		return WorldThicknessCm * ViewportWidthPx / (2.f * DistanceCm * TanHalfFov);
	};

	// 从贴地看到星图远处：屏幕宽度处处是 2 px。
	const float DistancesCm[] = { 1.0e3f, 1.0e5f, 5.0e6f, 6.0e7f };
	for (const float DistanceCm : DistancesCm)
	{
		const float World = PlanetEditBrush::PreviewLineThicknessWorld(
			DistanceCm, FovDegrees, ViewportWidthPx, ThicknessPx, FallbackCm);
		TestTrue(*FString::Printf(TEXT("距离 %.0f cm 处投影回 2 px"), DistanceCm),
			FMath::IsNearlyEqual(ToPixels(World, DistanceCm), ThicknessPx, 1.0e-3f));
	}

	// 距离翻倍 → 世界厚度也翻倍（像素宽不变的另一面）。
	const float Near = PlanetEditBrush::PreviewLineThicknessWorld(
		1.0e5f, FovDegrees, ViewportWidthPx, ThicknessPx, FallbackCm);
	const float Far = PlanetEditBrush::PreviewLineThicknessWorld(
		2.0e5f, FovDegrees, ViewportWidthPx, ThicknessPx, FallbackCm);
	TestTrue(TEXT("距离翻倍厚度翻倍"), FMath::IsNearlyEqual(Far, Near * 2.f, 1.0e-2f));

	// 想要更粗的线，按比例变厚。
	const float Thicker = PlanetEditBrush::PreviewLineThicknessWorld(
		1.0e5f, FovDegrees, ViewportWidthPx, ThicknessPx * 2.f, FallbackCm);
	TestTrue(TEXT("目标像素翻倍 → 世界厚度翻倍"),
		FMath::IsNearlyEqual(Thicker, Near * 2.f, 1.0e-2f));

	// 窄 FOV 把画面放大，同样的像素宽需要的世界厚度更小。
	const float Narrow = PlanetEditBrush::PreviewLineThicknessWorld(
		1.0e5f, 60.f, ViewportWidthPx, ThicknessPx, FallbackCm);
	TestTrue(TEXT("FOV 60 比 FOV 90 更薄"), Narrow < Near);

	// 相机信息不全时退回世界厘米，不能画出 0 宽的线。
	TestTrue(TEXT("距离 0 退回兜底"),
		PlanetEditBrush::PreviewLineThicknessWorld(
			0.f, FovDegrees, ViewportWidthPx, ThicknessPx, FallbackCm) == FallbackCm);
	TestTrue(TEXT("视口宽 0 退回兜底"),
		PlanetEditBrush::PreviewLineThicknessWorld(
			1.0e5f, FovDegrees, 0.f, ThicknessPx, FallbackCm) == FallbackCm);
	TestTrue(TEXT("像素宽 0 退回兜底"),
		PlanetEditBrush::PreviewLineThicknessWorld(
			1.0e5f, FovDegrees, ViewportWidthPx, 0.f, FallbackCm) == FallbackCm);
	TestTrue(TEXT("FOV 0 退回兜底"),
		PlanetEditBrush::PreviewLineThicknessWorld(
			1.0e5f, 0.f, ViewportWidthPx, ThicknessPx, FallbackCm) == FallbackCm);
	TestTrue(TEXT("FOV 180 退回兜底"),
		PlanetEditBrush::PreviewLineThicknessWorld(
			1.0e5f, 180.f, ViewportWidthPx, ThicknessPx, FallbackCm) == FallbackCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushBoxPreviewTest,
	"StellarEra.Planet.EditBrush.BoxPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushBoxPreviewTest::RunTest(const FString& Parameters)
{
	auto CheckBox = [this](const FVector& LocalCenter, const int32 SizeVoxels)
	{
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(LocalCenter, SizeVoxels);
		const FPlanetEditVoxelBox VoxelBox = PlanetEditBrush::MakeVoxelBox(Box);

		TArray<FPlanetEditSegment> Segments;
		PlanetEditBrush::BuildBoxPreviewSegments(VoxelBox.Min, VoxelBox.MaxExclusive, Segments);

		TestEqual(TEXT("方块预览正好 12 条棱"), Segments.Num(), 12);

		// 所有端点必须落在真正的体素盒范围内，且每个轴都用满两端 ——
		// 否则预览会比实际挖/放的格子大或小。
		FVector Min(FLT_MAX);
		FVector Max(-FLT_MAX);
		for (const FPlanetEditSegment& Segment : Segments)
		{
			for (const FVector& Point : { Segment.Start, Segment.End })
			{
				Min = FVector::Min(Min, Point);
				Max = FVector::Max(Max, Point);
			}
		}

		const FVector ExpectedMin(VoxelBox.Min.X, VoxelBox.Min.Y, VoxelBox.Min.Z);
		const FVector ExpectedMax(
			VoxelBox.MaxExclusive.X, VoxelBox.MaxExclusive.Y, VoxelBox.MaxExclusive.Z);
		TestTrue(TEXT("预览下界等于体素盒下界"), Min.Equals(ExpectedMin));
		TestTrue(TEXT("预览上界等于体素盒上界"), Max.Equals(ExpectedMax));
	};

	// 尺寸 1 / 2 / 8 都要覆盖：1 是退化盒（Min == Max + 1），2 曾因开闭区间
	// 换算错少一格，8 是上限。
	CheckBox(FVector(0.f), 1);
	CheckBox(FVector(0.f), 2);
	CheckBox(FVector(10.f, -3.f, 7.f), 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetSaveObjectPathTest,
	"StellarEra.Planet.SaveObjectPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetSaveObjectPathTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("常规路径"),
		AVoxelPlanetActor::MakePlanetSaveObjectAssetPath(TEXT("UniverseMap"), TEXT("BP_VoxelPlanet2")),
		FString(TEXT("/Game/Saves/PlanetVoxel/UniverseMap/BP_VoxelPlanet2")));

	// PIE 前缀必须剥掉，否则同一条目每开一次 PIE 就换一个资产路径。
	TestEqual(TEXT("剥掉 UEDPIE 前缀并清洗非法字符"),
		AVoxelPlanetActor::MakePlanetSaveObjectAssetPath(TEXT("UEDPIE_0_UniverseMap"), TEXT("BP Planet:2")),
		FString(TEXT("/Game/Saves/PlanetVoxel/UniverseMap/BP_Planet_2")));

	TestEqual(TEXT("空输入有稳定回退"),
		AVoxelPlanetActor::MakePlanetSaveObjectAssetPath(FString(), FString()),
		FString(TEXT("/Game/Saves/PlanetVoxel/Untitled/Planet")));

	return true;
}

// 星图笔刷是 8..128，与角色的 1..8 是两套区间。旧的 1 参 ClampSize 会把
// 32 悄悄夹成 8，所以「带区间的重载」必须是独立函数，不能只改常量。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushClampSizeRangeTest,
	"StellarEra.Planet.EditBrush.ClampSizeRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushClampSizeRangeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 夹到下限 8"), PlanetEditBrush::ClampSize(0, 8, 128), 8);
	TestEqual(TEXT("999 夹到上限 128"), PlanetEditBrush::ClampSize(999, 8, 128), 128);
	TestEqual(TEXT("32 原样通过"), PlanetEditBrush::ClampSize(32, 8, 128), 32);
	TestEqual(TEXT("8 正好在下限上"), PlanetEditBrush::ClampSize(8, 8, 128), 8);
	TestEqual(TEXT("128 正好在上限上"), PlanetEditBrush::ClampSize(128, 8, 128), 128);
	TestEqual(TEXT("上限小于下限时以下限为准"), PlanetEditBrush::ClampSize(0, 10, 2), 10);
	TestEqual(TEXT("上限小于下限时不越过下限"), PlanetEditBrush::ClampSize(99, 10, 2), 10);
	TestEqual(TEXT("下限 0 被抬到 1"), PlanetEditBrush::ClampSize(0, 0, 4), 1);
	return true;
}

// 128 体素的方块必须真的是 128 格宽。MakeBox 内部若仍走 1..8 的钳制，
// 这里会得到 8 格 —— 星图方块笔刷「永远只有 8 格」就是这么来的。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushBoxBounds128Test,
	"StellarEra.Planet.EditBrush.BoxBounds128",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushBoxBounds128Test::RunTest(const FString& Parameters)
{
	const FPlanetEditBox Big = PlanetEditBrush::MakeBox(
		FVector(1000.f, 1000.f, 1000.f), 128,
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels);

	// Low = (128-1)/2 = 63，High = 128-1-63 = 64。
	TestEqual(TEXT("128 Min.X"), Big.Min.X, 1000 - 63);
	TestEqual(TEXT("128 Max.X"), Big.Max.X, 1000 + 64);
	TestEqual(TEXT("128 X 方向宽度（含端点）"), Big.Max.X - Big.Min.X + 1, 128);
	TestEqual(TEXT("128 Y 方向宽度（含端点）"), Big.Max.Y - Big.Min.Y + 1, 128);
	TestEqual(TEXT("128 Z 方向宽度（含端点）"), Big.Max.Z - Big.Min.Z + 1, 128);

	const FPlanetEditBox Mid = PlanetEditBrush::MakeBox(
		FVector(1000.f), 32,
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels);
	TestEqual(TEXT("32 X 方向宽度（含端点）"), Mid.Max.X - Mid.Min.X + 1, 32);

	// 半边长 = 尺寸 × 体素尺寸 / 2。128 × 50 cm = 6400 cm 直径，半边长 3200 cm。
	TestEqual(TEXT("128 × 50cm 的半边长"),
		PlanetEditBrush::GetHalfExtentCm(128, 50.f,
			PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels),
		3200.f);

	// 放的偏移必须跟着区间版走，否则 128 的方块会只偏移 4 格。
	const FVector Hit(0.f, 0.f, 1000.f);
	const FVector Placed = PlanetEditBrush::GetEditCenterWorld(
		Hit, FVector(0.f, 0.f, 1.f), 128, 50.f, /*bPlace=*/true,
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels);
	TestTrue(TEXT("放的球心沿法线外移 3200cm"), Placed.Equals(Hit + FVector(0.f, 0.f, 3200.f)));

	return true;
}

// 笔刷尺寸区间：角色 1..8、星图 8..128 共用同一份夹取逻辑。
// 旧的一参 ClampSize 会把 32 这样的星图尺寸悄悄夹成 8 —— 这就是本测试要钉的回归。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditComponentRangeTest,
	"StellarEra.Planet.EditComponent.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditComponentRangeTest::RunTest(const FString& Parameters)
{
	UPlanetEditComponent* Edit = NewObject<UPlanetEditComponent>();

	// 默认就是角色的区间与提示：老组件不该因为新增配置项改变行为。
	TestEqual(TEXT("默认下限 1"), Edit->GetMinBrushSizeVoxels(), 1);
	TestEqual(TEXT("默认上限 8"), Edit->GetMaxBrushSizeVoxels(), 8);
	TestEqual(TEXT("默认提示"), Edit->GetPlaceActionHint(), FString(TEXT("右键点放")));
	TestEqual(TEXT("默认步长 1（角色一档一格）"), Edit->GetBrushSizeStepVoxels(), 1);
	Edit->AdjustBrushSize(100);
	TestEqual(TEXT("+100 夹到 8"), Edit->GetBrushSizeVoxels(), 8);
	Edit->AdjustBrushSize(-100);
	TestEqual(TEXT("-100 夹到 1"), Edit->GetBrushSizeVoxels(), 1);

	// 星图区间 8..128，用 32 作样例初始值。
	Edit->ConfigureBrushRange(8, 128, 32);
	TestEqual(TEXT("样例初始值 32 原样保留"), Edit->GetBrushSizeVoxels(), 32);
	TestEqual(TEXT("星图下限 8"), Edit->GetMinBrushSizeVoxels(), 8);
	TestEqual(TEXT("星图上限 128"), Edit->GetMaxBrushSizeVoxels(), 128);
	Edit->AdjustBrushSize(1000);
	TestEqual(TEXT("+1000 夹到 128"), Edit->GetBrushSizeVoxels(), 128);
	Edit->AdjustBrushSize(-1000);
	TestEqual(TEXT("−1000 夹到 8"), Edit->GetBrushSizeVoxels(), 8);

	// 区间写反时不崩、保下限（与 PlanetEditBrush::ClampSize 的约定一致）。
	Edit->ConfigureBrushRange(10, 2, 0);
	TestEqual(TEXT("上下限写反保下限"), Edit->GetBrushSizeVoxels(), 10);
	TestEqual(TEXT("上下限写反时上限被抬到下限"), Edit->GetMaxBrushSizeVoxels(), 10);

	// 按档调尺寸：一档 = BrushSizeStepVoxels，粗调再 ×10。
	// 星图上限 1280 配一档 10 体素，不粗调得按一百多下才够得着。
	Edit->ConfigureBrushRange(
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels, 64);
	Edit->BrushSizeStepVoxels = 10;
	Edit->AdjustBrushSizeByStep(/*bIncrease=*/true, /*bCoarse=*/false);
	TestEqual(TEXT("细调一档 64→74"), Edit->GetBrushSizeVoxels(), 74);
	Edit->AdjustBrushSizeByStep(/*bIncrease=*/true, /*bCoarse=*/true);
	TestEqual(TEXT("粗调一档 +100"), Edit->GetBrushSizeVoxels(), 174);
	Edit->AdjustBrushSizeByStep(/*bIncrease=*/false, /*bCoarse=*/true);
	TestEqual(TEXT("粗调一档 −100"), Edit->GetBrushSizeVoxels(), 74);
	Edit->AdjustBrushSizeByStep(/*bIncrease=*/true, /*bCoarse=*/true);
	Edit->AdjustBrushSize(100000);
	TestEqual(TEXT("粗调也夹在上限 1280"), Edit->GetBrushSizeVoxels(), 1280);
	return true;
}

// 大笔刷不再记录 ModifiedValues。插件在 TVoxelDataImpl::Set 里按**整个包围盒**
// AddUninitialized（FModifiedVoxelValue 20 字节），1280 体素的球就是 43 GB ——
// 这条钉住「超过预算就关记录」的分界，也把代价 Size³ 写在纸面上。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushBigBrushPolicyTest,
	"StellarEra.Planet.EditBrush.BigBrushPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushBigBrushPolicyTest::RunTest(const FString& Parameters)
{
	// 工具包围盒是 ±(R + 3) 体素，边长 = Size + 6。
	TestEqual(TEXT("128 体素扫 134³ 格"),
		PlanetEditBrush::EstimateToolCellCount(128), 2406104LL);
	TestEqual(TEXT("1280 体素扫 1286³ 格"),
		PlanetEditBrush::EstimateToolCellCount(1280), 2126781656LL);

	// 分界：152 还在 4e6 预算内，153（159³）就超了。
	TestTrue(TEXT("64 记录"), PlanetEditBrush::ShouldRecordModifiedValues(64));
	TestTrue(TEXT("152 仍记录"), PlanetEditBrush::ShouldRecordModifiedValues(152));
	TestFalse(TEXT("153 不记录"), PlanetEditBrush::ShouldRecordModifiedValues(153));
	TestFalse(TEXT("1280 不记录"), PlanetEditBrush::ShouldRecordModifiedValues(1280));

	// 粗调只放大倍数；步长 0 也至少走一格。
	TestEqual(TEXT("细调 10"), PlanetEditBrush::ResolveBrushStep(10, false), 10);
	TestEqual(TEXT("粗调 100"), PlanetEditBrush::ResolveBrushStep(10, true), 100);
	TestEqual(TEXT("步长 0 抬到 1"), PlanetEditBrush::ResolveBrushStep(0, false), 1);
	TestEqual(TEXT("步长 0 粗调也走 10"), PlanetEditBrush::ResolveBrushStep(0, true), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushShouldDigNowTest,
	"StellarEra.Planet.EditBrush.ShouldDigNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushShouldDigNowTest::RunTest(const FString& Parameters)
{
	float Accumulator = 0.f;

	// 未到间隔：不挖，累加器保留。
	TestFalse(TEXT("0.05 s 未到 0.12 s 间隔"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 0.05f, 0.12f));
	TestTrue(TEXT("未到间隔时累加器保留"),
		FMath::IsNearlyEqual(Accumulator, 0.05f, 1.e-4f));

	// 跨过间隔：挖一次并清零。
	TestTrue(TEXT("再走 0.08 s 跨过 0.12 s"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 0.08f, 0.12f));
	TestTrue(TEXT("触发后累加器清零"),
		FMath::IsNearlyEqual(Accumulator, 0.f, 1.e-4f));

	// 一帧巨大 DeltaSeconds 只触发一次，不做 while 补偿。
	TestTrue(TEXT("10 s 的单帧只触发一次"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 10.f, 0.12f));
	TestTrue(TEXT("大帧之后累加器仍是 0"),
		FMath::IsNearlyEqual(Accumulator, 0.f, 1.e-4f));
	TestFalse(TEXT("同一累加器不会连续返回 true"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 0.f, 0.12f));

	// Interval <= 0 按 0.01 处理。注意这个下限只挡同一帧内的重复调用：
	// 30 / 60 / 90 FPS 的帧时长都大于 0.01 s，靠下限兜底仍会每帧触发。
	float ZeroInterval = 0.f;
	TestFalse(TEXT("间隔 0 时 0.001 s 不触发"),
		PlanetEditBrush::ShouldDigNow(ZeroInterval, 0.001f, 0.f));
	TestTrue(TEXT("间隔 0 时累计到 0.021 s 触发"),
		PlanetEditBrush::ShouldDigNow(ZeroInterval, 0.02f, 0.f));

	// 下限钉死在 0.01：两条探针跨在它两侧，实现改成 0.02 时第二行会变 false。
	float FloorProbe = 0.f;
	TestFalse(TEXT("下限探针：0.009 s 不触发"),
		PlanetEditBrush::ShouldDigNow(FloorProbe, 0.009f, 0.f));
	TestTrue(TEXT("下限探针：累计 0.018 s 触发"),
		PlanetEditBrush::ShouldDigNow(FloorProbe, 0.009f, 0.f));

	// 负 delta 按 0：不触发，也不能把已经攒下的时间倒扣回去。
	float NegativeDelta = 0.05f;
	TestFalse(TEXT("负 delta 不触发"),
		PlanetEditBrush::ShouldDigNow(NegativeDelta, -1.f, 0.12f));
	TestTrue(TEXT("负 delta 不改变累加器"),
		FMath::IsNearlyEqual(NegativeDelta, 0.05f, 1.e-4f));

	// 累加器恰好等于间隔也触发（判定是「达到或超过」，不是「跨过」）。
	// 用精确的 0.12f：0.05f + 0.07f 在 float32 下是 0.1200000048，比间隔略大，
	// 钉不住「等号也触发」；后一条只是照实覆盖累加到间隔的常规路径。
	float AtBoundary = 0.12f;
	TestTrue(TEXT("恰好等于间隔也触发"),
		PlanetEditBrush::ShouldDigNow(AtBoundary, 0.f, 0.12f));
	TestTrue(TEXT("精确边界触发后清零"),
		FMath::IsNearlyEqual(AtBoundary, 0.f, 1.e-4f));

	float AccumToBoundary = 0.05f;
	TestTrue(TEXT("0.05 + 0.07 达到间隔也触发"),
		PlanetEditBrush::ShouldDigNow(AccumToBoundary, 0.07f, 0.12f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetEditContinuousDigTest,
	"StellarEra.Planet.EditComponent.ContinuousDig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetEditContinuousDigTest::RunTest(const FString& Parameters)
{
	// 纯状态接口：NewObject 一个不注册、不进世界的临时组件即可。
	TStrongObjectPtr<UPlanetEditComponent> Edit(
		NewObject<UPlanetEditComponent>(GetTransientPackage()));
	if (!TestNotNull(TEXT("临时编辑组件"), Edit.Get()))
	{
		return false;
	}

	TestFalse(TEXT("默认不连续挖"), Edit->IsContinuousDigging());

	Edit->SetContinuousDigging(true);
	TestTrue(TEXT("开启后可读回"), Edit->IsContinuousDigging());

	// 退出编辑模式必须顺手关掉连续挖：标志留着会让下次进编辑模式直接开挖，
	// 也会让星图相机一直以为在连挖。
	Edit->SetEditMode(true);
	TestTrue(TEXT("进入编辑模式不改变连续挖状态"), Edit->IsContinuousDigging());
	Edit->SetEditMode(false);
	TestFalse(TEXT("退出编辑模式自动关闭连续挖"), Edit->IsContinuousDigging());

	// 重复关闭是幂等的：松开左键会无条件调一次。
	Edit->SetContinuousDigging(false);
	TestFalse(TEXT("重复关闭幂等"), Edit->IsContinuousDigging());

	return true;
}
#endif
