#include "CadenceArcDebuggerSelection.h"

#include "Resolver/CadenceArcResolver.h"

namespace CadenceArc::Editor::DebuggerSelection
{
	namespace
	{
		TWeakObjectPtr<UCadenceArcResolver> GSelectedResolver;
		FString GSelectedLabel;
	}

	void Set(UCadenceArcResolver* Resolver, const FString& Label)
	{
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
}
