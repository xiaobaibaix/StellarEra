// 阶段 2 的构造框架：加权 Voronoi 板块 + 板块对边界类型。大陆形状不在这里（见 GeoContinentField）。
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
};

struct FGeoPlateFrame
{
	TArray<FGeoPlate> Plates;
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

	/** 板块边界扰动：多尺度 fBm 扭采样方向。不扭时 Voronoi 边界是完美圆弧，海沟、山脉跟着变成画出来的线。 */
	FVector3f WarpDirection(const FGeoPlateFrame& Frame, const FVector3f& Dir, const FPlanetPlateParams& P);

	/** 加权距离 = 弦距 / 权重；输出最近与次近两块及到的加权距离（方向单位）。 */
	void NearestTwo(const FGeoPlateFrame& Frame, const FVector3f& Dir,
		int32& OutNear, int32& OutSecond, float& OutNearDist, float& OutSecondDist);
}
