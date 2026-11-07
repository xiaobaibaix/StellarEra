// 分块跨帧挖掘的纯几何回归测试。块不重叠且并集恰好等于包围盒，逐格谓词又只读
// 格子自身坐标，这三条成立即等价 —— 所以必须机械化检查。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetEditBrush.h"
#include "PlanetProgressiveEdit.h"
#include "PlanetProgressiveEditKernel.h"
#include "VoxelIntBox.h"
#include "VoxelValue.h"
#include "VoxelVector.h"
// 为了直接调插件的 GetBounds / SphereEdit / BoxEdit 与我们的实现比对。
// 插件把 inline 定义放在 .inl 里，头文件本身只有声明。
#include "VoxelTools/Impl/VoxelBoxToolsImpl.inl"
#include "VoxelTools/Impl/VoxelSphereToolsImpl.inl"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveChunkTest,
	"StellarEra.Planet.Editor.ProgressiveChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveChunkTest::RunTest(const FString& Parameters)
{
	// --- 1. 球包围盒必须与插件逐分量相同 ---
	// 直接调插件的 GetBounds 比对，不写死期望数字（我们抄的就是它）。中心取非
	// 整数：整格坐标看不出 float / double 差别，取整方向出错也会被掩盖。
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
	// 用非对齐的盒子起点，专门覆盖「块边界不落在网格上」的情况。
	const auto CheckTiling = [&](const FPlanetEditVoxelBox& Box, int32 ChunkSize, const TCHAR* Label)
	{
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(Box, ChunkSize, Chunks);

		TestTrue(FString::Printf(TEXT("%s：必须切出至少一块"), Label), Chunks.Num() > 0);
		if (Chunks.Num() == 0)
		{
			return;
		}

		// 块体积累加必须恰好等于原盒：重叠偏大、漏块偏小，两种错都能抓到。
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

		// 体积和与并集相等仍不能保证无重叠（可能既重叠又漏），逐格计数兜底。
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
	// 任何一轴 MaxExclusive <= Min 就返回空列表，而不是产出负尺寸的块。
	{
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(
			FPlanetEditVoxelBox{FIntVector(0, 0, 0), FIntVector(10, 10, 0)}, 128, Chunks);
		TestEqual(TEXT("空盒（Z 跨度 0）必须切出 0 块"), Chunks.Num(), 0);
	}

	// --- 3b. 块边长 MAX_int32 必须退化成恰好 1 块 ---
	// 同步路径就这么调；同时防 Min + MAX_int32 的 int32 溢出（会让循环失控）。
	{
		const FPlanetEditVoxelBox Big{FIntVector(-5000, 200, -7), FIntVector(5000, 900, 13)};
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(Big, MAX_int32, Chunks);
		TestEqual(TEXT("块边长 MAX_int32 必须切出恰好 1 块"), Chunks.Num(), 1);
		if (Chunks.Num() == 1)
		{
			TestTrue(TEXT("单块必须与原盒逐分量相同"),
				Chunks[0].Min == Big.Min && Chunks[0].MaxExclusive == Big.MaxExclusive);
		}
	}

	// --- 4. 小块笔刷必须退化成单块 ---
	// 半径 8 的球包围盒只有 22³，切 128 必须恰好 1 块，小笔刷路径才不变。
	{
		const FPlanetEditVoxelBox Small = PlanetEditBrush::MakeSphereEditVoxelBox(
			FVector(100.0, 200.0, 300.0), 8.f);
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(Small, 128, Chunks);
		TestEqual(TEXT("半径 8 的球必须只切出 1 块"), Chunks.Num(), 1);
	}

	return true;
}

// ---------------------------------------------------------------------------
// 逐格等价：插件「一次写完整个包围盒」 vs 我们「按块写完整个包围盒」
// ---------------------------------------------------------------------------
// 上一组钉几何前提，这一组钉结论：不写死期望值，基准是插件自己的 SphereEdit / BoxEdit。

namespace
{
	/**
	 * 只为引擎测试存在的假数据，只实现插件用到的 Set(Bounds, Lambda)，
	 * 于是 SphereEdit / BoxEdit 不需要体素世界就能跑（并行与否不改变每格结果）。
	 */
	struct FProgressiveTestVoxelData
	{
		FVoxelIntBox Bounds;
		TArray<FVoxelValue> Cells;

		/** Set 被调用时遍历到的格数。0 = 插件压根没调进来，否则是谓词自己早退。 */
		int32 SetCalls = 0;

		void Init(const FVoxelIntBox& InBounds)
		{
			Bounds = InBounds;
			// 零初始化：FVoxelValue 的默认构造不写成员（见 VoxelValue.h）。
			Cells.SetNumZeroed(int32(InBounds.Count()));
		}

		FVoxelValue& At(const int32 X, const int32 Y, const int32 Z)
		{
			return Cells[Index(X, Y, Z)];
		}

		const FVoxelValue& At(const int32 X, const int32 Y, const int32 Z) const
		{
			return Cells[Index(X, Y, Z)];
		}

		// 插件调用的是 Data.template Set<FVoxelValue>(...)，签名必须对上。
		template<typename... TArgs, typename F>
		void Set(const FVoxelIntBox& InBounds, F Lambda)
		{
			InBounds.Iterate([&](int32 X, int32 Y, int32 Z)
			{
				++SetCalls;
				Lambda(X, Y, Z, At(X, Y, Z));
			});
		}

		/**
		 * 确定性种子必须铺满整个存储量程。FVoxelValue 是 16 位 [-32767, 32767]；按 8 位
		 * ±127 撒会让球的 Max-merge 恒等于旧值、逐格比对假通过。跳过 INVALID（-32768）。
		 */
		void SeedMixed()
		{
			// 量程若被改成 8 位体素值，这里必须跟着改，否则种子又会被压到
			// 量程的一角而失去意义。宁可编译不过，也不要静默退化成空转。
			static_assert(FVoxelValue::MAX_VOXELVALUE == 32767,
				"种子量程按 16 位体素值写的；改成 8 位后需要重算量程与类型");

			constexpr int32 MaxStorage = int32(FVoxelValue::MAX_VOXELVALUE);
			constexpr uint32 StorageSpan = uint32(MaxStorage) * 2 + 1;

			Bounds.Iterate([&](int32 X, int32 Y, int32 Z)
			{
				const uint32 Hash =
					uint32(X) * 73856093u ^ uint32(Y) * 19349663u ^ uint32(Z) * 83492791u;
				FVoxelValue Value;
				Value.GetStorage() =
					int16(int32((Hash >> 3) % StorageSpan) - MaxStorage);
				At(X, Y, Z) = Value;
			});
		}

	private:
		int32 Index(const int32 X, const int32 Y, const int32 Z) const
		{
			const int32 SizeY = Bounds.Max.Y - Bounds.Min.Y;
			const int32 SizeZ = Bounds.Max.Z - Bounds.Min.Z;
			return ((X - Bounds.Min.X) * SizeY + (Y - Bounds.Min.Y)) * SizeZ + (Z - Bounds.Min.Z);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveKernelTest,
	"StellarEra.Planet.Editor.ProgressiveKernel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveKernelTest::RunTest(const FString& Parameters)
{
	/** 逐格比对两份数据，报第一处差异。 */
	const auto CompareCells = [this](
		const FProgressiveTestVoxelData& Expected,
		const FProgressiveTestVoxelData& Actual,
		const FString& Label)
	{
		int32 MismatchCount = 0;
		FString FirstMismatch;
		Expected.Bounds.Iterate([&](int32 X, int32 Y, int32 Z)
		{
			const FVoxelValue& A = Expected.At(X, Y, Z);
			const FVoxelValue& B = Actual.At(X, Y, Z);
			if (A != B)
			{
				if (MismatchCount == 0)
				{
					FirstMismatch = FString::Printf(TEXT("首处 (%d,%d,%d)：插件 %s(%d)，我们 %s(%d)"),
						X, Y, Z, *A.ToString(), int32(A.GetStorage()),
						*B.ToString(), int32(B.GetStorage()));
				}
				++MismatchCount;
			}
		});

		TestEqual(FString::Printf(TEXT("%s：逐格必须与插件完全一致（差异格数 %d，%s）"),
			*Label, MismatchCount, *FirstMismatch), MismatchCount, 0);
	};

	// --- 1. 球：插件一次写完 vs 我们按块写 ---
	// 中心取非整数，半径覆盖「小于 2」（内圈阈值夹到 0）与「远大于块」两种极端。
	{
		const FVoxelVector Centers[] = {
			FVoxelVector(0.0, 0.0, 0.0),
			FVoxelVector(12.4, -7.6, 3.5),
		};
		const float Radii[] = {0.5f, 1.f, 3.f, 8.f, 20.f};
		// 7 让每笔切成上千块（边界与格子、球面全不对齐），128 是运行期默认块长。
		const int32 ChunkSizes[] = {7, 128};

		for (const FVoxelVector& Center : Centers)
		{
			for (const float Radius : Radii)
			{
				for (const int32 ChunkSize : ChunkSizes)
				{
					for (const bool bPlace : {false, true})
					{
						const FString Label = FString::Printf(TEXT("球 R=%.1f 中心=%s 块=%d %s"),
							Radius, *Center.ToString(), ChunkSize, bPlace ? TEXT("填") : TEXT("挖"));

						const FVoxelIntBox Bounds = FVoxelSphereToolsImpl::GetBounds(Center, Radius);

						FProgressiveTestVoxelData PluginSide;
						PluginSide.Init(Bounds);
						PluginSide.SeedMixed();

						// 留存未编辑的种子：证明这一轮确实改到了东西，否则比对可能空转。
						const TArray<FVoxelValue> UntouchedSeed = PluginSide.Cells;

						if (bPlace)
						{
							FVoxelSphereToolsImpl::SphereEdit<true>(PluginSide, Center, Radius);
						}
						else
						{
							FVoxelSphereToolsImpl::SphereEdit<false>(PluginSide, Center, Radius);
						}

						FProgressiveTestVoxelData OurSide;
						OurSide.Init(Bounds);
						OurSide.SeedMixed();

						const FPlanetEditVoxelBox FullBox{Bounds.Min, Bounds.Max};
						TArray<FPlanetEditVoxelBox> Chunks;
						PlanetEditBrush::SplitVoxelBoxIntoChunks(FullBox, ChunkSize, Chunks);

						const FVector CenterVoxels(Center.X, Center.Y, Center.Z);
						float SquaredRadiusPlus2 = 0.f;
						float SquaredRadiusMinus2 = 0.f;
						PlanetProgressiveKernel::GetSphereThresholds(
							Radius, SquaredRadiusPlus2, SquaredRadiusMinus2);

						for (const FPlanetEditVoxelBox& Chunk : Chunks)
						{
							for (int32 X = Chunk.Min.X; X < Chunk.MaxExclusive.X; ++X)
							{
								for (int32 Y = Chunk.Min.Y; Y < Chunk.MaxExclusive.Y; ++Y)
								{
									for (int32 Z = Chunk.Min.Z; Z < Chunk.MaxExclusive.Z; ++Z)
									{
										FVoxelValue& Value = OurSide.At(X, Y, Z);
										Value = PlanetProgressiveKernel::EvaluateSphereValue(
											Radius, SquaredRadiusPlus2, SquaredRadiusMinus2,
											PlanetProgressiveKernel::SquaredDistanceTo(
												CenterVoxels, X, Y, Z),
											Value, bPlace);
									}
								}
							}
						}

						TestTrue(Label + TEXT("：必须切出至少一块（否则下面的比对是空转）"),
							Chunks.Num() > 0);
						int32 PluginChangedCells = 0;
						for (int32 Index = 0; Index < PluginSide.Cells.Num(); ++Index)
						{
							PluginChangedCells += PluginSide.Cells[Index] != UntouchedSeed[Index] ? 1 : 0;
						}
						TestTrue(FString::Printf(
							TEXT("%s：插件必须真的改到体素（改了 %d / %d 格，Set 遍历 %d 格，"
								 "Plus2=%.3f Minus2=%.3f）"),
							*Label, PluginChangedCells, PluginSide.Cells.Num(), PluginSide.SetCalls,
							SquaredRadiusPlus2, SquaredRadiusMinus2),
							PluginChangedCells > 0);
						CompareCells(PluginSide, OurSide, Label);
					}
				}
			}
		}
	}

	// --- 2. 方块：贴边判定必须用整笔编辑的盒子 ---
	// 盒子边长故意不是块边长的整数倍：若内核误用「当前块的盒子」判贴边，
	// 每个块的内壁都会被刷成表面。
	{
		const FPlanetEditVoxelBox FullBox{FIntVector(-33, -18, 7), FIntVector(33, 19, 28)};
		const FVoxelIntBox Bounds(FullBox.Min, FullBox.MaxExclusive);
		const int32 ChunkSizes[] = {5, 16, 128};

		for (const int32 ChunkSize : ChunkSizes)
		{
			for (const bool bPlace : {false, true})
			{
				const FString Label = FString::Printf(
					TEXT("方块 块=%d %s"), ChunkSize, bPlace ? TEXT("填") : TEXT("挖"));

				FProgressiveTestVoxelData PluginSide;
				PluginSide.Init(Bounds);
				PluginSide.SeedMixed();
				const TArray<FVoxelValue> UntouchedSeed = PluginSide.Cells;
				if (bPlace)
				{
					FVoxelBoxToolsImpl::BoxEdit<true>(PluginSide, Bounds);
				}
				else
				{
					FVoxelBoxToolsImpl::BoxEdit<false>(PluginSide, Bounds);
				}

				FProgressiveTestVoxelData OurSide;
				OurSide.Init(Bounds);
				OurSide.SeedMixed();

				TArray<FPlanetEditVoxelBox> Chunks;
				PlanetEditBrush::SplitVoxelBoxIntoChunks(FullBox, ChunkSize, Chunks);
				for (const FPlanetEditVoxelBox& Chunk : Chunks)
				{
					for (int32 X = Chunk.Min.X; X < Chunk.MaxExclusive.X; ++X)
					{
						for (int32 Y = Chunk.Min.Y; Y < Chunk.MaxExclusive.Y; ++Y)
						{
							for (int32 Z = Chunk.Min.Z; Z < Chunk.MaxExclusive.Z; ++Z)
							{
								FVoxelValue& Value = OurSide.At(X, Y, Z);
								Value = PlanetProgressiveKernel::EvaluateBoxValue(
									FIntVector(X, Y, Z), FullBox.Min, FullBox.MaxExclusive,
									Value, bPlace);
							}
						}
					}
				}

				TestTrue(Label + TEXT("：必须切出至少一块（否则下面的比对是空转）"),
					Chunks.Num() > 0);
				TestTrue(Label + TEXT("：插件必须真的改到体素（否则比对是空转）"),
					PluginSide.Cells != UntouchedSeed);
				CompareCells(PluginSide, OurSide, Label);
			}
		}
	}

	// --- 3. 球阈值的三段语义（代表点）---
	// 单独钉边界条件：「>」与「<=」写反在整数中心上可能表现不出来。
	{
		const float Radius = 8.f;
		float SquaredRadiusPlus2 = 0.f;
		float SquaredRadiusMinus2 = 0.f;
		PlanetProgressiveKernel::GetSphereThresholds(
			Radius, SquaredRadiusPlus2, SquaredRadiusMinus2);

		TestEqual(TEXT("阈值 (R+2)²"), SquaredRadiusPlus2, FMath::Square(Radius + 2));
		TestEqual(TEXT("阈值 (R-2)²"), SquaredRadiusMinus2, FMath::Square(Radius - 2));

		// D = R - 2：内圈边界（包含），挖成完全空、填成完全实。
		const float AtMinus2 = FMath::Square(Radius - 2);
		TestTrue(TEXT("D = R-2 挖掘必须是完全空"),
			PlanetProgressiveKernel::EvaluateSphereValue(
				Radius, SquaredRadiusPlus2, SquaredRadiusMinus2, AtMinus2,
				FVoxelValue::Full(), false).IsTotallyEmpty());
		TestTrue(TEXT("D = R-2 填充必须是完全实"),
			PlanetProgressiveKernel::EvaluateSphereValue(
				Radius, SquaredRadiusPlus2, SquaredRadiusMinus2, AtMinus2,
				FVoxelValue::Empty(), true).IsTotallyFull());

		// D = R：衰减带正中，得到表面 0（挖填都是）。
		const float AtRadius = FMath::Square(Radius);
		TestTrue(TEXT("D = R 挖掘必须得到表面 0"),
			PlanetProgressiveKernel::EvaluateSphereValue(
				Radius, SquaredRadiusPlus2, SquaredRadiusMinus2, AtRadius,
				FVoxelValue::Full(), false).IsNull());
		TestTrue(TEXT("D = R 填充必须得到表面 0"),
			PlanetProgressiveKernel::EvaluateSphereValue(
				Radius, SquaredRadiusPlus2, SquaredRadiusMinus2, AtRadius,
				FVoxelValue::Empty(), true).IsNull());

		// D = R + 3：超出外圈，原值一字不动。
		const FVoxelValue Untouched{ 0.25f };
		TestTrue(TEXT("D = R+3 必须完全不动"),
			PlanetProgressiveKernel::EvaluateSphereValue(
				Radius, SquaredRadiusPlus2, SquaredRadiusMinus2, FMath::Square(Radius + 3),
				Untouched, false) == Untouched);
	}

	return true;
}

// 单 op 的 EvaluateOps 必须与直接调 EvaluateSphereValue / EvaluateBoxValue 逐格同值。
// 球心非整数，好让 ±2 边界落在格子中间、两侧都被走到。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveOpsKernelTest,
	"StellarEra.Planet.Editor.Kernel.OpsMatchSingle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveOpsKernelTest::RunTest(const FString& Parameters)
{
	const FVoxelValue Seeds[] = {
		FVoxelValue::Empty(), FVoxelValue::Full(),
		FVoxelValue{ 0.3f }, FVoxelValue{ -0.7f } };

	// --- 球 ---
	const float Radius = 6.f;
	float Plus2 = 0.f;
	float Minus2 = 0.f;
	PlanetProgressiveKernel::GetSphereThresholds(Radius, Plus2, Minus2);

	FPlanetEditOp SphereOp;
	SphereOp.Shape = EPlanetBrushShape::Sphere;
	SphereOp.CenterVoxels = FVector(1.5, -2.25, 3.75);
	SphereOp.RadiusVoxels = Radius;
	SphereOp.SquaredRadiusPlus2 = Plus2;
	SphereOp.SquaredRadiusMinus2 = Minus2;
	SphereOp.Bounds = PlanetEditBrush::MakeSphereEditVoxelBox(SphereOp.CenterVoxels, Radius);

	int32 SphereMismatch = 0;
	for (int32 X = SphereOp.Bounds.Min.X; X < SphereOp.Bounds.MaxExclusive.X; ++X)
	{
		for (int32 Y = SphereOp.Bounds.Min.Y; Y < SphereOp.Bounds.MaxExclusive.Y; ++Y)
		{
			for (int32 Z = SphereOp.Bounds.Min.Z; Z < SphereOp.Bounds.MaxExclusive.Z; ++Z)
			{
				const float SquaredDistance = PlanetProgressiveKernel::SquaredDistanceTo(
					SphereOp.CenterVoxels, X, Y, Z);

				for (int32 SeedIndex = 0; SeedIndex < UE_ARRAY_COUNT(Seeds); ++SeedIndex)
				{
					for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
					{
						const bool bPlace = PlaceIndex != 0;
						const FVoxelValue Direct = PlanetProgressiveKernel::EvaluateSphereValue(
							Radius, Plus2, Minus2, SquaredDistance, Seeds[SeedIndex], bPlace);
						const FVoxelValue ThroughOp = PlanetProgressiveKernel::EvaluateOps(
							&SphereOp, 1, X, Y, Z, Seeds[SeedIndex], bPlace);

						if (Direct != ThroughOp)
						{
							++SphereMismatch;
						}
					}
				}
			}
		}
	}
	TestEqual(TEXT("单 op 的球求值与 EvaluateSphereValue 逐格同值"), SphereMismatch, 0);

	// --- 方块：贴边判定读的是 Op.Bounds，单 op 时就是整笔编辑的盒子 ---
	FPlanetEditOp BoxOp;
	BoxOp.Shape = EPlanetBrushShape::Cube;
	BoxOp.CenterVoxels = FVector(2.0, 2.0, 2.0);
	BoxOp.Bounds = PlanetEditBrush::MakeVoxelBox(
		PlanetEditBrush::MakeBox(BoxOp.CenterVoxels, 6, 1, 64));

	int32 BoxMismatch = 0;
	for (int32 X = BoxOp.Bounds.Min.X; X < BoxOp.Bounds.MaxExclusive.X; ++X)
	{
		for (int32 Y = BoxOp.Bounds.Min.Y; Y < BoxOp.Bounds.MaxExclusive.Y; ++Y)
		{
			for (int32 Z = BoxOp.Bounds.Min.Z; Z < BoxOp.Bounds.MaxExclusive.Z; ++Z)
			{
				for (int32 SeedIndex = 0; SeedIndex < UE_ARRAY_COUNT(Seeds); ++SeedIndex)
				{
					for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
					{
						const bool bPlace = PlaceIndex != 0;
						const FVoxelValue Direct = PlanetProgressiveKernel::EvaluateBoxValue(
							FIntVector(X, Y, Z), BoxOp.Bounds.Min, BoxOp.Bounds.MaxExclusive,
							Seeds[SeedIndex], bPlace);
						const FVoxelValue ThroughOp = PlanetProgressiveKernel::EvaluateOps(
							&BoxOp, 1, X, Y, Z, Seeds[SeedIndex], bPlace);

						if (Direct != ThroughOp)
						{
							++BoxMismatch;
						}
					}
				}
			}
		}
	}
	TestEqual(TEXT("单 op 的方块求值与 EvaluateBoxValue 逐格同值"), BoxMismatch, 0);

	// --- 两个 op：必须等于「先应用 op0、再应用 op1」 ---
	FPlanetEditOp TwoOps[2] = { SphereOp, SphereOp };
	TwoOps[1].CenterVoxels = FVector(4.0, 0.0, 0.0);
	TwoOps[1].Bounds = PlanetEditBrush::MakeSphereEditVoxelBox(TwoOps[1].CenterVoxels, Radius);

	int32 OrderMismatch = 0;
	for (int32 X = TwoOps[0].Bounds.Min.X; X < TwoOps[0].Bounds.MaxExclusive.X; ++X)
	{
		for (int32 Y = TwoOps[0].Bounds.Min.Y; Y < TwoOps[0].Bounds.MaxExclusive.Y; ++Y)
		{
			for (int32 Z = TwoOps[0].Bounds.Min.Z; Z < TwoOps[0].Bounds.MaxExclusive.Z; ++Z)
			{
				for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
				{
					const bool bPlace = PlaceIndex != 0;
					FVoxelValue Expected = PlanetProgressiveKernel::EvaluateSphereValue(
						Radius, Plus2, Minus2,
						PlanetProgressiveKernel::SquaredDistanceTo(TwoOps[0].CenterVoxels, X, Y, Z),
						FVoxelValue::Full(), bPlace);
					Expected = PlanetProgressiveKernel::EvaluateSphereValue(
						Radius, Plus2, Minus2,
						PlanetProgressiveKernel::SquaredDistanceTo(TwoOps[1].CenterVoxels, X, Y, Z),
						Expected, bPlace);

					const FVoxelValue ThroughOps = PlanetProgressiveKernel::EvaluateOps(
						TwoOps, 2, X, Y, Z, FVoxelValue::Full(), bPlace);
					if (Expected != ThroughOps)
					{
						++OrderMismatch;
					}
				}
			}
		}
	}
	TestEqual(TEXT("两个 op 必须按顺序依次生效"), OrderMismatch, 0);

	return true;
}

// 装配的纯几何入口：没有行星也要用同一套公式。钉两件事：包围盒 / 块数与笔刷
// 函数逐分量一致（不能有第二套公式）；每块 NumOps 等于创建时的 op 数。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveJobSetupTest,
	"StellarEra.Planet.Editor.Job.SetupFromRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveJobSetupTest::RunTest(const FString& Parameters)
{
	FPlanetEditRequest Sphere;
	Sphere.Shape = EPlanetBrushShape::Sphere;
	Sphere.CenterVoxels = FVector(10.0, -4.0, 7.0);
	Sphere.RadiusVoxels = 6.f;
	Sphere.SizeVoxels = 10;
	Sphere.MinSizeVoxels = 1;
	Sphere.MaxSizeVoxels = 64;
	Sphere.bPlace = false;
	Sphere.bMultiThreaded = false;
	Sphere.ChunkSizeVoxels = 4;

	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("球装配成功"), Job->SetupFromRequest(Sphere)))
	{
		return false;
	}

	const FPlanetEditVoxelBox ExpectedBounds =
		PlanetEditBrush::MakeSphereEditVoxelBox(Sphere.CenterVoxels, Sphere.RadiusVoxels);

	TestEqual(TEXT("一个 op"), Job->GetOpCount(), 1);
	TestTrue(TEXT("整笔包围盒与笔刷同一个函数同值"),
		Job->GetFullBounds().Min == ExpectedBounds.Min
		&& Job->GetFullBounds().MaxExclusive == ExpectedBounds.MaxExclusive);
	TestTrue(TEXT("op 的包围盒就是整笔的包围盒"),
		Job->GetOps()[0].Bounds.Min == ExpectedBounds.Min
		&& Job->GetOps()[0].Bounds.MaxExclusive == ExpectedBounds.MaxExclusive);

	TArray<FPlanetEditVoxelBox> ExpectedChunks;
	PlanetEditBrush::SplitVoxelBoxIntoChunks(
		ExpectedBounds, Sphere.ChunkSizeVoxels, ExpectedChunks);
	TestEqual(TEXT("块数与切块函数一致"), Job->GetChunkCount(), ExpectedChunks.Num());
	TestEqual(TEXT("还没开始推"), Job->GetNextChunkIndex(), 0);
	TestFalse(TEXT("还没做完"), Job->IsFinished());

	int32 WrongPrefix = 0;
	for (const FPlanetEditChunk& Chunk : Job->GetChunks())
	{
		if (Chunk.NumOps != 1)
		{
			++WrongPrefix;
		}
	}
	TestEqual(TEXT("只有一个 op 时每个块的前缀都是 1"), WrongPrefix, 0);

	// --- 方块：包围盒走 MakeBox + MakeVoxelBox，半径留 0 ---
	FPlanetEditRequest Cube = Sphere;
	Cube.Shape = EPlanetBrushShape::Cube;
	Cube.RadiusVoxels = 0.f;
	Cube.CenterVoxels = FVector(2.0, 2.0, 2.0);
	Cube.SizeVoxels = 6;

	TSharedPtr<FPlanetProgressiveEdit> CubeJob = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("方块装配成功"), CubeJob->SetupFromRequest(Cube)))
	{
		return false;
	}

	const FPlanetEditVoxelBox ExpectedCubeBounds = PlanetEditBrush::MakeVoxelBox(
		PlanetEditBrush::MakeBox(Cube.CenterVoxels, Cube.SizeVoxels,
			Cube.MinSizeVoxels, Cube.MaxSizeVoxels));
	TestTrue(TEXT("方块包围盒与笔刷同一个函数同值"),
		CubeJob->GetFullBounds().Min == ExpectedCubeBounds.Min
		&& CubeJob->GetFullBounds().MaxExclusive == ExpectedCubeBounds.MaxExclusive);
	TestEqual(TEXT("方块没有半径"), CubeJob->GetOps()[0].RadiusVoxels, 0.f, 0.001f);

	// --- 再装配一次必须整体重置：旧 op / 旧块不能残留 ---
	FPlanetEditRequest Again = Sphere;
	Again.CenterVoxels = FVector(1000.0, 0.0, 0.0);
	if (TestTrue(TEXT("重新装配成功"), Job->SetupFromRequest(Again)))
	{
		TestEqual(TEXT("重新装配后 op 数回到 1"), Job->GetOpCount(), 1);
		TestTrue(TEXT("重新装配后包围盒换到了新位置"),
			Job->GetFullBounds().Min.X > ExpectedBounds.MaxExclusive.X);
		TestEqual(TEXT("重新装配后进度归零"), Job->GetNextChunkIndex(), 0);
	}

	return true;
}

namespace
{
	/** 合并测试共用的一套请求：球、半径 6 体素、4 体素的块（好切出多块）。 */
	FPlanetEditRequest MakeMergeTestRequest(const FVector& Center, const bool bPlace = false)
	{
		FPlanetEditRequest Request;
		Request.Shape = EPlanetBrushShape::Sphere;
		Request.CenterVoxels = Center;
		Request.RadiusVoxels = 6.f;
		Request.SizeVoxels = 10;
		Request.MinSizeVoxels = 1;
		Request.MaxSizeVoxels = 64;
		Request.bPlace = bPlace;
		Request.bMultiThreaded = false;
		Request.ChunkSizeVoxels = 4;
		return Request;
	}

	/**
	 * 方块版同一套请求：10 体素边长、4 体素的块。第二笔挪 2 体素后，新块里有整片
	 * 格子落在第一笔包围盒之外 —— 正是「合并后旧 op 越界写格子」的覆盖区。
	 */
	FPlanetEditRequest MakeMergeTestCubeRequest(const FVector& Center, const bool bPlace = false)
	{
		FPlanetEditRequest Request;
		Request.Shape = EPlanetBrushShape::Cube;
		Request.CenterVoxels = Center;
		Request.RadiusVoxels = 0.f;
		Request.SizeVoxels = 10;
		Request.MinSizeVoxels = 1;
		Request.MaxSizeVoxels = 64;
		Request.bPlace = bPlace;
		Request.bMultiThreaded = false;
		Request.ChunkSizeVoxels = 4;
		return Request;
	}

	/**
	 * 合并等价测试的合成初值：空 / 实 / 表面三种。哈希必须用 uint32 乘法
	 * （int32 溢出是 UB），按模 2³² 回绕才确定、跨平台一致。
	 */
	FVoxelValue MakeMergeSeed(const int32 X, const int32 Y, const int32 Z)
	{
		const uint32 Hash =
			static_cast<uint32>(X) * 73856093u
			^ static_cast<uint32>(Y) * 19349663u
			^ static_cast<uint32>(Z) * 83492791u;
		switch (Hash % 3)
		{
		case 0:  return FVoxelValue::Empty();
		case 1:  return FVoxelValue::Full();
		default: return FVoxelValue{ 0.25f };
		}
	}

	void TestBoxesEqual(
		FAutomationTestBase& Test, const TCHAR* What,
		const FPlanetEditVoxelBox& Actual, const FPlanetEditVoxelBox& Expected)
	{
		Test.TestTrue(What,
			Actual.Min == Expected.Min && Actual.MaxExclusive == Expected.MaxExclusive);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeRejectTest,
	"StellarEra.Planet.Editor.Merge.Rejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeRejectTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(MakeMergeTestRequest(FVector::ZeroVector))))
	{
		return false;
	}

	const int32 ChunksBefore = Job->GetChunkCount();
	const FPlanetEditVoxelBox FullBefore = Job->GetFullBounds();

	TestFalse(TEXT("不相交必须拒绝"),
		Job->TryMerge(MakeMergeTestRequest(FVector(500.0, 0.0, 0.0)), 8));

	{
		FPlanetEditRequest Box = MakeMergeTestRequest(FVector(1.0, 0.0, 0.0));
		Box.Shape = EPlanetBrushShape::Cube;
		Box.RadiusVoxels = 0.f;
		TestFalse(TEXT("形状不同必须拒绝"), Job->TryMerge(Box, 8));
	}

	{
		FPlanetEditRequest Bigger = MakeMergeTestRequest(FVector(1.0, 0.0, 0.0));
		Bigger.SizeVoxels = 12;
		TestFalse(TEXT("尺寸不同必须拒绝"), Job->TryMerge(Bigger, 8));
	}

	TestFalse(TEXT("挖填不同必须拒绝"),
		Job->TryMerge(MakeMergeTestRequest(FVector(1.0, 0.0, 0.0), /*bPlace=*/true), 8));

	// 5. op 数到上限：相交、不重复，但 MaxOps = 1 时已满。
	TestFalse(TEXT("op 数到上限必须拒绝"),
		Job->TryMerge(MakeMergeTestRequest(FVector(3.0, 0.0, 0.0)), /*MaxOps=*/1));

	// 一次拒绝都不能改状态。
	TestEqual(TEXT("拒绝后 op 数不变"), Job->GetOpCount(), 1);
	TestEqual(TEXT("拒绝后块数不变"), Job->GetChunkCount(), ChunksBefore);
	TestBoxesEqual(*this, TEXT("拒绝后包围盒不变"), Job->GetFullBounds(), FullBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeDedupeTest,
	"StellarEra.Planet.Editor.Merge.DedupesIdenticalOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeDedupeTest::RunTest(const FString& Parameters)
{
	const FPlanetEditRequest Request = MakeMergeTestRequest(FVector(2.0, 2.0, 2.0));

	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(Request)))
	{
		return false;
	}

	const int32 ChunksBefore = Job->GetChunkCount();
	const int32 OpsBefore = Job->GetOpCount();

	// 同一个落笔再来一遍：挖取 max、填取 min，重复是同值，返回 true 但不追加。
	TestTrue(TEXT("重复的落笔返回 true"), Job->TryMerge(Request, 8));
	TestEqual(TEXT("重复的落笔不追加 op"), Job->GetOpCount(), OpsBefore);
	TestEqual(TEXT("重复的落笔不追加块"), Job->GetChunkCount(), ChunksBefore);

	// 去重必须排在 op 上限之前：先判上限的话，同一片区域到达上限后会突然排新任务。
	TestTrue(TEXT("MaxOps=1 时重复落笔仍返回 true"), Job->TryMerge(Request, /*MaxOps=*/1));
	TestEqual(TEXT("MaxOps=1 时重复落笔不追加 op"), Job->GetOpCount(), 1);
	TestEqual(TEXT("MaxOps=1 时重复落笔不追加块"), Job->GetChunkCount(), ChunksBefore);

	for (int32 Index = 0; Index < 5; ++Index)
	{
		Job->TryMerge(Request, 8);
	}
	TestEqual(TEXT("连点 5 下仍然只有一个 op"), Job->GetOpCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeProgressTest,
	"StellarEra.Planet.Editor.Merge.PreservesProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeProgressTest::RunTest(const FString& Parameters)
{
	const FPlanetEditRequest First = MakeMergeTestRequest(FVector::ZeroVector);

	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(First)))
	{
		return false;
	}

	const int32 ChunksBefore = Job->GetChunkCount();
	// 前缀必须严格落在老块列表内部：游标为 0 时 Insert 与 Append 无法区分。
	if (!TestTrue(TEXT("老块数必须大于 3（否则进度下标挑不出来）"), ChunksBefore > 3))
	{
		return false;
	}

	// 模拟「已经做了三块」。ApplyNextChunk 没有行星时直接判完成，生产路径推不动
	// 游标，所以走测试专用的缝。
	Job->SetNextChunkIndexForTest(3);

	const int32 IndexBefore = Job->GetNextChunkIndex();
	const FPlanetEditVoxelBox AppliedBefore = Job->GetAppliedBounds();
	const FPlanetEditVoxelBox FullBefore = Job->GetFullBounds();

	// 已做前缀的块边界：合并后这三块必须原样待在 [0, 3)。
	FPlanetEditVoxelBox OldPrefix[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		OldPrefix[Index] = Job->GetChunks()[Index].Bounds;
	}

	const FPlanetEditRequest Second = MakeMergeTestRequest(FVector(4.0, 0.0, 0.0));
	TestTrue(TEXT("相交必须接受"), Job->TryMerge(Second, 8));

	// 下面要取 Ops()[1] 当期望值：op 数不对时先失败退出，别越界。
	if (!TestEqual(TEXT("op 数 +1"), Job->GetOpCount(), 2))
	{
		return false;
	}
	TestTrue(TEXT("块数增加"), Job->GetChunkCount() > ChunksBefore);

	// 进度一字未动：已做前缀的下标不变，AppliedBounds 不变。
	TestEqual(TEXT("已做前缀不变"), Job->GetNextChunkIndex(), IndexBefore);
	TestBoxesEqual(*this, TEXT("AppliedBounds 不变"), Job->GetAppliedBounds(), AppliedBefore);

	// 整笔范围是两支的并集。
	TestBoxesEqual(*this, TEXT("FullBounds == 两支包围盒的并集"), Job->GetFullBounds(),
		PlanetEditBrush::UnionVoxelBox(FullBefore, Job->GetOps()[1].Bounds));

	// --- 位置断言：新块必须插在已做前缀之后，而不是追加到末尾 ---
	//
	// 布局：[0,3) 老前缀 | [3,3+M) 新块 | [3+M,N+M) 老块尾巴。只按 NumOps 计数不够：
	// Append 也能对上，却会让已做前缀被整段重做。尾巴起点是 3+M 而非 N（新块可能比 N-3 还多）。
	const int32 NewChunkCount = Job->GetChunkCount() - ChunksBefore;
	const int32 OldTailStart = IndexBefore + NewChunkCount;
	const TArray<FPlanetEditChunk>& Chunks = Job->GetChunks();

	for (int32 Index = 0; Index < IndexBefore; ++Index)
	{
		TestEqual(*FString::Printf(
			TEXT("已做前缀区间 [0,%d)：块 %d 的 NumOps 必须是 1"), IndexBefore, Index),
			Chunks[Index].NumOps, 1);
		TestBoxesEqual(*this, *FString::Printf(
			TEXT("已做前缀区间 [0,%d)：块 %d 的边界必须原样保留"), IndexBefore, Index),
			Chunks[Index].Bounds, OldPrefix[Index]);
	}

	for (int32 Index = IndexBefore; Index < OldTailStart; ++Index)
	{
		TestEqual(*FString::Printf(
			TEXT("新块区间 [%d,%d)：块 %d 的 NumOps 必须是 2"),
			IndexBefore, OldTailStart, Index),
			Chunks[Index].NumOps, 2);
	}

	for (int32 Index = OldTailStart; Index < Job->GetChunkCount(); ++Index)
	{
		TestEqual(*FString::Printf(
			TEXT("老块尾巴区间 [%d,%d)：块 %d 的 NumOps 必须是 1"),
			OldTailStart, Job->GetChunkCount(), Index),
			Chunks[Index].NumOps, 1);
	}

	// 三段必须正好铺满全部块，且老块一个不多一个不少。
	TestEqual(TEXT("老块总数不变（前缀 + 尾巴）"),
		IndexBefore + (Job->GetChunkCount() - OldTailStart), ChunksBefore);

	return true;
}

// 合并的正确性判据：逐格全等。路径 1 是两次独立编辑依次执行；路径 2 是合并后
// 按运行期真实顺序逐块、逐格过 op。合并发生在一笔都没推过时，新块插到 0 号、
// 先于老块执行 —— 顺序反过来也必须同值，这是「挖取 max / 填取 min 可交换」的
// 要点。用合成网格是因为 AVoxelPlanetActor 需要带 Voxel LOD 管理器的 UWorld，
// 无头测试建不起来。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeEquivalenceTest,
	"StellarEra.Planet.Editor.Merge.ResultEqualsSequential",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeEquivalenceTest::RunTest(const FString& Parameters)
{
	// 三种初值铺满：向空与向实两个方向的边界都会被走到（MakeMergeSeed）。
	for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
	{
		const bool bPlace = PlaceIndex != 0;

		const FPlanetEditRequest First = MakeMergeTestRequest(FVector::ZeroVector, bPlace);
		const FPlanetEditRequest Second = MakeMergeTestRequest(FVector(6.0, 0.0, 0.0), bPlace);

		TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
		if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(First)))
		{
			return false;
		}
		if (!TestTrue(TEXT("合并成功"), Job->TryMerge(Second, 8)))
		{
			return false;
		}
		TestEqual(TEXT("合并后有两个 op"), Job->GetOpCount(), 2);

		const TArray<FPlanetEditOp>& Ops = Job->GetOps();
		const FPlanetEditVoxelBox Union =
			PlanetEditBrush::UnionVoxelBox(Ops[0].Bounds, Ops[1].Bounds);

		const FIntVector Min = Union.Min;
		const FIntVector Size(
			Union.MaxExclusive.X - Min.X,
			Union.MaxExclusive.Y - Min.Y,
			Union.MaxExclusive.Z - Min.Z);

		auto IndexOf = [&Min, &Size](const int32 X, const int32 Y, const int32 Z)
		{
			return (X - Min.X) + Size.X * ((Y - Min.Y) + Size.Y * (Z - Min.Z));
		};

		TArray<FVoxelValue> Initial;
		Initial.SetNum(Size.X * Size.Y * Size.Z);
		for (int32 X = Min.X; X < Union.MaxExclusive.X; ++X)
		{
			for (int32 Y = Min.Y; Y < Union.MaxExclusive.Y; ++Y)
			{
				for (int32 Z = Min.Z; Z < Union.MaxExclusive.Z; ++Z)
				{
					Initial[IndexOf(X, Y, Z)] = MakeMergeSeed(X, Y, Z);
				}
			}
		}

		// 路径 1：依次执行两次独立编辑（每笔只有自己那一个 op）。
		TArray<FVoxelValue> Sequential = Initial;
		for (const FPlanetEditOp& Op : Ops)
		{
			for (int32 X = Op.Bounds.Min.X; X < Op.Bounds.MaxExclusive.X; ++X)
			{
				for (int32 Y = Op.Bounds.Min.Y; Y < Op.Bounds.MaxExclusive.Y; ++Y)
				{
					for (int32 Z = Op.Bounds.Min.Z; Z < Op.Bounds.MaxExclusive.Z; ++Z)
					{
						const int32 Index = IndexOf(X, Y, Z);
						Sequential[Index] = PlanetProgressiveKernel::EvaluateOps(
							&Op, 1, X, Y, Z, Sequential[Index], bPlace);
					}
				}
			}
		}

		// 路径 2：合并后的块，按运行期顺序（从 NextChunkIndex 起）逐块应用。
		TArray<FVoxelValue> MergedValues = Initial;
		for (int32 ChunkIndex = Job->GetNextChunkIndex();
			ChunkIndex < Job->GetChunks().Num(); ++ChunkIndex)
		{
			const FPlanetEditChunk& Chunk = Job->GetChunks()[ChunkIndex];
			for (int32 X = Chunk.Bounds.Min.X; X < Chunk.Bounds.MaxExclusive.X; ++X)
			{
				for (int32 Y = Chunk.Bounds.Min.Y; Y < Chunk.Bounds.MaxExclusive.Y; ++Y)
				{
					for (int32 Z = Chunk.Bounds.Min.Z; Z < Chunk.Bounds.MaxExclusive.Z; ++Z)
					{
						const int32 Index = IndexOf(X, Y, Z);
						MergedValues[Index] = PlanetProgressiveKernel::EvaluateOps(
							Ops.GetData(), Chunk.NumOps, X, Y, Z, MergedValues[Index], bPlace);
					}
				}
			}
		}

		int32 Mismatch = 0;
		for (int32 Index = 0; Index < Sequential.Num(); ++Index)
		{
			if (Sequential[Index] != MergedValues[Index])
			{
				++Mismatch;
			}
		}
		TestEqual(
			bPlace ? TEXT("填：合并结果与依次独立执行逐格全等")
			       : TEXT("挖：合并结果与依次独立执行逐格全等"),
			Mismatch, 0);
	}

	return true;
}

// 方块版判据：合并后按运行期块序执行 == 依次独立执行，逐格全等。球对合并免疫，因为
// EvaluateSphereValue 对 (R+2)² 之外返回原值；EvaluateBoxValue 没有盒内判定，盒外格子
// 会被写成 Full/Empty。第二个方块沿 +X/+Y/−X 偏移，保证覆盖到盒外格子。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeCubeEquivalenceTest,
	"StellarEra.Planet.Editor.Merge.CubeResultEqualsSequential",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeCubeEquivalenceTest::RunTest(const FString& Parameters)
{
	// 六种组合 = 挖/填 × 三组偏移（+X、+Y、−X）。只守一根轴的守卫删掉就有人变红。
	static const FVector Offsets[] = {
		FVector(2.0, 0.0, 0.0),   // +X
		FVector(0.0, 2.0, 0.0),   // +Y
		FVector(-2.0, 0.0, 0.0)   // −X
	};
	static const TCHAR* const OffsetNames[] = { TEXT("+X"), TEXT("+Y"), TEXT("-X") };
	constexpr int32 NumOffsets = UE_ARRAY_COUNT(Offsets);

	for (int32 CaseIndex = 0; CaseIndex < 2 * NumOffsets; ++CaseIndex)
	{
		const bool bPlace = CaseIndex >= NumOffsets;
		const int32 OffsetIndex = CaseIndex % NumOffsets;
		const FString Label = FString::Printf(
			TEXT("%s/%s"), bPlace ? TEXT("方块填") : TEXT("方块挖"), OffsetNames[OffsetIndex]);

		// 两个相交但不相等的方块：第二个伸出第一个的包围盒，合并后新块里有一整片
		// 格子落在 op0 的盒外。
		const FPlanetEditRequest First = MakeMergeTestCubeRequest(FVector::ZeroVector, bPlace);
		const FPlanetEditRequest Second = MakeMergeTestCubeRequest(Offsets[OffsetIndex], bPlace);

		TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
		if (!TestTrue(Label + TEXT("：装配成功"), Job->SetupFromRequest(First)))
		{
			return false;
		}
		if (!TestTrue(Label + TEXT("：合并成功"), Job->TryMerge(Second, 8)))
		{
			return false;
		}
		TestEqual(Label + TEXT("：合并后有两个 op"), Job->GetOpCount(), 2);

		const TArray<FPlanetEditOp>& Ops = Job->GetOps();
		const FPlanetEditVoxelBox Union =
			PlanetEditBrush::UnionVoxelBox(Ops[0].Bounds, Ops[1].Bounds);

		// 夹具前提：第二个盒子必须真的伸出第一个，否则覆盖不到「盒外格子」。
		TestTrue(Label + TEXT("：第二个盒子必须伸出第一个盒子之外（否则测不到缺陷）"),
			Union.Min.X < Ops[0].Bounds.Min.X || Union.Min.Y < Ops[0].Bounds.Min.Y
			|| Union.Min.Z < Ops[0].Bounds.Min.Z
			|| Union.MaxExclusive.X > Ops[0].Bounds.MaxExclusive.X
			|| Union.MaxExclusive.Y > Ops[0].Bounds.MaxExclusive.Y
			|| Union.MaxExclusive.Z > Ops[0].Bounds.MaxExclusive.Z);

		const FIntVector Min = Union.Min;
		const FIntVector Size(
			Union.MaxExclusive.X - Min.X,
			Union.MaxExclusive.Y - Min.Y,
			Union.MaxExclusive.Z - Min.Z);

		auto IndexOf = [&Min, &Size](const int32 X, const int32 Y, const int32 Z)
		{
			return (X - Min.X) + Size.X * ((Y - Min.Y) + Size.Y * (Z - Min.Z));
		};

		TArray<FVoxelValue> Initial;
		Initial.SetNum(Size.X * Size.Y * Size.Z);
		for (int32 X = Min.X; X < Union.MaxExclusive.X; ++X)
		{
			for (int32 Y = Min.Y; Y < Union.MaxExclusive.Y; ++Y)
			{
				for (int32 Z = Min.Z; Z < Union.MaxExclusive.Z; ++Z)
				{
					Initial[IndexOf(X, Y, Z)] = MakeMergeSeed(X, Y, Z);
				}
			}
		}

		// 路径 1：依次独立执行两笔。每笔只在自己的包围盒内评估 —— 这就是钉住的语义。
		TArray<FVoxelValue> Sequential = Initial;
		for (const FPlanetEditOp& Op : Ops)
		{
			for (int32 X = Op.Bounds.Min.X; X < Op.Bounds.MaxExclusive.X; ++X)
			{
				for (int32 Y = Op.Bounds.Min.Y; Y < Op.Bounds.MaxExclusive.Y; ++Y)
				{
					for (int32 Z = Op.Bounds.Min.Z; Z < Op.Bounds.MaxExclusive.Z; ++Z)
					{
						const int32 Index = IndexOf(X, Y, Z);
						Sequential[Index] = PlanetProgressiveKernel::EvaluateOps(
							&Op, 1, X, Y, Z, Sequential[Index], bPlace);
					}
				}
			}
		}

		// 路径 2：合并后的块，按运行期顺序（从 NextChunkIndex 起）逐块应用。
		TArray<FVoxelValue> Merged = Initial;
		int32 CellsOutsideOp0WithBothOps = 0;
		for (int32 ChunkIndex = Job->GetNextChunkIndex();
			ChunkIndex < Job->GetChunks().Num(); ++ChunkIndex)
		{
			const FPlanetEditChunk& Chunk = Job->GetChunks()[ChunkIndex];
			for (int32 X = Chunk.Bounds.Min.X; X < Chunk.Bounds.MaxExclusive.X; ++X)
			{
				for (int32 Y = Chunk.Bounds.Min.Y; Y < Chunk.Bounds.MaxExclusive.Y; ++Y)
				{
					for (int32 Z = Chunk.Bounds.Min.Z; Z < Chunk.Bounds.MaxExclusive.Z; ++Z)
					{
						// 这一格是否在多 op 块里、且落在 op0 的盒外：缺陷只在这里显现。
						if (Chunk.NumOps > 1
							&& (X < Ops[0].Bounds.Min.X || X >= Ops[0].Bounds.MaxExclusive.X
								|| Y < Ops[0].Bounds.Min.Y || Y >= Ops[0].Bounds.MaxExclusive.Y
								|| Z < Ops[0].Bounds.Min.Z || Z >= Ops[0].Bounds.MaxExclusive.Z))
						{
							++CellsOutsideOp0WithBothOps;
						}

						const int32 Index = IndexOf(X, Y, Z);
						Merged[Index] = PlanetProgressiveKernel::EvaluateOps(
							Ops.GetData(), Chunk.NumOps, X, Y, Z, Merged[Index], bPlace);
					}
				}
			}
		}

		TestTrue(Label + TEXT("：新块必须覆盖 op0 盒外的格子（否则这条测试是空转）"),
			CellsOutsideOp0WithBothOps > 0);

		// 两条路径都必须真的改到东西：否则「两边都没动」也能通过比对。
		int32 Changed = 0;
		int32 Mismatch = 0;
		FString FirstMismatch;
		for (int32 X = Min.X; X < Union.MaxExclusive.X; ++X)
		{
			for (int32 Y = Min.Y; Y < Union.MaxExclusive.Y; ++Y)
			{
				for (int32 Z = Min.Z; Z < Union.MaxExclusive.Z; ++Z)
				{
					const int32 Index = IndexOf(X, Y, Z);
					if (Merged[Index] != Initial[Index])
					{
						++Changed;
					}
					if (Sequential[Index] != Merged[Index])
					{
						if (Mismatch == 0)
						{
							FirstMismatch = FString::Printf(
								TEXT("首处 (%d,%d,%d)：依次独立 %s(%d)，合并 %s(%d)"),
								X, Y, Z,
								*Sequential[Index].ToString(), int32(Sequential[Index].GetStorage()),
								*Merged[Index].ToString(), int32(Merged[Index].GetStorage()));
						}
						++Mismatch;
					}
				}
			}
		}

		TestTrue(FString::Printf(TEXT("%s：合并必须真的改到体素（改了 %d 格）"), *Label, Changed),
			Changed > 0);
		TestEqual(FString::Printf(
			TEXT("%s：合并结果与依次独立执行逐格全等（差异格数 %d，%s）"),
			*Label, Mismatch, *FirstMismatch), Mismatch, 0);
	}

	return true;
}

// 轮转游标三条：0 号队首永不进轮转；末尾回绕到 1 而非 0；越界夹回 1 而非取模
// （取模在队列动态变化时会跳笔）。任一条错都会让某笔饿着或被跳过。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveScheduleTest,
	"StellarEra.Planet.Editor.Schedule.FairCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveScheduleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("空队列没有候选"), PlanetEditSchedule::GetFairCandidateCount(0), 0);
	TestEqual(TEXT("单笔没有候选"), PlanetEditSchedule::GetFairCandidateCount(1), 0);
	TestEqual(TEXT("三笔有两个候选"), PlanetEditSchedule::GetFairCandidateCount(3), 2);

	TestEqual(TEXT("空队列取不到下标"),
		PlanetEditSchedule::GetNextFairIndex(INDEX_NONE, 0), INDEX_NONE);
	TestEqual(TEXT("单笔取不到下标"),
		PlanetEditSchedule::GetNextFairIndex(INDEX_NONE, 1), INDEX_NONE);

	// 首次从 1 开始：0 是队首，由阶段一负责。
	TestEqual(TEXT("游标未初始化时从 1 开始"),
		PlanetEditSchedule::GetNextFairIndex(INDEX_NONE, 3), 1);
	TestEqual(TEXT("1 之后是 2"), PlanetEditSchedule::GetNextFairIndex(1, 3), 2);
	TestEqual(TEXT("2 之后回绕到 1"), PlanetEditSchedule::GetNextFairIndex(2, 3), 1);
	TestEqual(TEXT("越界夹回 1"), PlanetEditSchedule::GetNextFairIndex(9, 3), 1);
	TestEqual(TEXT("两笔时 2 越界夹回 1"), PlanetEditSchedule::GetNextFairIndex(2, 2), 1);
	TestEqual(TEXT("游标 0 不进轮转"), PlanetEditSchedule::GetNextFairIndex(0, 3), 1);

	return true;
}

#endif
