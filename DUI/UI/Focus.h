// ============================================================================
//  DUI/UI/Focus.h — 焦点服务与 Tab 链（06-Events §6）
//
//  焦点唯一，挂在元素上：换焦点时旧元素清 State::Focused、新元素置位，
//  样式状态变体自动生效（03-Style §6.2），无需开发者干预。
//
//  Tab 链：树序（深度优先）收集可聚焦元素，按 tabIndex 排序——
//    >0 组（数值升序）→ 0 组（文档序）→ <0 组（数值升序）。
//  Shift+Tab 反向移动，到头循环。
// ============================================================================
#pragma once

#include "Core/Core.h"

namespace dui {

class Element;

// 当前焦点元素（无焦点 = nullptr）
Element* FocusedElement() noexcept;

// e 是否为当前焦点元素
bool IsFocused(const Element* e) noexcept;

// 设置焦点：旧元素清 State::Focused，新元素置位；e 为 nullptr 清焦点。
// 重复设置同一元素幂等。
void SetFocus(Element* e);

// 焦点元素被销毁时调用：清焦点（只清状态位，不触发虚钩子 —— 派生部分可能已析构）
void ClearFocusIf(Element* e);

// Tab 链移动焦点：root 子树内可聚焦元素按 tabIndex 排序后移动一格。
// reverse = false → 下一个；true → 上一个（Shift+Tab）。
// 无可聚焦元素 → 清焦点并返回 false。
bool FocusNext(Element* root, bool reverse);

// 闪烁节拍（光标闪烁）：焦点元素参与闪烁时需要定时器节拍。
bool NeedsBlinkTick() noexcept;

// 执行一次闪烁节拍：焦点元素切换闪烁状态（如光标可见性）并标脏。
// 返回 false 表示不需要继续节拍（调用方可停表）。
bool DispatchBlink();

// 焦点元素的插入点（光标）矩形（客户区 DIP）：无焦点或焦点元素不提供插入点
// 时返回 false。IME 候选窗口据此定位到光标处。
bool QueryInsertionPoint(Rect& outRect);

}  // namespace dui
