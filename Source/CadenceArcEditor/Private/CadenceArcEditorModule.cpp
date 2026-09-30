#include "CadenceArcEditorModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/SCadenceArcDebuggerPanel.h"
#include "Widgets/SCadenceArcHistoryPanel.h"

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

	// 调试历史单独一个 Tab，跟随 Arc Debugger 当前选中的 Resolver
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		"CadenceArcHistory",
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& SpawnTabArgs) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SCadenceArcHistoryPanel)
				];
		})
	)
	.SetDisplayName(NSLOCTEXT("CadenceArcEditor", "HistoryTabTitle", "Arc History"))
	.SetTooltipText(
		NSLOCTEXT("CadenceArcEditor", "HistoryTabTooltip",
			"CadenceArc History: what the resolver selected in Arc Debugger did, and why calls failed."))
	.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());
}

void FCadenceArcEditorModule::ShutdownModule()
{
	// 编辑器退出时 Slate 可能已经先关闭，这时不能再访问全局 TabManager
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("CadenceArcEditor");
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("CadenceArcHistory");
}

// 第二个参数必须与模块名完全一致：.uplugin 的 Name、Build.cs 的类名、文件夹名
IMPLEMENT_MODULE(FCadenceArcEditorModule, CadenceArcEditor)
