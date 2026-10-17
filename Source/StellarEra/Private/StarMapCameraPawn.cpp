// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"

AStarMapCameraPawn::AStarMapCameraPawn()
{
	// 不 tick：位置在 CalcCamera 里现算，见类注释。
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("StarMapCamera"));
	SetRootComponent(Camera);

	// 视线方向由本 Pawn 自己管（OrbitRotation），不走控制器的旋转。
	Camera->bUsePawnControlRotation = false;
}

void AStarMapCameraPawn::EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation)
{
	// 「原地不动、不选中」：把进入前的视角直接拿来当基准，于是进入星图时画面不变。
	CameraLocation = InCameraLocation;
	OrbitRotation = InCameraRotation;
	LastSeenSelected = nullptr;

	// 顺手把 Actor 摆过去，方便在 PIE 里看它、点它。它不影响相机。
	SetActorLocationAndRotation(InCameraLocation, InCameraRotation, /*bSweep=*/false);
}

void AStarMapCameraPawn::OrbitLook(float DeltaYaw, float DeltaPitch)
{
	// 只动朝向不动距离 —— 下一帧 CalcCamera 按新朝向重算位置，看起来就是「绕着焦点转」。
	OrbitRotation.Yaw += DeltaYaw;
	// 夹到 ±89°：到 90° 时视线与 up 平行，Rotation 的 roll 会翻掉，画面会打转。
	OrbitRotation.Pitch = FMath::Clamp(OrbitRotation.Pitch + DeltaPitch, -89.0, 89.0);
	OrbitRotation.Roll = 0.0;
}

void AStarMapCameraPawn::AddZoom(float Value)
{
	// 夹的是**目标距离本身**，不是最终算出来的相机位置 —— 这条经验来自飞船的
	// AddZoom（SpaceshipPawn.cpp:469-471）：夹最终值的话，玩家滚过头会攒下一堆
	// 被 Clamp 吃掉的无效偏移，往回滚要先「还债」，前几格看起来像没反应。
	//
	// 取负号：Value > 0 是滚轮上滚 = 拉近 = 距离变小。
	const double Factor = FMath::Pow(ZoomStepFactor, -double(Value));
	OrbitDistanceCm = FMath::Clamp(
		OrbitDistanceCm * Factor, MinOrbitDistanceCm, MaxOrbitDistanceCm);
}

AActor* AStarMapCameraPawn::ResolveSelectedBody() const
{
	UWorld* World = GetWorld();
	UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	return Subsystem ? Subsystem->GetSelectedBody() : nullptr;
}

void AStarMapCameraPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	// 先让父类从相机组件取 FOV / 后处理等，再把位置和朝向覆盖掉。
	Super::CalcCamera(DeltaTime, OutResult);

	AActor* Selected = ResolveSelectedBody();
	if (Selected != LastSeenSelected.Get())
	{
		// 选中状态变了。从**当前**相机位置反推绕行参数：视线方向 = 相机→焦点，
		// 距离 = 两者实际距离。下面重算出的位置因此与当前位置重合 —— 不跳变。
		LastSeenSelected = Selected;
		if (Selected)
		{
			const FVector ToFocus = Selected->GetActorLocation() - CameraLocation;
			const double Distance = ToFocus.Size();

			// 夹距离本身（不是夹算出来的位置）。夹是必要的：相机不能钻进星体里。
			// 真夹到时会有一次跳变，这是唯一允许跳变的场合。
			OrbitDistanceCm = FMath::Clamp(Distance, MinOrbitDistanceCm, MaxOrbitDistanceCm);

			if (!ToFocus.IsNearlyZero())
			{
				OrbitRotation = ToFocus.Rotation();
			}
		}
	}

	if (Selected)
	{
		CameraLocation = Selected->GetActorLocation()
			- OrbitRotation.Vector() * OrbitDistanceCm;
	}
	// 未选中：CameraLocation 保持不动 —— 这就是「冻结」。

	OutResult.Location = CameraLocation;
	OutResult.Rotation = OrbitRotation;
}

AActor* AStarMapCameraPawn::PickBodyAtScreenPosition(
	TArrayView<const FStarMapScreenCandidate> Candidates,
	const FVector2D& ScreenPoint,
	float PixelRadius)
{
	const double RadiusSq = double(PixelRadius) * double(PixelRadius);
	double BestDistSq = TNumericLimits<double>::Max();
	AActor* Best = nullptr;

	for (const FStarMapScreenCandidate& Candidate : Candidates)
	{
		if (!Candidate.Actor)
		{
			continue;
		}

		const double DistSq = FVector2D::DistSquared(Candidate.ScreenPosition, ScreenPoint);
		if (DistSq > RadiusSq)
		{
			// 超出容差。边界含：恰好等于半径不算「超出」。
			continue;
		}

		if (DistSq < BestDistSq)
		{
			// 严格小于 —— 等距时先出现的赢，结果确定。
			BestDistSq = DistSq;
			Best = Candidate.Actor;
		}
	}

	return Best;
}
