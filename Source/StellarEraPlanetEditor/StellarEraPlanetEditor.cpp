#include "StellarEraPlanetEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "SGeoViewerPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

namespace
{
	const FName GeoViewerTabName(TEXT("StellarEraGeoViewer"));
}

class FStellarEraPlanetEditorModule : public IModuleInterface
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
	}

	virtual void ShutdownModule() override
	{
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GeoViewerTabName);
		}
	}
};

IMPLEMENT_MODULE(FStellarEraPlanetEditorModule, StellarEraPlanetEditor);

static FAutoConsoleCommand CGeoOpenViewer(
	TEXT("StellarEra.Geo.OpenViewer"),
	TEXT("打开地质查看器面板（也在 窗口 → 工具 → 地质查看器）。"),
	FConsoleCommandDelegate::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(GeoViewerTabName); }));
