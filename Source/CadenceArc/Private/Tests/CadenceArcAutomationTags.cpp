#include "Tests/CadenceArcAutomationTags.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "NativeGameplayTags.h"

namespace CadenceArc::Tests
{
	FGameplayTag AutomationTag(const TCHAR* TagName)
	{
		check(IsInGameThread());
		// 模块卸载时随静态变量销毁，原生 Tag 随之注销
		static TMap<FName, TUniquePtr<FNativeGameplayTag>> Registered;
		const FName Name(TagName);
		TUniquePtr<FNativeGameplayTag>& Tag = Registered.FindOrAdd(Name);
		if (!Tag)
		{
			Tag = MakeUnique<FNativeGameplayTag>(UE_PLUGIN_NAME, UE_MODULE_NAME, Name, TEXT("CadenceArc automation test tag"),
			                                     ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
		}
		return Tag->GetTag();
	}
}

#endif
