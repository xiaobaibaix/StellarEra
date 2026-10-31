// Copyright Voxel Plugin SAS. All Rights Reserved.
// biome cubemap：按方向烘配色，供材质查表 —— 颜色因此与网格采样密度无关。

#include "PlanetBiomeCubemap.h"

#include "PlanetGeologyField.h"
#include "Engine/TextureCube.h"
#include "TextureResource.h"

/**
 * 面 → 主轴 / u 轴 / v 轴。与 D3D 的立方体贴图约定逐项对应（见头文件表格）。
 *
 * 写成表而不是 switch：烘焙与逆映射两处都要用同一张表，写两遍必然写岔一个符号，
 * 而符号错只会表现为「某两面颜色对调」，很难和配色本身的问题区分开。
 */
namespace
{
	struct FFaceBasis
	{
		FVector Major;
		FVector UAxis;
		FVector VAxis;
	};

	const FFaceBasis FaceBases[PlanetBiomeCubemap::NumFaces] =
	{
		{ FVector( 1.0,  0.0,  0.0), FVector(0.0, 0.0, -1.0), FVector(0.0, -1.0, 0.0) }, // +X
		{ FVector(-1.0,  0.0,  0.0), FVector(0.0, 0.0,  1.0), FVector(0.0, -1.0, 0.0) }, // -X
		{ FVector( 0.0,  1.0,  0.0), FVector(1.0, 0.0,  0.0), FVector(0.0,  0.0, 1.0) }, // +Y
		{ FVector( 0.0, -1.0,  0.0), FVector(1.0, 0.0,  0.0), FVector(0.0,  0.0,-1.0) }, // -Y
		{ FVector( 0.0,  0.0,  1.0), FVector(1.0, 0.0,  0.0), FVector(0.0, -1.0, 0.0) }, // +Z
		{ FVector( 0.0,  0.0, -1.0), FVector(-1.0, 0.0, 0.0), FVector(0.0, -1.0, 0.0) }, // -Z
	};
}

FVector PlanetBiomeCubemap::FaceUVToDirection(int32 Face, float U, float V)
{
	const int32 ClampedFace = FMath::Clamp(Face, 0, NumFaces - 1);
	const FFaceBasis& Basis = FaceBases[ClampedFace];

	// 归一化 UV → 面上的二维坐标：0 映射到 -1，1 映射到 +1。
	const double Sc = 2.0 * double(U) - 1.0;
	const double Tc = 2.0 * double(V) - 1.0;

	const FVector Direction = Basis.Major + Basis.UAxis * Sc + Basis.VAxis * Tc;
	return Direction.GetSafeNormal();
}

void PlanetBiomeCubemap::DirectionToFaceUV(
	const FVector& UnitDir, int32& OutFace, float& OutU, float& OutV)
{
	const FVector Dir = UnitDir.GetSafeNormal();
	const double AbsX = FMath::Abs(Dir.X);
	const double AbsY = FMath::Abs(Dir.Y);
	const double AbsZ = FMath::Abs(Dir.Z);

	// sc/tc 是「投影到本面二维坐标」，除以主轴分量的绝对值归一化到 [-1, 1]。
	double Sc = 0.0;
	double Tc = 0.0;
	if (AbsX >= AbsY && AbsX >= AbsZ)
	{
		OutFace = Dir.X >= 0.0 ? int32(EFace::PositiveX) : int32(EFace::NegativeX);
		const double Ma = FMath::Max(AbsX, UE_DOUBLE_SMALL_NUMBER);
		Sc = (Dir.X >= 0.0 ? -Dir.Z : Dir.Z) / Ma;
		Tc = -Dir.Y / Ma;
	}
	else if (AbsY >= AbsZ)
	{
		OutFace = Dir.Y >= 0.0 ? int32(EFace::PositiveY) : int32(EFace::NegativeY);
		const double Ma = FMath::Max(AbsY, UE_DOUBLE_SMALL_NUMBER);
		Sc = Dir.X / Ma;
		Tc = (Dir.Y >= 0.0 ? Dir.Z : -Dir.Z) / Ma;
	}
	else
	{
		OutFace = Dir.Z >= 0.0 ? int32(EFace::PositiveZ) : int32(EFace::NegativeZ);
		const double Ma = FMath::Max(AbsZ, UE_DOUBLE_SMALL_NUMBER);
		Sc = (Dir.Z >= 0.0 ? Dir.X : -Dir.X) / Ma;
		Tc = -Dir.Y / Ma;
	}

	OutU = float((Sc + 1.0) * 0.5);
	OutV = float((Tc + 1.0) * 0.5);
}

void PlanetBiomeCubemap::BakeFaceColors(
	const FPlanetNoiseParams& Params,
	const FPlanetGeologyField* Geology,
	double Radius,
	int32 Face,
	int32 Resolution,
	TArray<FColor>& OutColors)
{
	OutColors.Reset();
	if (Resolution <= 0)
	{
		return;
	}

	OutColors.SetNumUninitialized(Resolution * Resolution);

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		// 取像素中心：不然第一行/第一列会落在面的边界上，正反映射都不稳定。
		const float V = (float(Y) + 0.5f) / float(Resolution);
		for (int32 X = 0; X < Resolution; ++X)
		{
			const float U = (float(X) + 0.5f) / float(Resolution);
			const FVector Dir = FaceUVToDirection(Face, U, V);

			// 颜色函数的深度参数只喂给地质采样。这里取该方向的**真实地表半径**
			// （基准面 + 几何外凸），与生成器在地表采样点上算出的深度同阶 ——
			// 传 0 或传 Radius 都会让矿脉色整体偏一层。
			const double BaseSurface =
				Radius + double(Params.Amplitude) * double(ComputePlanetOffset(Params, Dir));
			double SurfaceRadius = BaseSurface;
			if (Geology)
			{
				// 与 GetSurfaceRadiusVoxels 同样的不动点迭代一次。
				SurfaceRadius += double(Geology->SampleGeometryDelta(Dir, Radius - BaseSurface));
			}

			const FLinearColor Color = ComputePlanetSurfaceColor(
				Params, Geology, Radius, Dir, SurfaceRadius);

			// 存 sRGB 编码值，贴图侧 SRGB=true 再解回来，两端落在同一个线性色上：
			// 顶点色走插件的 FVoxelUtilities::FloatToUINT8（线性量化，shader 直接
			// 当线性用），所以查表色必须也是**线性**结果，否则材质里 Lerp 的两端
			// 差一次 gamma，权重 0↔1 之间整颗星会亮暗一遍。
			OutColors[Y * Resolution + X] = Color.ToFColor(true);
		}
	}
}

UTextureCube* PlanetBiomeCubemap::BakeTexture(
	const FPlanetNoiseParams& Params,
	const FPlanetGeologyField* Geology,
	double Radius,
	int32 Resolution)
{
	if (Resolution <= 0)
	{
		return nullptr;
	}

	UTextureCube* Texture = UTextureCube::CreateTransient(
		Resolution, Resolution, PF_B8G8R8A8, NAME_None);
	if (!Texture || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlanetBiomeCubemap: 创建 %d 的 cubemap 失败"), Resolution);
		return nullptr;
	}

	// 数据是 ToFColor(true) 出来的 sRGB 编码值，所以贴图必须按 sRGB 采样把
	// 它解回线性 —— 顶点色那边（插件 FloatToUINT8）存的就是线性值，两端
	// 必须都落在线性上，否则整颗星会亮暗一遍。**不要**设 CompressionSettings：
	// 那只是给离线压缩用的提示，而瞬态贴图不压缩，设成 TC_VectorDisplacementmap
	// 反而有把 SRGB 关掉的风险。
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;

	// 直接写 Mips[0] 的 BulkData，不用 FTextureCubeResource::LockMip：
	// CreateTransient 之后还没有 RHI 资源，这里写完再 UpdateResource() 一次性建，
	// 少一次「建资源 → 锁 → 写 → 重建」的往返，也不需要 RHI 线程配合。
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	const int64 FaceBytes = int64(Resolution) * int64(Resolution) * sizeof(FColor);

	uint8* Dest = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (Dest == nullptr)
	{
		Mip.BulkData.Unlock();
		UE_LOG(LogTemp, Warning, TEXT("PlanetBiomeCubemap: cubemap 数据加锁失败"));
		return nullptr;
	}

	TArray<FColor> FaceColors;
	for (int32 Face = 0; Face < NumFaces; ++Face)
	{
		BakeFaceColors(Params, Geology, Radius, Face, Resolution, FaceColors);

		// 六个面在 BulkData 里按面序首尾相接（CreateTransient 的分配就是这个布局）。
		FMemory::Memcpy(Dest + int64(Face) * FaceBytes, FaceColors.GetData(), FaceBytes);
	}

	Mip.BulkData.Unlock();

	Texture->UpdateResource();
	return Texture;
}
