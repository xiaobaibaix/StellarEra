// Copyright Epic Games, Inc. All Rights Reserved.

#include "StellarEraCelestial.h"
#include "Modules/ModuleManager.h"

// 非 primary 模块。primary game module 全项目只能有一个，它留在 StellarEra.cpp。
//
// 原来的 StellarEraPlanetEditor（Editor 类型模块）已并入本模块：.uproject 的模块条目
// 不支持自定义路径，编辑器专用代码只能靠 #if WITH_EDITOR 与 Target.bBuildEditor 隔离。
#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "GeoBakeCache.h"
#include "GeoBakeNowCustomization.h"
#include "PropertyEditorModule.h"
#include "SGeoViewerPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

namespace
{
	const FName GeoViewerTabName(TEXT("StellarEraGeoViewer"));
	FDelegateHandle GPreExitSaveHandle;
}

void RegisterGeoBakeNotifications();
void UnregisterGeoBakeNotifications();

// 注册「地质查看器」面板（窗口 → 工具）与打开它的控制台命令。
class FStellarEraCelestialModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(GeoViewerTabName,
			FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SGeoViewerPanel)];
			}))
			.SetDisplayName(FText::FromString(TEXT("地质查看器")))
			.SetTooltipText(FText::FromString(TEXT("按阶段查看地质图层：全球展开地图、过球心剖面、读数与图例、导出 PNG")))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
		RegisterGeoBakeNotifications();

		// 「立即烘焙」属性行画成按钮（只改行外观，分类嵌套不受影响）。
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomPropertyTypeLayout(FGeoBakeNowAction::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGeoBakeNowCustomization::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();

		// 关编辑器时把脏的烘焙缓存静默落盘。
		GPreExitSaveHandle = FCoreDelegates::OnPreExit.AddLambda([]() { GeoBakeCache::SaveDirty(); });
	}

	virtual void ShutdownModule() override
	{
		UnregisterGeoBakeNotifications();
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomPropertyTypeLayout(FGeoBakeNowAction::StaticStruct()->GetFName());
		}
		FCoreDelegates::OnPreExit.Remove(GPreExitSaveHandle);
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GeoViewerTabName);
		}
	}
};

IMPLEMENT_MODULE(FStellarEraCelestialModule, StellarEraCelestial);

static FAutoConsoleCommand CGeoOpenViewer(
	TEXT("StellarEra.Geo.OpenViewer"),
	TEXT("打开地质查看器面板（也在 窗口 → 工具 → 地质查看器）。"),
	FConsoleCommandDelegate::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(GeoViewerTabName); }));
#else
IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraCelestial);
#endif
