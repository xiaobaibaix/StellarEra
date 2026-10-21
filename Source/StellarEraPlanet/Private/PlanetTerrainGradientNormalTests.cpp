// 着色法线在粗 LOD 上的回归测试。
//
// 背景：LOD > 0 的着色法线不是用存下来的 FVoxelValue，而是插件对**生成器浮点
// 值**做 6 点中心差分算出来的，采样间距 = 2^LOD 体素（VoxelMesher 的 Step）。
// 生成器如果先把密度夹到 [-1, 1]，间距一旦明显超过 DensityScale，6 个采样就
// 全部饱和，法线退化成只有 ±1 分量的 26 个轴向对角方向之一 —— 表现为粗 LOD
// 的大块明暗色块。
//
// 这里用纯球地形做判据：纯球面的密度场梯度方向就是径向，与 LOD、采样间距无关。
// 因此任何 LOD 上算出的法线都必须指向径向；偏差超过容差就说明密度又被夹断了。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetSphereTerrain.h"
#include "VoxelItemStack.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainGradientNormalTest,
	"StellarEra.Planet.Terrain.GradientNormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainGradientNormalTest::RunTest(const FString& Parameters)
{
	// 与 UniverseMap 的原点行星同量级：半径 20000 体素、体素 100 cm。
	constexpr float Radius = 20000.f;

	FVoxelPlanetTerrainContext Context;
	Context.Radius = Radius;
	const FVoxelPlanetSphereTerrainInstance Instance(Context);

	// 故意选一个三个分量都不相等的方向：法线被量化成 (±1, ±1, ±1) 时，
	// 与真实径向的夹角才不会被对称性掩盖（轴向方向上两者本来就重合）。
	const FVector UnitDir = FVector(1.0, 2.0, 3.0).GetSafeNormal();
	const FVector SurfacePoint = Context.Center + UnitDir * double(Radius);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	const auto ValueAt = [&](const FVector& P, int32 LOD)
	{
		return double(Instance.GetValueImpl(v_flt(P.X), v_flt(P.Y), v_flt(P.Z), LOD, Items));
	};

	// 机制本身：LOD 8 的采样间距是 256 体素，是 DensityScale(5) 的 51 倍。
	// 地表外 256 体素处的密度必须保持 51 左右 —— 旧实现会把它夹成 1。
	const double ValueAboveSurface = ValueAt(SurfacePoint + UnitDir * 256.0, 8);
	TestTrue(
		FString::Printf(TEXT("地表外 256 体素的密度不能被夹断（实测 %.2f，期望 > 10）"), ValueAboveSurface),
		ValueAboveSurface > 10.0);

	// 每个 LOD 的法线都必须指向径向。LOD 0-2 的间距不超过 4 体素，旧实现也能
	// 通过；LOD 3 起旧实现开始饱和，偏差随 LOD 增大到约 22°。
	for (int32 LOD = 0; LOD <= 8; ++LOD)
	{
		const double Offset = double(1 << LOD);

		const FVector Gradient(
			ValueAt(SurfacePoint + FVector(Offset, 0.0, 0.0), LOD) -
				ValueAt(SurfacePoint - FVector(Offset, 0.0, 0.0), LOD),
			ValueAt(SurfacePoint + FVector(0.0, Offset, 0.0), LOD) -
				ValueAt(SurfacePoint - FVector(0.0, Offset, 0.0), LOD),
			ValueAt(SurfacePoint + FVector(0.0, 0.0, Offset), LOD) -
				ValueAt(SurfacePoint - FVector(0.0, 0.0, Offset), LOD));

		const FVector Normal = Gradient.GetSafeNormal();
		const double Dot = FVector::DotProduct(Normal, UnitDir);
		const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0, 1.0)));

		TestTrue(
			FString::Printf(TEXT("LOD %d 的法线应指向径向（实测偏差 %.2f°）"), LOD, AngleDeg),
			AngleDeg < 1.0);
	}

	return true;
}

#endif
