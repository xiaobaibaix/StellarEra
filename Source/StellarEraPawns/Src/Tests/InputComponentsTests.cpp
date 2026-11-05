// 输入组件的接线回归测试：两个 Pawn 的 CDO 上都必须挂着各自的输入组件。
//
// 断言 CDO 而不是运行时实例：默认子对象在构造函数里创建，CDO 上就有，
// 不需要世界、不需要 PIE。组件被误删或被蓝图覆盖掉，这里立刻红。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CosmosBody.h"
#include "PlanetCharacter.h"
#include "PlanetCharacterInputComponent.h"
#include "SpaceshipInputComponent.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCharacterInputComponentTest,
	"StellarEra.Pawns.InputComponentWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCharacterInputComponentTest::RunTest(const FString& Parameters)
{
	const APlanetCharacter* CharacterCDO = GetDefault<APlanetCharacter>();
	if (!TestNotNull(TEXT("APlanetCharacter CDO"), CharacterCDO))
	{
		return false;
	}

	const UPlanetCharacterInputComponent* CharacterInput =
		CharacterCDO->FindComponentByClass<UPlanetCharacterInputComponent>();
	TestNotNull(TEXT("角色 CDO 上的 UPlanetCharacterInputComponent"), CharacterInput);
	if (CharacterInput)
	{
		TestEqual(TEXT("默认灵敏度"), CharacterInput->LookSensitivity, 1.2f);
		TestFalse(TEXT("默认不反 Y"), CharacterInput->bInvertLookY);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipInputComponentTest,
	"StellarEra.Pawns.SpaceshipInputComponentWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipInputComponentTest::RunTest(const FString& Parameters)
{
	const ASpaceshipPawn* ShipCDO = GetDefault<ASpaceshipPawn>();
	if (!TestNotNull(TEXT("ASpaceshipPawn CDO"), ShipCDO))
	{
		return false;
	}

	const USpaceshipInputComponent* ShipInput =
		ShipCDO->FindComponentByClass<USpaceshipInputComponent>();
	TestNotNull(TEXT("飞船 CDO 上的 USpaceshipInputComponent"), ShipInput);
	if (ShipInput)
	{
		TestEqual(TEXT("默认灵敏度"), ShipInput->LookSensitivity, 1.2f);
		TestFalse(TEXT("默认不反 Y"), ShipInput->bInvertLookY);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipImplementsCosmosBodyTest,
	"StellarEra.Pawns.ShipImplementsCosmosBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipImplementsCosmosBodyTest::RunTest(const FString& Parameters)
{
	const ASpaceshipPawn* ShipCDO = GetDefault<ASpaceshipPawn>();
	if (!TestNotNull(TEXT("ASpaceshipPawn CDO"), ShipCDO))
	{
		return false;
	}

	// Cosmos 靠这个转型驱动飞船；接口没实现的话飞船在求解器里是颗无接触点质量。
	TestNotNull(TEXT("飞船实现 ICosmosBody"), Cast<ICosmosBody>(ShipCDO));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
