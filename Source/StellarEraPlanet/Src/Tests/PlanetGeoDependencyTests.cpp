// 目录边界靠这条测试守：Core/Snapshot 一旦 include 了 Engine 或 Voxel，求值核就不能脱离引擎测试。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoDependencyTest, "StellarEra.Planet.Geo.Dependency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoDependencyTest::RunTest(const FString& Parameters)
{
	const FString GeoDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/StellarEraPlanet/Src/Geo"));
	struct FRule { const TCHAR* SubDir; TArray<FString> Banned; };
	const TArray<FString> PureBanned = { TEXT("Engine/"), TEXT("CoreUObject"), TEXT("UObject/"), TEXT("GameFramework/"),
		TEXT("Components/"), TEXT("\"Voxel"), TEXT(".generated.h") };
	const FRule Rules[] = {
		{ TEXT("Core"), PureBanned },
		{ TEXT("Snapshot"), PureBanned },
		{ TEXT("Bake"), { TEXT("Engine/World"), TEXT("GameFramework/"), TEXT("Components/"), TEXT("\"Voxel"), TEXT(".generated.h") } },
		// Assets 自己的 .generated.h 合法，只禁体素插件。
		{ TEXT("Assets"), { TEXT("\"Voxel") } },
	};

	int32 Scanned = 0;
	for (const FRule& Rule : Rules)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(GeoDir, Rule.SubDir), TEXT("*.*"), true, false);
		for (const FString& File : Files)
		{
			if (!File.EndsWith(TEXT(".h")) && !File.EndsWith(TEXT(".cpp"))) { continue; }
			TArray<FString> Lines;
			FFileHelper::LoadFileToStringArray(Lines, *File);
			++Scanned;
			for (const FString& Line : Lines)
			{
				if (!Line.TrimStart().StartsWith(TEXT("#include"))) { continue; }
				for (const FString& Bad : Rule.Banned)
				{
					if (Line.Contains(Bad))
					{
						AddError(FString::Printf(TEXT("%s 违反 Geo/%s 依赖规则：%s"),
							*FPaths::GetCleanFilename(File), Rule.SubDir, *Line.TrimStartAndEnd()));
					}
				}
			}
		}
	}
	TestTrue(TEXT("确实扫到了文件"), Scanned >= 15);
	return true;
}

#endif
