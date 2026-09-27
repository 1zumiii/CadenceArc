#pragma once

#include "CoreMinimal.h"
#include "Graph/CadenceArcGraph.h"
#include "Modules/ModuleManager.h"
#include "Resolver/CadenceArcResolver.h"

// Runtime 模块不得依赖本模块。
class FCadenceArcEditorModule : public IModuleInterface
{
private:
	struct FResolverOption
	{
		TWeakObjectPtr<UCadenceArcResolver> Resolver;
		FString Label;
	};

	TArray<TSharedPtr<FResolverOption>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FResolverOption>>> ResolverCombo;
	TWeakObjectPtr<UCadenceArcResolver> SelectedResolver;
	FDelegateHandle EndPIEHandle;

public:
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	void RefreshResolvers();
};
