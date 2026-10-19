// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 角色运动动画的资产级验收测试。
//
// 它不跑 PIE：editor world 不执行 BeginPlay，无法断言运行期 Mesh 上的 AnimClass，
// 所以这里断言两件能静态读到的事实：
//   1. 四个 AS_ 序列存在、时长与轨道正确；
//   2. BP_PlanetCharacter 生成类 CDO 的 CharacterAnimClass 已指向 ABP_PlanetCharacter_C。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "UObject/UObjectGlobals.h"
#include "PlanetCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetCharacterAnimationAssetsTest,
	"StellarEra.Vehicle.AnimationAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetCharacterAnimationAssetsTest::RunTest(const FString& Parameters)
{
	const TCHAR* AnimPaths[] = {
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Idle.AS_Pomni_Idle"),
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Walk.AS_Pomni_Walk"),
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Run.AS_Pomni_Run"),
		TEXT("/Game/Anims/PlanetCharacter/AS_Pomni_Air.AS_Pomni_Air"),
	};

	for (const TCHAR* Path : AnimPaths)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Path);
		TestNotNull(FString::Printf(TEXT("%s 可加载"), Path), Sequence);
		if (!Sequence)
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s 时长 > 0"), Path), Sequence->GetPlayLength() > 0.f);

		const IAnimationDataModel* Model = Sequence->GetDataModel();
		TestNotNull(FString::Printf(TEXT("%s 有数据模型"), Path), Model);
		if (Model)
		{
			TestTrue(FString::Printf(TEXT("%s 含 Leg_L_4 轨道"), Path),
				Model->IsValidBoneTrackName(TEXT("Leg_L_4")));
			TestTrue(FString::Printf(TEXT("%s 含 Arm_R_90 轨道"), Path),
				Model->IsValidBoneTrackName(TEXT("Arm_R_90")));
		}
	}

	UClass* BlueprintClass = LoadClass<APlanetCharacter>(
		nullptr, TEXT("/Game/Code/BP_PlanetCharacter.BP_PlanetCharacter_C"));
	TestNotNull(TEXT("BP_PlanetCharacter 可加载"), BlueprintClass);
	if (BlueprintClass)
	{
		const APlanetCharacter* CDO =
			Cast<APlanetCharacter>(BlueprintClass->GetDefaultObject());
		TestNotNull(TEXT("BP_PlanetCharacter CDO"), CDO);
		if (CDO)
		{
			TestNotNull(TEXT("CharacterAnimClass 已配置"), CDO->CharacterAnimClass.Get());
			if (CDO->CharacterAnimClass)
			{
				TestEqual(TEXT("CharacterAnimClass 指向 ABP_PlanetCharacter"),
					CDO->CharacterAnimClass->GetName(),
					FString(TEXT("ABP_PlanetCharacter_C")));
			}
		}
	}

	return true;
}

#endif
