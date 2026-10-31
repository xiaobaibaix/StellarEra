// 星图相机体素调用器配置的回归测试。
//
// 星图相机与飞船会在同一片地形上交替出现（驾驶 ↔ 星图），所以调用器参数
// 必须和飞船（SpaceshipPawn.cpp 构造函数）保持一致 —— 两套值不一致的话，
// 切模式的瞬间 LOD 会抖一下。这里锁的是：
//   1. 恰好四个调用器（细气泡 + LOD3 环 + LOD5 环 + LOD6 过渡环）；
//   2. 四档 LOD 数值与飞船一致；
//   3. 碰撞只开在细气泡上。
//
// 断言的是构造函数产出的 CDO，不需要世界、不需要 PIE。
//
// 与飞船测试的唯一差别：这里是 **Simple** 版本而不是 WithPrediction。
// 本 Pawn 不 Tick，位置在 CalcCamera 里现算，GetVelocity() 恒为零，
// 预测位移永远是 0 —— 挂预测版只是多一层空转。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "StarMapCameraPawn.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapCameraInvokerTest,
	"StellarEra.StarMap.CameraVoxelInvokers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapCameraInvokerTest::RunTest(const FString& Parameters)
{
	AStarMapCameraPawn* CDO = GetMutableDefault<AStarMapCameraPawn>();
	if (!TestNotNull(TEXT("AStarMapCameraPawn CDO"), CDO))
	{
		return false;
	}

	// 「恰好四个」从反射表数，而不是数我们写了哪几个变量名：
	// 以后有人加第五个调用器，这条会先失败。
	int32 InvokerPropertyCount = 0;
	for (TFieldIterator<FObjectProperty> It(CDO->GetClass()); It; ++It)
	{
		if (It->PropertyClass &&
			It->PropertyClass->IsChildOf(UVoxelInvokerComponentBase::StaticClass()))
		{
			++InvokerPropertyCount;
		}
	}
	TestEqual(TEXT("调用器数量"), InvokerPropertyCount, 4);

	const auto FindInvoker = [CDO](const TCHAR* Name) -> const UVoxelSimpleInvokerComponent*
	{
		return Cast<UVoxelSimpleInvokerComponent>(CDO->GetDefaultSubobjectByName(Name));
	};

	const UVoxelSimpleInvokerComponent* Fine = FindInvoker(TEXT("VoxelInvoker"));
	const UVoxelSimpleInvokerComponent* Ring3 = FindInvoker(TEXT("VoxelInvokerLOD3"));
	const UVoxelSimpleInvokerComponent* Ring5 = FindInvoker(TEXT("VoxelInvokerLOD5"));
	const UVoxelSimpleInvokerComponent* Ring6 = FindInvoker(TEXT("VoxelInvokerLOD6"));

	if (!TestNotNull(TEXT("细气泡 VoxelInvoker"), Fine) ||
		!TestNotNull(TEXT("中间环 VoxelInvokerLOD3"), Ring3) ||
		!TestNotNull(TEXT("中间环 VoxelInvokerLOD5"), Ring5) ||
		!TestNotNull(TEXT("过渡环 VoxelInvokerLOD6"), Ring6))
	{
		return false;
	}

	// 类必须是项目自己的 AlwaysLocal 子类：插件原类只把「被本地控制器 Possess
	// 的 Pawn」算作 local invoker，进/出星图换 Possess 时会把星图调用器踢出
	// LOD、触发全量重建（见 AlwaysLocalVoxelInvokerComponent.h）。
	TestTrue(TEXT("细气泡是 AlwaysLocal 类"),
		Cast<UAlwaysLocalVoxelInvokerComponent>(Fine) != nullptr);
	TestTrue(TEXT("LOD3 环是 AlwaysLocal 类"),
		Cast<UAlwaysLocalVoxelInvokerComponent>(Ring3) != nullptr);
	TestTrue(TEXT("LOD5 环是 AlwaysLocal 类"),
		Cast<UAlwaysLocalVoxelInvokerComponent>(Ring5) != nullptr);
	TestTrue(TEXT("LOD6 环是 AlwaysLocal 类"),
		Cast<UAlwaysLocalVoxelInvokerComponent>(Ring6) != nullptr);

	// 细气泡：LOD 1 @ 80 米、碰撞 30 米 —— 与飞船逐项相同。
	TestEqual(TEXT("细气泡 LODToSet"), Fine->LODToSet, 1);
	TestEqual(TEXT("细气泡 LODRange（cm）"), Fine->LODRange, 8000.f);
	TestEqual(TEXT("细气泡 CollisionsRange（cm）"), Fine->CollisionsRange, 3000.f);
	TestTrue(TEXT("细气泡负责碰撞"), Fine->bUseForCollisions);

	// 中间环：只做 LOD。碰撞必须关 —— 碰撞细分路径不读 LODToSet、锁死 Height 0，
	// 环的范围会请求数万个区块。
	TestEqual(TEXT("LOD3 环 LODToSet"), Ring3->LODToSet, 3);
	TestEqual(TEXT("LOD3 环 LODRange（cm）"), Ring3->LODRange, 30000.f);
	TestFalse(TEXT("LOD3 环不负责碰撞"), Ring3->bUseForCollisions);
	TestFalse(TEXT("LOD3 环不负责导航"), Ring3->bUseForNavmesh);
	TestFalse(TEXT("LOD3 环不接事件"), Ring3->bUseForEvents);
	TestFalse(TEXT("LOD3 环不参与优先级"), Ring3->bUseForPriorities);

	TestEqual(TEXT("LOD5 环 LODToSet"), Ring5->LODToSet, 5);
	TestEqual(TEXT("LOD5 环 LODRange（cm）"), Ring5->LODRange, 100000.f);
	TestFalse(TEXT("LOD5 环不负责碰撞"), Ring5->bUseForCollisions);
	TestFalse(TEXT("LOD5 环不负责导航"), Ring5->bUseForNavmesh);
	TestFalse(TEXT("LOD5 环不接事件"), Ring5->bUseForEvents);
	TestFalse(TEXT("LOD5 环不参与优先级"), Ring5->bUseForPriorities);

	// 过渡环：叶块 1.02 公里（行星 3 体素 50 cm），补 LOD5 与行星 MaxLOD
	// 之间那一档 —— 行星压到 LOD 7 之后，512 米与 2.05 公里之间就只剩这一级台阶。
	TestEqual(TEXT("LOD6 环 LODToSet"), Ring6->LODToSet, 6);
	TestEqual(TEXT("LOD6 环 LODRange（cm）"), Ring6->LODRange, 1000000.f);
	TestFalse(TEXT("LOD6 环不负责碰撞"), Ring6->bUseForCollisions);
	TestFalse(TEXT("LOD6 环不负责导航"), Ring6->bUseForNavmesh);
	TestFalse(TEXT("LOD6 环不接事件"), Ring6->bUseForEvents);
	TestFalse(TEXT("LOD6 环不参与优先级"), Ring6->bUseForPriorities);

	// 四档都开着 LOD —— 关掉任何一档都会让那一圈星球变糊。
	TestTrue(TEXT("细气泡做 LOD"), Fine->bUseForLOD);
	TestTrue(TEXT("LOD3 环做 LOD"), Ring3->bUseForLOD);
	TestTrue(TEXT("LOD5 环做 LOD"), Ring5->bUseForLOD);
	TestTrue(TEXT("LOD6 环做 LOD"), Ring6->bUseForLOD);

	// 碰撞范围必须落在细气泡的 LOD 范围内，否则「能摸到的地形」比「画出来的」
	// 还远，飞船/相机贴地时会先撞上一片粗糙网格。
	TestTrue(TEXT("碰撞范围 ≤ 细气泡 LOD 范围"),
		Fine->CollisionsRange <= Fine->LODRange);

	return true;
}

#endif
