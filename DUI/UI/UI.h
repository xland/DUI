// ============================================================================
//  DUI/UI/UI.h — UI 层聚合头（01-Layering §2 UI）
//
//  按类拆分的头（M5.6 起）：
//    UI/Style.h    属性、状态、样式值、级联解析、StyleBuilder
//    UI/Element.h  元素树节点（树结构、状态位、本地覆盖、点击绑定）
//    UI/Panel.h    容器基类（AddChild 创建即定型）
//    UI/Text.h     文本控件 + Button
//  本头聚合以上全部并声明跨模块入口（布局 / 命中测试），业务代码仍只
//  include "DUI/DUI.h"。
// ============================================================================

#pragma once

#include "UI/Style.h"
#include "UI/Element.h"
#include "UI/Panel.h"
#include "UI/Text.h"

namespace dui {

// Flex 单遍约束布局（UI/Layout.cpp，04-Layout §6）
void RunLayout(Element* root, const Vec2& sizeDip);
void ClearDirtyTree(Element* root);
// 子树是否含布局失效（Frame 决定是否重排：只重绘的帧跳过 Measure/Arrange）
bool SubtreeLayoutDirty(const Element* e);

// 命中测试与最小输入（UI/Event.cpp，06-Events M3 子集）
Element* HitTest(Element* root, const Vec2& ptDip);
void DispatchClick(Element* hit);
void UpdateHoverChain(Element* previous, Element* current);
void SetPressedChain(Element* hit, bool on);

// 沿祖先链找最近的滚动容器（滚轮路由目标；Event.cpp）
class Scroller;
Scroller* FindScroller(Element* e);

}  // namespace dui
