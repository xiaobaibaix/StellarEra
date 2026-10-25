// 飞船体素调用器配置的回归测试。
//
// 飞行中「不同 LOD 的网格重叠」由三段配置共同决定：
//   1. 行星调用器把整颗星压在 LOD 8（叶块 8.2 公里）；
//   2. 飞船只有 LOD 1 细气泡（80 米），与 LOD 8 之间落差 7 级；
//   3. 插件在旧块被替代后还要淡出 2 × ChunksDitheringDuration。
// 这里锁住第 2 段：细气泡 + 两个中间环，且环只做 LOD、不碰碰撞。
//
// 断言的是构造函数产出的 CDO 配置，不是「能编译」。
// 运行期生成的是 BP_SpaceshipPawn_C，所以直接加载蓝图再查组件。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "SpaceshipPawn.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipLODInvokerTest,
	"StellarEra.Spaceship.SpaceshipLODInvokers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipLODInvokerTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	if (!TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass))
	{
		return false;
	}

	ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	if (!TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO))
	{
		return false;
	}

	const auto FindInvoker = [CDO](const TCHAR* Name) -> const UVoxelInvokerWithPredictionComponent*
	{
		return Cast<UVoxelInvokerWithPredictionComponent>(CDO->GetDefaultSubobjectByName(Name));
	};

	const UVoxelInvokerWithPredictionComponent* Fine = FindInvoker(TEXT("VoxelInvoker"));
	const UVoxelInvokerWithPredictionComponent* Ring3 = FindInvoker(TEXT("VoxelInvokerLOD3"));
	const UVoxelInvokerWithPredictionComponent* Ring5 = FindInvoker(TEXT("VoxelInvokerLOD5"));

	if (!TestNotNull(TEXT("细气泡 VoxelInvoker"), Fine) ||
		!TestNotNull(TEXT("中间环 VoxelInvokerLOD3"), Ring3) ||
		!TestNotNull(TEXT("中间环 VoxelInvokerLOD5"), Ring5))
	{
		return false;
	}

	// 类必须是我们自己的 AlwaysLocal 子类，不能退回插件原类：原类只把「被本地
	// 控制器 Possess 的 Pawn」算作 local invoker，进/出星图换 Possess 时会把
	// 飞船调用器踢出 LOD、触发全量重建（见 AlwaysLocalVoxelInvokerComponent.h）。
	// 蓝图子类若残留旧类的组件模板覆盖，这几条会当场变红。
	TestTrue(TEXT("细气泡是 AlwaysLocal 类"),
		Cast<UAlwaysLocalVoxelInvokerWithPredictionComponent>(Fine) != nullptr);
	TestTrue(TEXT("LOD3 环是 AlwaysLocal 类"),
		Cast<UAlwaysLocalVoxelInvokerWithPredictionComponent>(Ring3) != nullptr);
	TestTrue(TEXT("LOD5 环是 AlwaysLocal 类"),
		Cast<UAlwaysLocalVoxelInvokerWithPredictionComponent>(Ring5) != nullptr);

	// 细气泡：LOD 1 @ 80 米、碰撞 30 米。PredictionTime 不能再大 ——
	// 门控上限 0.6 km/s = 6e4 cm/s，× 0.03 s = 18 米，仍小于两个范围。
	TestEqual(TEXT("细气泡 LODToSet"), Fine->LODToSet, 1);
	TestEqual(TEXT("细气泡 LODRange（cm）"), Fine->LODRange, 8000.f);
	TestEqual(TEXT("细气泡 CollisionsRange（cm）"), Fine->CollisionsRange, 3000.f);
	TestTrue(TEXT("细气泡负责碰撞"), Fine->bUseForCollisions);
	TestTrue(TEXT("细气泡启用预测"), Fine->bEnablePrediction);
	TestEqual(TEXT("细气泡 PredictionTime（秒）"), Fine->PredictionTime, 0.03f, 1e-4f);

	// 中间环：只做 LOD。碰撞必须关 —— 环的范围在 Height 0 下会请求数万个区块。
	TestEqual(TEXT("LOD3 环 LODToSet"), Ring3->LODToSet, 3);
	TestEqual(TEXT("LOD3 环 LODRange（cm）"), Ring3->LODRange, 30000.f);
	TestFalse(TEXT("LOD3 环不负责碰撞"), Ring3->bUseForCollisions);
	TestFalse(TEXT("LOD3 环不负责导航"), Ring3->bUseForNavmesh);
	TestFalse(TEXT("LOD3 环不接事件"), Ring3->bUseForEvents);
	TestFalse(TEXT("LOD3 环不参与优先级"), Ring3->bUseForPriorities);
	TestTrue(TEXT("LOD3 环启用预测"), Ring3->bEnablePrediction);
	TestEqual(TEXT("LOD3 环 PredictionTime（秒）"), Ring3->PredictionTime, 0.1f, 1e-4f);

	TestEqual(TEXT("LOD5 环 LODToSet"), Ring5->LODToSet, 5);
	TestEqual(TEXT("LOD5 环 LODRange（cm）"), Ring5->LODRange, 100000.f);
	TestFalse(TEXT("LOD5 环不负责碰撞"), Ring5->bUseForCollisions);
	TestFalse(TEXT("LOD5 环不负责导航"), Ring5->bUseForNavmesh);
	TestFalse(TEXT("LOD5 环不接事件"), Ring5->bUseForEvents);
	TestFalse(TEXT("LOD5 环不参与优先级"), Ring5->bUseForPriorities);
	TestTrue(TEXT("LOD5 环启用预测"), Ring5->bEnablePrediction);
	TestEqual(TEXT("LOD5 环 PredictionTime（秒）"), Ring5->PredictionTime, 0.2f, 1e-4f);

	// 高速门控：默认 0.6 / 0.2 km/s + 0.5 秒冷却。门控只关细气泡的碰撞
	// （bUseForCollisions），LOD 三档始终开着 —— 关掉 LOD 会让星球变糊。
	// 这三个属性在 protected 区（只给蓝图调），测试用反射读 CDO。
	const auto GetFloatProperty = [CDO](const TCHAR* Name) -> float
	{
		const FFloatProperty* Prop = FindFProperty<FFloatProperty>(CDO->GetClass(), Name);
		return Prop ? Prop->GetPropertyValue_InContainer(CDO) : -1.f;
	};

	const float SuspendSpeed = GetFloatProperty(TEXT("VoxelInvokerSuspendSpeedKmPerSecond"));
	const float ResumeSpeed = GetFloatProperty(TEXT("VoxelInvokerResumeSpeedKmPerSecond"));
	const float ToggleCooldown = GetFloatProperty(TEXT("VoxelInvokerToggleCooldownSeconds"));

	TestEqual(TEXT("关 invoker 速度阈值（km/s）"), SuspendSpeed, 0.6f);
	TestEqual(TEXT("开 invoker 速度阈值（km/s）"), ResumeSpeed, 0.2f);
	TestEqual(TEXT("启停冷却（秒）"), ToggleCooldown, 0.5f);
	TestTrue(TEXT("回差为正"), SuspendSpeed > ResumeSpeed);

	// 门控上限处的预测偏移必须仍在各自范围内，否则飞船会在上限速度下
	// 把采样点推出自己的盒子（细气泡以较小的碰撞范围为准）。
	const float SuspendSpeedCmPerSecond = SuspendSpeed * 1.0e5f;
	TestTrue(TEXT("细气泡预测偏移 ≤ 碰撞范围"),
		SuspendSpeedCmPerSecond * Fine->PredictionTime <= Fine->CollisionsRange);
	TestTrue(TEXT("LOD3 环预测偏移 ≤ LODRange"),
		SuspendSpeedCmPerSecond * Ring3->PredictionTime <= Ring3->LODRange);
	TestTrue(TEXT("LOD5 环预测偏移 ≤ LODRange"),
		SuspendSpeedCmPerSecond * Ring5->PredictionTime <= Ring5->LODRange);

	return true;
}

#endif
