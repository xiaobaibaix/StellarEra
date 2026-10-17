// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "VoxelCharacter.h"
#include "Components/PrimitiveComponent.h"

#if VOXEL_ENGINE_VERSION >= 508
void AVoxelCharacter::SetBase(FMovementBaseInterfaceData* MovementBaseInterfaceData, const FName BoneName, const bool bNotifyActor)
{
	FMovementBaseInterfaceData RedirectedData;
	if (MovementBaseInterfaceData &&
		MovementBaseInterfaceData->IsValid())
	{
		// LoadClass to not depend on the voxel module
		static UClass* const VoxelWorldClass = LoadClass<UObject>(nullptr, TEXT("/Script/Voxel.VoxelWorld"));

		UObject* BaseOwner = MovementBaseInterfaceData->GetMovementBaseObjectOwner();
		if (ensure(VoxelWorldClass) &&
			BaseOwner &&
			BaseOwner->IsA(VoxelWorldClass))
		{
			const AActor* BaseActor = Cast<AActor>(BaseOwner);
			UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(BaseActor->GetRootComponent());
			if (ensure(Root))
			{
				RedirectedData = FMovementBaseInterfaceData(Root);
				MovementBaseInterfaceData = &RedirectedData;
			}
		}
	}

	Super::SetBase(MovementBaseInterfaceData, BoneName, bNotifyActor);
}
#else
void AVoxelCharacter::SetBase(UPrimitiveComponent* NewBase, const FName BoneName, const bool bNotifyActor)
{
	if (NewBase)
	{
		// LoadClass to not depend on the voxel module
		static UClass* const VoxelWorldClass = LoadClass<UObject>(nullptr, TEXT("/Script/Voxel.VoxelWorld"));

		const AActor* BaseOwner = NewBase->GetOwner();
		if (ensure(VoxelWorldClass) &&
			BaseOwner &&
			BaseOwner->IsA(VoxelWorldClass))
		{
			NewBase = Cast<UPrimitiveComponent>(BaseOwner->GetRootComponent());
			ensure(NewBase);
		}
	}

	Super::SetBase(NewBase, BoneName, bNotifyActor);
}
#endif
