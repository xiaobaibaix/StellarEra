#include "UniverseScaleControls.h"

#include "EngineUtils.h"
#include "UObject/UnrealType.h"
#include "VoxelPlanetActor.h"

AUniverseScaleControls::AUniverseScaleControls()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AUniverseScaleControls::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 拖滑条时每帧都会来一次，只在提交（非 Interactive）时重摆。
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AUniverseScaleControls, DistanceScale)
		&& PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		ApplyScale();
	}
}

void AUniverseScaleControls::ApplyScale()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Cosmos 在 Planet 下游，本模块不能直接 include；按类路径取，避免加模块依赖。
	UClass* OrbitClass = FindObject<UClass>(nullptr, TEXT("/Script/StellarEraCosmos.CosmosOrbitComponent"));
	if (!OrbitClass)
	{
		return;
	}

	AActor* Star = ReferenceStar.LoadSynchronous();
	AActor* Body = ReferenceBody.LoadSynchronous();
	if (!Star || !Body)
	{
		double BestMass = -1.0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->FindComponentByClass(OrbitClass))
			{
				continue;
			}
			if (!Body && It->IsA<AVoxelPlanetActor>())
			{
				Body = *It;
			}
			for (UActorComponent* Component : It->GetComponents())
			{
				if (const FDoubleProperty* Mass = CastField<FDoubleProperty>(
					Component->GetClass()->FindPropertyByName(TEXT("MassEarth"))))
				{
					const double Value = Mass->GetPropertyValue_InContainer(Component);
					if (Value > BestMass)
					{
						BestMass = Value;
						Star = *It;
					}
				}
			}
		}
	}
	if (!Star || !Body)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UniverseScale] 找不到参考恒星或参考行星，未缩放。"));
		return;
	}

	const FVector Ref = Star->GetActorLocation();
	const double Current = (Body->GetActorLocation() - Ref).Size();
	if (Current <= 1.0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UniverseScale] 参考行星与恒星重合，未缩放。"));
		return;
	}

	const double Target = BaselineDistanceCm / FMath::Max(DistanceScale, 1.0);
	const double Factor = Target / Current;

	int32 Moved = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->FindComponentByClass(OrbitClass))
		{
			continue;
		}
		It->SetActorLocation(
			Ref + (It->GetActorLocation() - Ref) * Factor, false, nullptr, ETeleportType::TeleportPhysics);
		++Moved;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[UniverseScale] 倍率 %.4g：行星目标 %.3e cm（当前 %.3e），缩放系数 %.4g，移动 %d 个天体"),
		DistanceScale, Target, Current, Factor, Moved);
}
