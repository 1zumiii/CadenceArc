#pragma once

// BuildGraphLayout 的内部阶段，只给 Layout/ 下的实现文件用，画布和面板不应包含它。
//
// 分层布局（Sugiyama 的简化版），每一步都只依赖数组顺序，同一张图每次结果一致：
// 1. 从入口深度优先遍历，标出回边（指向当前递归路径上的节点），同时得到可达性和前序、后序；
// 2. 去掉回边后的图无环，按拓扑序求最长路径作为列号，所有剩余的边都严格指向右侧；
// 3. 跨多列的边在每个中间列放一个通道（不绘制的占位），和真实节点一起按重心排序以减少交叉；
// 4. 纵坐标按连线的端口位置双向对齐，每列保持顺序和最小间距（保序回归），通道同样占高度；
// 5. 目标列不在源列右侧的边分配底部通道；最后生成每条边的实际绘制折线。
//    前向边按 EdgeStyle 画成光滑曲线或直角折线（竖线在列间空隙里分配互不重叠的轨道）。
// 打开 bSortPorts 时，第 3 步排好列内顺序后按出边去向重排端口行，再做第 4 步，纵向对齐用的是新的端口位置。
// 打开引用标记（ReferenceMinSpan）时，跨列多的边在第 2 步之后改成引用：不占通道、不参与排序，只画两端的短标记。
// CompactChains 在分层前把简单链收成一个格子，链内成员纵向展开；外部沿用同一排序与走线算法。
// 因此紧凑模式的 Column 表示显示列，链内前进允许同列，不能再仅凭几何方向判定回边。
// 路径合法性来自结构：穿过中间列时是通道里的水平线，换高度的 S 曲线只出现在两列之间的空隙里，
// 回边和自环的竖线也只在空隙里，所以线不会压在无关节点上。
//
// 阶段之间只通过 FBuildContext 传递数据。每个阶段函数的注释写明它读取和写入的字段，
// 调用顺序见 CadenceArcGraphLayout.cpp 里的 BuildGraphLayout。

#include "CoreMinimal.h"
#include "Layout/CadenceArcGraphLayout.h"

class UCadenceArcGraph;

namespace CadenceArc::Editor::LayoutBuild
{
	// 分层后的一个格子：可达的真实节点，或长边在中间列的通道
	struct FLayerItem
	{
		int32 NodeIndex = INDEX_NONE; // 通道为 INDEX_NONE
		TArray<int32> Members; // 普通格子只有一个成员；紧凑链作为一个整体参与排序和保序回归。
		int32 EdgeIndex = INDEX_NONE; // 通道所属的边
		int32 Column = 0;
		double Height = 0.0;
		double Y = 0.0;
		bool bHasSelfLoop = false; // 自环从节点底部绕过，下方要多留一点
		bool IsChannel() const { return NodeIndex == INDEX_NONE; }
	};

	// 相邻两列之间的一段连线：From 在左列，To 在右列。偏移是连线在格子上的纵向位置
	struct FItemLink
	{
		int32 From = INDEX_NONE;
		int32 To = INDEX_NONE;
		double FromOffset = 0.0;
		double ToOffset = 0.0;
	};

	struct FBuildContext
	{
		FBuildContext(const UCadenceArcGraph& InGraph, const FCadenceArcLayoutParams& InParams, FCadenceArcGraphLayout& InLayout)
			: Graph(InGraph), Params(InParams), Layout(InLayout)
		{
		}

		const UCadenceArcGraph& Graph;
		const FCadenceArcLayoutParams& Params;
		FCadenceArcGraphLayout& Layout; // 结果，各阶段逐步填写

		// 读图
		int32 NumNodes = 0;
		TMap<FGameplayTag, int32> NodeIndexMap; // 重复 Tag 指向数组中的第一个节点
		int32 EntryIndex = INDEX_NONE; // 入口节点不存在时为 INDEX_NONE
		TArray<TArray<int32>> OutEdges; // 每个节点的出边在 Layout.Edges 中的索引，按 Transition 顺序

		// 深度优先
		TArray<uint8> Visit; // 0 未访问，1 在当前递归路径上，2 已完成
		TArray<int32> PreOrder; // 节点首次访问的序号，未访问为 INDEX_NONE
		TArray<int32> PostOrder; // 按完成顺序排列的节点
		int32 NextPreOrder = 0;

		// 紧凑链（分层模式下没有链，数组仍然初始化）
		TArray<int32> ChainOfNode; // 节点所在链在 Layout.FoldedChains 中的索引，不在链中为 INDEX_NONE
		TArray<double> NodeOffset; // 节点在所在格子里的纵向偏移（链内成员依次往下排）
		double ChainGap = 0.0; // 链内相邻成员之间的空隙
		double ChainStub = 0.0; // 链内连线离开端口、接近目标时的水平距离

		// 列号
		int32 NumReachableColumns = 0;

		// 格子、通道与相邻列之间的连线
		TArray<FLayerItem> Items;
		TArray<int32> ItemOfNode; // 节点所在的格子，不可达为 INDEX_NONE
		TArray<TArray<int32>> EdgeChannels; // 每条前向边依次经过的通道
		TArray<FItemLink> Links;
		TArray<int32> EdgeFirstLink; // 每条前向边从源节点出发的那段连线，端口重排后要按新行号改它的 FromOffset
		TArray<TArray<int32>> IncomingLinks;
		TArray<TArray<int32>> OutgoingLinks;
		TArray<TArray<int32>> ItemPredecessors;
		TArray<TArray<int32>> ItemSuccessors;

		// 排序与纵坐标
		TArray<TArray<int32>> Columns; // 每列的格子，自上而下
		TArray<int32> ItemRow; // 格子在所在列里的位置（含通道），与 Columns 一致
		TArray<TPair<double, double>> ChannelBands; // 每个通道格子的空闲带 [上界, 下界]

		// 走线
		double ContentBottom = 0.0; // 节点和通道的最低点，底部通道从这里往下排
		TArray<FVector2D> EntryPoints; // 每条边在目标标题行左侧的接入点

		// 参与列号和排序的边：两端都可达、目标有效、不是回边
		bool IsForwardEdge(const FCadenceArcLayoutEdge& Edge) const;
		// 同一紧凑链内相邻成员之间的边
		bool IsChainEdge(const FCadenceArcLayoutEdge& Edge) const;
		double ColumnX(int32 Column) const;
		double LaneY(int32 Lane) const;
		FVector2D PortPoint(const FCadenceArcLayoutEdge& Edge) const;
	};

	// ---- CadenceArcLayoutRanking.cpp：读图、深度优先、列号、引用边 ----

	// 写 Layout.Nodes（尺寸）、Layout.Edges（含坏边、自环、平行边）、NumNodes、NodeIndexMap、EntryIndex、OutEdges
	void ReadGraph(FBuildContext& Context);
	// 从入口深度优先：写 Edge.bIsBackEdge、Node.bReachable、Visit、PreOrder、PostOrder
	void MarkBackEdgesAndReachability(FBuildContext& Context);
	// 最长路径列号（链内边长度为 0）：写可达节点的 Node.Column、NumReachableColumns
	void AssignColumns(FBuildContext& Context);
	// 列号差不小于 ReferenceMinSpan 的边：写 Edge.bIsReference
	void MarkReferences(FBuildContext& Context);

	// ---- CadenceArcLayoutChains.cpp：紧凑链 ----

	// 紧凑模式下找出简单链：写 Layout.FoldedChains、ChainOfNode、NodeOffset、ChainGap、ChainStub
	void FindCompactChains(FBuildContext& Context);
	// 所有位置确定后计算每条链的外框，并在需要时扩大 Layout.Size
	void FinalizeChainBounds(FBuildContext& Context);

	// ---- CadenceArcLayoutOrdering.cpp：格子与列内排序 ----

	// 写 Items、ItemOfNode、EdgeChannels、Links 及四个邻接表
	void BuildLayerItems(FBuildContext& Context);
	// 初始按前序，再做重心扫描：写 Columns、ItemRow、可达节点的 Node.Row、Layout.MaxRows
	void OrderColumns(FBuildContext& Context);
	// 打开 bSortPorts 时按出边下一步去向的上下顺序重排端口行：写 Edge.PortSlot，并同步各边第一段连线的 FromOffset
	void AssignPortSlots(FBuildContext& Context);

	// ---- CadenceArcLayoutPlacement.cpp：纵坐标 ----

	// 紧排后按连线端口双向对齐，整体下移到留白处：写 Item.Y、可达节点的 Node.Position
	void PlaceItems(FBuildContext& Context);
	// 写 ChannelBands
	void ComputeChannelBands(FBuildContext& Context);
	// 不可达节点放在最后一列：写它们的 Column、Row、Position，以及 Layout.NumColumns、Layout.MaxRows
	void PlaceUnreachableNodes(FBuildContext& Context);

	// ---- CadenceArcLayoutRouting.cpp：走线 ----

	// 写 Edge.ReturnLane、Layout.NumReturnLanes、ContentBottom
	void AssignReturnLanes(FBuildContext& Context);
	// 写 EntryPoints
	void ComputeEntryPoints(FBuildContext& Context);
	// 写 Edge.Path，引用边另写 ReferenceBox、EntryStub
	void BuildEdgePaths(FBuildContext& Context);
	// 写 Layout.Size（紧凑链外框另由 FinalizeChainBounds 扩大）
	void ComputeCanvasSize(FBuildContext& Context);
}
