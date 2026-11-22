// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraCosmos.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "CosmosLayoutPreview.h"
#endif

/**
 * 非 primary 模块（primary game module 全项目只能有一个，它留在 StellarEra.cpp）。
 * 编辑器下额外挂一个 ticker：CVar StellarEra.Cosmos.EditorPreview 打开时，
 * 每 0.1 秒按映射重新摆放天体，方便在编辑器里转相机看布局与尺寸。
 */
class FStellarEraCosmosModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([](float)
			{
				const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
					TEXT("StellarEra.Cosmos.EditorPreview"));
				const bool bEnabled = CVar && CVar->GetInt() != 0;
				StellarEraCosmosUpdateEditorPreview(bEnabled);
				return true;
			}), 0.1f);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		}
#endif
	}

private:
	FTSTicker::FDelegateHandle TickerHandle;
};

IMPLEMENT_MODULE(FStellarEraCosmosModule, StellarEraCosmos);
