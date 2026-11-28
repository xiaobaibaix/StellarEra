// 阶段 2 的构造框架：加权 Voronoi 板块 + 板块运动（欧拉极），边界类型由相对运动算出。大陆形状不在这里（见 GeoContinentField）。
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
	/** 角速度矢量（欧拉极 × 角速率）：任一点 p 的速度 = Omega × p，板块中心处大小 = Speed。 */
	FVector3f Omega = FVector3f::ZeroVector;
	float Speed = 0.f;
	FVector3f VelocityAt(const FVector3f& Dir) const { return FVector3f::CrossProduct(Omega, Dir); }
};

/** 边界两侧的相对运动：挤压 > 0 = 靠拢，< 0 = 分开；剪切 = 沿边界错动。单位同板块速度。 */
struct FGeoBoundaryMotion
{
	float Convergence = 0.f;
	float Shear = 0.f;
	/** 1 汇聚 / 4 离散 / 5 转换（汇聚的洋陆细分由调用方按大陆度判）。 */
	uint8 Type = 0;
};

struct FGeoPlateFrame
{
	TArray<FGeoPlate> Plates;
	uint64 WarpSeed = 0;

	int32 Num() const { return Plates.Num(); }
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

	/** Dir 处 A、B 两块板块的相对运动。法向取两板块中心连线在切平面上的投影（边界扭曲不改运动学）。 */
	FGeoBoundaryMotion BoundaryMotion(const FGeoPlateFrame& Frame, const FVector3f& Dir, int32 A, int32 B, const FPlanetPlateParams& P);
}
