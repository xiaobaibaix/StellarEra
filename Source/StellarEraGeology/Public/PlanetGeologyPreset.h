// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质预设资产：一颗行星的元素表 + 外观 + 几何参数。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlanetGeologyTypes.h"
#include "UObject/SoftObjectPath.h"
#include "PlanetGeologyPreset.generated.h"

class UPlanetGeologyDistribution;
class UPlanetGeologyPreset;

#if WITH_EDITOR
/** 预设内容被改动（编辑器）。观察视图用它自动重建。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlanetGeologyPresetChanged, UPlanetGeologyPreset*);
#endif

/**
 * 一种矿的分布参数。
 *
 * 全部是「这颗行星上这种矿长什么样」的旋钮，与地形模块的地位相同：资产里调，
 * 改完重建行星即可看到。距离一律是归一化空间的半径比例，换行星大小不改变相对尺寸。
 */
USTRUCT(BlueprintType)
struct STELLARERAGEOLOGY_API FPlanetGeologyElementParams
{
	GENERATED_BODY()

	/** 稳定标识，只用于日志、随机种子派生。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FLinearColor Color = FLinearColor::White;

	/**
	 * 只影响颜色混合权重与图例占比，**不改几何**。
	 * 想改矿囊密度请调 PodCount / PodRadius。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element",
		meta = (ClampMin = "0.0"))
	float Abundance = 1.0f;

	/**
	 * 这个元素的分布生成器。多个生成器逐点取 max（不是相加）。
	 * 空数组且 PodCount > 0 时走遗留兜底，旧资产因此在运行期保持原样。
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Distribution")
	TArray<TObjectPtr<UPlanetGeologyDistribution>> Distributions;

	/** 撒多少个矿囊。0 = 这个元素在这颗行星上不存在。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods",
		meta = (ClampMin = "0", ClampMax = "512", DeprecatedProperty))
	int32 PodCount = 16;

	/**
	 * 矿囊名义半径（占行星基准半径的比例）。
	 *
	 * 重叠对的数量按 PodCount² × (2·r)³ 增长：半径砍半，重叠少 8 倍。
	 * 想看不同元素互相穿插，就把这个值调大，别只加 PodCount。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods",
		meta = (ClampMin = "0.001", ClampMax = "0.25", DeprecatedProperty))
	float PodRadius = 0.05f;

	/** 每囊半径的随机抖动幅度（±比例）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DeprecatedProperty))
	float PodRadiusJitter = 0.5f;

	/**
	 * 深度偏好强度。**0 = 全深度体积均匀**（默认，也是「不要分层」的语义）。
	 * 大于 0 时按 PodDepthCenter/Spread 做带权拒绝采样，越深越偏。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods|Depth",
		meta = (ClampMin = "0.0", ClampMax = "0.9", DeprecatedProperty))
	float PodDepthBias = 0.0f;

	/** 偏好深度（占半径比例，0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods|Depth",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DeprecatedProperty))
	float PodDepthCenter = 0.25f;

	/** 偏好深度的高斯宽度（占半径比例）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods|Depth",
		meta = (ClampMin = "0.01", ClampMax = "1.0", DeprecatedProperty))
	float PodDepthSpread = 0.15f;

	/** 一个矿囊直径内大约几个噪声周期。越大概括破碎；低于 1 时整囊可能落进噪声谷而消失。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "1.0", DeprecatedProperty))
	float ShapeDetail = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "1", ClampMax = "6", DeprecatedProperty))
	int32 Octaves = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "1.0", DeprecatedProperty))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DeprecatedProperty))
	float Gain = 0.5f;

	/** 0 时由 Hash(Id) 派生，保证不同元素默认不相关。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (DeprecatedProperty))
	int32 Seed = 0;

	/** 噪声稀疏阈值，越高矿囊越破碎。0 = 不稀疏。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", ClampMax = "0.95", DeprecatedProperty))
	float VeinThreshold = 0.5f;
};

/**
 * 一颗行星的地质预设。**唯一数据源**：JSON 预设已退役。
 *
 * 消费链：AVoxelPlanetActor::GeologyAsset → UVoxelPlanetGenerator::GeologyPreset
 * → FPlanetGeologyField::BuildFromPreset。改完资产后调用 ReloadGeology 重建行星。
 *
 * 元素数组的顺序 = 采样顺序 = 图例顺序 = 矿体组件下标，四者共用。
 */
UCLASS(BlueprintType)
class STELLARERAGEOLOGY_API UPlanetGeologyPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	//~ Begin UObject Interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	/** 每次编辑自增。缓存键靠它判断「同一份资产的内容变了」。 */
	int32 GetRevision() const { return Revision; }

	/**
	 * 内容变了：修订号自增，编辑器里再广播一次。
	 *
	 * 嵌套子对象（分布生成器）的 PostEditChangeProperty 也走这里 —— 引擎不会替
	 * 子对象通知外层预设。少这一次通知，观测预览不重建、地形缓存键也不变。
	 */
	void NotifyContentChanged();

	/**
	 * 行星名 → 预设资产路径：`earth` → `/Game/Geology/Planets/DA_Geology_Earth`。
	 *
	 * **路径规则只留这一份**。三个消费方都必须走它：`PostLoad` 的迁移
	 * （`AVoxelPlanetActor::MakeLegacyGeologyAssetPath` 转发到这里）、
	 * `StellarEra.CreateEarthGeologyPreset` 建资产、自动化测试。
	 * 任何一处另拼字面量，一旦漂移就是「旧关卡永久没矿」，而症状只是「行星没矿」。
	 *
	 * 纯函数：去首尾空格 → 空名返回空路径 → 首字母转大写。
	 */
	static FSoftObjectPath MakeAssetPath(const FString& PlanetName);

	/**
	 * 旧字段 → 显式囊状生成器。**只在 Distributions 为空且 PodCount > 0 时动作**。
	 *
	 * 种子必须是旧公式 `Seed != 0 ? Seed : Hash(Id) & 0x7fffffff`，不是生成器基类的
	 * 派生规则 —— 换了公式，旧行星的矿囊位置就全变了。
	 *
	 * PodCount 置 0 是「已迁移」标记：已保存的资产不会再迁，未保存的每次加载
	 * 都重建同一个生成器，结果一致。
	 */
	static void MigrateLegacyElement(FPlanetGeologyElementParams& Element, UObject* Outer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geology")
	TArray<FPlanetGeologyElementParams> Elements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geology")
	FPlanetGeologyAppearance Appearance;

	/** 矿囊把地表顶起的最大幅度（体素）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geology",
		meta = (ClampMin = "0.0", ClampMax = "64"))
	float GeometryAmplitude = 1.5f;

	/**
	 * 内容修订号。非 0 起，每次 PostEditChangeProperty 自增。
	 *
	 * 只做缓存失效用，不是数据：场景里改资产而不保存，读到的也是新内容。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geology")
	int32 Revision = 1;

#if WITH_EDITOR
	/**
	 * 内容变更广播。只在编辑器存在；无人订阅时零成本。
	 * 用静态多播而不是 UPROPERTY 委托：订阅者是 C++ 组件，不需要蓝图序列化。
	 */
	FOnPlanetGeologyPresetChanged OnPresetChanged;
#endif
};
