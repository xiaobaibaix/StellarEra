// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacterAnimInstance.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "PlanetCharacter.h"

void UPlanetCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const APlanetCharacter* Character = Cast<APlanetCharacter>(TryGetPawnOwner());
	if (!Character)
	{
		GroundSpeed = 0.f;
		bIsFalling = false;
		return;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FVector Velocity = Character->GetVelocity();
	const FVector GravityDirection =
		Movement ? Movement->GetGravityDirection() : FVector::DownVector;

	GroundSpeed = GravityDirection.IsNearlyZero()
		? Velocity.Size()
		: FVector::VectorPlaneProject(Velocity, GravityDirection).Size();
	bIsFalling = Movement && Movement->IsFalling();
}
