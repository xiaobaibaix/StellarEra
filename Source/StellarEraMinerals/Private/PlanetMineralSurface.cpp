// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralSurface.h"

#include "PlanetGeologyField.h"

#include "Async/ParallelFor.h"
#include "Generators/MarchingCubes.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"

void PlanetMineralSurface::Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
	FBuildResult& Out)
{
	Out.MeshesByElement.Reset();
	Out.GridPoints = 0;
	Out.BuildSeconds = 0.0;

	const int32 Resolution = Params.GridResolution;
	const double RadiusCm = double(Params.SurfaceRadiusCm);
	if (Field.IsEmpty() || Resolution < 8 || RadiusCm <= 0.0 || Params.VoxelSizeCm <= 0.f)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();

	const int32 ElementCount = Field.NumElements();
	Out.MeshesByElement.SetNum(ElementCount);

	const int32 CornerCount = Resolution + 1;
	const int32 TotalPoints = CornerCount * CornerCount * CornerCount;
	const double CubeSize = (2.0 * RadiusCm) / double(Resolution);

	// FMarchingCubes 内部按 (int)(Bounds.Width() / CubeSize) + 1 定角点数；浮点除法若
	// 得到 Resolution - 1，最外一圈（深度 0 附近，硅带就在那儿）会被整片裁掉。
	// 把盒子放宽 1e-9 的相对量（≈ 0.0001 cm），让除法稳定落在 Resolution 上。
	const double HalfExtent = RadiusCm * (1.0 + 1e-9);
	const double GridMin = -HalfExtent;
	const double InvCube = 1.0 / CubeSize;
	const double VoxelSizeCm = double(Params.VoxelSizeCm);

	// [点][元素] 稠密场：一次并行采样，后面每个元素各取一面，不重复调噪声。
	TArray<float> Norms;
	Norms.SetNumZeroed(TotalPoints * ElementCount);

	FThreadSafeCounter EvaluatedPoints;

	ParallelFor(TotalPoints, [&](int32 LinearIndex)
	{
		const int32 X = LinearIndex % CornerCount;
		const int32 Y = (LinearIndex / CornerCount) % CornerCount;
		const int32 Z = LinearIndex / (CornerCount * CornerCount);

		// 角点与 MC 自己的格点重合：Min + k * CubeSize。
		const double PX = GridMin + double(X) * CubeSize;
		const double PY = GridMin + double(Y) * CubeSize;
		const double PZ = GridMin + double(Z) * CubeSize;

		const double Distance = FMath::Sqrt(PX * PX + PY * PY + PZ * PZ);
		if (Distance > RadiusCm || Distance <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return; // 球外 / 球心：场值恒 0，数组本来就是 0
		}

		const double Depth = (RadiusCm - Distance) / VoxelSizeCm;
		if (Depth < 0.0)
		{
			return; // 球外
		}

		TArray<float> PointNorms;
		Field.SampleElementNorms(FVector(PX / Distance, PY / Distance, PZ / Distance),
			Depth, PointNorms);

		const int32 Base = LinearIndex * ElementCount;
		for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
		{
			Norms[Base + ElementIndex] =
				PointNorms.IsValidIndex(ElementIndex) ? PointNorms[ElementIndex] : 0.f;
		}

		EvaluatedPoints.Increment();
	});

	Out.GridPoints = EvaluatedPoints.GetValue();

	for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
	{
		// 三线性插值：把 MC 的任意查询点换成 8 个角点的加权和。
		// 闭包捕获了 ElementIndex，所以必须定义在元素循环里。
		auto SampleGrid = [&Norms, CornerCount, ElementCount, ElementIndex, Resolution](
			const UE::Math::TVector<double>& P, double Min, double InvCell) -> double
		{
			const double GX = FMath::Clamp((P.X - Min) * InvCell, 0.0, double(Resolution));
			const double GY = FMath::Clamp((P.Y - Min) * InvCell, 0.0, double(Resolution));
			const double GZ = FMath::Clamp((P.Z - Min) * InvCell, 0.0, double(Resolution));

			const int32 X0 = FMath::Min(int32(GX), Resolution - 1);
			const int32 Y0 = FMath::Min(int32(GY), Resolution - 1);
			const int32 Z0 = FMath::Min(int32(GZ), Resolution - 1);
			const double FX = GX - double(X0);
			const double FY = GY - double(Y0);
			const double FZ = GZ - double(Z0);

			auto At = [&Norms, CornerCount, ElementCount, ElementIndex](
				int32 InX, int32 InY, int32 InZ) -> double
			{
				return double(Norms[((InZ * CornerCount + InY) * CornerCount + InX)
					* ElementCount + ElementIndex]);
			};

			const double C00 = FMath::Lerp(At(X0, Y0, Z0), At(X0 + 1, Y0, Z0), FX);
			const double C10 = FMath::Lerp(At(X0, Y0 + 1, Z0), At(X0 + 1, Y0 + 1, Z0), FX);
			const double C01 = FMath::Lerp(At(X0, Y0, Z0 + 1), At(X0 + 1, Y0, Z0 + 1), FX);
			const double C11 = FMath::Lerp(At(X0, Y0 + 1, Z0 + 1), At(X0 + 1, Y0 + 1, Z0 + 1), FX);
			const double C0 = FMath::Lerp(C00, C10, FY);
			const double C1 = FMath::Lerp(C01, C11, FY);
			return FMath::Lerp(C0, C1, FZ);
		};

		UE::Geometry::FMarchingCubes Marching;
		Marching.Bounds = UE::Geometry::TAxisAlignedBox3<double>(
			UE::Math::TVector<double>::Zero(), HalfExtent);
		Marching.CubeSize = CubeSize;
		Marching.IsoValue = double(Params.IsoValue);
		// Implicit 只是一次三线性插值，角点值也已经预计算，值缓存的额外开销不值得。
		Marching.bEnableValueCaching = false;
		Marching.bParallelCompute = true;
		Marching.Implicit = [&SampleGrid, GridMin, InvCube](UE::Math::TVector<double> P) -> double
		{
			return SampleGrid(P, GridMin, InvCube);
		};

		Marching.Generate();

		FPlanetMineralMesh& Mesh = Out.MeshesByElement[ElementIndex];
		Mesh.VerticesCm.Reserve(Marching.Vertices.Num());
		for (const UE::Math::TVector<double>& Vertex : Marching.Vertices)
		{
			// 球外角点恒为 0（上面早退），内侧角点为正：MC 在这对端点间线性插值时，
			// 顶点会落到 1 - Iso/fa 的外侧比例上，也就是球外。观察体的半透明壳正好
			// 画在 R 上，伸出去的顶点会戳出玻璃球，所以这里逐个径向夹回球面。
			const double LengthCm = Vertex.Size();
			const double ScaleFactor = LengthCm > RadiusCm ? RadiusCm / LengthCm : 1.0;
			Mesh.VerticesCm.Add(FVector3f(
				float(Vertex.X * ScaleFactor),
				float(Vertex.Y * ScaleFactor),
				float(Vertex.Z * ScaleFactor)));
		}

		Mesh.Triangles.Reserve(Marching.Triangles.Num() * 3);
		for (const UE::Geometry::FIndex3i& Triangle : Marching.Triangles)
		{
			Mesh.Triangles.Add(Triangle.A);
			Mesh.Triangles.Add(Triangle.B);
			Mesh.Triangles.Add(Triangle.C);
		}

		int32 Occupied = 0;
		for (int32 PointIndex = 0; PointIndex < TotalPoints; ++PointIndex)
		{
			if (Norms[PointIndex * ElementCount + ElementIndex] > Params.IsoValue)
			{
				++Occupied;
			}
		}
		Mesh.OccupiedCells = Occupied;
	}

	Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
}
