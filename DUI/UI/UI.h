// ============================================================================
//  DUI/UI/UI.h — UI 层聚合头（01-Layering §2 UI）
//
//  按类拆分的头（M5.6 起）：
//    UI/Style.h    属性、状态、样式值、级联解析、链式样式（dui::Style）
//    UI/Element.h  元素树节点（树结构、状态位、本地覆盖、点击绑定）
//    UI/Panel.h    容器基类（AddChild 创建即定型）
//    UI/Text.h     文本控件 + Button
//    UI/Icon.h     内置矢量图标（IconName + Icon 控件）
//    UI/TextBox.h  文本编辑控件（TextBox 单行 / TextArea 多行 / 密码掩码）
//    UI/Image.h    图像控件（位图；Windows 内置解码器格式）
//    UI/Gif.h      动画图像控件（Pause / Play / Stop / SetLoopCount）
//    UI/Animation.h 命令式属性动画（属性标签 / AnimConfig / AnimationHandle）
//    UI/Focus.h    焦点服务与 Tab 链
//  本头聚合以上全部并声明跨模块入口（布局 / 命中测试），业务代码仍只
//  include "DUI/DUI.h"。
// ============================================================================

#pragma once

#include "UI/Style.h"
#include "UI/Element.h"
#include "UI/Panel.h"
#include "UI/Text.h"
#include "UI/Icon.h"
#include "UI/TextBox.h"
#include "UI/Image.h"
#include "UI/Gif.h"
#include "UI/Animation.h"
#include "UI/Focus.h"

namespace dui {

// Flex 单遍约束布局（UI/Layout.cpp，04-Layout §6）
// scale = 当前 DPI 缩放（图像类控件据此把像素尺寸换算为 DIP）
void RunLayout(Element* root, const Vec2& sizeDip, float scale = 1.f);
void ClearDirtyTree(Element* root);
// 子树是否含布局失效（Frame 决定是否重排：只重绘的帧跳过 Measure/Arrange）
bool SubtreeLayoutDirty(const Element* e);

// 命中测试与最小输入（UI/Event.cpp，06-Events M3 子集）
Element* HitTest(Element* root, const Vec2& ptDip);
void DispatchClick(Element* hit, const Vec2& pt, PointerButton button);
void UpdateHoverChain(Element* previous, Element* current);
void SetPressedChain(Element* hit, bool on);

// 元素级指针事件派发（UI/Event.cpp）：
//   移入/移出 —— 直达事件：按"悬停链"差分触发（进入新链的元素触发移入，
//                 离开旧链的元素触发移出；移到子元素上不算离开父元素）
//   移动/按下/弹起/滚轮 —— 双阶段：隧道（根 → 命中）+ 冒泡（命中 → 根）
// 派发只经过"有绑定者"的元素，无绑定直接跳过；handled 可终止后续阶段。
void DispatchHoverTransitions(Element* previous, Element* current, const Vec2& pt);
void DispatchPointerMove(Element* hit, const Vec2& pt);
void DispatchPointerDown(Element* hit, const Vec2& pt, PointerButton button);
void DispatchPointerUp(Element* hit, const Vec2& pt, PointerButton button);
bool DispatchWheel(Element* hit, const Vec2& pt, float delta, bool horizontal);

// 键盘事件派发（UI/Event.cpp）：从焦点元素出发，双阶段路由（隧道 + 冒泡）。
// 返回 true 表示已被处理（handled）——调用方据此决定默认行为（如 Tab 移焦）。
bool DispatchKeyDown(Element* target, int key, bool ctrl, bool shift, bool alt);
bool DispatchKeyUp(Element* target, int key, bool ctrl, bool shift, bool alt);
bool DispatchChar(Element* target, wchar_t ch);

// 沿祖先链找最近的滚动容器（滚轮路由目标；Event.cpp）
class Scroller;
Scroller* FindScroller(Element* e);

// 沿祖先链找最近的可聚焦元素（点击取得焦点用；Event.cpp）
Element* FindFocusable(Element* e);

// ---------------------------------------------------------------------------
// 剪贴板服务（UI 层不依赖 Platform，01-Layering §1）：
//   平台实现（CF_UNICODETEXT）由 App 层在启动时注入；未注入时剪贴板操作失败。
// ---------------------------------------------------------------------------
using ClipboardReadFn = bool (*)(String& out);
using ClipboardWriteFn = bool (*)(const String& text);
void SetClipboardService(ClipboardReadFn read, ClipboardWriteFn write);

}  // namespace dui
