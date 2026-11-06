// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraInputActionBuilder.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace StellarEraInput
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	void MapKey(
		UInputMappingContext* Context,
		UInputAction* Action,
		const FKey& Key,
		const TArray<UInputModifier*>& Modifiers)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		for (UInputModifier* Modifier : Modifiers)
		{
			Mapping.Modifiers.Add(Modifier);
		}
	}

	UInputModifier* MakeNegate(UObject* Outer)
	{
		return NewObject<UInputModifierNegate>(Outer);
	}

	UInputModifier* MakeSwizzleYXZ(UObject* Outer)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	}
}
