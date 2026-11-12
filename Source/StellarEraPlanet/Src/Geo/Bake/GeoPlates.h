// 阶段 2 的构造框架：加权 Voronoi 板块 + 陆核铺点 + 板块对边界类型。
// 只在烘焙期存在；求值期不许重算 Voronoi（每点 7 次距离就是预算的 3 倍）。
#pragma once
#include "CoreMinimal.h"
#include "PlanetRecipeConfig.h"

enum class EGeoPlateKind : uint8 { Continental, Oceanic, Mixed };

struct FGeoPlate
{
	FVector3f Center = FVector3f::UpVector;
	float Weight = 1.f;
	EGeoPlateKind Kind = EGeoPlateKind::Oceanic;
	float StyleOffset = 0.f;   // 体素
	float StyleAmp = 0.f;      // 体素
	uint64 StyleSeed = 0;
};

struct FGeoContinentCore
{
	FVector3f Center = FVector3f::UpVector;
	float RadiusRatio = 0.f;   // 占 R，未乘 α；α 在阶段里二分
};

struct FGeoPlateFrame
{
	TArray<FGeoPlate> Plates;
	TArray<FGeoContinentCore> Cores;
	/** 上三角对类型：角标 A*N+B（A<B），值 1…5，见 02-plates.md 的 BoundaryType 字典。 */
	TArray<uint8> PairBoundaryType;
	uint64 WarpSeed = 0;

	int32 Num() const { return Plates.Num(); }
	uint8 BoundaryTypeBetween(int32 A, int32 B) const;
	uint64 Hash() const;
};

namespace GeoPlates
{
	/** 只用 Config.Seed 与 Plates 参数；连调两次必须逐位一致。 */
	FGeoPlateFrame Build(const FPlanetRecipeConfig& Config);

	/** 板块边界扰动：不加 warp 时 Voronoi 边界是完美圆弧，一眼假。 */
	FVector3f WarpDirection(const FGeoPlateFrame& Frame, const FVector3f& Dir, float WarpAmp);

	/** 加权距离 = 弦距 / 权重；输出最近与次近两块及到的加权距离（方向单位）。 */
	void NearestTwo(const FGeoPlateFrame& Frame, const FVector3f& Dir,
		int32& OutNear, int32& OutSecond, float& OutNearDist, float& OutSecondDist);
}
