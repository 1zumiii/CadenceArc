#include "SCadenceArcDebuggerPanel.h"

#include "SCadenceArcGraphCanvas.h"
#include "SlateOptMacros.h"
#include "Graph/CadenceArcGraph.h"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SCadenceArcDebuggerPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		+SScrollBox::Slot()  // SScrollBox 可以放多个子项，所以用 + Slot()
		[
			SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+SScrollBox::Slot()
			[
				SAssignNew(Canvas, SCadenceArcGraphCanvas) // 和 SNew 一样，但会把指针存进 Canvas
			]
		]
	
	];
	
	// 临时数据源：第 4 步完成后改为选中 Resolver 的 GetGraph()
	Canvas->SetGraph(LoadObject<UCadenceArcGraph>(nullptr,
		TEXT("/Game/Tests/Combos/DA_TestComboGraphAutoRelease.DA_TestComboGraphAutoRelease")));
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION
