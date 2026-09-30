// 紧凑链（ECadenceArcLayoutMode::CompactChains）：把一进一出的简单链收成一个格子，链内成员纵向展开。

#include "CadenceArcLayoutBuild.h"

namespace CadenceArc::Editor::LayoutBuild
{
	namespace
	{
		// 按真实入/出边计数，而不是去重后的邻居数：平行条件、坏目标、不可达来源都不能被悄悄藏进链。
		// 分组只接收一进一出的中间节点；入口、分叉、汇合及接触 DFS 回边的节点保留在外层。
		void CollectChains(FBuildContext& Context)
		{
			FCadenceArcGraphLayout& Layout = Context.Layout;
			TArray<TArray<int32>> Incoming;
			TArray<TArray<int32>> Outgoing;
			Incoming.SetNum(Layout.Nodes.Num());
			Outgoing.SetNum(Layout.Nodes.Num());
			for (int32 Index = 0; Index < Layout.Edges.Num(); ++Index)
			{
				const FCadenceArcLayoutEdge& Edge = Layout.Edges[Index];
				Outgoing[Edge.SourceNodeIndex].Add(Index);
				if (!Edge.IsBrokenTarget())
				{
					Incoming[Edge.TargetNodeIndex].Add(Index);
				}
			}
			TArray<bool> Eligible;
			Eligible.Init(false, Layout.Nodes.Num());
			for (int32 Index = 0; Index < Layout.Nodes.Num(); ++Index)
			{
				Eligible[Index] = Index != Context.EntryIndex && Layout.Nodes[Index].bReachable
					&& Incoming[Index].Num() == 1 && Outgoing[Index].Num() == 1
					&& Context.IsForwardEdge(Layout.Edges[Incoming[Index][0]])
					&& Context.IsForwardEdge(Layout.Edges[Outgoing[Index][0]]);
			}
			for (int32 Index = 0; Index < Layout.Nodes.Num(); ++Index)
			{
				if (!Eligible[Index] || Eligible[Layout.Edges[Incoming[Index][0]].SourceNodeIndex])
				{
					continue; // 只从每段最大简单链的头部收集，数组次序不必等于执行次序。
				}
				FCadenceArcLayoutChain Chain;
				int32 Next = Index;
				while (Eligible[Next])
				{
					Chain.NodeIndices.Add(Next);
					Next = Layout.Edges[Outgoing[Next][0]].TargetNodeIndex;
				}
				if (Chain.NodeIndices.Num() >= 2)
				{
					Layout.FoldedChains.Add(MoveTemp(Chain));
				}
			}
		}
	}

	bool FBuildContext::IsChainEdge(const FCadenceArcLayoutEdge& Edge) const
	{
		return !Edge.IsBrokenTarget() && ChainOfNode[Edge.SourceNodeIndex] != INDEX_NONE
			&& ChainOfNode[Edge.SourceNodeIndex] == ChainOfNode[Edge.TargetNodeIndex];
	}

	void FindCompactChains(FBuildContext& Context)
	{
		const FCadenceArcLayoutParams& Params = Context.Params;
		if (Params.Mode == ECadenceArcLayoutMode::CompactChains)
		{
			CollectChains(Context);
		}
		Context.ChainOfNode.Init(INDEX_NONE, Context.NumNodes);
		Context.NodeOffset.Init(0.0, Context.NumNodes);
		// 内部的横线从相邻节点的间隙经过，圆角与最粗的高亮线也需要空间。
		Context.ChainGap = FMath::Max(static_cast<double>(Params.NodeGap),
			2.0 * (Params.CornerRadius + Params.ChannelGap + Params.ChannelHeight * 0.5));
		Context.ChainStub = FMath::Max(static_cast<double>(Params.ReturnStub),
			Params.CornerRadius + Params.ChannelGap + Params.ChannelHeight * 0.5);
		for (int32 ChainIndex = 0; ChainIndex < Context.Layout.FoldedChains.Num(); ++ChainIndex)
		{
			double Offset = 0.0;
			for (const int32 Member : Context.Layout.FoldedChains[ChainIndex].NodeIndices)
			{
				Context.ChainOfNode[Member] = ChainIndex;
				Context.NodeOffset[Member] = Offset;
				Offset += Context.Layout.Nodes[Member].Size.Y + Context.ChainGap;
			}
		}
	}

	void FinalizeChainBounds(FBuildContext& Context)
	{
		FCadenceArcGraphLayout& Layout = Context.Layout;
		for (FCadenceArcLayoutChain& Chain : Layout.FoldedChains)
		{
			for (const int32 Member : Chain.NodeIndices)
			{
				Chain.Bounds += Layout.Nodes[Member].Position;
				Chain.Bounds += Layout.Nodes[Member].Position + Layout.Nodes[Member].Size;
			}
			// 横向包含局部绕线；纵向只添加分组轮廓留白，不能侵占相邻格子的通道。
			Chain.Bounds = FBox2D(Chain.Bounds.Min - FVector2D(Context.ChainStub + 4.0, 4.0),
				Chain.Bounds.Max + FVector2D(Context.ChainStub + 4.0, 4.0));
			Layout.Size.X = FMath::Max(Layout.Size.X, Chain.Bounds.Max.X + Context.Params.Padding);
			Layout.Size.Y = FMath::Max(Layout.Size.Y, Chain.Bounds.Max.Y + Context.Params.Padding);
		}
	}
}
