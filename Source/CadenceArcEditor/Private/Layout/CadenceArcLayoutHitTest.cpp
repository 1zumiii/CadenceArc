#include "CadenceArcLayoutHitTest.h"

#include "CadenceArcGraphLayout.h"

int32 HitTestNode(const FCadenceArcGraphLayout& Layout, const FVector2D& Point)
{
	for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		if (FBox2D(Node.Position, Node.Position + Node.Size).IsInside(Point))
		{
			return Node.NodeIndex;
		}
	}
	return INDEX_NONE;
}

int32 HitTestEdge(const FCadenceArcGraphLayout& Layout, const FVector2D& Point, const double Tolerance)
{
	int32 Best = INDEX_NONE;
	double BestDistance = Tolerance;
	const auto Consider = [&Best, &BestDistance](const int32 EdgeIndex, const double Distance)
	{
		if (Distance < BestDistance || (Distance == BestDistance && Best == INDEX_NONE))
		{
			Best = EdgeIndex;
			BestDistance = Distance;
		}
	};
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		if (Edge.bIsReference && Edge.ReferenceBox.IsInside(Point))
		{
			Consider(EdgeIndex, 0.0);
		}
		for (const TArray<FVector2D>* Path : {&Edge.Path, &Edge.EntryStub})
		{
			for (int32 Index = 1; Index < Path->Num(); ++Index)
			{
				Consider(EdgeIndex, FMath::Sqrt(FMath::PointDistToSegmentSquared(
					FVector(Point, 0.0), FVector((*Path)[Index - 1], 0.0), FVector((*Path)[Index], 0.0))));
			}
		}
	}
	return Best;
}
