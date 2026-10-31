// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 单位迁移的回归测试。PostLoad 是旧资产（M☉ / AU每年）进入新字段
// （M⊕ / km/s）的唯一通道，写错一次就会把整套天体的质量或速度改掉。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CosmosOrbitComponent.h"
#include "CosmosTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraOrbitMigrationTest,
	"StellarEra.Units.OrbitComponentMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraOrbitMigrationTest::RunTest(const FString& Parameters)
{
	// ── 1. 旧资产：改写一次，旧字段清零 ──
	{
		UCosmosOrbitComponent* Legacy = NewObject<UCosmosOrbitComponent>();
		Legacy->MassSolar = 0.873237;
		Legacy->InitialVelocity = FVector(0.0, 50.0, 0.0);

		Legacy->PostLoad();

		TestEqual(TEXT("MassSolar 换算成 MassEarth"),
			Legacy->MassEarth, 290740.8393297756, 1.0e-9);
		TestEqual(TEXT("MassSolar 迁移后清零"), Legacy->MassSolar, 0.0, 0.0);
		TestEqual(TEXT("InitialVelocity 换算成 km/s"),
			Legacy->InitialVelocityKmPerSecond.Y, 237.0235231766674, 1.0e-12);
		TestTrue(TEXT("InitialVelocity 迁移后清零"), Legacy->InitialVelocity.IsZero());

		// ── 2. 幂等：同一份对象再 PostLoad 一次，值不变 ──
		Legacy->PostLoad();
		TestEqual(TEXT("重复 PostLoad 不改变 MassEarth"),
			Legacy->MassEarth, 290740.8393297756, 1.0e-9);
		TestEqual(TEXT("重复 PostLoad 不改变初速度"),
			Legacy->InitialVelocityKmPerSecond.Y, 237.0235231766674, 1.0e-12);
	}

	// ── 3. 新资产：新字段非默认值，旧字段为零，PostLoad 不碰 ──
	{
		UCosmosOrbitComponent* Modern = NewObject<UCosmosOrbitComponent>();
		Modern->MassEarth = 5.0;
		Modern->InitialVelocityKmPerSecond = FVector(1.0, 2.0, 3.0);

		Modern->PostLoad();

		TestEqual(TEXT("新资产的 MassEarth 不被迁移改动"), Modern->MassEarth, 5.0, 0.0);
		TestEqual(TEXT("新资产的初速 X 不被迁移改动"),
			Modern->InitialVelocityKmPerSecond.X, 1.0, 0.0);
	}

	// ── 4. 默认值 = 地球质量 ──
	{
		UCosmosOrbitComponent* Fresh = NewObject<UCosmosOrbitComponent>();
		TestEqual(TEXT("默认 MassEarth = 1 M⊕"), Fresh->MassEarth, 1.0, 0.0);
		TestEqual(TEXT("旧 MassSolar 默认 = 0"), Fresh->MassSolar, 0.0, 0.0);
		TestTrue(TEXT("默认初速为零"), Fresh->InitialVelocityKmPerSecond.IsZero());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
