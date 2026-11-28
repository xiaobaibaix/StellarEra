#include "StarParamBridge.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

void UStarParamBridge::ApplyToChild(AActor* Child)
{
	if (!Child)
	{
		return;
	}

	TArray<UStaticMeshComponent*> MeshComponents;
	Child->GetComponents<UStaticMeshComponent>(MeshComponents);
	for (UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		// 恒星本体叫 StarMesh；没有同名的就退回第一个网格。
		if (MeshComponent && (MeshComponents.Num() == 1 || MeshComponent->GetFName() == TEXT("StarMesh")))
		{
			MeshComponent->SetRelativeScale3D(FVector(MeshScale));
			return;
		}
	}
}

bool UStarParamBridge::SupportsChild(const AActor* Child) const
{
	if (!Child)
	{
		return false;
	}
	TArray<UStaticMeshComponent*> MeshComponents;
	const_cast<AActor*>(Child)->GetComponents<UStaticMeshComponent>(MeshComponents);
	return MeshComponents.Num() > 0;
}
