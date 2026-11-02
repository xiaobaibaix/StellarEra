#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarBodyComponent.generated.h"

class UWorld;

/**
 * 「这个 Actor 是一颗恒星」的标记 + 主光接管开关。
 *
 * ── 为什么不能只靠 UCosmosOrbitComponent 判断恒星 ──────────────
 * 判断恒星的代码是大气组件，它在本模块；而 UCosmosOrbitComponent 在 Cosmos 模块。
 * `StellarEraCosmos → StellarEraPlanet` 这条依赖已经存在，反向再来一条就成环
 * （见 docs/superpowers/specs/2026-11-02-sun-star-bodies-design.md §4）。
 *
 * 于是恒星的身份拆成两个互不认识的组件，由关卡里的同一个 Actor 同时挂上：
 *   - UCosmosOrbitComponent（Cosmos）：质量多少、初速多少 —— 物理身份
 *   - 本组件（Planet）            ：这是一颗恒星、是否接管主光 —— 视觉 / 大气身份
 * Cosmos 不需要知道「恒星」这个概念，Planet 也不需要知道 N 体。
 *
 * 与 UCosmosOrbitComponent 同样的取舍：**不 Tick，组件自己不存位置**。
 * 恒星位置就是 Owner 的位置，每帧现取 —— 恒星在 N 体里是会动的。
 *
 * 恒星的颜色由各自的材质实例（MI_Sun_*）承载，这里不再冗余一份：
 * 多恒星光照本就列为非目标，留一个没人消费的颜色字段只会误导后来者。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UStarBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarBodyComponent();

	/**
	 * 这颗恒星是否接管关卡主光（第一盏 ADirectionalLight）的朝向。
	 *
	 * 只对该行星**最近**的那颗恒星有意义：大气按"最近的恒星"算明暗分界，
	 * 地形若还按另一盏灯的朝向算，两边会打架。多颗恒星时只有最近那颗生效，
	 * 所以同时只有一个写者。
	 *
	 * 关掉它就退回纯装饰的恒星：天空照得到它，但它不转灯。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StellarEra|Star")
	bool bDrivesDirectionalLight = true;

	/**
	 * 世界上离 FromLocation 最近的恒星组件；World 为空或世界里没有恒星时返回 nullptr。
	 *
	 * 用「最近」而不是「唯一」：三颗恒星下每颗行星应当被自己那颗恒星照亮。
	 * 每次都完整遍历 —— 关卡里天体是个位数，成本可以忽略，换来的是恒星移动、
	 * 增删时不需要任何失效通知。
	 */
	static UStarBodyComponent* FindNearest(const UWorld* World, const FVector& FromLocation);
};
