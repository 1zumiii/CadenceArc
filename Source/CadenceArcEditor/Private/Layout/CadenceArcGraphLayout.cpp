#include "CadenceArcGraphLayout.h"

#include "Layout/Stages/CadenceArcLayoutBuild.h"

// 布局算法的整体说明见 CadenceArcLayoutBuild.h；各阶段的实现按主题分在 Layout/ 下的几个文件里。
// 这里只规定阶段的先后顺序：后面的阶段依赖前面阶段写入 FBuildContext 的字段。
FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph, const FCadenceArcLayoutParams& Params)
{
	using namespace CadenceArc::Editor::LayoutBuild;

	FCadenceArcGraphLayout Layout;
	FBuildContext Context(Graph, Params, Layout);
	ReadGraph(Context);
	if (Context.NumNodes == 0)
	{
		Layout.Size = FVector2D(2.0 * Params.Padding, 2.0 * Params.Padding);
		return Layout;
	}

	// 1、2：回边与可达性、（紧凑模式下的）简单链、列号、引用边
	MarkBackEdgesAndReachability(Context);
	FindCompactChains(Context);
	AssignColumns(Context);
	MarkReferences(Context);

	// 3、4：格子与列内顺序、纵坐标、通道空闲带、不可达节点
	BuildLayerItems(Context);
	OrderColumns(Context);
	AssignPortSlots(Context);
	PlaceItems(Context);
	ComputeChannelBands(Context);
	PlaceUnreachableNodes(Context);

	// 5：底部通道、接入点、绘制路径、画布尺寸
	AssignReturnLanes(Context);
	ComputeEntryPoints(Context);
	BuildEdgePaths(Context);
	ComputeCanvasSize(Context);
	FinalizeChainBounds(Context);
	return Layout;
}
