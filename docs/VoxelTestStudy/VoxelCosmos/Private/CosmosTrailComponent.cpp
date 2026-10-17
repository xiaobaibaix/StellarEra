// 轨道拖尾 —— 实现。

#include "CosmosTrailComponent.h"
#include "CosmosTypes.h"

UCosmosTrailComponent::UCosmosTrailComponent()
{
	// 拖尾靠 TickComponent 采样，所以组件自己开 Tick。
	// 但 Actor 的 PrimaryActorTick 关掉了，需要这里单独启用。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// 拖尾是程序化生成的，不需要静态网格资源
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	bCastDynamicShadow = false;
	bReceivesDecals = false;
}

void UCosmosTrailComponent::ResetTrail()
{
	TrailPoints.Reset();
	SampleTimer = 0.f;

	// 清掉已生成的网格数据，否则会留上一帧的残影
	ClearMeshSection(0);
}

void UCosmosTrailComponent::AddSample(const FVector& WorldLocation)
{
	// 环形缓冲：新点插入头部，超出长度就丢最旧的
	TrailPoints.Insert(WorldLocation, 0);

	const int32 MaxPoints = FMath::Max(TrailLength, 2);
	while (TrailPoints.Num() > MaxPoints)
	{
		TrailPoints.Pop();
	}
}

void UCosmosTrailComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 拿到宿主 Actor 的世界位置
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FVector WorldLocation = Owner->GetActorLocation();

	// --- 采样 ---
	bool bShouldSample = true;

	if (SampleInterval > 0.f)
	{
		SampleTimer += DeltaTime;
		bShouldSample = (SampleTimer >= SampleInterval);
		if (bShouldSample)
		{
			SampleTimer = 0.f;
		}
	}

	if (bShouldSample && MinSampleDistanceAu > 0.0 && TrailPoints.Num() > 0)
	{
		// 移动距离太小就跳过，避免慢速公转时轨迹挤成一团
		const double MinDistance = MinSampleDistanceAu * CosmosUnits::AuToCentimeter;
		const double Moved = double(FVector::Dist(WorldLocation, TrailPoints[0]));
		if (Moved < MinDistance)
		{
			bShouldSample = false;
		}
	}

	if (bShouldSample)
	{
		AddSample(WorldLocation);
	}

	// --- 渲染 ---
	if (!bEnabled)
	{
		ClearMeshSection(0);
		return;
	}

	// 找相机用于朝向计算。拿不到就退化用世界Z轴展开。
	FVector CameraLocation = WorldLocation;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector ViewLoc = FVector::ZeroVector;
			FRotator ViewRot = FRotator::ZeroRotator;
			PC->GetPlayerViewPoint(ViewLoc, ViewRot);
			CameraLocation = ViewLoc;
		}
	}

	RebuildRibbon(CameraLocation);
}

void UCosmosTrailComponent::RebuildRibbon(const FVector& CameraLocation)
{
	const int32 NumPoints = TrailPoints.Num();
	if (NumPoints < 2)
	{
		ClearMeshSection(0);
		return;
	}

	const float HalfWidth = TrailWidth * 0.5f;
	const float SafeWidth = FMath::Max(HalfWidth, 1.0f);

	TArray<FVector> Vertices;
	TArray<uint32> Indices;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;

	Vertices.Reserve(NumPoints * 2);
	UVs.Reserve(NumPoints * 2);
	Colors.Reserve(NumPoints * 2);
	Indices.Reserve((NumPoints - 1) * 6);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		const FVector& Point = TrailPoints[i];

		// 条带方向：指向轨迹的前进方向。
		// 头部用下一个点，尾部用上一个点，中间取平均值。
		FVector Direction;
		if (i == 0)
		{
			Direction = TrailPoints[1] - Point;
		}
		else if (i == NumPoints - 1)
		{
			Direction = Point - TrailPoints[i - 1];
		}
		else
		{
			Direction = TrailPoints[i - 1] - TrailPoints[i + 1];
		}

		if (Direction.IsNearlyZero())
		{
			Direction = FVector::UpVector;
		}
		Direction.Normalize();

		// 展开方向 = 视线方向 × 轨迹方向。
		// 这样条带永远正对摄像机，不会侧过去看不见。
		FVector ViewDir = (Point - CameraLocation);
		if (ViewDir.IsNearlyZero())
		{
			ViewDir = FVector::UpVector;
		}
		ViewDir.Normalize();

		FVector Side = FVector::CrossProduct(ViewDir, Direction);
		if (Side.IsNearlyZero())
		{
			// 视线与轨迹平行时退化，随便取一个垂直方向
			Side = FVector::CrossProduct(Direction, FVector::UpVector);
			if (Side.IsNearlyZero())
			{
				Side = FVector::CrossProduct(Direction, FVector::RightVector);
			}
		}
		Side = Side.GetSafeNormal() * SafeWidth;

		Vertices.Add(Point - Side);
		Vertices.Add(Point + Side);

		// U 沿轨迹方向，V 横跨宽度
		const float U = 1.f - float(i) / float(NumPoints - 1);
		UVs.Add(FVector2D(U, 0.f));
		UVs.Add(FVector2D(U, 1.f));

		// 颜色渐变：头尾插值
		const float T = float(i) / float(NumPoints - 1);
		const FLinearColor Color = FLinearColor::LerpUsingHSV(HeadColor, TailColor, T);
		Colors.Add(Color);
		Colors.Add(Color);

		// 每个四边形两个三角形
		if (i < NumPoints - 1)
		{
			const uint32 Base = uint32(i * 2);
			Indices.Add(Base + 0);
			Indices.Add(Base + 1);
			Indices.Add(Base + 2);

			Indices.Add(Base + 1);
			Indices.Add(Base + 3);
			Indices.Add(Base + 2);
		}
	}

	// 写入动态网格段。
	// 最后三个参数分别是 bNeedsRecalcNormals / bUse16BitIndices / bUseReverseTopology，
	// 拖尾是纯色带，法线无用，索引用 16 位省带宽，顶点绕序反了也不影响双面渲染。
	UpdateMeshSection(0,
		Vertices,
		Indices,
		UVs,
		Colors,
		TArray<FVector>(),
		false,
		false,
		0);
}
