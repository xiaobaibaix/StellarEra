// 程序化球形星球生成器 —— 插件适配层实现。
//
// 这一层只做转发：所有数学都在 FVoxelPlanetTerrain / FVoxelPlanetMaterializer 里。

#include "VoxelPlanetGenerator.h"
#include "VoxelPlanetTerrain.h"
#include "VoxelPlanetMaterializer.h"
#include "VoxelMaterialBuilder.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetGenerator::GetInstance()
{
	return MakeVoxelShared<FVoxelPlanetGeneratorInstance>(*this);
}

///////////////////////////////////////////////////////////////////////////////

FVoxelPlanetGeneratorInstance::FVoxelPlanetGeneratorInstance(UVoxelPlanetGenerator& MyGenerator)
	// 关键：把参数拷贝成 const 成员。
	// 之后 GetValueImpl 被任意线程并发调用时全程只读，无需加锁。
	: Super(&MyGenerator)
	, Radius(MyGenerator.Radius)
	, Center(MyGenerator.Center)
	, Caves(MyGenerator.Caves)
	, Shell(MyGenerator.Shell)
	, Sea(MyGenerator.Sea)
{
	Terrain = MakeVoxelShared<FVoxelPlanetTerrain>();
	Materializer = MakeVoxelShared<FVoxelPlanetMaterializer>();
}

void FVoxelPlanetGeneratorInstance::Init(const FVoxelGeneratorInit& InitStruct)
{
	const UVoxelPlanetGenerator* Generator = Cast<UVoxelPlanetGenerator>(Object.Get());
	if (!Generator)
	{
		return;
	}

	// 把地形配置交给纯计算类
	Terrain->Init(
		Generator->Continents,
		Generator->Mountains,
		Generator->Details,
		Generator->Ravines,
		Generator->Caves,
		Generator->Seed);

	Materializer->Init(
		Generator->Sea,
		Generator->Land,
		Generator->Shell,
		Generator->Continents);

	// 缓存区间推导所需的常量，避免在 GetValueRangeImpl 里重复计算
	MaxRadiusOffset = float(Terrain->GetMaxRadiusOffset());
	MaxCrustRadius = Radius + MaxRadiusOffset;
	// 深埋判定的余量：留两倍振幅，确保内部体素一定能被短路
	CrustPadding = 2.f * MaxRadiusOffset;
}

v_flt FVoxelPlanetGeneratorInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 采样点相对星球中心的偏移。加极小量避免密度恰好为 0（会让网格提取出错）。
	const double PX = double(X) - Center.X + 0.001;
	const double PY = double(Y) - Center.Y + 0.001;
	const double PZ = double(Z) - Center.Z + 0.001;

	const double DistanceSq = PX * PX + PY * PY + PZ * PZ;
	const double Distance = FMath::Sqrt(DistanceSq);
	if (Distance < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVoxelValue::Full();
	}

	//-----------------------------------------------------------------------
	// 这里是整个星球最关键的一行
	//-----------------------------------------------------------------------
	// 归一化到单位球面后再采样噪声。
	// 直接用世界坐标的话，噪声有效频率会随半径变化：
	//   · 两极出现涂抹状畸变
	//   · 赤道附近特征被横向拉伸
	// 归一化后地表能无缝包裹球体。
	const double InvDistance = 1.0 / Distance;
	const double NX = PX * InvDistance;
	const double NY = PY * InvDistance;
	const double NZ = PZ * InvDistance;

	// 四层地形数学全部委托给纯计算类
	const double SurfaceRadius = Radius + Terrain->GetRadiusOffset(NX, NY, NZ);

	// 基础距离场：地壳内为负，外部为正
	double Value = Distance - SurfaceRadius;

	// 空心壳层：只保留最外层厚度的地壳
	if (Shell.Thickness > 0.f)
	{
		const double InnerSdf = Distance - (Radius - Shell.Thickness);
		Value = FMath::Max(Value, -InnerSdf);
	}

	// 洞穴：脊状场扣除，DepthFade 让洞穴在地表附近淡出，防止打穿外壳
	if (Caves.Amount > 0.f)
	{
		const double CaveNoiseValue = Terrain->GetCaveField(NX, NY, NZ);
		const double Ridged = 1.0 - FMath::Abs(CaveNoiseValue);
		const double DepthFade = FMath::Clamp(
			(SurfaceRadius - Distance) / FMath::Max(float(MaxRadiusOffset), 1.f), 0.0, 1.0);
		const double CaveSdf = (Ridged - Caves.Amount) * Caves.Frequency * DepthFade;
		Value = FMath::Max(Value, -CaveSdf);
	}

	// 密度下游会被 clamp 到 [-1, 1]。除以常数是为了重新缩放梯度，
	// 而光照法线正是从梯度算出来的：
	//   · 除数太小 → 梯度饱和 → 渲染成死板的纯色
	//   · 除数太大 → 梯度被压扁 → 法线抖动、噪点
	// 5 是官方示例使用的值。
	Value /= 5.0;

	return v_flt(FMath::Clamp(Value, -1.0, 1.0));
}

FVoxelMaterial FVoxelPlanetGeneratorInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	const double PX = double(X) - Center.X + 0.001;
	const double PY = double(Y) - Center.Y + 0.001;
	const double PZ = double(Z) - Center.Z + 0.001;
	const double Distance = FMath::Sqrt(PX * PX + PY * PY + PZ * PZ);

	FVoxelMaterialBuilder Builder;
	Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);

	// 空心壳层的内核
	if (Shell.Thickness > 0.f && Distance < Radius - Shell.Thickness)
	{
		Builder.SetColor(Materializer->GetCoreColor());
		return Builder.Build();
	}

	// 深埋地壳快速返回。
	// 星球绝大多数被查询的体素都在内部，跳过噪声求值能省很多开销。
	if (Distance < double(MaxCrustRadius - CrustPadding))
	{
		Builder.SetColor(Materializer->GetSurfaceColor(Radius + Terrain->GetRadiusOffset(0, 0, 1), Radius));
		return Builder.Build();
	}

	const double InvDistance = Distance > UE_DOUBLE_SMALL_NUMBER ? 1.0 / Distance : 0.0;
	const double NX = PX * InvDistance;
	const double NY = PY * InvDistance;
	const double NZ = PZ * InvDistance;

	// 与 GetValueImpl 调用同一个函数，保证材质与地形起伏严格一致，
	// 不会出现「颜色画在山上但山不在那」的错位。
	const double SurfaceRadius = Radius + Terrain->GetRadiusOffset(NX, NY, NZ);

	Builder.SetColor(Materializer->GetSurfaceColor(SurfaceRadius, Radius));
	return Builder.Build();
}

TVoxelRange<v_flt> FVoxelPlanetGeneratorInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	//-----------------------------------------------------------------------
	// 球体唯一的性能命门
	//-----------------------------------------------------------------------
	// 半径 1000 的星球占据边长 2000 的立方体，其中 99% 以上是空的。
	// 返回精确区间后，插件能整块跳过 chunk，一个体素都不用碰。
	//
	// 契约：区间必须包含 GetValueImpl 在 Bounds 内可能产生的每一个值。
	// 算窄了 → 星球出现空洞。

	// 盒子到星球中心的最近距离：每轴 clamp 到>= 0 即得最近点
	const double MinX = FMath::Max(0.0, double(Bounds.Min.X) - Center.X);
	const double MinY = FMath::Max(0.0, double(Bounds.Min.Y) - Center.Y);
	const double MinZ = FMath::Max(0.0, double(Bounds.Min.Z) - Center.Z);

	const double MinDistance = FMath::Sqrt(MinX * MinX + MinY * MinY + MinZ * MinZ);

	const double MinValue = (MinDistance - (Radius + MaxRadiusOffset)) / 5.0;

	// 壳层与洞穴两项都包在 FMath::Max(Value, ...) 里，只会往上推，
	// 所以MinValue 依然是有效下界。上界无法低于 clamp 上限。
	//
	// 无法证明「整块都是实体」没有实际代价：星球内部本来就看不见，
	// 而上面那个下界已经跳过了所有空域 —— 那才是开销的大头。
	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(1.0));
}

FVector FVoxelPlanetGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// 星球上的「上」是背离球心的径向方向，不是世界 Z 轴。
	// 植被 Spawners 用它来定向草石。
	// 返回 FVector::UpVector 会让草石在两极躺平、赤道侧着长。
	const FVector P(double(X) - Center.X, double(Y) - Center.Y, double(Z) - Center.Z);
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
