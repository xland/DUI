// ============================================================================
//  DUI/UI/Focus.cpp — 焦点服务与 Tab 链的实现
// ============================================================================

#include "UI/Focus.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "Core/Dispatcher.h"
#include "UI/Element.h"

namespace dui {

namespace {

Element* g_focus = nullptr;   // 当前焦点（唯一，挂在元素上）

// 焦点变化 → 唤醒消息泵：主循环可能正阻塞在 WaitMessage（焦点可能由程序
// 代码直接设置，无输入消息），需要醒来重绘并同步闪烁节拍（07-Threading 泵）。
void WakeUiPump() {
    if (Dispatcher* d = Dispatcher::Ui()) d->Post([] {});
}

// 深度优先收集可聚焦元素（文档序）
void CollectFocusable(Element* e, std::vector<Element*>& out) {
    if (e == nullptr) return;
    if (e->IsFocusable()) out.push_back(e);
    for (Element* c : e->Children()) CollectFocusable(c, out);
}

// Tab 链排序键：>0 组 → 0 组 → <0 组；组内按 tabIndex 升序（0 组保持文档序）
std::pair<int, int> TabKey(Element* e) {
    const int ti = e->TabIndex();
    if (ti > 0) return {0, ti};
    if (ti == 0) return {1, 0};
    return {2, ti};
}

}  // namespace

Element* FocusedElement() noexcept { return g_focus; }

bool IsFocused(const Element* e) noexcept { return e != nullptr && e == g_focus; }

void SetFocus(Element* e) {
    if (e == g_focus) return;
    Element* old = g_focus;
    g_focus = e;                       // 先更新全局态，钩子里查到的即新焦点
    if (old != nullptr) {
        old->SetState(State::Focused, false);
        old->OnFocusChanged(false);
    }
    if (e != nullptr) {
        e->SetState(State::Focused, true);
        e->OnFocusChanged(true);
    }
    WakeUiPump();   // 焦点可能由程序代码设置（无输入消息）→ 唤醒泵重绘
}

void ClearFocusIf(Element* e) {
    if (g_focus != e) return;
    g_focus = nullptr;
    e->SetState(State::Focused, false);
    WakeUiPump();   // 光标需消失 → 唤醒泵重绘并停表
}

bool FocusNext(Element* root, bool reverse) {
    std::vector<Element*> list;
    CollectFocusable(root, list);
    if (list.empty()) {
        SetFocus(nullptr);
        return false;
    }
    std::stable_sort(list.begin(), list.end(),
                     [](Element* a, Element* b) { return TabKey(a) < TabKey(b); });

    const int n = static_cast<int>(list.size());
    int cur = -1;
    for (int i = 0; i < n; ++i) {
        if (list[static_cast<size_t>(i)] == g_focus) { cur = i; break; }
    }
    int next;
    if (cur < 0) {
        next = reverse ? n - 1 : 0;          // 当前焦点不在链上 → 取端点
    } else {
        next = reverse ? (cur - 1 + n) % n : (cur + 1) % n;
    }
    SetFocus(list[static_cast<size_t>(next)]);
    return true;
}

// ---------------------------------------------------------------------------
// 闪烁节拍（光标闪烁）
// ---------------------------------------------------------------------------

bool NeedsBlinkTick() noexcept {
    return g_focus != nullptr && g_focus->IsBlinkable();
}

bool DispatchBlink() {
    if (g_focus == nullptr) return false;
    return g_focus->OnBlink();
}

bool QueryInsertionPoint(Rect& outRect) {
    if (g_focus == nullptr) return false;
    return g_focus->InsertionPoint(outRect);
}

}  // namespace dui
