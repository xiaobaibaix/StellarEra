// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraCosmos.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"

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
				// 开关的真源是关卡片里那个 EditorPlanetControls 的勾选属性。
				// 直接反射读它，不依赖 CVar 同步（面板/Python/撤销都可能绕过属性回调）。
				bool bEnabled = false;
				bool bFoundSwitch = false;
				if (GEngine)
				{
					for (const FWorldContext& Context : GEngine->GetWorldContexts())
					{
						UWorld* World = Context.World();
						if (!World || Context.WorldType != EWorldType::Editor)
						{
							continue;
						}
						for (TActorIterator<AActor> It(World); It; ++It)
						{
							if (const FBoolProperty* Switch = CastField<FBoolProperty>(
								It->GetClass()->FindPropertyByName(TEXT("bEditorMappingPreview"))))
							{
								bEnabled = Switch->GetPropertyValue_InContainer(*It);
								bFoundSwitch = true;
								break;
							}
						}
						break;
					}
				}

				// 没有控制 Actor 时退回 CVar，保留控制台开关。
				if (!bFoundSwitch)
				{
					const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
						TEXT("StellarEra.Cosmos.EditorPreview"));
					bEnabled = CVar && CVar->GetInt() != 0;
				}

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
