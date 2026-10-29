// 分块跨帧挖掘的纯几何回归测试。
//
// 这里钉的是「切块」这件事的正确性：一块一块地写，与一次写完整个包围盒，
// 逐格语义必须完全相同。前提是块**不重叠**且**并集恰好等于包围盒** ——
// 满足这两条，同一个逐格谓词在块上应用一次、在整盒上应用一次，结果按定义
// 相同（谓词只读格子自己的坐标，不依赖邻格）。所以这两个性质就是等价性的
// 全部依据，必须机械化检查。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetEditBrush.h"
#include "VoxelIntBox.h"
#include "VoxelVector.h"
// 为了直接调 FVoxelSphereToolsImpl::GetBounds 与我们的实现比对。
// 插件把 inline 定义放在 .inl 里，头文件本身只有声明。
#include "VoxelTools/Impl/VoxelSphereToolsImpl.inl"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveChunkTest,
	"StellarEra.Planet.Editor.ProgressiveChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveChunkTest::RunTest(const FString& Parameters)
{
	// --- 1. 球包围盒必须与插件逐分量相同 ---
	// 直接调插件的 GetBounds 比对，不写死期望数字：我们抄的就是它，
	// 它改了我们必须跟着改（否则球的边缘会被削掉一圈或多挖一圈）。
	//
	// 中心取带小数的坐标：整格坐标下 float / double 的差别看不出来，
	// 非整数才是取整方向出错时最先暴露的地方。
	{
		const FVoxelVector Centers[] = {
			FVoxelVector(0.0, 0.0, 0.0),
			FVoxelVector(1234.75, -987.5, 20000.25),
			FVoxelVector(-0.4, 0.6, 1.5),
		};
		const float Radii[] = {0.5f, 1.f, 7.f, 64.f, 640.f};

		for (const FVoxelVector& Center : Centers)
		{
			for (const float Radius : Radii)
			{
				const FVoxelIntBox Expected = FVoxelSphereToolsImpl::GetBounds(Center, Radius);
				const FPlanetEditVoxelBox Actual = PlanetEditBrush::MakeSphereEditVoxelBox(
					FVector(Center.X, Center.Y, Center.Z), Radius);

				TestTrue(FString::Printf(
					TEXT("球包围盒 R=%.1f 中心=%s：Min 期望 %s，实际 %s"),
					Radius, *Center.ToString(), *Expected.Min.ToString(), *Actual.Min.ToString()),
					Expected.Min == Actual.Min);
				TestTrue(FString::Printf(
					TEXT("球包围盒 R=%.1f 中心=%s：Max(开) 期望 %s，实际 %s"),
					Radius, *Center.ToString(), *Expected.Max.ToString(),
					*Actual.MaxExclusive.ToString()),
					Expected.Max == Actual.MaxExclusive);
			}
		}
	}

	// --- 2. 切块：不重叠、并集 == 原盒、尺寸不超预算 ---
	// 用非对齐的盒子起点，专门覆盖「块边界不落在 32 网格上」的情况 ——
	// ParallelSet 按叶子重叠迭代，边界叶子被两块各处理一部分，并集仍正确。
	const auto CheckTiling = [&](const FPlanetEditVoxelBox& Box, int32 ChunkSize, const TCHAR* Label)
	{
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(Box, ChunkSize, Chunks);

		TestTrue(FString::Printf(TEXT("%s：必须切出至少一块"), Label), Chunks.Num() > 0);
		if (Chunks.Num() == 0)
		{
			return;
		}

		// 体积守恒 + 无重叠：块体积累加必须**恰好**等于原盒体积。
		// 重叠会让和偏大，漏掉一块会让和偏小，两种错都能抓到。
		int64 SumCells = 0;
		FIntVector UnionMin = Chunks[0].Min;
		FIntVector UnionMax = Chunks[0].MaxExclusive;
		bool bSizesOk = true;
		for (const FPlanetEditVoxelBox& Chunk : Chunks)
		{
			const FIntVector Size = Chunk.GetSize();
			SumCells += int64(Size.X) * Size.Y * Size.Z;
			bSizesOk &= Size.X <= ChunkSize && Size.Y <= ChunkSize && Size.Z <= ChunkSize;
			UnionMin = FIntVector(
				FMath::Min(UnionMin.X, Chunk.Min.X),
				FMath::Min(UnionMin.Y, Chunk.Min.Y),
				FMath::Min(UnionMin.Z, Chunk.Min.Z));
			UnionMax = FIntVector(
				FMath::Max(UnionMax.X, Chunk.MaxExclusive.X),
				FMath::Max(UnionMax.Y, Chunk.MaxExclusive.Y),
				FMath::Max(UnionMax.Z, Chunk.MaxExclusive.Z));
		}

		const FIntVector BoxSize = Box.GetSize();
		const int64 BoxCells = int64(BoxSize.X) * BoxSize.Y * BoxSize.Z;
		TestEqual(FString::Printf(TEXT("%s：块体积累加必须等于原盒（重叠/漏块都会破坏它）"), Label),
			SumCells, BoxCells);
		TestTrue(FString::Printf(TEXT("%s：并集 Min 必须等于原盒 Min"), Label), UnionMin == Box.Min);
		TestTrue(FString::Printf(TEXT("%s：并集 Max 必须等于原盒 Max"), Label),
			UnionMax == Box.MaxExclusive);
		TestTrue(FString::Printf(TEXT("%s：单块边长不得超预算"), Label), bSizesOk);

		// 体积累加相等 + 并集相等 还不足以保证无重叠（可能既有重叠又有漏）。
		// 逐格计数兜底：每格必须**恰好**被块覆盖一次。盒子较大时抽样，
		// 避免测试本身太慢。
		const int32 StepX = FMath::Max(BoxSize.X / 12, 1);
		const int32 StepY = FMath::Max(BoxSize.Y / 12, 1);
		const int32 StepZ = FMath::Max(BoxSize.Z / 12, 1);
		bool bCoveredOnce = true;
		for (int32 X = Box.Min.X; X < Box.MaxExclusive.X; X += StepX)
		{
			for (int32 Y = Box.Min.Y; Y < Box.MaxExclusive.Y; Y += StepY)
			{
				for (int32 Z = Box.Min.Z; Z < Box.MaxExclusive.Z; Z += StepZ)
				{
					int32 Hits = 0;
					for (const FPlanetEditVoxelBox& Chunk : Chunks)
					{
						if (X >= Chunk.Min.X && X < Chunk.MaxExclusive.X &&
							Y >= Chunk.Min.Y && Y < Chunk.MaxExclusive.Y &&
							Z >= Chunk.Min.Z && Z < Chunk.MaxExclusive.Z)
						{
							++Hits;
						}
					}
					if (Hits != 1)
					{
						bCoveredOnce = false;
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("%s：抽样格子必须恰好被一块覆盖"), Label), bCoveredOnce);
	};

	CheckTiling(FPlanetEditVoxelBox{FIntVector(0, 0, 0), FIntVector(10, 10, 10)}, 128, TEXT("小盒"));
	// Z 轴 501 → 1299：起点与终点都不是 128 的整数倍，块边界必然切在格子中间。
	CheckTiling(FPlanetEditVoxelBox{FIntVector(-1007, 33, 501), FIntVector(255, 300, 1299)}, 128,
		TEXT("非对齐中盒"));
	CheckTiling(FPlanetEditVoxelBox{FIntVector(-640, -640, -640), FIntVector(640, 640, 640)}, 128,
		TEXT("1280 立方"));

	// --- 3. 无效盒必须切出 0 块 ---
	// 任何一轴 MaxExclusive <= Min 就没有格子可写，应当返回空列表，
	// 而不是产出一堆负尺寸的块让调用方去判。
	{
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(
			FPlanetEditVoxelBox{FIntVector(0, 0, 0), FIntVector(10, 10, 0)}, 128, Chunks);
		TestEqual(TEXT("空盒（Z 跨度 0）必须切出 0 块"), Chunks.Num(), 0);
	}

	// --- 4. 小块笔刷必须退化成单块 ---
	// 半径 8 的球包围盒只有 22³，切 128 的块必须恰好得到 1 块 ——
	// 这条保证小笔刷的路径与改动前完全一致。
	{
		const FPlanetEditVoxelBox Small = PlanetEditBrush::MakeSphereEditVoxelBox(
			FVector(100.0, 200.0, 300.0), 8.f);
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(Small, 128, Chunks);
		TestEqual(TEXT("半径 8 的球必须只切出 1 块"), Chunks.Num(), 1);
	}

	return true;
}

#endif
