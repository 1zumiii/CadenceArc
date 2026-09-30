#pragma once

#include "CoreMinimal.h"

struct FCadenceArcGraphLayout;

// 鼠标命中测试（布局坐标）。节点优先于边：落在节点矩形里就返回该节点，否则返回离点最近、
// 距离不超过 Tolerance 的边的实际绘制路径；都没有命中时返回 INDEX_NONE。距离相同按索引取小的，结果确定。
// 引用边的标记矩形内部算距离 0，接入线和 Path 一样按线段算。
int32 HitTestNode(const FCadenceArcGraphLayout& Layout, const FVector2D& Point);
int32 HitTestEdge(const FCadenceArcGraphLayout& Layout, const FVector2D& Point, double Tolerance);
