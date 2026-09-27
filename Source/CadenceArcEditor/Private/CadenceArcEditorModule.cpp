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
		NSLOCTEXT("CadenceArcEditor", "DebuggerTabTitle", "CadenceArc Debugger"))
	.SetTooltipText(
		NSLOCTEXT("CadenceArcEditor", "DebuggerTabTooltip", 
			"Read-only runtime view of a CadenceArc resolver."))
	.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());
}

void FCadenceArcEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("CadenceArcEditor");
}

void FCadenceArcEditorModule::RefreshResolvers()
{
	UCadenceArcResolver* CurrentResolver = SelectedResolver.Get();
	Options.Empty();
	for (TObjectIterator<UCadenceArcResolver> It; It; ++It)
	{
		UCadenceArcResolver* Resolver = *It;
		if (!IsValid(Resolver) || !Resolver->IsInitialized())
			continue;

		const UWorld* World = Resolver->GetWorld();
		if (!IsValid(World) || !World->IsPlayInEditor())
			continue;

		const AActor* Actor = Resolver->GetTypedOuter<AActor>();
		const FString OwnerName = Actor ? Actor->GetName() : Resolver->GetName();
		const FString Label =
			FString::Printf(TEXT("%s @ %s"), *OwnerName, *World->GetName());

		Options.Add(MakeShared<FResolverOption>(
			FResolverOption{.Resolver = Resolver, .Label = Label}
		));
	}
	Options.Sort([](const TSharedPtr<FResolverOption>& A, const TSharedPtr<FResolverOption>& B)
	{
		return A->Label < B->Label;
	});
	ResolverCombo->RefreshOptions();
	//若原选择仍在列表中就保留，否则清空选择。
	if (CurrentResolver && Options.ContainsByPredicate(
		[CurrentResolver](const TSharedPtr<FResolverOption>& Option)
		{
			return Option->Resolver == CurrentResolver;
		}))
	{
		SelectedResolver = CurrentResolver;
	}
	else
	{
		SelectedResolver.Reset();
	}
}

// 第二个参数必须与模块名完全一致：.uplugin 的 Name、Build.cs 的类名、文件夹名
IMPLEMENT_MODULE(FCadenceArcEditorModule, CadenceArcEditor)
