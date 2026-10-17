// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "VoxelMinimal.h"
#include "GameFramework/Character.h"
#include "VoxelCharacter.generated.h"

UCLASS(BlueprintType)
class VOXEL_API AVoxelCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// This override is required for base replication to work properly, as voxel world components are generated at runtime & not replicated
#if VOXEL_ENGINE_VERSION >= 508
	virtual void SetBase(FMovementBaseInterfaceData* MovementBaseInterfaceData, FName BoneName, bool bNotifyActor) override;
#else
	virtual void SetBase(UPrimitiveComponent* NewBase, FName BoneName, bool bNotifyActor) override;
#endif
};
