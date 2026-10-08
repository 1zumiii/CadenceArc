#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CadenceArc::Tests
{
	/**
	 * 自动化测试使用的 Gameplay Tag。第一次请求时才注册为原生 Tag，因此平时打开编辑器时，
	 * 这些 Tag 不会出现在使用本插件的项目的 Tag 选择器里，只存在于运行过测试的进程中。
	 * 注册发生在 Runtime 模块中；编辑器模块不能定义原生 Tag，它的测试也通过这个函数取得 Tag。
	 */
	CADENCEARC_API FGameplayTag AutomationTag(const TCHAR* TagName);

	// 用法与原生 Tag 变量相同，转换为 FGameplayTag 时才注册
	struct FAutomationTag
	{
		const TCHAR* Name;

		FGameplayTag GetTag() const { return AutomationTag(Name); }
		operator FGameplayTag() const { return GetTag(); }
	};
}
#endif
