// 一颗星的地质配方资产：只放「地质怎么长」。半径归行星、体素尺寸归体素世界，这里不再存。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlanetRecipeConfig.h"
#include "PlanetRecipe.generated.h"

class UGeoMineralCatalog;
class UGeoMineralScatterer;

/** 单块板块的运动覆盖。板块编号在地质查看器「阶段 2 · 板块编号」图层里读。 */
USTRUCT(BlueprintType)
struct FGeoPlateOverride
{
	GENERATED_BODY()

	/** 要覆盖的板块编号（0 起）。 */
	UPROPERTY(EditAnywhere, Category = "Plate", meta = (DisplayName = "板块编号", ClampMin = "0", ClampMax = "14"))
	int32 PlateIndex = 0;

	/** 勾上才覆盖速度。 */
	UPROPERTY(EditAnywhere, Category = "Plate", meta = (DisplayName = "覆盖速度"))
	bool bOverrideSpeed = true;

	/** 板块速度（相对单位，可理解为 cm/年）。越快，与它相撞的边界山越窄越高、海沟越深。 */
	UPROPERTY(EditAnywhere, Category = "Plate", meta = (DisplayName = "板块速度", ClampMin = "0", ClampMax = "30", EditCondition = "bOverrideSpeed"))
	float Speed = 10.f;

	/** 勾上才覆盖方向。 */
	UPROPERTY(EditAnywhere, Category = "Plate", meta = (DisplayName = "覆盖方向"))
	bool bOverrideDirection = false;

	/** 运动方向：板块中心处的方位角，0 = 北，90 = 东（从星球外看顺时针）。 */
	UPROPERTY(EditAnywhere, Category = "Plate", meta = (DisplayName = "运动方向角", ClampMin = "0", ClampMax = "360", EditCondition = "bOverrideDirection"))
	float DirectionDeg = 0.f;
};

UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew)
class STELLARERACELESTIAL_API UPlanetRecipe : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 随机种子：换一个数就是另一套大陆布局与圈层起伏，其它参数含义不变。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (DisplayName = "随机种子"))
	int32 Seed = 1337;

	/** 烘焙贴图每个立方体面的分辨率。越大海岸越细、烘焙越慢；半径 40000 时 1024 一格约 60 体素。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (DisplayName = "贴图面分辨率", ClampMin = "32", ClampMax = "2048"))
	int32 FaceRes = 1024;

	// 阶段 1：圈层。下面五项是「从地表往下的累计深度 ÷ 半径」，必须一项比一项大。
	/** 本分类总开关：关掉后整类（含子阶段）都不参与烘焙。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）", meta = (DisplayName = "Enable")) bool bStage1SphereEnabled = true;
	/** 阶段 1（球体与圈层）总开关：关掉后后面阶段读不到圈层场，校验会报错。 */
	UPROPERTY() bool bRunStage1 = true;  // 旧阶段开关：面板已由分类总开关取代，仅保留序列化
	/** 地壳底界深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|圈层", meta = (DisplayName = "地壳底界深度比例", EditCondition = "bStage1SphereEnabled", ClampMin = "0.01", ClampMax = "0.99"))
	float CrustRatio = 0.25f;
	/** 上地幔底界深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|圈层", meta = (DisplayName = "上地幔底界深度比例", EditCondition = "bStage1SphereEnabled", ClampMin = "0.01", ClampMax = "0.99"))
	float UpperMantleRatio = 0.38f;
	/** 地幔过渡带底界深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|圈层", meta = (DisplayName = "过渡带底界深度比例", EditCondition = "bStage1SphereEnabled", ClampMin = "0.01", ClampMax = "0.99"))
	float TransitionRatio = 0.42f;
	/** 下地幔底界（核幔边界）深度占半径比例。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|圈层", meta = (DisplayName = "下地幔底界深度比例", EditCondition = "bStage1SphereEnabled", ClampMin = "0.01", ClampMax = "0.99"))
	float MantleOuterRatio = 0.55f;
	/** 外核底界深度占半径比例；再往里是内核。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|圈层", meta = (DisplayName = "外核底界深度比例", EditCondition = "bStage1SphereEnabled", ClampMin = "0.01", ClampMax = "0.99"))
	float OuterCoreRatio = 0.80f;

	/** 本段开关：关掉后莫霍面按正球面处理，圈层照常生成。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|莫霍面", meta = (EditCondition = "bStage1SphereEnabled", DisplayName = "Enable")) bool bRunStage1Moho = true;

	/** 莫霍面（地壳 / 地幔分界）起伏幅度占半径比例；0 = 正球面。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|莫霍面", meta = (DisplayName = "莫霍面起伏幅度", EditCondition = "bStage1SphereEnabled && bRunStage1Moho", ClampMin = "0", ClampMax = "0.1"))
	float MohoWobbleRatio = 0.01f;
	/** 莫霍面起伏的噪声层数：越多细节越碎。 */
	UPROPERTY(EditAnywhere, Category = "阶段1（球体与圈层）|莫霍面", meta = (DisplayName = "莫霍面噪声层数", EditCondition = "bStage1SphereEnabled && bRunStage1Moho", ClampMin = "1", ClampMax = "6"))
	int32 WobbleOctaves = 3;
	/** 本分类总开关：关掉后整类（含子阶段）都不参与烘焙。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）", meta = (DisplayName = "Enable")) bool bStage2PlatesEnabled = true;
	/** 阶段 2（板块与大陆）总开关。 */
	UPROPERTY() bool bRunStage2 = true;  // 旧阶段开关：面板已由分类总开关取代，仅保留序列化
	/** 板块数量（3–15）。只决定板块边界位置，给阶段 3 造山用；不影响大陆形状。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|板块", meta = (DisplayName = "板块数量", EditCondition = "bStage2PlatesEnabled", ClampMin = "3", ClampMax = "15"))
	int32 PlateCount = 7;

	/** 板块边界弯折程度：越大边界越曲折、凸角凹湾越多；0 = 规则圆弧。超过约 0.6 会把板块撕出碎块。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|板块", meta = (DisplayName = "板块边界弯折", EditCondition = "bStage2PlatesEnabled", ClampMin = "0", ClampMax = "1"))
	float BoundaryWarp = 0.5f;

	/** 板块边界小尺度锯齿：越大边界越毛糙。海沟、山脉都沿边界走，会跟着变。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|板块", meta = (DisplayName = "板块边界细节", EditCondition = "bStage2PlatesEnabled", ClampMin = "0", ClampMax = "0.2"))
	float BoundaryDetail = 0.08f;

	/** 板块速度下限（相对单位，可理解为 cm/年）。每块板块在上下限之间随机取一个速度、随机取方向。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|板块", meta = (DisplayName = "板块速度下限", EditCondition = "bStage2PlatesEnabled", ClampMin = "0", ClampMax = "30"))
	float PlateSpeedMin = 1.f;

	/** 板块速度上限。两块板块相对速度越大，碰撞带的山越窄越高。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|板块", meta = (DisplayName = "板块速度上限", EditCondition = "bStage2PlatesEnabled", ClampMin = "0", ClampMax = "30"))
	float PlateSpeedMax = 10.f;

	/** 转换断层判据：沿边界的错动超过挤压 / 拉开的这么多倍，就判为转换断层（几乎不长山）。越小转换断层越多。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|板块", meta = (DisplayName = "转换断层判据", EditCondition = "bStage2PlatesEnabled", ClampMin = "0.1", ClampMax = "10"))
	float TransformShearRatio = 1.5f;

	/** 单块板块覆盖：按编号单独指定速度 / 方向，用来刻意做出某条特别高（快撞）或特别宽（慢撞）的山脉。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|板块", meta = (DisplayName = "单块板块覆盖", EditCondition = "bStage2PlatesEnabled", TitleProperty = "PlateIndex"))
	TArray<FGeoPlateOverride> PlateOverrides;

	// 平铺而非 USTRUCT 镜像：USTRUCT 会把 CoreUObject 拖进 Geo/Bake，check_architecture 判层次违规。
	/** 大陆框架阶数：越大陆块越多越小。1–2 超大陆，4 类地球，6 以上配高海岸破碎度出群岛。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|大陆", meta = (DisplayName = "大陆框架阶数", EditCondition = "bStage2PlatesEnabled", ClampMin = "1", ClampMax = "12"))
	int32 FrameDegree = 4;

	/** 陆地占星球表面积的比例（0.3 = 三成陆地七成海）。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|大陆", meta = (DisplayName = "陆地占比", EditCondition = "bStage2PlatesEnabled", ClampMin = "0.05", ClampMax = "0.95"))
	float LandFrac = 0.30f;

	/** 海岸破碎度：越大半岛、海湾、近海小岛越多；0 = 只有平滑的大轮廓。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|大陆", meta = (DisplayName = "海岸破碎度", EditCondition = "bStage2PlatesEnabled", ClampMin = "0", ClampMax = "3"))
	float CoastRoughness = 0.35f;

	/** 陆架浅海占海洋面积的比例：越大离岸越远才变深。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|大陆", meta = (DisplayName = "陆架占比", EditCondition = "bStage2PlatesEnabled", ClampMin = "0", ClampMax = "0.9"))
	float ShelfFrac = 0.15f;

	/** 最高陆地高出海平面多少，占半径比例（R=40000 时 0.006 ≈ 240 体素）。星球变大变小时形状不变。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|大陆", meta = (DisplayName = "陆地最高海拔", EditCondition = "bStage2PlatesEnabled", ClampMin = "0", ClampMax = "0.1"))
	float LandHeightRatio = 0.006f;

	/** 最深洋盆低于海平面多少，占半径比例（R=40000 时 0.012 ≈ 480 体素）。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|大陆", meta = (DisplayName = "洋盆最大深度", EditCondition = "bStage2PlatesEnabled", ClampMin = "0.0001", ClampMax = "0.1"))
	float OceanDepthRatio = 0.012f;
	/** 阶段 2 的撒矿（条带状铁矿 / 块状硫化物 / 锰结核）。 */
	UPROPERTY(EditAnywhere, Category = "阶段2（板块与大陆）|大陆|撒矿", meta = (DisplayName = "启用撒矿", EditCondition = "bStage2PlatesEnabled")) bool bScatterStage2 = true;
	/** 本阶段的撒矿器：增删、排序只影响本阶段。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "阶段2（板块与大陆）|大陆|撒矿", meta = (DisplayName = "本阶段撒矿器", EditCondition = "bStage2PlatesEnabled"))
	TArray<TObjectPtr<UGeoMineralScatterer>> ScatterStage2Minerals;

	// 阶段 3：宏观地形。全部占半径比例，括号里是 R=40000 时的体素数。
	/** 本分类总开关：关掉后整类（含子阶段）都不参与烘焙。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）", meta = (DisplayName = "Enable")) bool bStage3TerrainEnabled = true;
	/** 阶段 3（宏观地形）总开关。 */
	UPROPERTY() bool bRunStage3 = true;  // 旧阶段开关：面板已由分类总开关取代，仅保留序列化
	/** 山脉整体开关：关掉等于山脉起伏幅度 = 0，山带仍参与哈希与其它子阶段。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|山脉", meta = (DisplayName = "启用", EditCondition = "bStage3TerrainEnabled"))
	bool bMountains = true;

	/** 山脉起伏幅度：山顶 ≈ 陆台 + 0.8 × 它（0.05 ≈ 2000 体素）。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|山脉", meta = (DisplayName = "山脉起伏幅度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.2"))
	float MountainHeightRatio = 0.05f;

	/** 海沟整体开关：关掉等于海沟深度 = 0（沉积充填 / 坍塌 / 增生楔跟着一起没了）。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|海沟", meta = (DisplayName = "启用", EditCondition = "bStage3TerrainEnabled"))
	bool bTrenches = true;

	/** 海沟在洋底之下再往下切多深（0.025 ≈ 1000 体素）。只在俯冲边界的洋侧出现。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|海沟", meta = (DisplayName = "海沟在洋底之下再往下切多深", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.2"))
	float TrenchDepthRatio = 0.025f;

	/** 平原整体开关：关掉等于平原起伏 = 0。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|平原", meta = (DisplayName = "启用", EditCondition = "bStage3TerrainEnabled"))
	bool bPlains = true;

	/** 山带以外的平原丘陵起伏（0.002 ≈ 80 体素）。0 = 平原是平的。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|平原", meta = (DisplayName = "山带以外的平原丘陵起伏", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.05"))
	float PlainReliefRatio = 0.002f;

	/** 造山带宽度（0.06 ≈ 2400 体素）。越大山脉越宽、覆盖面积越大。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|山脉", meta = (DisplayName = "造山带宽度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0.002", ClampMax = "0.3"))
	float BeltWidthRatio = 0.06f;

	/** 山脊间距（0.06 ≈ 2400 体素）。越小山脊越密越碎。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|山脉", meta = (DisplayName = "山脊间距", EditCondition = "bStage3TerrainEnabled", ClampMin = "0.005", ClampMax = "0.5"))
	float RidgeWavelengthRatio = 0.06f;

	/** 侵蚀整体开关：关掉等于侵蚀遍数 = 0。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|侵蚀", meta = (DisplayName = "启用", EditCondition = "bStage3TerrainEnabled"))
	bool bErosion = true;

	/** 热力侵蚀遍数：越多山越圆、坡越缓。0 = 不侵蚀。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|侵蚀", meta = (DisplayName = "热力侵蚀遍数", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "50"))
	int32 ErosionIterations = 5;

	/** 侵蚀强度：每遍陡坡向周围平均高度靠拢的比例。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|侵蚀", meta = (DisplayName = "侵蚀强度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float ErosionStrength = 0.2f;

	/** 休止角（度）：比它陡的坡才会被侵蚀削平。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|侵蚀", meta = (DisplayName = "休止角", EditCondition = "bStage3TerrainEnabled", ClampMin = "5", ClampMax = "80"))
	float TalusAngleDeg = 30.f;

	/** 海沟沉积充填：离大陆越近泥沙越多、海沟越被填浅。0 = 不填，1 = 近岸海沟几乎填平成浅洼地；远洋段始终保持深。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|海沟", meta = (DisplayName = "海沟沉积充填", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float TrenchSedimentFill = 0.7f;

	/** 海沟坍塌：两壁滑塌、被削缓，横断面从 V 形变成宽浅的槽。0 = 不塌，1 = 最缓。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|海沟", meta = (DisplayName = "海沟坍塌", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float TrenchSlump = 0.5f;

	/** 增生楔：海沟靠陆一侧被刮积起来的低缓隆起，始终在海面以下。0 = 没有。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|海沟", meta = (DisplayName = "增生楔", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float TrenchWedge = 0.5f;

	/** 挤压速度对山带宽度的影响：快撞 → 窄而密，慢撞 → 宽而缓（整体抬升）。0 = 所有山带一样宽。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|山脉", meta = (DisplayName = "挤压速度对山带宽度的影响", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.9"))
	float SpeedWidthEffect = 0.6f;

	/** 挤压速度对山高与海沟深的影响：快撞 → 高、海沟深，慢撞 → 低、海沟浅。0 = 一样高。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|山脉", meta = (DisplayName = "挤压速度对山高与海沟深的影响", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.9"))
	float SpeedHeightEffect = 0.6f;

	/** 洋中脊与裂谷整体开关：关掉等于洋中脊高度、大陆裂谷深度都 = 0。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|洋中脊与裂谷", meta = (DisplayName = "启用", EditCondition = "bStage3TerrainEnabled"))
	bool bRidgeRift = true;

	/** 洋中脊高度：离散边界（板块分开）在海里形成的宽缓海底山脉，正中有一道轴部裂谷（0.006 ≈ 240 体素）。始终在水下。0 = 没有。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|洋中脊与裂谷", meta = (DisplayName = "洋中脊高度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.05"))
	float MidOceanRidgeHeightRatio = 0.006f;

	/** 洋中脊宽度：越大海底隆起越宽越缓。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|洋中脊与裂谷", meta = (DisplayName = "洋中脊宽度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0.005", ClampMax = "0.3"))
	float MidOceanRidgeWidthRatio = 0.08f;

	/** 大陆裂谷深度：离散边界在陆上撕开的地堑，两侧肩部抬起（0.004 ≈ 160 体素）。谷底不会低于海平面。0 = 没有。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|洋中脊与裂谷", meta = (DisplayName = "大陆裂谷深度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.05"))
	float RiftDepthRatio = 0.004f;

	/** 大陆裂谷宽度。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|洋中脊与裂谷", meta = (DisplayName = "大陆裂谷宽度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0.002", ClampMax = "0.1"))
	float RiftWidthRatio = 0.015f;

	/** 古老造山带整体开关：关掉等于古老造山带高度 = 0。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "启用", EditCondition = "bStage3TerrainEnabled"))
	bool bOldOrogen = true;

	/** 古老造山带高度：古老大陆内部被长期剥蚀的旧山脉，低矮圆缓（0.008 ≈ 320 体素）。0 = 没有。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "古老造山带高度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "0.05"))
	float OldOrogenHeightRatio = 0.008f;

	/** 古老造山带主脊疏密：越大主脊越多、间距越小。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "古老造山带主脊疏密", EditCondition = "bStage3TerrainEnabled", ClampMin = "0.5", ClampMax = "12"))
	float OldOrogenDensity = 3.f;

	/** 古老造山带辐射范围：主脊到山脚的距离（0.045 ≈ 现今山带半宽的 1.5 倍）。越大山体越宽越缓。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "古老造山带辐射范围", EditCondition = "bStage3TerrainEnabled", ClampMin = "0.005", ClampMax = "0.2"))
	float OldOrogenWidthRatio = 0.045f;

	/** 支脉强度：山坡上垂直主脊伸出的支脉有多明显。0 = 只有主脊和平滑山坡。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "支脉强度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float OldOrogenSpurStrength = 0.6f;

	/** 覆盖度：古老山系保留多少条。0 = 没有，1 = 所有主脊都长山。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "覆盖度", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float OldOrogenCoverage = 0.4f;

	/** 断接比例：沿走向有多少段断开或压成垭口。0 = 连续不断，1 = 大部分断开成孤立山块。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "断接比例", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float OldOrogenGapFraction = 0.3f;

	/** 宽窄收放：同一条山系沿走向变宽变窄的幅度。0 = 等宽，1 = 最窄处只剩三成。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|古老造山带", meta = (DisplayName = "宽窄收放", EditCondition = "bStage3TerrainEnabled", ClampMin = "0", ClampMax = "1"))
	float OldOrogenWidthVariation = 0.5f;
	/** 阶段 3 的撒矿。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|宏观地形|撒矿", meta = (DisplayName = "启用撒矿", EditCondition = "bStage3TerrainEnabled")) bool bScatterStage3 = true;
	/** 本阶段的撒矿器：增删、排序只影响本阶段。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "阶段3（地形与构造）|宏观地形|撒矿", meta = (DisplayName = "本阶段撒矿器", EditCondition = "bStage3TerrainEnabled"))
	TArray<TObjectPtr<UGeoMineralScatterer>> ScatterStage3Minerals;

	// 阶段 4：火山。尺度同样占半径比例。
	/** 本分类总开关：关掉后整类（含子阶段）都不参与烘焙。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）", meta = (DisplayName = "Enable")) bool bStage4MagmaEnabled = true;
	/** 阶段 4（岩浆：火山 + 岩体）总开关。 */
	UPROPERTY() bool bRunStage4 = true;  // 旧阶段开关：面板已由分类总开关取代，仅保留序列化
	/** 本段开关：关掉后不放火山；岩墙、层状、岩管照常，但依赖弧火山/溢流玄武岩的岩基、岩株、岩床会一起消失。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山", meta = (DisplayName = "启用火山", EditCondition = "bStage4MagmaEnabled")) bool bRunStage4Volcano = true;

	/** 俯冲弧火山：大洋板块俯冲到另一板块下方时，上盘一侧排成一串的陡峭层状火山。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|弧火山", meta = (DisplayName = "俯冲弧火山", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano"))
	bool bArcVolcanoes = true;

	/** 热点火山链：地幔柱在板块下方不动，板块漂过时留下一串越来越老、越来越矮的盾状火山。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|热点与盾状火山", meta = (DisplayName = "热点火山", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano"))
	bool bHotspots = true;

	/** 溢流玄武岩：大陆裂谷处大面积喷出的熔岩，形成平顶、边缘陡坎的台地。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|溢流玄武岩", meta = (DisplayName = "溢流玄武岩", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano"))
	bool bFloodBasalt = true;

	/** 破火山口：部分大火山顶部塌陷，变成环形洼地加外环壁。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|破火山口", meta = (DisplayName = "破火山口", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano"))
	bool bCalderas = true;

	/** 火山渣锥：活火山周围成簇的小锥。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|火山渣锥", meta = (DisplayName = "火山渣锥", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano"))
	bool bCinderCones = true;

	/** 层状火山底半径（占半径，0.024 ≈ 960 体素）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|层状火山", meta = (DisplayName = "层状火山底半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0.002", ClampMax = "0.2"))
	float StratoBaseRatio = 0.024f;

	/** 层状火山高（占半径，0.012 ≈ 480 体素）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|层状火山", meta = (DisplayName = "层状火山高", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.1"))
	float StratoHeightRatio = 0.012f;

	/** 层状火山火口半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|层状火山", meta = (DisplayName = "层状火山火口半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.05"))
	float StratoCraterRatio = 0.004f;

	/** 火口深（占半径，0.002 ≈ 80 体素）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|层状火山", meta = (DisplayName = "火口深", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.05"))
	float CraterDepthRatio = 0.002f;

	/** 沿火山弧相邻两座火山的间距（占半径）。越小火山越密。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|弧火山", meta = (DisplayName = "沿火山弧相邻两座火山的间距", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0.005", ClampMax = "0.5"))
	float ArcSpacingRatio = 0.04f;

	/** 火山弧离板块边界的距离（占半径），总在上盘一侧。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|弧火山", meta = (DisplayName = "火山弧离板块边界的距离", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.2"))
	float ArcOffsetRatio = 0.03f;

	/** 弧火山中活火山的比例：挤压越快的段越容易活跃。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|弧火山", meta = (DisplayName = "弧火山中活火山的比例", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "1"))
	float ArcActiveFraction = 0.5f;

	/** 盾状火山底半径（占半径），宽而缓。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|热点与盾状火山", meta = (DisplayName = "盾状火山底半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0.002", ClampMax = "0.3"))
	float ShieldBaseRatio = 0.04f;

	/** 盾状火山高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|热点与盾状火山", meta = (DisplayName = "盾状火山高", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.1"))
	float ShieldHeightRatio = 0.008f;

	/** 地幔柱热点个数，每个热点一条火山链。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|热点与盾状火山", meta = (DisplayName = "地幔柱热点个数，每个热点一条火山链", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "32"))
	int32 HotspotCount = 4;

	/** 热点链上相邻火山的间距（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|热点与盾状火山", meta = (DisplayName = "热点链上相邻火山的间距", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0.005", ClampMax = "0.5"))
	float HotspotSpacingRatio = 0.06f;

	/** 每条热点链最多几座火山；漂进别的板块会提前截断。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|热点与盾状火山", meta = (DisplayName = "每条热点链最多几座火山", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "1", ClampMax = "30"))
	int32 HotspotChainLength = 6;

	/** 每段大陆裂谷出现溢流玄武岩台地的概率。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|溢流玄武岩", meta = (DisplayName = "每段大陆裂谷出现溢流玄武岩台地的概率", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "1"))
	float FloodBasaltChance = 0.3f;

	/** 溢流玄武岩台地半宽（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|溢流玄武岩", meta = (DisplayName = "溢流玄武岩台地半宽", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0.005", ClampMax = "0.5"))
	float FloodBasaltSizeRatio = 0.06f;

	/** 溢流玄武岩台地高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|溢流玄武岩", meta = (DisplayName = "溢流玄武岩台地高", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.05"))
	float FloodBasaltHeightRatio = 0.003f;

	/** 大火山塌成破火山口的比例（只在底半径大于中位数的火山里抽）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|破火山口", meta = (DisplayName = "大火山塌成破火山口的比例", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.5"))
	float CalderaFraction = 0.15f;

	/** 破火山口半径（占半径），最多到火山底半径的六成。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|破火山口", meta = (DisplayName = "破火山口半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0.002", ClampMax = "0.2"))
	float CalderaRadiusRatio = 0.016f;

	/** 破火山口深（占半径）。陆上不会挖到海平面以下。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|破火山口", meta = (DisplayName = "破火山口深", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.05"))
	float CalderaDepthRatio = 0.008f;

	/** 每座活火山周围的火山渣锥个数。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|火山渣锥", meta = (DisplayName = "每座活火山周围的火山渣锥个数", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "20"))
	int32 CinderPerVolcano = 4;

	/** 火山渣锥底半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|火山渣锥", meta = (DisplayName = "火山渣锥底半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0.001", ClampMax = "0.05"))
	float CinderBaseRatio = 0.006f;

	/** 火山渣锥高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|火山|火山渣锥", meta = (DisplayName = "火山渣锥高", EditCondition = "bStage4MagmaEnabled && bRunStage4Volcano", ClampMin = "0", ClampMax = "0.05"))
	float CinderHeightRatio = 0.002f;

	// 阶段 4b：地下侵入体。字段与 FPlanetIntrusionParams 一一对应，尺度同样占半径比例。
	/** 本段开关：关掉后不放岩体与蚀变晕，火山照常生成。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入", meta = (DisplayName = "启用侵入", EditCondition = "bStage4MagmaEnabled")) bool bRunStage4Intrusion = true;

	/** 岩基：弧火山段下方的大花岗岩体。没有弧火山就没有岩基。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩基", meta = (DisplayName = "岩基", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion && bRunStage4Volcano && bArcVolcanoes"))
	bool bBatholiths = true;

	/** 岩株：每座弧火山正下方的小侵入体（4c 斑岩铜宿主）。没有弧火山就没有岩株。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩株", meta = (DisplayName = "岩株", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion && bRunStage4Volcano && bArcVolcanoes"))
	bool bStocks = true;

	/** 岩墙：裂谷处近垂直的薄板。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩墙", meta = (DisplayName = "岩墙", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion"))
	bool bDikes = true;

	/** 岩床：溢流玄武岩台地与克拉通下的近水平薄板。台地那部分需要溢流玄武岩。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩床", meta = (DisplayName = "岩床", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion && bRunStage4Volcano && bFloodBasalt"))
	bool bSills = true;

	/** 层状侵入体：古老克拉通的叠层（4c 铬铂宿主）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|层状侵入体", meta = (DisplayName = "层状侵入体", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion"))
	bool bLayeredIntrusions = true;

	/** 金伯利岩管（4c 金刚石宿主）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|金伯利岩管", meta = (DisplayName = "金伯利岩管", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion"))
	bool bKimberlites = true;

	/** 岩体周围的蚀变晕（只写材质线索）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|蚀变晕", meta = (DisplayName = "蚀变晕", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion"))
	bool bAlteration = true;

	/** 岩基长半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩基", meta = (DisplayName = "岩基长半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "0.5"))
	float BatholithLongRatio = 0.08f;

	/** 岩基短半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩基", meta = (DisplayName = "岩基短半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "0.5"))
	float BatholithShortRatio = 0.04f;

	/** 岩基竖直半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩基", meta = (DisplayName = "岩基竖直半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "0.5"))
	float BatholithThickRatio = 0.03f;

	/** 岩基顶深（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩基", meta = (DisplayName = "岩基顶深", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "0.5"))
	float BatholithTopDepthRatio = 0.05f;

	/** 段内至少几座弧火山才长岩基。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩基", meta = (DisplayName = "段内至少几座弧火山才长岩基", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "1", ClampMax = "32"))
	int32 BatholithMinVolcanoes = 3;

	/** 岩株半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩株", meta = (DisplayName = "岩株半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.0005", ClampMax = "0.1"))
	float StockRadiusRatio = 0.008f;

	/** 岩株顶深（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩株", meta = (DisplayName = "岩株顶深", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "0.3"))
	float StockTopDepthRatio = 0.03f;

	/** 每条裂谷的岩墙数。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩墙", meta = (DisplayName = "每条裂谷的岩墙数", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "32"))
	int32 DikePerRift = 2;

	/** 岩墙厚（半宽，占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩墙", meta = (DisplayName = "岩墙厚", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.0002", ClampMax = "0.05"))
	float DikeThicknessRatio = 0.002f;

	/** 岩墙半高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩墙", meta = (DisplayName = "岩墙半高", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "0.2"))
	float DikeLengthRatio = 0.02f;

	/** 岩墙顶深（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩墙", meta = (DisplayName = "岩墙顶深", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "0.3"))
	float DikeTopDepthRatio = 0.02f;

	/** 岩床半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩床", meta = (DisplayName = "岩床半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "0.3"))
	float SillRadiusRatio = 0.05f;

	/** 岩床半厚（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩床", meta = (DisplayName = "岩床半厚", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.0002", ClampMax = "0.05"))
	float SillThicknessRatio = 0.002f;

	/** 岩床顶深（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩床", meta = (DisplayName = "岩床顶深", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "0.3"))
	float SillTopDepthRatio = 0.02f;

	/** 克拉通内岩床撒布密度。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|岩床", meta = (DisplayName = "克拉通内岩床撒布密度", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "1"))
	float CratonSillDensity = 0.4f;

	/** 层状侵入体每组的层数。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|层状侵入体", meta = (DisplayName = "层状侵入体每组的层数", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "1", ClampMax = "20"))
	int32 LayeredCount = 4;

	/** 单层半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|层状侵入体", meta = (DisplayName = "单层半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "0.2"))
	float LayeredRadiusRatio = 0.03f;

	/** 单层半厚（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|层状侵入体", meta = (DisplayName = "单层半厚", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.0002", ClampMax = "0.05"))
	float LayeredThicknessRatio = 0.0025f;

	/** 层间距（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|层状侵入体", meta = (DisplayName = "层间距", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.0002", ClampMax = "0.05"))
	float LayeredGapRatio = 0.004f;

	/** 整组顶深（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|层状侵入体", meta = (DisplayName = "整组顶深", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "0.3"))
	float LayeredTopDepthRatio = 0.06f;

	/** 岩管半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|金伯利岩管", meta = (DisplayName = "岩管半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.0002", ClampMax = "0.05"))
	float KimberliteRadiusRatio = 0.002f;

	/** 岩管半高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|金伯利岩管", meta = (DisplayName = "岩管半高", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "0.1"))
	float KimberliteHalfHeightRatio = 0.01f;

	/** 岩管顶深（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|金伯利岩管", meta = (DisplayName = "岩管顶深", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "0.3"))
	float KimberliteTopDepthRatio = 0.004f;

	/** 全局最多几根岩管。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|金伯利岩管", meta = (DisplayName = "全局最多几根岩管", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "64"))
	int32 KimberliteMax = 8;

	/** 大陆裂谷的碳酸岩体（4c 稀土宿主）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|碳酸岩", meta = (DisplayName = "大陆裂谷的碳酸岩体", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion"))
	bool bCarbonatites = true;

	/** 碳酸岩半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|碳酸岩", meta = (DisplayName = "碳酸岩半径", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.0005", ClampMax = "0.1"))
	float CarbonatiteRadiusRatio = 0.006f;

	/** 碳酸岩顶深（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|碳酸岩", meta = (DisplayName = "碳酸岩顶深", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "0.3"))
	float CarbonatiteTopDepthRatio = 0.01f;

	/** 每段裂谷出现碳酸岩的概率。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|碳酸岩", meta = (DisplayName = "每段裂谷出现碳酸岩的概率", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "1"))
	float CarbonatiteChance = 0.35f;

	/** 候选去重间距（弦长，单位球）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|碳酸岩", meta = (DisplayName = "候选去重间距", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0.001", ClampMax = "1"))
	float CarbonatiteSpacingRatio = 0.12f;

	/** 岩株蚀变晕半径倍数（岩基固定 1.25）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|蚀变晕", meta = (DisplayName = "岩株蚀变晕半径倍数", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "1", ClampMax = "8"))
	float AlterHaloScale = 2.2f;

	/** 钾化外边界（归一化距离）。三项必须一项比一项大，否则中间那圈消失。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|蚀变晕", meta = (DisplayName = "钾化外边界", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "1"))
	float AlterKeratiticBand = 0.3f;

	/** 绢英岩化外边界（归一化距离）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|蚀变晕", meta = (DisplayName = "绢英岩化外边界", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "1"))
	float AlterSericiticBand = 0.55f;

	/** 泥化外边界（归一化距离）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|蚀变晕", meta = (DisplayName = "泥化外边界", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion", ClampMin = "0", ClampMax = "1"))
	float AlterArgillicBand = 0.8f;
	/** 阶段 4 的撒矿（4c-1 / 4c-2 原生矿）。 */
	UPROPERTY(EditAnywhere, Category = "阶段4（火山与侵入）|侵入|撒矿", meta = (DisplayName = "启用撒矿", EditCondition = "bStage4MagmaEnabled && bRunStage4Intrusion")) bool bScatterStage4 = true;
	/** 本阶段的撒矿器：增删、排序只影响本阶段。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "阶段4（火山与侵入）|侵入|撒矿", meta = (DisplayName = "本阶段撒矿器", EditCondition = "bStage4MagmaEnabled"))
	TArray<TObjectPtr<UGeoMineralScatterer>> ScatterStage4Minerals;

	// 阶段 5：气候。温度、降水都是 0–1 归一化（温度不是摄氏度），全部进 Hash()。
	/** 本分类总开关：关掉后整类（含子阶段）都不参与烘焙。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）", meta = (DisplayName = "Enable")) bool bStage5ClimateEnabled = true;
	/** 阶段 5（气候）总开关。 */
	UPROPERTY() bool bRunStage5 = true;  // 旧阶段开关：面板已由分类总开关取代，仅保留序列化
	/** 气候场每面分辨率。与贴图 FaceRes 解耦：调细气候网格不用重烘贴图。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候", meta = (DisplayName = "气候场每面分辨率", EditCondition = "bStage5ClimateEnabled", ClampMin = "32", ClampMax = "512"))
	int32 ClimateRes = 256;

	/** 自转轴倾角（度）。本轮只记录、不模拟季节，存下来是为了将来季节化时不用改配方格式。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|温度", meta = (DisplayName = "自转轴倾角", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "90"))
	float AxialTiltDeg = 23.4f;

	/** 赤道归一化温度：1 表示整个赤道带满值，配合 PoleTemp 决定南北温差。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|温度", meta = (DisplayName = "赤道归一化温度", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float EquatorTemp = 1.f;

	/** 极点归一化温度：越低极区越冷、冰原面积越大。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|温度", meta = (DisplayName = "极点归一化温度", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float PoleTemp = 0.05f;

	/** 高程升到 0.08R 时扣掉的温度份额（比例）；超过 0.08R 不再继续扣，避免高峰把整块大陆压到冰点。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|温度", meta = (EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float LapseRate = 0.35f;

	/** 大陆度对温度的影响幅度（比例）：以大陆度 0.5 为基准，内陆更热、海岸更凉。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|温度", meta = (DisplayName = "大陆度对温度的影响幅度", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float Continentality = 0.15f;

	/** 赤道带降水基准（0–1 归一化）。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "赤道带降水基准", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BaseEquator = 0.9f;

	/** 30° 干旱带降水基准：比赤道低才出得了沙漠带。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "30° 干旱带降水基准", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BaseDry = 0.15f;

	/** 45° 中纬降水基准。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "45° 中纬降水基准", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BaseMid = 0.7f;

	/** 极区降水基准：冷但通常不干，两极形成冰原而非沙漠。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "极区降水基准", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BasePole = 0.15f;

	/** 离海衰减幅度（比例）：大陆度 1 的内陆扣掉它，海岸与海洋不扣。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "离海衰减幅度", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float OceanFalloff = 0.3f;

	/** 雨影强度（比例）：逆风回溯每翻过 0.02R 抬升就扣掉它，一步只扣一份。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "雨影强度", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float RainShadow = 0.8f;

	/** 逆风回溯步数上限。成本随它线性增长（texels × steps），调大要留意烘焙时间。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "逆风回溯步数上限", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "256"))
	int32 ShadowSteps = 32;

	/** 每步跨几个气候 texel：步长越大越省时间，但会跳过窄山脊。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "每步跨几个气候 texel", EditCondition = "bStage5ClimateEnabled", ClampMin = "1", ClampMax = "16"))
	int32 ShadowStride = 2;

	/** 全局降水偏移，调参用：不动任何纬度基准，只整体加减。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|降水", meta = (DisplayName = "全局降水偏移，调参用", EditCondition = "bStage5ClimateEnabled", ClampMin = "-1", ClampMax = "1"))
	float PrecipBias = 0.f;

	// 群系阈值：查表顺序固定，首个命中即返回，重排会改掉整张图。
	/** 冰原阈值：温度低于它。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "冰原阈值", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeIceTemp = 0.2f;

	/** 沙漠阈值：降水低于它（查表排在冰原之后、其余温度判据之前）。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "沙漠阈值", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeDesertPrecip = 0.2f;

	/** 苔原阈值：温度低于它。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "苔原阈值", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeTundraTemp = 0.35f;

	/** 针叶林温度上限。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "针叶林温度上限", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeForestTemp = 0.55f;

	/** 针叶林降水下限：太干会落到草原。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "针叶林降水下限", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeForestPrecip = 0.35f;

	/** 雨林温度下限。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "雨林温度下限", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeRainTemp = 0.7f;

	/** 雨林降水下限。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "雨林降水下限", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeRainPrecip = 0.6f;

	/** 稀树草原温度下限：够热但不够湿的落这里。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "稀树草原温度下限", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeSavannaTemp = 0.75f;

	/** 温带林降水下限：排在草原前，是最后一个有条件的群系。 */
	UPROPERTY(EditAnywhere, Category = "阶段5（气候）|气候|生物群系", meta = (DisplayName = "温带林降水下限", EditCondition = "bStage5ClimateEnabled", ClampMin = "0", ClampMax = "1"))
	float BiomeTemperatePrecip = 0.5f;

	// 阶段 6：水系与剥蚀，全部进 Hash()。

	/** 本分类总开关：关掉后整类（含子阶段）都不参与烘焙。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）", meta = (DisplayName = "Enable")) bool bStage6HydroEnabled = true;

	/** 阶段 6（侵蚀与水文）总开关。 */
	UPROPERTY() bool bRunStage6 = true;  // 旧阶段开关：面板已由分类总开关取代，仅保留序列化
	/** 是否刻河谷；关掉后只算剥蚀深度，高程不变。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "刻河谷", EditCondition = "bStage6HydroEnabled"))
	bool bRivers = true;

	/** 成为河道的最小归一化汇流量。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "成为河道的最小归一化汇流量", EditCondition = "bStage6HydroEnabled", ClampMin = "0.00001", ClampMax = "0.1"))
	float RiverThreshold = 0.0008f;

	/** 下切强度（占 R）：深度 = 它 × √(汇流/阈值) × √(坡度/参考坡度)。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "下切强度", EditCondition = "bStage6HydroEnabled", ClampMin = "0", ClampMax = "0.05"))
	float CarveRatio = 0.002f;

	/** 最大下切深度（占 R）。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "最大下切深度", EditCondition = "bStage6HydroEnabled", ClampMin = "0", ClampMax = "0.05"))
	float CarveMaxRatio = 0.008f;

	/** 谷半宽（方向单位，随汇流增大）；按公式生效到约 4 个宏观格的半宽，更宽的谷被截到这个上限。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "谷半宽", EditCondition = "bStage6HydroEnabled", ClampMin = "0.0005", ClampMax = "0.1"))
	float ValleyWidthRatio = 0.006f;

	/** 坡度参考值（高程差 / 弧长），下切与剥蚀都按它归一。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "坡度参考值", EditCondition = "bStage6HydroEnabled", ClampMin = "0.0001", ClampMax = "1"))
	float SlopeRef = 0.02f;

	/** 剥蚀强度（占 R）。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "剥蚀强度", EditCondition = "bStage6HydroEnabled", ClampMin = "0", ClampMax = "0.1"))
	float ErosionRatio = 0.01f;

	/** 洼地集水区平均降水低于它即为内流盆地，否则是湖。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (EditCondition = "bStage6HydroEnabled", ClampMin = "0", ClampMax = "1"))
	float EndorheicPrecip = 0.3f;

	/** 填洼后保留的极小坡度（占 R），保证平地也有流向。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "填洼后保留的极小坡度", EditCondition = "bStage6HydroEnabled", ClampMin = "0", ClampMax = "0.001"))
	float FlatSlopeRatio = 1e-6f;

	/** 山带遮蔽对剥蚀的加权：0 全域均匀，1 只剥山带。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|水文", meta = (DisplayName = "山带遮蔽对剥蚀的加权", EditCondition = "bStage6HydroEnabled", ClampMin = "0", ClampMax = "1"))
	float MountainErosionWeight = 0.7f;

	// 阶段 6b：出露结算，全部进 Hash()。


	/** 阶段 7（出露：6b）总开关。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|出露", meta = (DisplayName = "Enable", EditCondition = "bStage6HydroEnabled")) bool bRunStage7 = true;
	/** 与出露的 Enable 重复（阶段只做出露这一件事）：面板已隐藏，仅保留序列化。 */
	UPROPERTY() bool bExposure = true;

	/** 抬升量占剥蚀深度的比例：1 = 剥蚀多少就抬多少，0 = 不动。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|出露", meta = (DisplayName = "抬升量占剥蚀深度的比例", EditCondition = "bStage6HydroEnabled && bRunStage7", ClampMin = "0", ClampMax = "2"))
	float LiftScale = 1.0f;

	/** 单个原语最大抬升（占 R）：极端剥蚀下的安全网，防止整条矿被顶到地表。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|出露", meta = (DisplayName = "单个原语最大抬升", EditCondition = "bStage6HydroEnabled && bRunStage7", ClampMin = "0", ClampMax = "0.2"))
	float MaxLiftRatio = 0.02f;
	/** 阶段 7 的撒矿（6c-1 砂矿 / 6c-2 蒸发岩与煤）。 */
	UPROPERTY(EditAnywhere, Category = "阶段6（水系与侵蚀）|出露|撒矿", meta = (DisplayName = "启用撒矿", EditCondition = "bStage6HydroEnabled && bRunStage7")) bool bScatterStage7 = true;
	/** 本阶段的撒矿器：增删、排序只影响本阶段。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "阶段6（水系与侵蚀）|出露|撒矿", meta = (DisplayName = "本阶段撒矿器", EditCondition = "bStage6HydroEnabled && bRunStage7"))
	TArray<TObjectPtr<UGeoMineralScatterer>> ScatterStage7Minerals;

	// 阶段 3A：沉积盖层。盖在低地上，给层控铅锌、古煤、蒸发岩、砂岩型铀提供宿主。
	/** 阶段 3A（沉积盖层）总开关。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|盖层", meta = (DisplayName = "Enable", EditCondition = "bStage3TerrainEnabled")) bool bRunStage9 = true;
	/** 最厚盖层占半径比例：R=40000 时 0.01 约 400 体素。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|盖层", meta = (DisplayName = "最厚盖层占半径比例", EditCondition = "bStage3TerrainEnabled && bRunStage9", ClampMin = "0", ClampMax = "0.05"))
	float MaxStrataThicknessRatio = 0.01f;
	/** 低地上限：高出海平面不到「陆高跨度 × 它」的地方才盖。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|盖层", meta = (DisplayName = "低地上限", EditCondition = "bStage3TerrainEnabled && bRunStage9", ClampMin = "0.05", ClampMax = "1"))
	float StrataLowlandCutoff = 0.45f;
	/** 砂岩占盖层的份额上限（0–1）。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|盖层", meta = (DisplayName = "砂岩占盖层的份额上限", EditCondition = "bStage3TerrainEnabled && bRunStage9", ClampMin = "0", ClampMax = "1"))
	float StrataSandstoneShare = 0.45f;
	/** 砂岩 + 页岩的累计份额上限；再往上是石灰岩。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|盖层", meta = (DisplayName = "砂岩 + 页岩的累计份额上限", EditCondition = "bStage3TerrainEnabled && bRunStage9", ClampMin = "0", ClampMax = "1"))
	float StrataShaleShare = 0.72f;
	/** 阶段 3A 的撒矿（层控铅锌 / 古煤 / 古蒸发岩 / 砂岩型铀）。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|盖层|撒矿", meta = (DisplayName = "启用撒矿", EditCondition = "bStage3TerrainEnabled && bRunStage9")) bool bScatterStage9 = true;
	/** 本阶段的撒矿器：增删、排序只影响本阶段。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "阶段3（地形与构造）|盖层|撒矿", meta = (DisplayName = "本阶段撒矿器", EditCondition = "bStage3TerrainEnabled && bRunStage9"))
	TArray<TObjectPtr<UGeoMineralScatterer>> ScatterStage9Minerals;

	// 阶段 3B：构造变形变质。断层带与变质级。
	/** 阶段 3B（构造变形变质）总开关。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|变质", meta = (DisplayName = "Enable", EditCondition = "bStage3TerrainEnabled")) bool bRunStage10 = true;
	/** 断层带半宽（方向单位）。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|变质", meta = (DisplayName = "断层带半宽", EditCondition = "bStage3TerrainEnabled && bRunStage10", ClampMin = "0.001", ClampMax = "0.05"))
	float FaultWidthRatio = 0.012f;
	/** 褶皱场基波长（方向单位）。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|变质", meta = (DisplayName = "褶皱场基波长", EditCondition = "bStage3TerrainEnabled && bRunStage10", ClampMin = "0.005", ClampMax = "0.2"))
	float FoldWavelengthRatio = 0.03f;
	/** 埋深超过它（占 R）的岩石按变质处理。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|变质", meta = (DisplayName = "埋深超过它", EditCondition = "bStage3TerrainEnabled && bRunStage10", ClampMin = "0", ClampMax = "0.3"))
	float MetamorphicDepthRatio = 0.09f;
	/** 石灰岩转大理岩的变质级门槛。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|变质", meta = (DisplayName = "石灰岩转大理岩的变质级门槛", EditCondition = "bStage3TerrainEnabled && bRunStage10", ClampMin = "0", ClampMax = "1"))
	float MarbleThreshold = 0.55f;
	/** 造山带对变质级的加权。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|变质", meta = (DisplayName = "造山带对变质级的加权", EditCondition = "bStage3TerrainEnabled && bRunStage10", ClampMin = "0", ClampMax = "2"))
	float OrogenBoost = 0.6f;
	/** 阶段 3B 的撒矿（造山型金 / 石墨 / 大理岩）。 */
	UPROPERTY(EditAnywhere, Category = "阶段3（地形与构造）|变质|撒矿", meta = (DisplayName = "启用撒矿", EditCondition = "bStage3TerrainEnabled && bRunStage10")) bool bScatterStage10 = true;
	/** 本阶段的撒矿器：增删、排序只影响本阶段。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "阶段3（地形与构造）|变质|撒矿", meta = (DisplayName = "本阶段撒矿器", EditCondition = "bStage3TerrainEnabled && bRunStage10"))
	TArray<TObjectPtr<UGeoMineralScatterer>> ScatterStage10Minerals;

	// 阶段 7：表生风化。只改最外面一层皮与土壤类型。
	/** 本分类总开关：关掉后整类（含子阶段）都不参与烘焙。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）", meta = (DisplayName = "Enable")) bool bStage7WeatheringEnabled = true;
	/** 阶段 7（表生风化）总开关。 */
	UPROPERTY() bool bRunStage8 = true;  // 旧阶段开关：面板已由分类总开关取代，仅保留序列化
	/** 最厚风化壳占半径比例：R=40000 时 0.002 约 80 体素。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化", meta = (DisplayName = "最厚风化壳占半径比例", EditCondition = "bStage7WeatheringEnabled", ClampMin = "0", ClampMax = "0.02"))
	float WeatheringMaxDepthRatio = 0.002f;
	/** 温度对风化强度的权重。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化", meta = (DisplayName = "温度对风化强度的权重", EditCondition = "bStage7WeatheringEnabled", ClampMin = "0", ClampMax = "1"))
	float WeatheringTemperatureWeight = 0.5f;
	/** 降水对风化强度的权重。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化", meta = (DisplayName = "降水对风化强度的权重", EditCondition = "bStage7WeatheringEnabled", ClampMin = "0", ClampMax = "1"))
	float WeatheringPrecipitationWeight = 0.5f;
	/** 坡度扣减强度：1 = 陡坡直接没有风化壳。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化", meta = (DisplayName = "坡度扣减强度", EditCondition = "bStage7WeatheringEnabled", ClampMin = "0", ClampMax = "1"))
	float WeatheringSlopePenalty = 0.8f;
	/** 红土的温度门槛。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化", meta = (DisplayName = "红土的温度门槛", EditCondition = "bStage7WeatheringEnabled", ClampMin = "0", ClampMax = "1"))
	float LateriteTemp = 0.7f;
	/** 红土的降水门槛。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化", meta = (DisplayName = "红土的降水门槛", EditCondition = "bStage7WeatheringEnabled", ClampMin = "0", ClampMax = "1"))
	float LateritePrecip = 0.5f;
	/** 高岭土的温度门槛。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化", meta = (DisplayName = "高岭土的温度门槛", EditCondition = "bStage7WeatheringEnabled", ClampMin = "0", ClampMax = "1"))
	float KaolinTemp = 0.45f;
	/** 阶段 7 的撒矿（铝土矿 / 红土镍 / 次生铜 / 铁帽 / 高岭土）。 */
	UPROPERTY(EditAnywhere, Category = "阶段7（风化）|风化|撒矿", meta = (DisplayName = "启用撒矿", EditCondition = "bStage7WeatheringEnabled")) bool bScatterStage8 = true;
	/** 本阶段的撒矿器：增删、排序只影响本阶段。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "阶段7（风化）|风化|撒矿", meta = (DisplayName = "本阶段撒矿器", EditCondition = "bStage7WeatheringEnabled"))
	TArray<TObjectPtr<UGeoMineralScatterer>> ScatterStage8Minerals;

	/** 矿物目录：编号、中文名、调试色。撒矿器只引用这里的编号。 */
	UPROPERTY(EditAnywhere, Category = "Minerals")
	TObjectPtr<UGeoMineralCatalog> MineralCatalog;

	/** 旧版单一撒矿器列表：加载时按规则阶段拆进各阶段列表后清空，只为迁移兼容，面板里隐藏。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Minerals", meta = (DisplayName = "LegacyMineralScatterers", EditCondition = "false", EditConditionHides))
	TArray<TObjectPtr<UGeoMineralScatterer>> MineralScatterers;

	/** 拷成烘焙用的 POD。半径由行星给（行星是半径的唯一真源），配方本身不存。 */
	FPlanetRecipeConfig ToConfig(double RadiusVoxels) const;

	virtual void PostLoad() override;
};
