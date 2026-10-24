// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetSurfaceGridComponent.h"

#include "PlanetEditGridGeometry.h"
#include "VoxelPlanetActor.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneTypes.h"
#include "TimerManager.h"
// FVoxelData::GetValue 是头文件里的 inline 转调，模板定义在 VoxelData.inl 里。
// 只含 VoxelData.h 能编过、但链接时找不到 FVoxelData::Get<FVoxelValue>；
// VoxelDataIncludes.h 是插件给的伞形头，把 .inl 与锁一起带进来。
#include "VoxelData/VoxelDataIncludes.h"
#include "VoxelVector.h"

UPlanetSurfaceGridComponent::UPlanetSurfaceGridComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 必须排在 N 体子系统之后：子系统在 FTickableGameObject::TickObjects 里把
	// 行星搬到本帧位置，而相机更新在它之后。排进 TG_PostUpdateWork 的组件
	// 能读到本帧最终位置，绘制出的网格不会比行星慢一帧。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AVoxelPlanetActor* UPlanetSurfaceGridComponent::GetPlanet() const
{
	return Cast<AVoxelPlanetActor>(GetOwner());
}

void UPlanetSurfaceGridComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RebuildTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UPlanetSurfaceGridComponent::SetGridVisible(const bool bVisible)
{
	if (bGridVisible == bVisible)
	{
		return;
	}

	bGridVisible = bVisible;
	SetComponentTickEnabled(bVisible);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RebuildTimer);
	}

	if (bVisible)
	{
		EnsureLineBatch();
		RebuildCache();
		if (LineBatch)
		{
			LineBatch->SetVisibility(true);
		}
	}
	else if (LineBatch)
	{
		// Flush 清掉上一帧提交的线；隐藏后再画也不会有残留。
		LineBatch->Flush();
		LineBatch->SetVisibility(false);
	}
}

void UPlanetSurfaceGridComponent::NotifyVoxelEdited()
{
	if (!bGridVisible)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	Timers.ClearTimer(RebuildTimer);

	if (RebuildDebounceSeconds <= 0.f)
	{
		RebuildCache();
		return;
	}

	Timers.SetTimer(RebuildTimer,
		FTimerDelegate::CreateUObject(this, &UPlanetSurfaceGridComponent::RebuildCache),
		RebuildDebounceSeconds, false);
}

void UPlanetSurfaceGridComponent::EnsureLineBatch()
{
	if (LineBatch)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	LineBatch = NewObject<ULineBatchComponent>(Owner, TEXT("PlanetSurfaceGridLines"));
	LineBatch->SetupAttachment(Owner->GetRootComponent());
	LineBatch->RegisterComponent();
}

void UPlanetSurfaceGridComponent::RebuildCache()
{
	CachedDirections.Reset();
	CachedVoxelPositions.Reset();
	CachedSegments.Reset();

	AVoxelPlanetActor* Planet = GetPlanet();
	if (!Planet || !Planet->IsCreated())
	{
		return;
	}

	FVoxelData& Data = Planet->GetData();
	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const float BaseRadiusVoxels = Planet->GetSurfaceRadiusCm() / VoxelSize;
	const int32 SearchSteps = FMath::Max(1, MaxSearchSteps);

	TArray<FPlanetEditGridLine> Lines;
	PlanetEditGrid::BuildLines(GridStepDegrees, Lines);

	// GetSurfaceRadiusCmAtDirection 的解析半径已经应用了与密度场相同的
	// ComputePlanetOffset，所以它离真实地表只差约一个体素 —— 这里向外留的余量
	// 不是给噪声的，是给**编辑**的：笔刷能把地形抬高，采样必须覆盖抬升后的表面。
	// 再加上 FindRadiusVoxels 向外 / 向内各探 SearchSteps 步，锁必须覆盖「所有
	// 方向解析半径的最大值 + 编辑与搜索余量」，否则采样点会落在锁外，读到未
	// 同步的数据。
	//
	// 方向空间前提：这里的 Direction 是**体素空间**方向；GetSurfaceRadiusCmAtDirection
	// 接受世界方向。两者能直接共用，只因为行星 Actor 不带旋转（见
	// VoxelPlanetActor.cpp 中 GetSurfaceRadiusCmAtDirection 的说明）。若将来行星
	// 会自转，必须先把方向变换到世界空间再查询。
	float MaxGuessVoxels = BaseRadiusVoxels;
	for (const FPlanetEditGridLine& Line : Lines)
	{
		for (const FVector& Direction : Line.Directions)
		{
			MaxGuessVoxels = FMath::Max(MaxGuessVoxels,
				Planet->GetSurfaceRadiusCmAtDirection(Direction) / VoxelSize);
		}
	}

	const int32 BoundsExtent = FMath::CeilToInt(MaxGuessVoxels + SearchSteps * 2.f + 2.f);
	const FVoxelIntBox Bounds(FIntVector(-BoundsExtent), FIntVector(BoundsExtent));
	FVoxelReadScopeLock Lock(Data, Bounds, TEXT("PlanetSurfaceGrid"));

	const auto SampleEmpty = [&Data](const FVector& Direction, const float RadiusVoxels) -> bool
	{
		// Direction 的分量是 double，RoundToInt(double) 返回 int64；显式窄化。
		const FIntVector P(
			static_cast<int32>(FMath::RoundToInt(Direction.X * RadiusVoxels)),
			static_cast<int32>(FMath::RoundToInt(Direction.Y * RadiusVoxels)),
			static_cast<int32>(FMath::RoundToInt(Direction.Z * RadiusVoxels)));
		return Data.GetValue(P, 0).IsEmpty();
	};

	const auto FindRadiusVoxels = [&](const FVector& Direction) -> float
	{
		// 解析猜测与 GetValue 都在**数据空间**；只有 AVoxelWorld::WorldOffset 为零时
		// 数据空间 == 局部空间（本项目从不启用自定义 rebasing，见调用点说明）。
		const float Guess = Planet->GetSurfaceRadiusCmAtDirection(Direction) / VoxelSize;
		const bool bGuessEmpty = SampleEmpty(Direction, Guess);

		// 向外、向内各扫一遍，取先遇到的符号翻转。
		for (const float Sign : {1.f, -1.f})
		{
			float PrevR = Guess;
			bool bPrevEmpty = bGuessEmpty;

			for (int32 Step = 1; Step <= SearchSteps; ++Step)
			{
				const float R = Guess + Sign * Step * 2.f;
				if (R < 2.f)
				{
					break;
				}

				const bool bEmpty = SampleEmpty(Direction, R);
				if (bEmpty != bPrevEmpty)
				{
					float EmptyR = bPrevEmpty ? PrevR : R;
					float SolidR = bPrevEmpty ? R : PrevR;

					// 4 次二分 → 体素级精度；再多也不会让 5° 网格更平滑。
					for (int32 Iter = 0; Iter < 4; ++Iter)
					{
						const float Mid = (EmptyR + SolidR) * 0.5f;
						if (SampleEmpty(Direction, Mid))
						{
							EmptyR = Mid;
						}
						else
						{
							SolidR = Mid;
						}
					}

					return (EmptyR + SolidR) * 0.5f;
				}

				PrevR = R;
				bPrevEmpty = bEmpty;
			}
		}

		// 该方向没有编辑（或超出了搜索范围）：退回解析半径。
		return Guess;
	};

	const float OffsetVoxels = SurfaceOffsetCm / VoxelSize;

	for (const FPlanetEditGridLine& Line : Lines)
	{
		const int32 Num = Line.Directions.Num();
		if (Num < 2)
		{
			continue;
		}

		const int32 Base = CachedDirections.Num();
		CachedDirections.Reserve(Base + Num);
		CachedVoxelPositions.Reserve(Base + Num);

		for (const FVector& Direction : Line.Directions)
		{
			// 缓存位置也是数据空间（WorldOffset 为 0 时等于局部空间），绘制时才
			// 经 LocalToGlobalFloat 换成世界坐标。
			const float RadiusVoxels = FindRadiusVoxels(Direction) + OffsetVoxels;
			CachedDirections.Add(Direction);
			CachedVoxelPositions.Add(Direction * RadiusVoxels);
		}

		// 经线是开放折线（Num-1 段）；纬线首尾相接（Num 段）。
		const int32 SegmentCount = Line.bClosed ? Num : Num - 1;
		CachedSegments.Reserve(CachedSegments.Num() + SegmentCount);
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			CachedSegments.Add(FIntPoint(Base + Index, Base + (Index + 1) % Num));
		}
	}
}

void UPlanetSurfaceGridComponent::DrawCachedLines()
{
	AVoxelPlanetActor* Planet = GetPlanet();
	UWorld* World = GetWorld();
	if (!bGridVisible || !Planet || !LineBatch || !World || CachedSegments.Num() == 0)
	{
		// 早退也必须清空上一帧提交的线，否则旧线会一直留在画面上。
		if (LineBatch)
		{
			LineBatch->Flush();
		}
		return;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		// 早退也必须清空上一帧提交的线。
		LineBatch->Flush();
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// CameraDirection 是**世界空间**方向，而缓存方向是体素空间方向。两者能直接比较，
	// 同样只因为行星 Actor 不带旋转；若将来会自转，必须先用
	// InverseTransformVectorNoScale 把它换到体素空间。
	const FVector CameraDirection =
		(ViewLocation - Planet->GetActorLocation()).GetSafeNormal();
	if (CameraDirection.IsNearlyZero())
	{
		// 早退也必须清空上一帧提交的线。
		LineBatch->Flush();
		return;
	}

	TArray<FBatchedLine> Lines;
	Lines.Reserve(CachedSegments.Num());

	for (const FIntPoint& Segment : CachedSegments)
	{
		const FVector& DirectionA = CachedDirections[Segment.X];
		const FVector& DirectionB = CachedDirections[Segment.Y];
		if (!PlanetEditGrid::IsSegmentVisible(DirectionA, DirectionB, CameraDirection))
		{
			continue;
		}

		// 缓存是体素空间；LocalToGlobalFloat 已经包含 VoxelSize 与 Actor 变换，
		// 所以这里直接传体素坐标。
		const FVector A = Planet->LocalToGlobalFloat(
			FVoxelVector(CachedVoxelPositions[Segment.X]));
		const FVector B = Planet->LocalToGlobalFloat(
			FVoxelVector(CachedVoxelPositions[Segment.Y]));

		// LifeTime < 0：永不自动过期；本组件每帧 Flush + 重画。
		Lines.Emplace(A, B, GridColor, -1.f, LineThickness, static_cast<uint8>(SDPG_World));
	}

	LineBatch->Flush();
	LineBatch->DrawLines(Lines);
}

void UPlanetSurfaceGridComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bGridVisible)
	{
		// 自愈：网格可能在体素世界建好之前就被显示（SetGridVisible 时 RebuildCache
		// 因 !IsCreated() 直接返回，缓存会一直空着）。世界建好后补一次重建。
		if (CachedSegments.Num() == 0)
		{
			if (const AVoxelPlanetActor* P = GetPlanet())
			{
				if (P->IsCreated())
				{
					RebuildCache();
				}
			}
		}

		DrawCachedLines();
	}
}
