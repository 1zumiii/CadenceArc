#include "CadenceArcDebuggerSelection.h"

#include "Resolver/CadenceArcResolver.h"

namespace CadenceArc::Editor::DebuggerSelection
{
	namespace
	{
		TWeakObjectPtr<UCadenceArcResolver> GSelectedResolver;
		FString GSelectedLabel;
		FCadenceArcHistoryFocus GHistoryFocus;
	}

	void Set(UCadenceArcResolver* Resolver, const FString& Label)
	{
		if (Resolver != GSelectedResolver.Get())
		{
			GHistoryFocus = FCadenceArcHistoryFocus(); // 历史定位属于原来的实例
		}
		GSelectedResolver = Resolver;
		GSelectedLabel = Resolver ? Label : FString();
	}

	void Clear()
	{
		Set(nullptr, FString());
	}

	UCadenceArcResolver* GetResolver()
	{
		return GSelectedResolver.Get();
	}

	FString GetLabel()
	{
		return GSelectedResolver.IsValid() ? GSelectedLabel : FString();
	}

	void SetHistoryFocus(const FCadenceArcHistoryFocus& Focus)
	{
		GHistoryFocus = Focus;
	}

	const FCadenceArcHistoryFocus& GetHistoryFocus()
	{
		return GHistoryFocus;
	}
}
