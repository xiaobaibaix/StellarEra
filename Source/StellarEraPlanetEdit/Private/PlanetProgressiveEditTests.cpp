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

	// --- 3b. 块边长 MAX_int32 必须退化成恰好 1 块 ---
	// 同步路径（PlanetBrushApplicator）就是这么调的：一步做完，语义与
	// 「不分块」完全一致。这里同时防 `Min.X + MAX_int32` 的 int32 溢出 ——
	// 溢出会让循环失控、切出天文数字的块（或直接卡死）。
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

// ---------------------------------------------------------------------------
// 逐格等价：插件「一次写完整个包围盒」 vs 我们「按块写完整个包围盒」
// ---------------------------------------------------------------------------
//
// 上一组测试钉的是**几何前提**（块不重叠、并集 == 原盒）。这一组钉的是
// **结论**：满足那个前提时，我们分块写出来的体素值与插件原函数一次写完
// 完全相同。
//
// 做法上不写死期望值 —— 写死的话，公式改了我们跟着改数字，测试就成了
// 抄写。这里让**插件自己的** SphereEdit / BoxEdit 在假数据上跑一遍当基准，
// 再让我们内核按运行期的切块顺序跑一遍，逐格比对。基准永远是插件源码。

namespace
{
	/**
	 * 只为引擎测试存在的假数据：只实现插件工具用到的那个入口
	 * `template<typename... TArgs, typename F> Set(Bounds, Lambda)`。
	 * 有了它，FVoxelSphereToolsImpl::SphereEdit / FVoxelBoxToolsImpl::BoxEdit
	 * 不需要任何体素世界就能跑起来。
	 *
	 * 刻意**不**模拟 ParallelSet 的并行：并行与否不改变每格的结果，
	 * 这里要验的就是逐格取值。
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
		 * 确定性的种子：铺满**整个存储量程**，而不是清一色的空/实。
		 *
		 * 量程必须是真的量程。FVoxelValue 在本工程是 16 位
		 *（TVoxelValueImpl<int16>，`EIGHT_BITS_VOXEL_VALUE` 为 0），存储取
		 * [-32767, 32767]。一开始这里按 8 位写成了 ±127，结果球的 merge
		 * 路径（`Max(旧值, 新表面值)`）恒等于旧值 —— 半径 0.5 的球表面值
		 * 是 -4178，比任何 ±127 的种子都「实」，Max 永远取种子，于是一格
		 * 都没变，比对就变成了「两边都没动，所以一样」。
		 *
		 * 跳过 INVALID（-32768，即 FVoxelValue::Special，正常构造造不出来）。
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
	// 中心取非整数，半径取到「小于 2」（内圈阈值被夹到 0）与「远大于块」两种极端。
	{
		const FVoxelVector Centers[] = {
			FVoxelVector(0.0, 0.0, 0.0),
			FVoxelVector(12.4, -7.6, 3.5),
		};
		const float Radii[] = {0.5f, 1.f, 3.f, 8.f, 20.f};
		// 7 让每笔编辑切成上千块（块边界与格子、与球面全都不对齐），
		// 128 是运行期的默认块边长。
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

						// 留存未编辑的种子：稍后用来证明这一轮确实改到了东西，
						// 否则「两边一致」可能只是因为都没动过（比对空转）。
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

						// 按运行期那样一块一块写：块内逐格，整盒逐格同结果。
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
	// 盒子边长故意不是块边长的整数倍，块边界必然与盒子边界错开：
	// 若内核误用「当前块的盒子」判贴边，每个块的内壁都会被刷成表面，
	// 这一组会立刻炸。
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
	// 上面两组是端到端比对；这里单独钉住三段的边界条件，因为「>」与「<=」
	// 写反了在整数中心的球上可能表现不出来（格心恰好落在边界上时才有区别）。
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
//
// 这条钉的是重构的第一步：把「逐格求值」从 ApplyChunk 里抽出来时不改任何一格的结果。
// 位置用一个非整数的球心，好让 ±2 边界落在格子中间，边界两侧都被走到。
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

#endif
