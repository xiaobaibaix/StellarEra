// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物剖面：纯数学与剖面光栅化的自动化测试（spec §9 的 9 条中的 8 条）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PlanetCutawayMath.h"
#include "PlanetSectionImage.h"

namespace
{
	/** 造一个等边三角形环，避免依赖 128 段真实几何。 */
	FSectionRingScreenPolyline MakeTriangleRing(ESectionRing Ring, const FVector2D& Offset)
	{
		FSectionRingScreenPolyline Polyline;
		Polyline.Ring = Ring;
		Polyline.Points.Add(Offset + FVector2D(0.0, -100.0));
		Polyline.Points.Add(Offset + FVector2D(86.6, 50.0));
		Polyline.Points.Add(Offset + FVector2D(-86.6, 50.0));
		return Polyline;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayClipNormalTest,
	"StellarEra.Planet.Cutaway.Math.ClipNormalFacesCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayClipNormalTest::RunTest(const FString& Parameters)
{
	const FVector Center(100.0, 0.0, 0.0);
	const FVector PlaneNormal = FVector::UpVector;

	const FVector CameraAbove = Center + FVector::UpVector * 1000.0;
	TestTrue(TEXT("法线朝相机时保持不变"),
		FPlanetCutawayMath::ComputeClipNormal(PlaneNormal, CameraAbove, Center).Equals(
			FVector::UpVector, 1e-9));

	const FVector CameraBelow = Center - FVector::UpVector * 1000.0;
	TestTrue(TEXT("相机在反侧时翻转"),
		FPlanetCutawayMath::ComputeClipNormal(PlaneNormal, CameraBelow, Center).Equals(
			-FVector::UpVector, 1e-9));

	TestTrue(TEXT("相机在球心时原样返回"),
		FPlanetCutawayMath::ComputeClipNormal(PlaneNormal, Center, Center).Equals(
			FVector::UpVector, 1e-9));

	// spec §9：返回值必须是单位长度，即使输入法线不是单位向量。
	const FVector NonUnitNormal(0.0, 0.0, 5.0);

	const FVector ClipAbove =
		FPlanetCutawayMath::ComputeClipNormal(NonUnitNormal, CameraAbove, Center);
	TestTrue(TEXT("非单位法线朝相机时归一化为 +Z"),
		ClipAbove.Equals(FVector::UpVector, 1e-6));
	TestTrue(TEXT("非单位法线朝相机时返回单位长"),
		FMath::IsNearlyEqual(ClipAbove.Size(), 1.0, 1e-6));

	const FVector ClipBelow =
		FPlanetCutawayMath::ComputeClipNormal(NonUnitNormal, CameraBelow, Center);
	TestTrue(TEXT("非单位法线在反侧时归一化为 −Z"),
		ClipBelow.Equals(-FVector::UpVector, 1e-6));
	TestTrue(TEXT("非单位法线在反侧时返回单位长"),
		FMath::IsNearlyEqual(ClipBelow.Size(), 1.0, 1e-6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayPlaneBasisTest,
	"StellarEra.Planet.Cutaway.Math.PlaneBasisOrthonormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayPlaneBasisTest::RunTest(const FString& Parameters)
{
	// 逐个法线方向验证正交性与右手性：这是「图像不镜像」的唯一数学依据。
	const TArray<FVector> Normals = {
		FVector::UpVector,
		FVector::ForwardVector,
		FVector::RightVector,
		FVector(1.0, 2.0, 3.0).GetSafeNormal(),
		FVector(-0.3, 0.9, 0.2).GetSafeNormal(),
		FVector(0.0, 0.0, -1.0)
	};

	for (const FVector& Normal : Normals)
	{
		FVector AxisU = FVector::ZeroVector;
		FVector AxisV = FVector::ZeroVector;
		FPlanetCutawayMath::ComputePlaneBasis(Normal, AxisU, AxisV);

		TestTrue(TEXT("AxisU 单位长"), FMath::IsNearlyEqual(AxisU.Size(), 1.0, 1e-6));
		TestTrue(TEXT("AxisV 单位长"), FMath::IsNearlyEqual(AxisV.Size(), 1.0, 1e-6));
		TestTrue(TEXT("AxisU 垂直于法线"),
			FMath::IsNearlyZero(FVector::DotProduct(AxisU, Normal), 1e-6));
		TestTrue(TEXT("AxisV 垂直于法线"),
			FMath::IsNearlyZero(FVector::DotProduct(AxisV, Normal), 1e-6));
		TestTrue(TEXT("AxisU 垂直于 AxisV"),
			FMath::IsNearlyZero(FVector::DotProduct(AxisU, AxisV), 1e-6));
		TestTrue(TEXT("Cross(N, AxisU) == AxisV"),
			FVector::CrossProduct(Normal, AxisU).Equals(AxisV, 1e-6));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayRotatePlaneTest,
	"StellarEra.Planet.Cutaway.Math.RotatePlaneCompose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayRotatePlaneTest::RunTest(const FString& Parameters)
{
	const FQuat Identity = FQuat::Identity;

	// 绕 Y 转 90°：朝向的局部 Z（法线）应变成世界 +X。
	// 绕 Z 旋转不改变局部 Z，无法验证法线朝向；spec §9 也要求「旋转轴上的分量不变」。
	const FQuat Rotated = FPlanetCutawayMath::RotatePlane(Identity, FVector::RightVector, 90.f);
	TestTrue(TEXT("绕 Y 转 90° 后法线指向 +X"),
		Rotated.GetAxisZ().Equals(FVector::ForwardVector, 1e-6));

	// 再转 90° 应到 −Z（叠加，而不是覆盖）。
	const FQuat RotatedTwice = FPlanetCutawayMath::RotatePlane(Rotated, FVector::RightVector, 90.f);
	TestTrue(TEXT("叠加第二次旋转后法线指向 −Z"),
		RotatedTwice.GetAxisZ().Equals(-FVector::UpVector, 1e-6));

	// 0 度与零轴都不改变朝向。
	TestTrue(TEXT("0 度不变"),
		FPlanetCutawayMath::RotatePlane(Rotated, FVector::UpVector, 0.f).Equals(Rotated, 1e-9));
	TestTrue(TEXT("零轴不变"),
		FPlanetCutawayMath::RotatePlane(Rotated, FVector::ZeroVector, 45.f).Equals(Rotated, 1e-9));

	// spec §9：早退路径也必须返回单位四元数（输入故意给成非单位）。
	const FQuat NonUnitOrientation(0.f, 0.f, 0.f, 2.f);
	TestTrue(TEXT("零角度早退也归一化为单位四元数"),
		FMath::IsNearlyEqual(
			FPlanetCutawayMath::RotatePlane(NonUnitOrientation, FVector::UpVector, 0.f).Size(),
			1.0, 1e-6));

	// 世界左乘 vs 局部右乘：先绕世界 Y +90°（法线 → +X），再绕世界 Z +90°。
	// Delta * Orientation 会把 +X 转到 +Y；Orientation * Delta（局部右乘）会停在 +X。
	const FQuat RotatedThenYaw =
		FPlanetCutawayMath::RotatePlane(Rotated, FVector::UpVector, 90.f);
	TestTrue(TEXT("世界左乘：+X 再绕世界 Z 转到 +Y"),
		RotatedThenYaw.GetAxisZ().Equals(FVector::RightVector, 1e-6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayOpacityMaskTest,
	"StellarEra.Planet.Cutaway.Math.OpacityMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayOpacityMaskTest::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	const FVector Normal = FVector::UpVector;
	constexpr float InsideRadiusCm = 10500.f;

	// 关闭时恒为 1：这是「退出即恢复」的数值保证。
	TestEqual(TEXT("未激活时近侧也不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, 500.0), Origin, Normal, InsideRadiusCm, false), 1.f);

	// 近侧（法线正方向）且在内球里 → 0。
	TestEqual(TEXT("近侧内球完全裁掉"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, 500.0), Origin, Normal, InsideRadiusCm, true), 0.f);

	// 远侧（法线负方向）→ 1。
	TestEqual(TEXT("远侧不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, -500.0), Origin, Normal, InsideRadiusCm, true), 1.f);

	// 内球之外 → 1（大气/远景不误伤）。
	TestEqual(TEXT("内球之外不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(0.0, 0.0, 20000.0), Origin, Normal, InsideRadiusCm, true), 1.f);

	// 切面上（dot == 0）→ 1。
	TestEqual(TEXT("切面本身不裁"),
		FPlanetCutawayMath::ComputeOpacityMask(
			FVector(900.0, 900.0, 0.0), Origin, Normal, InsideRadiusCm, true), 1.f);

	// 以下三条把两个过渡因子拉出饱和区，避免错误实现被「全 0 / 全 1」掩盖。

	// z = 0.05：Half = saturate(0.05 × 10) = 0.5，Inside = 1 → Mask = 0.5（钉住 ×10）。
	TestTrue(TEXT("×10 过渡：Half=0.5 时遮罩为 0.5"),
		FMath::IsNearlyEqual(
			FPlanetCutawayMath::ComputeOpacityMask(
				FVector(0.0, 0.0, 0.05), Origin, Normal, InsideRadiusCm, true),
			0.5f, 1e-3f));

	// |P| = 10475：Half = 1，Inside = (10500 − 10475) × 0.02 = 0.5 → Mask = 0.5（钉住 ×0.02）。
	TestTrue(TEXT("×0.02 过渡：Inside=0.5 时遮罩为 0.5"),
		FMath::IsNearlyEqual(
			FPlanetCutawayMath::ComputeOpacityMask(
				FVector(0.0, 0.0, 10475.0), Origin, Normal, InsideRadiusCm, true),
			0.5f, 1e-3f));

	// P = (10475, 0, 0.05)：Half = 0.5、Inside = 0.5 → Mask = 1 − 0.5 × 0.5 = 0.75。
	// 若用 Min(Half, Inside) 代替乘积会得到 0.5，这条专门抓该错误。
	TestTrue(TEXT("两个因子各 0.5 时按乘积得 0.75"),
		FMath::IsNearlyEqual(
			FPlanetCutawayMath::ComputeOpacityMask(
				FVector(10475.0, 0.0, 0.05), Origin, Normal, InsideRadiusCm, true),
			0.75f, 1e-3f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetCutawayRingPickTest,
	"StellarEra.Planet.Cutaway.Math.RingPick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetCutawayRingPickTest::RunTest(const FString& Parameters)
{
	const FSectionRingScreenPolyline RingX = MakeTriangleRing(ESectionRing::X, FVector2D::ZeroVector);
	const FSectionRingScreenPolyline RingY = MakeTriangleRing(ESectionRing::Y, FVector2D(0.0, 300.0));
	const TArray<FSectionRingScreenPolyline> Rings = { RingX, RingY };

	ESectionRing Picked = ESectionRing::Z;

	// 命中 X 环的一条边（底边 y = -100）。
	TestTrue(TEXT("命中 X 环"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, -100.0), 12.f, Picked));
	TestEqual(TEXT("拾取结果为 X"), Picked, ESectionRing::X);

	// 命中 Y 环的顶点（y = 300 + 50）。
	TestTrue(TEXT("命中 Y 环"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, 350.0), 12.f, Picked));
	TestEqual(TEXT("拾取结果为 Y"), Picked, ESectionRing::Y);

	// 离所有环都远 → false。
	TestFalse(TEXT("远处不命中"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, 150.0), 12.f, Picked));

	// 容差边界：底边下方 10px 命中，13px 不命中。
	TestTrue(TEXT("容差内命中"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, -110.0), 12.f, Picked));
	TestFalse(TEXT("容差外不命中"),
		FPlanetCutawayMath::PickSectionRing(Rings, FVector2D(0.0, -113.0), 12.f, Picked));

	// 并列时 X 优先：两条环完全重合。
	const FSectionRingScreenPolyline RingZAtX =
		MakeTriangleRing(ESectionRing::Z, FVector2D::ZeroVector);
	const TArray<FSectionRingScreenPolyline> Overlapped = { RingX, RingZAtX };
	TestTrue(TEXT("重合时仍命中"),
		FPlanetCutawayMath::PickSectionRing(Overlapped, FVector2D(0.0, -100.0), 12.f, Picked));
	TestEqual(TEXT("并列取先出现的 X"), Picked, ESectionRing::X);

	return true;
}

namespace
{
	/** 一个均匀球 + 固定成分采样，供 Image 测试共用。 */
	FPlanetSectionImageParams MakeUniformSphereParams(float SectionRadiusCm = 10500.f)
	{
		FPlanetSectionImageParams Params;
		Params.Resolution = 101;
		Params.AxisU = FVector::ForwardVector;
		Params.AxisV = FVector::RightVector;
		Params.SectionRadiusCm = SectionRadiusCm;
		Params.VoxelSize = 100.f;
		Params.BaseRadiusVoxels = 100.f;
		Params.SurfaceColor = FLinearColor(0.2f, 0.4f, 0.6f);
		Params.Appearance = FPlanetGeologyAppearance();
		return Params;
	}

	bool ByteNear(uint8 Actual, uint8 Expected, int32 Tolerance = 1)
	{
		return FMath::Abs(int32(Actual) - int32(Expected)) <= Tolerance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetSectionImageSolidSphereTest,
	"StellarEra.Planet.Cutaway.Image.SolidSphereAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetSectionImageSolidSphereTest::RunTest(const FString& Parameters)
{
	// 均匀球：中心不透明，边界羽化，圆盘外全透明。
	constexpr int32 Res = 101;
	const FPlanetSectionImageParams Params = MakeUniformSphereParams();

	TArray<FColor> Pixels;
	FPlanetSectionImage::Render(Params,
		[](const FVector&) { return 10000.f; },
		[](const FVector&, double)
		{
			FPlanetGeologySample Sample;
			return Sample;
		},
		Pixels);

	TestEqual(TEXT("像素数 = Res²"), Pixels.Num(), Res * Res);
	TestEqual(TEXT("中心不透明"), int32(Pixels[50 + 50 * Res].A), 255);

	// (95,50)：u≈0.891 → PlaneRadius≈9356，在羽化带内侧。
	TestEqual(TEXT("内圈不透明"), int32(Pixels[95 + 50 * Res].A), 255);
	// (100,50)：u≈0.990 → PlaneRadius≈10396，越过表面 1.9px（> 1.5px 羽化）。
	TestEqual(TEXT("球外透明"), int32(Pixels[100 + 50 * Res].A), 0);
	// 角落在圆盘之外。
	TestEqual(TEXT("圆盘角透明"), int32(Pixels[0].A), 0);

	// 羽化带里必须存在中间值：证明边缘不是硬切。
	bool bFoundFeather = false;
	for (int32 X = 90; X < 101; ++X)
	{
		const uint8 Alpha = Pixels[X + 50 * Res].A;
		bFoundFeather |= (Alpha > 0 && Alpha < 255);
	}
	TestTrue(TEXT("存在半透明羽化像素"), bFoundFeather);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetSectionImagePresenceTest,
	"StellarEra.Planet.Cutaway.Image.PresenceThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetSectionImagePresenceTest::RunTest(const FString& Parameters)
{
	// 中心像素：Surface 10000、PlaneRadius 0 → Depth 100 体素 = BaseRadiusVoxels
	// → DepthShade = 1 − 0.4 = 0.6 → Base = SurfaceColor × 0.25 × 0.6。
	constexpr int32 Res = 101;
	const FPlanetSectionImageParams Params = MakeUniformSphereParams();
	const FLinearColor Base =
		Params.SurfaceColor * 0.25f * (1.f - 0.4f * FMath::Clamp(100.f / 100.f, 0.f, 1.f));

	auto RenderCenter = [&Params](float DominantNorm, float Purity, const FLinearColor& Mineral)
	{
		TArray<FColor> Pixels;
		FPlanetSectionImage::Render(Params,
			[](const FVector&) { return 10000.f; },
			[&](const FVector&, double)
			{
				FPlanetGeologySample Sample;
				Sample.DominantNorm = DominantNorm;
				Sample.Purity = Purity;
				Sample.MineralColor = Mineral;
				return Sample;
			},
			Pixels);
		return Pixels[50 + 50 * Res];
	};

	// A：DominantNorm 低于阈值（0.22）→ 纯底色。
	{
		const FColor Actual = RenderCenter(0.10f, 1.f, FLinearColor(1.f, 0.f, 0.f));
		const FColor Expected = Base.ToFColor(true);
		TestTrue(TEXT("低于阈值 = 底色"),
			ByteNear(Actual.R, Expected.R) && ByteNear(Actual.G, Expected.G)
			&& ByteNear(Actual.B, Expected.B));
		TestEqual(TEXT("低于阈值仍不透明"), int32(Actual.A), 255);
	}

	// B：阈值 + 渐变以上、纯度满 → 纯矿物色。
	{
		const FColor Actual = RenderCenter(0.50f, 1.f, FLinearColor(1.f, 0.f, 0.f));
		const FColor Expected = FLinearColor(1.f, 0.f, 0.f).ToFColor(true);
		TestTrue(TEXT("高纯度 = 矿物色"),
			ByteNear(Actual.R, Expected.R) && ByteNear(Actual.G, Expected.G)
			&& ByteNear(Actual.B, Expected.B));
	}

	// C：纯度 0.35 / 0.7 = 0.5 → 线性插值（spec §3.1），底色与矿物色各一半。
	{
		const FColor Actual = RenderCenter(0.50f, 0.35f, FLinearColor(1.f, 0.f, 0.f));
		const FColor Expected =
			(Base + (FLinearColor(1.f, 0.f, 0.f) - Base) * 0.5f).ToFColor(true);
		TestTrue(TEXT("半纯度 = 半混合"),
			ByteNear(Actual.R, Expected.R) && ByteNear(Actual.G, Expected.G)
			&& ByteNear(Actual.B, Expected.B));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetSectionImageDepthTest,
	"StellarEra.Planet.Cutaway.Image.DepthMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetSectionImageDepthTest::RunTest(const FString& Parameters)
{
	// SectionRadius = 1.05 × 10000 = 10500（spec §3.1 优先于 §9 的数值例子）。
	// Res = 101 时像素 x=60 的 u = (60.5/101)*2 − 1 ≈ 0.19802。
	constexpr int32 Res = 101;
	constexpr float SectionRadiusCm = 10500.f;
	constexpr float SurfaceRadiusCm = 10000.f;
	constexpr float VoxelSize = 100.f;

	const double U = (60.0 + 0.5) / double(Res) * 2.0 - 1.0;
	const double ExpectedPlaneRadiusCm = U * double(SectionRadiusCm);
	const double ExpectedDepthVoxels = (double(SurfaceRadiusCm) - ExpectedPlaneRadiusCm) / double(VoxelSize);

	// 这两个数是本计划对 spec §9 的裁定值：2079.2 / 79.208。
	TestTrue(TEXT("PlaneRadius ≈ 2079.2"),
		FMath::IsNearlyEqual(ExpectedPlaneRadiusCm, 2079.2, 1.0));
	TestTrue(TEXT("Depth ≈ 79.208"),
		FMath::IsNearlyEqual(ExpectedDepthVoxels, 79.208, 0.5));

	const FPlanetSectionImageParams Params = MakeUniformSphereParams(SectionRadiusCm);

	TArray<double> Depths;
	TArray<FVector> Directions;
	TArray<FColor> Pixels;
	FPlanetSectionImage::Render(Params,
		[](const FVector&) { return SurfaceRadiusCm; },
		[&](const FVector& Dir, double DepthVoxels)
		{
			Depths.Add(DepthVoxels);
			Directions.Add(Dir);
			FPlanetGeologySample Sample;
			return Sample;
		},
		Pixels);

	// 某个像素的深度应精确等于推导值，且方向就是 AxisU（v = 0 那一行）。
	// 半径相同的像素不止一个：像素中心落在 2/101 的格子上，
	// (±20,0)、(0,±20)、(±16,±12)、(±12,±16) 都满足 sqrt(k²+m²)=20，
	// 即同一圈上恰有 12 个像素同深度（实测 12）。所以按方向过滤到 +AxisU，锁定唯一解。
	int32 Matched = 0;
	bool bDirectionOk = false;
	for (int32 Index = 0; Index < Depths.Num(); ++Index)
	{
		if (FMath::Abs(Depths[Index] - ExpectedDepthVoxels) <= 0.05
			&& Directions[Index].Equals(FVector::ForwardVector, 1e-4))
		{
			++Matched;
			bDirectionOk = true;
		}
	}

	TestEqual(TEXT("恰有一个像素落在该深度"), Matched, 1);
	TestTrue(TEXT("该像素方向为 AxisU"), bDirectionOk);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
