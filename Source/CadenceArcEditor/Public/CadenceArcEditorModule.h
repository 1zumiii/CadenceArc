#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Runtime 模块不得依赖本模块。
class FCadenceArcEditorModule : public IModuleInterface
{

public:
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;
	
};
