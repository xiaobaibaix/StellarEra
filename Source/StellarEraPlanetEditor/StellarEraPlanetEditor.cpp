#include "StellarEraPlanetEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "GeoBakeCache.h"
#include "GeoBakeNowCustomization.h"
#include "PropertyEditorModule.h"
#include "SGeoViewerPanel.h"
#include "VoxelPlanetActor.h"
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
		RegisterGeoBakeNotifications();

		// 「立即烘焙」属性行画成按钮（只改行外观，分类嵌套不受影响）。
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomPropertyTypeLayout(FGeoBakeNowAction::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FGeoBakeNowCustomization::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();

		// 关编辑器时把脏的烘焙缓存静默落盘。必须用 OnEnginePreExit：OnPreExit 阶段
		// AssetTools 已卸载，SavePackage 的委托再去加载它会触发 bWasUnloadedAtShutdown 断言。
		GPreExitSaveHandle = FCoreDelegates::OnEnginePreExit.AddLambda([]() { GeoBakeCache::SaveDirty(); });
	}

	virtual void ShutdownModule() override
	{
		UnregisterGeoBakeNotifications();
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomPropertyTypeLayout(FGeoBakeNowAction::StaticStruct()->GetFName());
		}
		FCoreDelegates::OnEnginePreExit.Remove(GPreExitSaveHandle);
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
