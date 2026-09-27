#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SCadenceArcGraphCanvas;
/**
 * 
 */
class SCadenceArcDebuggerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCadenceArcDebuggerPanel)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<SCadenceArcGraphCanvas> Canvas;
};
