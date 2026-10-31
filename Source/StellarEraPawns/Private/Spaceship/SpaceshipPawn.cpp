// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipPawn.h"

#include "VoxelPlanetActor.h"
#include "PlanetAtmosphereComponent.h"
#include "PlanetCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"
#include "VoxelViewpointInvokerUtils.h"

namespace
{
	// 占位立方体的资产路径与比例，只在构造函数里用：裸放的 ASpaceshipPawn
	// （没有蓝图填 HullMesh）也得看得见一个大致像机身的东西。
	const TCHAR* FallbackHullPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	// 100cm 见方的立方体太小，撑成 600×200×100 才像个机身。这个比例只对
	// 占位立方体有意义 —— 蓝图里覆盖过 Hull 的缩放后以蓝图为准（子蓝图的
	// 覆盖值在构造函数之后才套用）。
	const FVector FallbackHullScale(6.f, 2.f, 1.f);

	/**
	 * 1 千米 = 1e5 厘米。
	 *
	 * 本模块不声明 StellarEraCore 依赖（设计选择：飞船层不引入 Cosmos 单位体系）。
	 * 注意不是防环 —— StellarEraCore 没有项目依赖，加进来也不会成环。
	 */
	constexpr float CentimetersPerKilometer = 1.0e5f;
}

ASpaceshipPawn::ASpaceshipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// --- 船体 ---
	//
	// Hull 是根组件：Actor 的变换锚点、求解器写回位置的落点都在它身上，不再额外
	// 套一个看不见的根球。相机的弹簧臂、体素调用器都挂在它下面。
	//
	// 碰撞形状 = 船体网格自身的 Simple Collision（导入时生成的 UCX 凸包）。
	// 3 m 的球罩不住 55 m × 83 m 的航天飞机，挡人/挡射线的位置全是错的。
	// 注意：这**不会**让飞船自己被地形挡住 —— 它的位移由 N 体求解器每帧
	// SetActorLocation(bSweep=false) 瞬移写入，接触高度由凸包探针（或兜底半径）决定。
	// 凸包只负责「别人撞船」、射线/重叠查询与相机弹簧臂。
	// 网格若没有 Simple Collision，这里不会有任何阻挡（物理需要简单碰撞）。
	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	SetRootComponent(Hull);
	Hull->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Hull->SetCollisionObjectType(ECC_Pawn);
	Hull->SetCollisionResponseToAllChannels(ECR_Block);

	// C++ 只给一个引擎立方体兜底，让裸放的 ASpaceshipPawn 不至于隐形。
	//
	// 真实模型不在这里。它属于「具体表现」，由子蓝图 BP_SpaceshipPawn 提供：
	// 换模型用 HullMesh 属性（或直接覆盖 Hull 的静态网格体），摆位置/朝向/缩放
	// 直接在蓝图视口里拖 Hull —— C++ 之后不再写 Hull 的任何变换，拖出来的值
	// 才不会被每次编译冲掉。
	//
	// 也正因为如此，这里不能写项目资产路径：蓝图 CDO 构造时会先跑一遍这个
	// 构造函数，之后才加载蓝图里存的值，构造期读到的永远是 C++ 默认值。
	// 真正套用 HullMesh 的地方是 ApplyHullMesh。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackHull(FallbackHullPath);
	if (FallbackHull.Succeeded())
	{
		Hull->SetStaticMesh(FallbackHull.Object);
		Hull->SetRelativeScale3D(FallbackHullScale);
	}
	else
	{
		// 连引擎立方体都拿不到时会得到一个没有网格、完全不可见的空 Actor，
		// 看上去就像"飞船没生成"。宁可吵一句，也不要静默失败。
		UE_LOG(LogTemp, Warning,
			TEXT("ASpaceshipPawn: 找不到占位立方体 %s，Hull 将没有网格。"), FallbackHullPath);
	}

	// --- 体素调用器 ---
	//
	// 挂在根节点而不是机头：调用器的位置同时决定 LOD 范围和碰撞范围，碰撞范围
	// 必须罩住飞船自身的物理体，否则飞机会撞进还没生成碰撞体的地形里。
	//
	// 用带预测的版本，而不是角色那种 UVoxelSimpleInvokerComponent：预测把采样点
	// 沿速度方向前移 PredictionTime 秒
	// （UVoxelInvokerWithPredictionComponent::GetInvokerGlobalPosition），凭空多出
	// 预警，且不额外扩大细分区域。停稳时速度为零，退化成普通调用器。
	//
	// ⚠ 调用器的位置是**搬到预测点**去的（同上：Position = 组件位置 + 速度 ×
	// PredictionTime），不是「两个点都罩住」—— 所以**每一个范围**都必须自己罩得住
	// 那个偏移量：硬约束是「范围 ≥ 启用期间的最大速度 × PredictionTime」。
	// 高速门控（VoxelInvokerSuspendSpeedKmPerSecond = 0.6 km/s = 6e4 厘米/秒）
	// 保证调用器只在低速时开着，按这个速度算偏移：
	//   细气泡 0.03 s → 1.8e3 厘米（18 米）≤ 两个范围（30 / 80 米）；
	//   LOD3  0.1  s → 6e3 厘米（60 米）≤ 300 米；
	//   LOD5  0.2  s → 1.2e4 厘米（120 米）≤ 1000 米。
	// 不门控时最大速度含 BoostMultiplier（3.6e5 厘米/秒），0.03 s 就要 108 米；
	// 巡航（关阻尼）下更是无上界 —— 任何有限范围都盖不住。那时只能把调用器关掉。
	//
	// ⚠⚠ **真正卡住上限的不是插件的块数安全阀，是显存。** 插件那个
	// voxel.renderer.MaxRenderOctreeChunks（默认 1e6）只在超限时把整棵八叉树作废
	// （静默失效、地形 LOD 冻结），它**不保证**上限以内的配置跑得动。2026-10-18 实测：
	// 本关卡世界边长 4096 体素、RENDER_CHUNK_SIZE = 32，取 LODRange = 4.5e5 时八叉树
	// 是**通过**的，却请求了约 3.3e5 个区块 —— 在 GTX 1650（4 GB）上直接报
	// 「Video memory has been exhausted (52,945 MB over budget)」，星球整片渲染不出来。
	// **范围要按显卡的显存反推，不能只按「别撞插件上限」反推。**
	//
	// 两条细分路径互相独立，块数各按自己盒子的**体积**涨，且**都不看盒子里有没有
	// 地形**：
	//
	//   (1) LOD —— FVoxelRenderOctree::ShouldSubdivideByDistance。与 ±LODRange 盒子
	//       相交的块细到 LODToSet 为止。叶块边长 32 · 2^LODToSet 体素。
	//   (2) 碰撞 —— ShouldSubdivideByOthers → IsInvokerInRange。与 ±CollisionsRange
	//       盒子相交的块**一路细到 Height 0**，**完全不经过 LODToSet**：
	//       IsInvokerInRange 只做一次包围盒相交测试，没有任何 LOD 判断。
	//
	// 块数都 ≈ (min(2·范围, 世界边长) / 叶块边长)³，父块再加约 14%
	// （1/8 + 1/64 + … = 1/7）。
	//
	// 每次 LOD 更新（默认 MinDelayBetweenLODUpdates = 0.1 秒一次）都会把两个盒子里
	// 新增的块整批丢进网格化队列。FVoxelDefaultRenderer::UpdateLODs 在游戏线程同步
	// 跑，**不吃 MeshUpdatesBudget**（那个预算只盖 MeshHandler->Tick 与
	// ProcessMeshUpdates），所以盒子越大、飞船越快，队列涨得越快、掉帧越狠。
	// 2026-10-21 定位：**远离行星照样卡，与地表距离无关** —— 飞船始终在行星的
	// 65.5 km 体素世界内，盒子随飞船平移，速度越快、每次更新扫过的叶块越多。
	// 现在细气泡两份各 ≈ 16 / 7 块，并加了速度门控（见 Tick 里的
	// VoxelInvokerSuspendSpeedKmPerSecond）。
	//
	// 关键取舍：**LODToSet 每加一档，叶块边长翻倍、块数少 8 倍。** 所以「覆盖半径」
	// 与「网格精度」是在**固定块数**下互换的 —— 想看得远就得让网格变粗：
	//
	//     LODToSet = 0 → 1 米网格（本关卡体素 100 厘米）；= 1 → 2 米；= 2 → 4 米
	//
	// 现在取 LODToSet = 1（2 米网格）+ LODRange = 80 米：叶块 64 米，
	// 块数 ≈ (160/64)³ ≈ 16。同样的盒子在 LODToSet = 0 时要 ≈ 1.3e2 块 —— 8 倍。
	// 4 GB 显存下这是可持续的取舍；飞船在空中高速掠过，脚下 2 米网格够用。真需要
	// 1 米网格贴脸时由角色调用器接手（PlanetCharacter.cpp：LODToSet = 0 + 200 米）。
	// 想要更大的 LOD 覆盖，就把 LODToSet 加一档、同时把 LODRange 翻倍（块数不变）。
	//
	// 碰撞那份**锁死在 Height 0，不能跟着 LODRange 一起放大**，否则它单独就把块数顶爆。
	// 现在取 ±30 米（罩住 0.03 秒预测偏移 18 米），块数 ≈ (60/32)³ ≈ 7，
	// 比 LOD 那份还小。
	// 可见区块本来就带碰撞（VoxelWorld 的 bComputeVisibleChunksCollisions 默认 true、
	// VisibleChunksCollisionsMaxLOD = 5），调用器这一份只是把飞船附近升级到最细。注意
	// 这些 Height-0 块**不参与渲染**（ByOthers 的子块 bChildrenVisible = false），所以
	// 它们只买碰撞精度，不买画质。
	//
	// **调参后要看游戏里的「Total tasks remaining」**（HUD 上直接显示）—— 那是当前待
	// 网格化的区块数，是唯一可靠的实测口径。它到几万就要警惕，几十万必炸显存。
	// 角色那份（PlanetCharacter.cpp）配 LODToSet = 0 + 200 米盒子，同理：盒子小才用得起。
	// 三个调用器都用 AlwaysLocal 版：进星图 = Possess(星图相机)，普通调用器会在
	// 那一帧全部掉出 LOD，强制整棵八叉树重建。详见 AlwaysLocalVoxelInvokerComponent.h。
	VoxelInvoker = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvoker"));
	VoxelInvoker->SetupAttachment(Hull);
	VoxelInvoker->bUseForLOD = true;
	VoxelInvoker->LODToSet = 1;
	VoxelInvoker->LODRange = 8000.f;
	VoxelInvoker->bUseForCollisions = true;
	VoxelInvoker->CollisionsRange = 3000.f;
	VoxelInvoker->bUseForNavmesh = false;
	VoxelInvoker->bUseForEvents = false;
	VoxelInvoker->bUseForPriorities = false;
	VoxelInvoker->bEnablePrediction = true;
	VoxelInvoker->PredictionTime = 0.03f;

	// ---- 中间 LOD 环：见头文件声明处的说明 ----
	//
	// 预测时间受硬约束「范围 ≥ 速度 × PredictionTime」。速度门控
	// （VoxelInvokerSuspendSpeedKmPerSecond = 0.6 km/s = 6e4 厘米/秒）保证调用器
	// 只在低速时开着，按这个上限算：
	//   - 细气泡 @ 0.03 秒：偏移 1.8e3 厘米（18 米）≤ 碰撞 3e3 / LOD 8e3；
	//   - LOD3 @ 0.1 秒：偏移 6e3 厘米（60 米）≤ 范围 3e4；
	//   - LOD5 @ 0.2 秒：偏移 1.2e4 厘米（120 米）≤ 范围 1e5。
	VoxelInvokerLOD3 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD3"));
	VoxelInvokerLOD3->SetupAttachment(Hull);
	VoxelInvokerLOD3->bUseForLOD = true;
	VoxelInvokerLOD3->LODToSet = 3;
	VoxelInvokerLOD3->LODRange = 30000.f;
	VoxelInvokerLOD3->bUseForCollisions = false;
	VoxelInvokerLOD3->bUseForNavmesh = false;
	VoxelInvokerLOD3->bUseForEvents = false;
	VoxelInvokerLOD3->bUseForPriorities = false;
	VoxelInvokerLOD3->bEnablePrediction = true;
	VoxelInvokerLOD3->PredictionTime = 0.1f;

	VoxelInvokerLOD5 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD5"));
	VoxelInvokerLOD5->SetupAttachment(Hull);
	VoxelInvokerLOD5->bUseForLOD = true;
	VoxelInvokerLOD5->LODToSet = 5;
	VoxelInvokerLOD5->LODRange = 100000.f;
	VoxelInvokerLOD5->bUseForCollisions = false;
	VoxelInvokerLOD5->bUseForNavmesh = false;
	VoxelInvokerLOD5->bUseForEvents = false;
	VoxelInvokerLOD5->bUseForPriorities = false;
	VoxelInvokerLOD5->bEnablePrediction = true;
	VoxelInvokerLOD5->PredictionTime = 0.2f;

	// 1 公里外的过渡环，与星图相机同一档：叶块 1.02 公里（行星 3 体素 50 cm）。
	// 行星的 MaxLOD 把全星压到 LOD 7（2.05 公里）之后，LOD5（512 米）到它就只差
	// 这一档。范围推到地平线之外（20 公里半径的星，1 公里高度地平线 6.4 公里），
	// 台阶落到看不见的地方，不会横在画面中间。
	// 预测约束：3.6e5 厘米/秒 × 0.2 s = 72000 ≤ 1000000（7%），留得出余量。
	VoxelInvokerLOD6 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD6"));
	VoxelInvokerLOD6->SetupAttachment(Hull);
	VoxelInvokerLOD6->bUseForLOD = true;
	VoxelInvokerLOD6->LODToSet = 6;
	VoxelInvokerLOD6->LODRange = 1000000.f;
	VoxelInvokerLOD6->bUseForCollisions = false;
	VoxelInvokerLOD6->bUseForNavmesh = false;
	VoxelInvokerLOD6->bUseForEvents = false;
	VoxelInvokerLOD6->bUseForPriorities = false;
	VoxelInvokerLOD6->bEnablePrediction = true;
	VoxelInvokerLOD6->PredictionTime = 0.2f;

	// 没有移动组件：位移完全归 N 体求解器（见类注释）。曾经的 UFloatingPawnMovement
	// 会每帧 MoveUpdatedComponent(Velocity * dt)，留着它就和求解器的写回抢着改位置
	// —— 而且「不喂它输入」也挡不住，它照样按残留速度搬人。

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Hull);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	// 继承横滚：相机与机身刚性绑定，按 A/D 压弯时画面跟着侧倾。
	//
	// 曾经是 false（"画面不该跟着倒过来"），代价是相机对机身姿态的响应不完整 ——
	// 横滚是玩家唯一能直接看见的转弯反馈，剥掉它等于按 A/D 时画面纹丝不动。
	//
	// 另一个更隐蔽的问题：bInheritRoll 剥的是**世界坐标**的 roll 分量，而飞船的
	// "水平"由 GetAutoLevelUp() 对齐到行星球面法线（见 Tick 第 3 段）。两者只在
	// 赤道重合，于是飞船在赤道以外平飞、机身一点没滚时，相机的地平线却是歪的。
	// 改成 true 后相机与机身完全一致，这个偏差自然消失。
	//
	// 横滚由玩家独占：AutoLevelSpeed 默认 0（关闭自动回正），按住 A/D 就是完整的
	// MaxRollRate 120°/s，松手后也不再自动拉回 —— 转了就转了。
	SpringArm->bInheritRoll = true;
	SpringArm->TargetArmLength = 1800.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 400.f);
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
}

void ASpaceshipPawn::ApplyHullMesh()
{
	// 只换网格，不碰变换。
	//
	// 位置/朝向/缩放全部归蓝图：在蓝图视口里选中 Hull 拖 Gizmo，值就存在蓝图
	// 资产里（关卡实例上也能单独调）。这里每编译一次都会被 OnConstruction 重跑，
	// 若在此写变换，蓝图里拖好的值会被反复冲回去，表现就是"模型怎么都调不动"。
	if (Hull && HullMesh)
	{
		Hull->SetStaticMesh(HullMesh);
	}

	// HullMesh 为空：什么都不做，而不是退回占位立方体。
	//
	// 空的语义是「网格体交给蓝图自己决定」—— 蓝图里覆盖的模型、或构造期塞进去的
	// 占位立方体都保持原样。这里若强行重置，蓝图里选好的模型会在每次编译时被
	// 清掉，那是比"属性没生效"更难查的坑。
}

void ASpaceshipPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 网格体在这里套用，而不是构造函数里：OnConstruction 是编辑器里能读到蓝图
	// 覆盖值的第一个时机，在蓝图 Class Defaults 里改 HullMesh 立刻能在视口看到。
	// 这里只换网格 —— 位置/朝向/缩放归蓝图，见 ApplyHullMesh 的注释。
	ApplyHullMesh();
	RebuildContactProbes();

#if WITH_EDITOR
	// 让飞船退出 World Partition 的空间加载。
	//
	// 按空间加载的 Actor 会被烘进流送单元，PIE 启动后随流送被卸载。实测开局登船那
	// 5 秒重试窗口里 FindNearestTo 一艘船都找不到（同一时刻关卡里明明摆着一艘），
	// 于是玩家拿到的是角色而不是飞船。行星 Actor 早就用同样的办法退出了流送
	// （见 AVoxelPlanetActor::OnConstruction 的 bAlwaysLoaded），这里保持一致。
	//
	// CanChangeIsSpatiallyLoadedFlag 在 Actor 已经烘进流送单元后会返回 false，
	// 那种情况只能由人在细节面板里手动改，这里不做无声的失败重试。
	if (GetIsSpatiallyLoaded() && CanChangeIsSpatiallyLoadedFlag())
	{
		Modify();
		SetIsSpatiallyLoaded(false);
	}
#endif
}

void ASpaceshipPawn::BeginPlay()
{
	Super::BeginPlay();

	// 再套一次：关卡里实例化出来的蓝图可能覆盖过 HullMesh，而实例的
	// OnConstruction 未必在属性就位之后跑过。重复调用是幂等的（只换网格）。
	ApplyHullMesh();
	RebuildContactProbes();

	// 自动回正需要一个重力参考：贴着行星时对齐到球面法线，太空中对齐到世界 Z。
	// 注意 AutoLevelSpeed 默认已为 0（自动回正关闭），这里查出来的 PlanetActor 因此
	// 暂时没人用 —— 保留是为了把该属性调回正数时能立刻生效，不必改代码。
	PlanetActor = FindNearestPlanet();

	// 抄下基准臂长：Tick 里的相机后缩量全部在它之上叠加。不写死 1800，
	// 因为蓝图或关卡实例可能改过 TargetArmLength，抄一次才不会把它覆盖掉。
	BaseArmLength = SpringArm ? SpringArm->TargetArmLength : 0.f;

	// 相机横滚滞后要用的两个基准量：
	//   - CameraBaseRotation：蓝图可能给相机设过相对朝向，Tick 每帧要往它上面叠
	//     横滚偏移，不抄一份就会把蓝图调好的值冲掉。
	//   - PreviousRotation：Tick 靠"本帧朝向 × 上一帧朝向的逆"差分出机身滚了多少，
	//     没有上一帧就无从差分。**必须在这里初始化** —— 留到第一次 Tick 才赋值的话，
	//     差分从单位四元数起算，开局摆位那一次旋转会被算成一次巨大的横滚。
	CameraBaseRotation = Camera ? Camera->GetRelativeRotation().Quaternion() : FQuat::Identity;
	PreviousRotation = GetActorQuat();

	// 落地自由视角绕着摇臂枢轴转，基准朝向同样抄一份：蓝图可能调过摇臂的相对朝向，
	// 每帧叠加偏移而不是直接覆盖，才不会把那个值冲掉。
	SpringArmBaseRotation = SpringArm ? SpringArm->GetRelativeRotation().Quaternion() : FQuat::Identity;

	// 调用器在 OnRegister 里已经自动启用了，这里按「还不是视点」关掉。
	// 飞船在 PIE 开局就生成、由玩家登上后才 Possess，这个窗口里它不该参与 LOD。
	ApplyViewpointInvokerState();
}

AVoxelPlanetActor* ASpaceshipPawn::FindNearestPlanet() const
{
	AVoxelPlanetActor* Nearest = nullptr;
	float NearestDistSq = FMath::Square(PlanetSearchRadius);

	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = *It;
		}
	}
	return Nearest;
}

FVector ASpaceshipPawn::GetAutoLevelUp() const
{
	if (PlanetActor)
	{
		const FVector Radial =
			(GetActorLocation() - PlanetActor->GetActorLocation()).GetSafeNormal();
		if (!Radial.IsNearlyZero())
		{
			return Radial;
		}
	}
	return FVector::UpVector;
}

FVector ASpaceshipPawn::ResolveVerticalThrustUp(
	const FVector& ShipLocation,
	const FVector& PlanetLocation,
	bool bHasPlanet,
	const FVector& BodyUp)
{
	// 与 GetAutoLevelUp 的区别只在退化回退：这里退回**机身本地 up**，
	// 而不是世界 Z —— 太空中横滚 / 俯仰过的飞船，升降键应沿机身背向。
	if (bHasPlanet)
	{
		const FVector Radial = (ShipLocation - PlanetLocation).GetSafeNormal();
		if (!Radial.IsNearlyZero())
		{
			return Radial;
		}
	}
	return BodyUp;
}

void ASpaceshipPawn::SetVoxelInvokerCollisionEnabled(const bool bEnabled)
{
	if (!VoxelInvoker)
	{
		return;
	}

	// 只改碰撞通道。bUseForLOD 必须保持 true —— 关掉 LOD 之后飞船身边只剩
	// 行星球心调用器的轨道 LOD（约 LOD 8），星球会明显变糊。
	// 插件在下一次 LOD 更新时比较新旧 bUseForCollisions，变化后自行重建。
	VoxelInvoker->bUseForCollisions = bEnabled;
}

void ASpaceshipPawn::SetViewpointInvokersActive(const bool bActive)
{
	bIsViewpoint = bActive;
	ApplyViewpointInvokerState();
}

void ASpaceshipPawn::ApplyViewpointInvokerState()
{
	// 高速门控只改 bUseForCollisions，与这里的整体启停正交，不参与合并。
	VoxelViewpointInvokers::SetEnabled(VoxelInvoker, bIsViewpoint);
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD3, bIsViewpoint);
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD5, bIsViewpoint);
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD6, bIsViewpoint);
}

void ASpaceshipPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 行星在动，最近的一颗会换人。飞船不需要像角色那样重新摆位 ——
	// PlanetActor 只喂给 GetAutoLevelUp() 和升降推力方向，换个参考方向就行。
	if (PlanetReevaluateInterval > 0.f)
	{
		PlanetReevaluateTimer -= DeltaSeconds;
		if (PlanetReevaluateTimer <= 0.f)
		{
			PlanetReevaluateTimer = PlanetReevaluateInterval;
			PlanetActor = FindNearestPlanet();
		}
	}

	// --- 0. 细气泡的碰撞通道按（行星系）速度启停 ---
	//
	// 远离行星仍卡的原因不是距离：飞船始终在行星的 65.5 km 体素世界内，调用器
	// 的盒子随飞船平移，每个 LOD 更新周期（≥ 0.1 秒）都要把扫过的块网格化，
	// 块数 × 速度 就是游戏线程上的无界开销。
	//
	// 只关碰撞，不关 LOD：碰撞路径会强制 Height 0（32 m 块），是两块开销里
	// 更贵的一路；LOD 只细到 LODToSet，而且关掉后身边只剩行星球心调用器的
	// 轨道 LOD（约 LOD 8），星球会变糊。速度超过阈值关碰撞，掉回低阈值再开
	// （回差 + 冷却，避免在阈值上反复重建）。
	//
	// 阈值按 **相对行星** 的速度算：行星自己随求解器每秒走几十公里，船贴着
	// 地面停着时 GetVelocity() 的绝对值也不小。逐帧位移相减即可，不必让
	// StellarEraSpaceship 反向依赖 StellarEra 去查求解器。
	const AVoxelPlanetActor* const SamplePlanet = PlanetActor;
	const FVector PlanetLocation = SamplePlanet ? SamplePlanet->GetActorLocation() : FVector::ZeroVector;
	const FVector ShipLocation = GetActorLocation();

	if (VoxelInvokerToggleTimer > 0.f)
	{
		VoxelInvokerToggleTimer = FMath::Max(0.f, VoxelInvokerToggleTimer - DeltaSeconds);
	}
	else if (VoxelInvokerSuspendSpeedKmPerSecond > 0.f
		&& bHasVoxelInvokerSpeedSample
		&& LastVoxelInvokerPlanet == SamplePlanet
		&& DeltaSeconds > SMALL_NUMBER)
	{
		const FVector RelativeDisplacement =
			(ShipLocation - LastVoxelInvokerShipLocation) - (PlanetLocation - LastVoxelInvokerPlanetLocation);
		const float RelativeSpeedKmPerSecond =
			RelativeDisplacement.Size() / DeltaSeconds / CentimetersPerKilometer;
		const float ResumeSpeed =
			FMath::Min(VoxelInvokerResumeSpeedKmPerSecond, VoxelInvokerSuspendSpeedKmPerSecond);

		if (!bVoxelInvokerCollisionSuspended && RelativeSpeedKmPerSecond > VoxelInvokerSuspendSpeedKmPerSecond)
		{
			bVoxelInvokerCollisionSuspended = true;
			VoxelInvokerToggleTimer = VoxelInvokerToggleCooldownSeconds;
			SetVoxelInvokerCollisionEnabled(false);
			UE_LOG(LogTemp, Display,
				TEXT("[Ship] 细气泡碰撞关闭（LOD 保留）：相对行星 %.2f km/s > %.2f"),
				RelativeSpeedKmPerSecond, VoxelInvokerSuspendSpeedKmPerSecond);
		}
		else if (bVoxelInvokerCollisionSuspended && RelativeSpeedKmPerSecond < ResumeSpeed)
		{
			bVoxelInvokerCollisionSuspended = false;
			VoxelInvokerToggleTimer = VoxelInvokerToggleCooldownSeconds;
			SetVoxelInvokerCollisionEnabled(true);
			UE_LOG(LogTemp, Display,
				TEXT("[Ship] 细气泡碰撞开启：相对行星 %.2f km/s < %.2f"),
				RelativeSpeedKmPerSecond, ResumeSpeed);
		}
	}

	// 采样每帧都更新，冷却期间也一样 —— 否则冷却结束后的第一帧会把 0.5 秒的
	// 位移当成一帧，量出离谱的速度。
	LastVoxelInvokerShipLocation = ShipLocation;
	LastVoxelInvokerPlanetLocation = PlanetLocation;
	LastVoxelInvokerPlanet = PlanetActor;
	bHasVoxelInvokerSpeedSample = SamplePlanet != nullptr;

	// --- 0. 落地自由视角判定 ---
	//
	// 落地且没有起飞方向的输入时，鼠标改为环绕停住的飞船，而不是转飞船。
	// IsLanded() 是一次全向球重叠（与按 E 下船同一口径），每帧一次，开销可忽略。
	const bool bWasFreeLookActive = bFreeLookActive;
	bFreeLookActive = ShouldUseFreeLook(IsLanded(), ThrustInput, VerticalInput);

	// 刚进入：清掉残余角速度。否则落地那一刻的最后一甩会在自由视角里继续带着船转，
	// 与「落地即冻结」矛盾。
	if (bFreeLookActive && !bWasFreeLookActive)
	{
		YawRate = 0.f;
		PitchRate = 0.f;
	}

	// --- 1. 角速度衰减 ---
	// 鼠标给的是冲量（见 Look），这里让角速度平滑地衰减回 0。两者的平衡
	// 就是“甩一下鼠标，飞船转一会儿再停下”的手感。
	YawRate = FMath::FInterpTo(YawRate, 0.f, DeltaSeconds, AngularDamping);
	PitchRate = FMath::FInterpTo(PitchRate, 0.f, DeltaSeconds, AngularDamping);

	// --- 2. 应用旋转（落地自由视角时整体冻结） ---
	//
	// 自由视角里船是停着的：鼠标分给相机，A/D 横滚也不该把船带着滚。
	// 起飞输入一出现（第 0 段判定为假），下一帧恢复。
	if (!bFreeLookActive)
	{
		FRotator DeltaRotation(PitchRate * DeltaSeconds, YawRate * DeltaSeconds, 0.f);
		DeltaRotation.Roll = RollInput * MaxRollRate * DeltaSeconds;
		AddActorLocalRotation(DeltaRotation);
	}

	// --- 3. 自动回正：只把横滚拉回水平，俯仰留给玩家（自由视角时同样冻结） ---
	if (!bFreeLookActive && AutoLevelSpeed > 0.f)
	{
		const FQuat Current = GetActorQuat();
		const FVector Forward = Current.GetForwardVector();
		// 参考“上”投影到垂直于机身前向的平面，就是机身“上”应该朝向的方向。
		// 参考方向贴着行星时是球面法线，太空中才是世界 Z —— 若一律用世界 Z，
		// 飞船在赤道附近会直接滚转 90°，落地射线随之指偏，IsLanded() 永远为假。
		FVector DesiredUp = FVector::VectorPlaneProject(GetAutoLevelUp(), Forward);
		if (DesiredUp.Normalize())
		{
			const FQuat Correction = FQuat::FindBetweenNormals(Current.GetUpVector(), DesiredUp);
			const float AngleRad = Correction.GetAngle();
			if (AngleRad > KINDA_SMALL_NUMBER)
			{
				// 限制每秒最多转 AutoLevelSpeed 度，避免生硬地瞬间弹正。
				const float MaxRad = FMath::DegreesToRadians(AutoLevelSpeed) * DeltaSeconds;
				const float Alpha = FMath::Min(1.f, MaxRad / AngleRad);
				AddActorWorldRotation(FQuat::Slerp(FQuat::Identity, Correction, Alpha));
			}
		}
	}

	// --- 4. 推力 ---
	//
	// 推力不再由这里施加 —— 本类只把「想施加多少」暴露出去
	// （GetThrustAccelerationWorldCmPerS2 / GetDampingTime），由 UCosmosSimulationSubsystem
	// 每帧下来读走、换算、写进求解器。
	//
	// 本段只剩相机后缩要用的 ThrustInput，见第 5 段。

	// --- 5. 按 W 时相机后缩 ---
	// 驱动量就是**推力输入本身**，不是速度也不是加速度：按住 W 就后缩，松开就回位；
	// 按 Shift 时再乘一个系数，缩得更远。
	//
	// 曾经用「速度大小的变化率」驱动（想让速度平稳后自动回位），但那样按住 W 匀速
	// 飞行时相机是回位的 —— 与「按住 W 就拉长」的直觉不符。改成输入驱动后，后缩量
	// 只取决于当前按着什么键：匀速飞、加速飞、贴着 MaxSpeed 飞，后缩量完全一样。
	//
	// 用 FInterpTo 而不是直接赋值：松手时是平滑收回，不是瞬间弹回。
	//
	// 只认正向输入：按 S 制动，不后缩。
	const float TargetPullback = ThrustInput > 0.f
		? CameraPullbackAtFullThrust * (bBoosting ? BoostPullbackMultiplier : 1.f)
		: 0.f;
	CameraPullback = FMath::FInterpTo(
		CameraPullback, TargetPullback, DeltaSeconds, CameraPullbackInterpSpeed);

	// 滚轮缩放（第 6 段）：平滑追向目标偏移，与后缩量**相加**后一起写进摇臂长度。
	//
	// 两个量必须在这里合并、并且只在这里写 TargetArmLength：谁要是在别处直接
	// 赋值给 TargetArmLength，下一帧就会被这一行冲掉，表现为"滚轮/后缩时有时无"。
	// 滚轮改的是 TargetZoomOffset（见 AddZoom），本函数每帧把它平滑成 ZoomOffset。
	ZoomOffset = FMath::FInterpTo(
		ZoomOffset, TargetZoomOffset, DeltaSeconds, ZoomInterpSpeed);

	if (SpringArm)
	{
		SpringArm->TargetArmLength = FMath::Clamp(
			BaseArmLength + ZoomOffset + CameraPullback, MinArmLength, MaxArmLength);
	}

	// --- 6. 相机横滚滞后 ---
	// SpringArm 的 bInheritRoll = true 让相机与机身刚性绑定（这是刻意的，见构造函数：
	// 横滚是玩家唯一能直接看见的转弯反馈）。代价是按一下 A/D 画面立刻跟着倒，很硬。
	//
	// 这里让相机慢一拍：机身每帧滚出来的角度先累进 CameraRollLag，再让它指数衰减回 0，
	// 相机则**反向**转这么多。净效果是相机横滚一直在追机身，但始终落后一点：
	//   点一下 A/D   → 机身冲得快、lag 来不及衰减，画面几乎不动；
	//   持续压杆     → lag 稳定在 MaxRollRate / CameraRollFollowSpeed 度；
	//   松手         → lag 衰减到 0，画面补齐，最终与机身完全一致（无残留偏差）。
	//
	// 用**实际旋转差**而不是 RollInput * MaxRollRate：后者只在横滚完全由输入驱动时
	// 成立，而 AutoLevelSpeed 一旦调回正数（那个属性就是留着复活的）也会滚机身。
	const FQuat CurrentRotation = GetActorQuat();
	{
		// 机身本帧相对上一帧多转了多少，表达在上一帧的机身坐标系里。
		const FQuat RotationDelta = PreviousRotation.Inverse() * CurrentRotation;

		// 只取绕机身前方（局部 X）的分量。小角度下四元数的向量部分 ≈ 轴 × (θ/2)，
		// 所以 2 × X 分量就是转角（弧度）。比 GetAngle() 稳：后者走 acos，
		// 在 θ→0 时精度塌陷，而这里每帧转角只有 2° 上下。
		float RollDelta = FMath::RadiansToDegrees(2.f * RotationDelta.X);

		// 单帧滚过 45° 只可能是开局摆位或传送，不是玩家压杆（120°/s 在 5fps 下也才
		// 24°/帧）。夹一下，免得那一下把画面甩飞再慢慢收回来。
		RollDelta = FMath::Clamp(RollDelta, -45.f, 45.f);

		CameraRollLag += RollDelta;
	}
	PreviousRotation = CurrentRotation;

	// CameraRollFollowSpeed <= 0 时 FInterpTo 直接返回目标值 0，等价于关掉滞后。
	CameraRollLag = FMath::FInterpTo(CameraRollLag, 0.f, DeltaSeconds, CameraRollFollowSpeed);

	if (Camera)
	{
		// 绕相机自身 X 轴（视线方向，与机身前方重合）反向转 lag，抵消掉相机从机身
		// 多继承的那部分横滚。四元数左乘基准朝向 = 先在相机局部空间里转，再套蓝图
		// 设的朝向。相机只在原地转，位置不动（SocketOffset 不跟着甩）。
		Camera->SetRelativeRotation(
			CameraBaseRotation * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-CameraRollLag)));
	}

	// --- 7. 落地自由视角：相机绕停住的飞船环绕 ---
	//
	// 偏移量在自由视角期间由 Look() 累加（见 ApplyFreeLookLookInput）。转的是摇臂
	// 而不是相机：摇臂的枢轴就是飞船根节点，所以镜头绕船环绕；相机自身的相对朝向
	// 仍留给第 6 段的横滚补偿。
	//
	// 退出（按 W / Space）后偏移量平滑回 0，镜头滑回船尾正后方。每帧写一次基准朝向
	// 的叠加值：上一次写入可能还带着未归零的小偏移，这样退出后能精确回到基准。
	if (!bFreeLookActive)
	{
		FreeLookYaw = FMath::FInterpTo(FreeLookYaw, 0.f, DeltaSeconds, FreeLookReturnSpeed);
		FreeLookPitch = FMath::FInterpTo(FreeLookPitch, 0.f, DeltaSeconds, FreeLookReturnSpeed);

		// FInterpTo 只会无限逼近 0，永远到不了；低于 0.01° 直接归零，让回位真正结束。
		if (FMath::Abs(FreeLookYaw) < 0.01f)
		{
			FreeLookYaw = 0.f;
		}
		if (FMath::Abs(FreeLookPitch) < 0.01f)
		{
			FreeLookPitch = 0.f;
		}
	}

	if (SpringArm)
	{
		SpringArm->SetRelativeRotation(
			SpringArmBaseRotation * FRotator(FreeLookPitch, FreeLookYaw, 0.f).Quaternion());
	}
}

void ASpaceshipPawn::SetThrust(float Value)
{
	ThrustInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::SetRoll(float Value)
{
	RollInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::SetVertical(float Value)
{
	VerticalInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::SetBoost(bool bValue)
{
	bBoosting = bValue;
}

void ASpaceshipPawn::AddZoom(float Value)
{
	// 夹的是**目标偏移**而不是最终臂长。只夹最终值的话，玩家滚过头会攒下一堆
	// 被 Clamp 吃掉的无效偏移，往回滚要先"还债"——前几格看起来像没反应。
	//
	// 区间由 MinArmLength / MaxArmLength 相对基准臂长换算。基准臂长在 BeginPlay
	// 里抄自组件，所以蓝图或关卡实例改过 TargetArmLength 也算数。
	const float MinOffset = MinArmLength - BaseArmLength;
	// 防御 MinArmLength > MaxArmLength 的误配：Clamp 要求 Min <= Max。
	const float MaxOffset = FMath::Max(MaxArmLength - BaseArmLength, MinOffset);

	// 取负：Value > 0 是滚轮上滚 = 拉近 = 摇臂变短。
	TargetZoomOffset = FMath::Clamp(TargetZoomOffset - Value * ZoomStep, MinOffset, MaxOffset);
}

void ASpaceshipPawn::TogglePreserveVelocity()
{
	bPreserveVelocity = !bPreserveVelocity;
}

FSpaceshipStatus ASpaceshipPawn::GetStatus() const
{
	FSpaceshipStatus Status;

	// 绝对速度取自求解器写回的值（GetVelocity 的覆写读的就是它）。
	// 相对速度减去参考行星的中心速度：大气与地表都以行星为参考系。
	// 世界厘米/秒 → 世界 km/s。
	Status.SpeedKmPerSecond = GetVelocity().Size() / CentimetersPerKilometer;
	Status.RelativeSpeedKmPerSecond = GetRelativeVelocity().Size() / CentimetersPerKilometer;
	Status.bHasReferencePlanet = PlanetActor != nullptr;

	// 实际极速（世界 km/s）= 阻尼极速（推力 × τ，巡航关着时有效）与**当前生效的**
	// 硬上限里较小且非 0 的那个；两个都没设才是 0（无上限）。
	// 硬上限按 Shift 分两档：按住时用 BoostMaxSpeedKmPerSecond，松开用
	// MaxSpeedKmPerSecond —— HUD 印的必须是玩家当前真正受着的那一条。
	const float Damping = GetDampingTime();
	const float DampingTopSpeed = Damping > 0.f
		? ThrustAccelerationKmPerS2 * (bBoosting ? BoostMultiplier : 1.f) * Damping
		: 0.f;

	const float ActiveMaxSpeedKmPerSecond =
		bBoosting ? BoostMaxSpeedKmPerSecond : MaxSpeedKmPerSecond;

	float EffectiveMaxSpeed = DampingTopSpeed;
	if (ActiveMaxSpeedKmPerSecond > 0.f)
	{
		EffectiveMaxSpeed = EffectiveMaxSpeed > 0.f
			? FMath::Min(EffectiveMaxSpeed, ActiveMaxSpeedKmPerSecond)
			: ActiveMaxSpeedKmPerSecond;
	}
	Status.MaxSpeedKmPerSecond = EffectiveMaxSpeed;

	Status.ThrustInput = ThrustInput;
	Status.bBoosting = bBoosting;
	Status.bPreserveVelocity = bPreserveVelocity;
	Status.bLanded = IsLanded();
	Status.MaxThrustAccelerationKmPerS2 =
		ThrustAccelerationKmPerS2 * (bBoosting ? BoostMultiplier : 1.f);

	// 大气：密度比每帧都算（HUD 用它区分真空 / 大气），阻力大小只在有系数时非零。
	Status.AtmosphereDensityRatio = GetAtmosphereDensityRatio();
	Status.AtmosphereDragKmPerS2 =
		GetAtmosphereDragAccelerationWorldCmPerS2().Size() / CentimetersPerKilometer;

	return Status;
}

bool ASpaceshipPawn::ShouldUseFreeLook(bool bLanded, float InThrustInput, float InVerticalInput)
{
	// W（+1 推力）/ Space（+1 升降）是起飞方向；S / Ctrl 给负值，落地时只是
	// 制动 / 下降，不退出自由视角。
	return bLanded && InThrustInput <= 0.f && InVerticalInput <= 0.f;
}

void ASpaceshipPawn::ApplyFreeLookLookInput(
	float DeltaYaw, float DeltaPitch, float MinPitch, float MaxPitch,
	float& InOutYaw, float& InOutPitch)
{
	InOutYaw = FRotator::NormalizeAxis(InOutYaw + DeltaYaw);

	// 防御 MinPitch > MaxPitch 的误配：Clamp 要求下限不大于上限。
	InOutPitch = FMath::Clamp(InOutPitch + DeltaPitch,
		FMath::Min(MinPitch, MaxPitch), FMath::Max(MinPitch, MaxPitch));
}

void ASpaceshipPawn::Look(float DeltaYaw, float DeltaPitch)
{
	// 落地自由视角：鼠标转的是相机，不是飞船。偏移量在第 7 段平滑回位。
	if (bFreeLookActive)
	{
		ApplyFreeLookLookInput(DeltaYaw, DeltaPitch,
			MinFreeLookPitch, MaxFreeLookPitch, FreeLookYaw, FreeLookPitch);
		return;
	}

	// 冲量模型：鼠标位移直接累加进角速度。甩一下鼠标，飞船会转一会儿再
	// 慢慢停下，而不是跟着鼠标立刻静止——这就是街机飞行的惯性感。
	YawRate = FMath::Clamp(YawRate + DeltaYaw * MouseImpulseScale, -MaxYawRate, MaxYawRate);
	PitchRate = FMath::Clamp(PitchRate + DeltaPitch * MouseImpulseScale, -MaxPitchRate, MaxPitchRate);
}

float ASpaceshipPawn::GetLandingQueryRadiusCm() const
{
	// 球心固定在机身中心（全向，不依赖机身横滚），但半径不能是常数：求解器的
	// 接触形状是 Hull 凸包探针，飞船是**船底着地**，机身中心因此比船底高出一整个
	// 船体深度（本资源约 1000 cm）。固定 600 cm 的球从机身中心出发根本够不到地面，
	// IsLanded() 恒为假 —— 落地后鼠标仍在转飞船、按 E 也下不了船。
	//
	// 所以半径 = 船体沿**重力方向**的最低点到机身中心的距离 + LandingProbeDistance。
	// 重力方向取 GetAutoLevelUp() 的反向（贴行星时是球心径向，太空中是世界 Z），
	// 与机身横滚无关；探针先过机身变换，滚成什么样最低点都算得对。
	const FVector Down = -GetAutoLevelUp();
	const FVector ActorLocation = GetActorLocation();
	const FTransform ActorTransform = GetActorTransform();

	double HullDepthCm = 0.0;
	for (const FVector& Probe : ContactProbesLocalCm)
	{
		const FVector WorldProbe = ActorTransform.TransformPosition(Probe);
		HullDepthCm = FMath::Max(HullDepthCm,
			double(FVector::DotProduct(WorldProbe - ActorLocation, Down)));
	}

	// 没有凸包探针时求解器退回 GetContactRadiusCm() 的球：球心在机身中心，
	// 球面就是接触面，深度即半径。
	if (ContactProbesLocalCm.Num() == 0)
	{
		HullDepthCm = GetContactRadiusCm();
	}

	return float(HullDepthCm + FMath::Max(LandingProbeDistance, 0.f));
}

bool ASpaceshipPawn::IsLanded() const
{
	if (GetVelocity().Size() > LandingSpeedThreshold)
	{
		return false;
	}

	// 全向检测球，球心在机身中心，与机身横滚无关；半径按船体深度现算
	// （见 GetLandingQueryRadiusCm）—— 船底着地时机身中心离地有一整个船体深度，
	// 固定半径的球会够不到地面。
	//
	// 原本是沿机身"下"方的射线（而不是世界下方，因为飞船停在球体侧面时世界下方
	// 是空的）。但自动回正已默认关闭，机身可以停在任何横滚角 —— 滚 90° 后"下方"
	// 就指向侧面，射线打不到地面，IsLanded 恒为假，人被卡在船里按 E 没反应。
	// 全向球没有这个问题。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceshipLanding), /*bTraceComplex=*/false, this);
	// 登船角色附着在飞船附近。它现在关着碰撞，但将来一旦因为交互、布娃娃之类重新打开，
	// 这个球会一直罩住它，IsLanded() 就永远为真、飞到空中也提示离船。先排除掉。
	if (BoardedCharacter)
	{
		Params.AddIgnoredActor(BoardedCharacter);
	}
	return GetWorld()->OverlapAnyTestByChannel(
		GetActorLocation(), FQuat::Identity, ECC_WorldStatic,
		FCollisionShape::MakeSphere(GetLandingQueryRadiusCm()), Params);
}

bool ASpaceshipPawn::CanBoardFrom(const FVector& CharacterLocation) const
{
	return FVector::DistSquared(CharacterLocation, GetActorLocation()) <= FMath::Square(BoardingRadius);
}

ASpaceshipPawn* ASpaceshipPawn::FindNearestTo(const UWorld* World, const FVector& FromLocation)
{
	if (!World)
	{
		return nullptr;
	}

	ASpaceshipPawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (TActorIterator<ASpaceshipPawn> It(World); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), FromLocation);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = *It;
		}
	}
	return Nearest;
}

void ASpaceshipPawn::SetBoardedCharacter(APlanetCharacter* Character)
{
	BoardedCharacter = Character;
}

float ASpaceshipPawn::GetContactRadiusCm() const
{
	// 兜底半径是世界尺度（厘米），与 CentimetersPerAu 同一套单位；属性本身就是
	// 厘米，不乘任何缩放。钳到非负：负半径会让求解器的接触解算反向推开飞船。
	return FMath::Max(ContactFallbackRadiusCm, 0.f);
}

bool ASpaceshipPawn::BuildContactProbes(TArray<FVector>& OutProbes) const
{
	OutProbes.Reset();

	const UStaticMesh* Mesh = Hull ? Hull->GetStaticMesh() : nullptr;
	const UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
	if (!BodySetup)
	{
		return false;
	}

	// 凸包顶点存的是元素空间坐标，每个元素还可能带自己的变换；再乘 Hull 相对
	// **Actor** 的变换（含蓝图里拖的缩放），才是 Actor 本地坐标。
	//
	// ⚠ 不能用 Hull->GetRelativeTransform()：Hull 是根组件，UE 对「没有 attach
	// 父组件」的组件把 RelativeLocation 直接存在世界坐标里（SceneComponent.cpp
	// 的 SetWorldLocation：「No parent, relative == world」）。于是
	// GetRelativeTransform() 返回的是**世界变换**，探针会被整体平移一个飞船的
	// 世界位置 —— 飞船飞了 2 AU，探针也跑 2 AU 外，接触永远检测不到。
	// 这里显式求「Hull 世界变换 → Actor 世界变换」的相对变换，根/子组件都成立。
	//
	// FTransform 的 operator* 是「先 A 后 B」：C = A * B 先应用 A 再应用 B
	// （见 TransformNonVectorized.h 里 Multiply 的注释）。所以写在左边的是 Elem。
	const FTransform HullToLocal =
		Hull->GetComponentTransform().GetRelativeTransform(GetActorTransform());

	for (const FKConvexElem& Elem : BodySetup->AggGeom.ConvexElems)
	{
		const FTransform ElemToLocal = Elem.GetTransform() * HullToLocal;
		for (const FVector& Vertex : Elem.VertexData)
		{
			OutProbes.Add(ElemToLocal.TransformPosition(Vertex));
		}
	}

	if (OutProbes.Num() == 0)
	{
		return false;
	}

	// 12 个凸包共几百个顶点，接触只需要形状：按 10 cm 网格去重。
	// 船体 50 m 量级下 10 cm 的误差远小于地形体素边长。
	constexpr double BucketCm = 10.0;
	TSet<FIntVector> Seen;
	TArray<FVector> Unique;
	Unique.Reserve(OutProbes.Num());

	for (const FVector& Probe : OutProbes)
	{
		const FIntVector Key(
			FMath::RoundToInt(Probe.X / BucketCm),
			FMath::RoundToInt(Probe.Y / BucketCm),
			FMath::RoundToInt(Probe.Z / BucketCm));

		if (Seen.Contains(Key))
		{
			continue;
		}
		Seen.Add(Key);
		Unique.Add(Probe);
	}

	OutProbes = MoveTemp(Unique);
	return true;
}

void ASpaceshipPawn::RebuildContactProbes()
{
	TArray<FVector> Probes;
	if (BuildContactProbes(Probes))
	{
		ContactProbesLocalCm = MoveTemp(Probes);
		UE_LOG(LogTemp, Display, TEXT("[Ship] 接触探针 %d 个"),
			ContactProbesLocalCm.Num());
	}
	else
	{
		ContactProbesLocalCm.Reset();
		UE_LOG(LogTemp, Warning,
			TEXT("[Ship] Hull 没有 Simple Collision，接触退回 %.0f cm 球"),
			ContactFallbackRadiusCm);
	}
}

float ASpaceshipPawn::ComputeSpeedLimitTaper(double SpeedCmPerSecond, double MaxSpeedCmPerSecond)
{
	// 上限 ≤ 0 = 不限。负值也按不限处理：ClampMin 只在编辑器里拦，运行时
	// 被蓝图或控制台写成负数时不该把推力整个关死。
	if (MaxSpeedCmPerSecond <= 0.0)
	{
		return 1.f;
	}

	// f = 1 − (v/vmax)²：v → vmax 时 f → 0，所以是渐近逼近。用平方而不是线性
	// 是因为线性在贴近上限时会突然「掉力」—— 半程还有 50% 推力、九成只剩 10%，
	// 手感上像被拽住；平方在半程还有 75%，到九成才明显收油。
	const double Ratio = FMath::Clamp(SpeedCmPerSecond / MaxSpeedCmPerSecond, 0.0, 1.0);
	return float(FMath::Clamp(1.0 - Ratio * Ratio, 0.0, 1.0));
}

float ASpaceshipPawn::ComputeAtmosphereDensityRatio(
	double AltitudeCm, double SurfaceRadiusCm,
	double ScaleHeightRatio, double AtmosphereHeightRatio,
	bool bAtmosphereEnabled)
{
	// 总开关关掉 = 这颗行星没有大气：密度恒为 0，阻力与 HUD 的「大气」提示
	// 都随之消失。放在参数校验之前——开关状态与几何参数无关。
	if (!bAtmosphereEnabled)
	{
		return 0.f;
	}

	// 参数非法 = 这颗行星没有可用的大气模型：宁可不加阻力，也不要除零或
	// exp 溢出把飞船弹飞。
	if (SurfaceRadiusCm <= 0.0 || ScaleHeightRatio <= 0.0 || AtmosphereHeightRatio <= 0.0)
	{
		return 0.f;
	}

	// 壳顶以上是真空。用 >= 而不是 >：壳顶本身算界外，避免边界上残留一份阻力。
	if (AltitudeCm >= SurfaceRadiusCm * AtmosphereHeightRatio)
	{
		return 0.f;
	}

	// 地面以下（撞进地形、或数值抖动给出负高度）按海平面密度处理。
	const double ScaleHeightCm = SurfaceRadiusCm * ScaleHeightRatio;
	const double NormalizedAltitude = FMath::Max(AltitudeCm, 0.0) / ScaleHeightCm;
	return float(FMath::Exp(-NormalizedAltitude));
}

FVector ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
	const FVector& VelocityCmPerSecond, double DensityRatio,
	double DragAccelerationKmPerS2)
{
	const double SpeedCmPerSecond = VelocityCmPerSecond.Size();
	if (SpeedCmPerSecond <= 0.0 || DensityRatio <= 0.0 || DragAccelerationKmPerS2 <= 0.0)
	{
		return FVector::ZeroVector;
	}

	// 速度换算成 km/s 代入 (v / 1 km/s)²，结果再乘回厘米/秒²。
	const double SpeedKmPerSecond = SpeedCmPerSecond / double(CentimetersPerKilometer);
	const double MagnitudeCmPerS2 = DragAccelerationKmPerS2 * DensityRatio
		* SpeedKmPerSecond * SpeedKmPerSecond * double(CentimetersPerKilometer);

	// 与速度严格反向：阻力只减速，不产生侧向力。
	return -VelocityCmPerSecond / SpeedCmPerSecond * MagnitudeCmPerS2;
}

float ASpaceshipPawn::GetAtmosphereDensityRatio() const
{
	if (!PlanetActor)
	{
		return 0.f;
	}

	const UPlanetAtmosphereComponent* Atmosphere =
		PlanetActor->FindComponentByClass<UPlanetAtmosphereComponent>();
	if (!Atmosphere)
	{
		return 0.f;
	}

	const double SurfaceRadiusCm = double(PlanetActor->GetSurfaceRadiusCm());
	if (SurfaceRadiusCm <= 0.0)
	{
		return 0.f;
	}

	// 高度取「到球心的距离 − 基准球面半径」，不含地形起伏：基准半径已经覆盖
	// 绝大部分落体区间，为一次密度查询逐帧做体素地表查询不值得。
	const double AltitudeCm =
		double(FVector::Dist(GetActorLocation(), PlanetActor->GetActorLocation()))
		- SurfaceRadiusCm;

	return ComputeAtmosphereDensityRatio(AltitudeCm, SurfaceRadiusCm,
		double(Atmosphere->RayleighScaleHeightRatio),
		double(Atmosphere->AtmosphereHeightRatio),
		Atmosphere->bAtmosphereEnabled);
}

FVector ASpaceshipPawn::GetAtmosphereDragAccelerationWorldCmPerS2() const
{
	if (!PlanetActor)
	{
		return FVector::ZeroVector;
	}

	const UPlanetAtmosphereComponent* Atmosphere =
		PlanetActor->FindComponentByClass<UPlanetAtmosphereComponent>();
	if (!Atmosphere || Atmosphere->DragAccelerationAtKmPerSecond <= 0.f)
	{
		return FVector::ZeroVector;
	}

	// 用相对行星的速度：大气跟着行星平移。拿惯性系绝对速度的话，贴着一颗
	// 公转中的行星静止时也会吃到「迎面风」（外圈行星约 27 km/s²），表现为
	// 飞船在地表上持续滑行。参考速度由子系统每帧推进来。
	return ComputeAtmosphereDragAccelerationWorldCmPerS2(
		GetRelativeVelocity(), double(GetAtmosphereDensityRatio()),
		double(Atmosphere->DragAccelerationAtKmPerSecond));
}

FVector ASpaceshipPawn::GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const
{
	// 机头 / 升降这一族只认**正向**输入：ThrustInput < 0（S）不是「沿机头反推」，
	// 而是制动，方向由速度决定（见下），与机头无关。
	//
	// 升降方向取**行星球心 → 飞船中心**的径向，而不是机身本地 up：
	// 贴地时飞船可能带着横滚 / 俯仰，按 Space 会沿机身斜着擦地。取径向后，
	// 不管落在球面哪一点、机身歪成什么样，升降键都是当地"垂直"。
	// 太空中（没有参考行星）才退回机身本地 up。
	//
	// 方向 = 机头 × 前推 + 径向 up × 升降，合成向量夹到长度 1 —— 与**原来的**
	// UFloatingPawnMovement::AddInputVector 的 GetClampedToMaxSize(1) 同规则：
	// 斜着同时按 W 和 Space 不该比单按一个多出 41% 的推力。
	const FVector UpDirection = ResolveVerticalThrustUp(
		GetActorLocation(),
		PlanetActor ? PlanetActor->GetActorLocation() : FVector::ZeroVector,
		PlanetActor != nullptr,
		GetActorUpVector());

	FVector Direction =
		GetActorForwardVector() * FMath::Max(ThrustInput, 0.f)
		+ UpDirection * VerticalInput;

	// Shift 乘在**大小**上而不是方向分量上：这样斜着按 W+Space+Shift 时，
	// 总量仍然只有单按一个的 1 倍（而不是 1.41 倍）。
	// km/s² → 厘米/秒²。1e5 是本地常数：本模块不能依赖 StellarEraCore。
	const float Magnitude = ThrustAccelerationKmPerS2 * CentimetersPerKilometer
		* (bBoosting ? BoostMultiplier : 1.f);

	if (ThrustInput < 0.f)
	{
		// 制动：沿速度反方向，大小按「本帧满推力能改变的速度」收敛 ——
		//
		//     |a| = A · min(1, |v| / (A·Δt))
		//
		// 本帧减速量 = min(A·Δt, |v|) ≤ |v|：一帧内恰好停住，绝不会推过头翻成反向。
		//
		// 刻度是 A·Δt 而不是某个固定速度，因为外力**整帧恒定** —— 求解器只在帧首
		// 读一次 ExternalAcceleration，整帧所有子步都用同一个值，所以一帧能改变多少
		// 速度就是 A·Δt。本关卡 A = 1.2e7 cm/s²，60 fps 下一帧 2e5 cm/s，比飞船整条
		// 速度尺度（阻尼极速 1.2e5）还大；固定刻度会在低帧率下先过冲，再被逐帧修正
		// 成来回翻符号的抖动，3 fps（编辑器不在前台时的实际帧率）下直接发散。
		//
		// Δt 用**调用方**那一帧的：与它真正要推进的那一帧对齐。本类自己 Tick 里抄
		// 一份的话，两个 Tick 谁先谁后不保证，编辑器切到后台掉到 3 fps 的那一帧会拿
		// 旧的小 Δt 去算 —— 制动量按 60 fps 估，实际却整帧推 0.33 秒，速度直接翻成
		// 反向的几十 km/s。
		//
		// 代价：制动只削速度、不抗重力 —— 低速时它给不出托住飞船的力，贴地悬停仍归
		// W / 升降键。另外它和阻尼一样作用在**惯性系**速度上，贴着行星按 S 是让飞船
		// 在惯性系里停下，不是「相对行星停住」（同 GetDampingTime 那条口径）。
		const FVector Velocity = GetVelocity();
		const double Speed = Velocity.Size();
		const double DeltaVPerFrame = double(Magnitude) * double(DeltaSeconds);
		if (Speed > 0.0 && DeltaVPerFrame > 0.0)
		{
			const double Taper = FMath::Min(1.0, Speed / DeltaVPerFrame);
			Direction += (-Velocity / Speed) * float(-ThrustInput * Taper);
		}
	}

	// 速度上限：只削**沿速度方向的正向**分量，横向推力与制动原样保留。
	//
	//     a_along' = a_along · f      f = 1 − (v/vmax)²
	//
	// 于是 v → vmax 是渐近的，而不是撞上一堵墙。为什么只削这一个分量：直接缩放
	// 整个推力向量的话，到顶后横向推力也没了 —— 转不了向、刹不住车，贴着行星
	// 时连抬头抗重力的力都拿不到，玩家会感觉「油门整个死掉」。
	//
	// 上限按 Shift 分两档：按住时用 BoostMaxSpeedKmPerSecond，松开用
	// MaxSpeedKmPerSecond。除取哪个值以外，两档的衰减规则完全相同 —— Shift 只是
	// 推得更快、上限更高，不是另一套操纵逻辑。
	//
	// 速度取 GetVelocity()，与阻尼、制动同一个惯性系（贴着行星时含行星自身的
	// 运动）。投影在**夹长度之前**做：夹长度只会缩短向量，不会把沿速分量放大回去。
	const float ActiveMaxSpeedKmPerSecond =
		bBoosting ? BoostMaxSpeedKmPerSecond : MaxSpeedKmPerSecond;
	const double MaxSpeedCmPerSecond =
		double(ActiveMaxSpeedKmPerSecond) * double(CentimetersPerKilometer);
	if (MaxSpeedCmPerSecond > 0.0)
	{
		const FVector Velocity = GetVelocity();
		const double Speed = Velocity.Size();
		if (Speed > KINDA_SMALL_NUMBER)
		{
			const FVector VelocityDirection = Velocity / Speed;
			const double AlongSpeed =
				FVector::DotProduct(Direction, VelocityDirection);
			if (AlongSpeed > 0.0)
			{
				const double Taper =
					ComputeSpeedLimitTaper(Speed, MaxSpeedCmPerSecond);
				Direction -= VelocityDirection * float(AlongSpeed * (1.0 - Taper));
			}
		}
	}

	// 大气阻力在最后叠加：它是**环境力**，不是操纵输入。速度上限只衰减推力，
	// 不该把阻力也一起衰减；推力为 0（松手）或巡航开着时阻力照样要有 ——
	// 否则「巡航关阻尼」会连空气阻力一起关掉，落进大气层仍是自由落体。
	FVector Acceleration = Direction.GetClampedToMaxSize(1.0) * Magnitude;
	Acceleration += GetAtmosphereDragAccelerationWorldCmPerS2();
	return Acceleration;
}

float ASpaceshipPawn::GetDampingTime() const
{
	// 巡航 = 只关阻尼，推力照给。关掉阻尼才有办法一路加速到圆轨道速度 ——
	// 阻尼极速只有它的 0.156 倍。
	return bPreserveVelocity ? 0.f : DampingTime;
}
