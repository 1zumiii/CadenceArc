#include "CadenceArcEditorModule.h"

#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/SCadenceArcDebuggerPanel.h"

void FCadenceArcEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		"CadenceArcEditor",
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& SpawnTabArgs) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SCadenceArcDebuggerPanel)
				];
		})
	)
	.SetDisplayName(
		NSLOCTEXT("CadenceArcEditor", "DebuggerTabTitle", "Arc Debugger")) // Minor Tab 最宽 160px，全名会被省略；全名见 Tooltip
	.SetTooltipText(
		NSLOCTEXT("CadenceArcEditor", "DebuggerTabTooltip", 
			"CadenceArc Debugger: read-only runtime view of a CadenceArc resolver."))
	.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());
	
}

void FCadenceArcEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("CadenceArcEditor");
}

// 第二个参数必须与模块名完全一致：.uplugin 的 Name、Build.cs 的类名、文件夹名
IMPLEMENT_MODULE(FCadenceArcEditorModule, CadenceArcEditor)
