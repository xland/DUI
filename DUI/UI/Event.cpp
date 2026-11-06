// ============================================================================
//  DUI/UI/Event.cpp — 命中测试与最小输入支持（06-Events，M3 子集）
//
//  M3 范围：按 Rect 的最深命中、hover/pressed 状态链维护（含祖先，CSS 直觉）、
//  Click 合成（按下抬起同元素）。
//  M6 追加：双阶段路由（隧道 + 冒泡，Phase 参数）与指针捕获（Element::SetCapture）。
//  M5.8 追加：元素级指针事件（移入/移出/移动/按下/弹起/滚轮）——
//    移入/移出按"悬停链"差分直达；移动/按下/弹起/滚轮冒泡，handled 可终止；
//    派发只经过有绑定者的元素（无绑定直接跳过），不做防抖与节流。
//  M5.9 追加：元素级键盘事件（KeyDown/KeyUp/Char）——从焦点元素出发双阶段路由；
//    派发前先走控件内部钩子（Element::Handle*），钩子消费则不再向上冒泡。
//
//  e.source 语义（按两个示例的用法实现）：处理器宿主元素 —— 点击落在子元素上
//  时冒泡到绑定处理器者，e.source 即该元素（06-Events §4.3 的"命中目标"以
//  示例为准收敛为此语义）。
// ============================================================================

#include "UI/UI.h"

#include "UI/Scroller.h"

namespace dui {

namespace {

// 指针捕获：窗口级唯一目标（06-Events §5）。捕获期间 Move/Up 直达该元素；
// 抬起由 App 层释放，元素析构由 ClearCaptureIf 清理。
Element* g_capture = nullptr;

bool InRect(const Rect& r, const Vec2& p) {
    return p.x >= r.x && p.x <= r.Right() && p.y >= r.y && p.y <= r.Bottom();
}

// 命中测试：ptAbs 是"未变换布局空间"的客户区绝对 DIP（与 Bounds() 同空间）。
// 子元素带 transform 时，先把指针逆变换到该元素的局部空间再判断；逐层施加
// 逆矩阵等价于整体 T⁻¹，嵌套自动成立（10-Rendering §6.3）。
Element* HitTestInner(Element* e, const Vec2& ptAbs) {
    const std::vector<Element*>& cs = e->Children();
    for (auto it = cs.rbegin(); it != cs.rend(); ++it) {   // 后绘制者在上：倒序命中
        Element* c = *it;
        const Transform2D t = ElementTransform(c);
        if (!t.IsInvertible()) continue;   // Scale → 0：元素缩没，不可命中
        const Vec2 qc = t.Inverse().Apply(ptAbs);
        const Rect& b = c->Bounds();
        if (b.w > 0.f && b.h > 0.f && InRect(b, qc)) {
            if (c->GetKind() == Kind::Scroller) {
                auto* sc = static_cast<Scroller*>(c);
                bool vertical = false;
                // 滚动条区域优先命中滚动容器本身（否则会穿透到被裁剪的内容）
                if (sc->HitScrollBar(qc, vertical)) return c;
                // 视口外（padding 区）同样归容器；视口内换算到内容坐标再命中子元素
                const Rect vp = sc->Viewport();
                if (!InRect(vp, qc)) return c;
                const Vec2 off = sc->ScrollOffset();
                return HitTestInner(c, Vec2{qc.x + off.x, qc.y + off.y});
            }
            return HitTestInner(c, qc);
        }
    }
    return e;
}

}  // namespace

// ---- 指针捕获服务（Element::SetCapture / ReleaseCapture 的入口）-------------
void SetCaptureElement(Element* e) noexcept { g_capture = e; }
Element* CapturedElement() noexcept { return g_capture; }
void ClearCaptureIf(Element* e) noexcept { if (g_capture == e) g_capture = nullptr; }

Element* HitTest(Element* root, const Vec2& pt) {
    if (root == nullptr) return nullptr;
    const Transform2D t = ElementTransform(root);
    if (!t.IsInvertible()) return nullptr;
    const Vec2 q = t.Inverse().Apply(pt);
    if (!InRect(root->Bounds(), q)) return nullptr;
    return HitTestInner(root, q);
}

// 沿祖先链找最近的滚动容器（滚轮事件的路由目标）
Scroller* FindScroller(Element* e) {
    for (Element* p = e; p != nullptr; p = p->Parent()) {
        if (p->GetKind() == Kind::Scroller) return static_cast<Scroller*>(p);
    }
    return nullptr;
}

// 沿祖先链找最近的可聚焦元素（点击取得焦点用；06-Events §6）
Element* FindFocusable(Element* e) {
    for (Element* p = e; p != nullptr; p = p->Parent()) {
        if (p->IsFocusable()) return p;
    }
    return nullptr;
}

// Click 的两阶段派发：隧道（根 → 命中）→ 冒泡（命中 → 根）。
// 用同一个事件对象贯穿，任一阶段置 handled 会终止后续全部派发（06-Events §5）。
void DispatchClick(Element* hit) {
    if (hit == nullptr) return;
    std::vector<Element*> chain;
    for (Element* e = hit; e != nullptr; e = e->Parent()) chain.push_back(e);
    ClickEvent ev;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {   // 隧道：根 → 命中
        Element* e = *it;
        if (!e->onTunnelClick_) continue;
        ev.source = e;
        e->onTunnelClick_(ev);
        if (ev.handled) return;
    }
    for (Element* e : chain) {                                   // 冒泡：命中 → 根
        if (!e->onClick_) continue;
        ev.source = e;
        e->onClick_(ev);
        if (ev.handled) return;
    }
}

// hover / pressed 沿祖先链生效（CSS 直觉：指针在子文本上 = 列表项 hover）
void UpdateHoverChain(Element* previous, Element* current) {
    for (Element* e = previous; e != nullptr; e = e->Parent()) e->SetState(State::Hover, false);
    for (Element* e = current; e != nullptr; e = e->Parent()) e->SetState(State::Hover, true);
}

void SetPressedChain(Element* hit, bool on) {
    if (hit == nullptr) return;
    for (Element* e = hit; e != nullptr; e = e->Parent()) e->SetState(State::Pressed, on);
}

// ---------------------------------------------------------------------------
// 元素级指针事件（M5.8）
// ---------------------------------------------------------------------------

namespace {

// e 是否在以 head 为头的祖先链上（悬停链差分用）
bool InChain(Element* e, Element* head) {
    for (Element* p = head; p != nullptr; p = p->Parent()) {
        if (p == e) return true;
    }
    return false;
}

// 冒泡派发：从命中元素向上，逐元素看是否有绑定；无绑定直接跳过；
// handled 为真则终止冒泡。返回是否被处理。
// hook = 控件内部钩子（子类重写）：先于绑定执行；返回 true 表示已消费
// （事件不再向上冒泡），但同一元素上的绑定仍会收到该事件。
template <class E>
bool BubbleDispatch(Element* hit, E& ev,
                    std::function<void(E&)> Element::Handlers::*slot,
                    bool (Element::*hook)(E&) = nullptr) {
    for (Element* e = hit; e != nullptr; e = e->Parent()) {
        const bool consumed = (hook != nullptr) ? (e->*hook)(ev) : false;
        const Element::Handlers* h = e->HandlersPtr();
        if (h != nullptr) {
            const std::function<void(E&)>& fn = h->*slot;
            if (fn) {
                ev.source = e;
                fn(ev);
            }
        }
        if (consumed || ev.handled) return true;
    }
    return false;
}

// 隧道派发：根 → 命中元素。用递归回溯得到根优先的顺序，零额外缓冲/分配。
// 控件内部钩子（Handle*）不参与隧道，仍只在冒泡阶段执行。
template <class E>
bool TunnelDispatch(Element* e, E& ev,
                    std::function<void(E&)> Element::Handlers::*slot) {
    if (e == nullptr) return false;
    if (e->Parent() != nullptr) {
        TunnelDispatch(e->Parent(), ev, slot);
        if (ev.handled) return true;   // 上层已拦截 → 子元素不再收到
    }
    const Element::Handlers* h = e->HandlersPtr();
    if (h != nullptr) {
        const std::function<void(E&)>& fn = h->*slot;
        if (fn) {
            ev.source = e;
            fn(ev);
        }
    }
    return ev.handled;
}

// 双阶段：隧道（根 → 命中）→ 冒泡（命中 → 根）。
// 任一阶段置 handled → 后续全部阶段不再派发（含目标元素自己的后续处理器）。
template <class E>
bool PhasedDispatch(Element* hit, E& ev,
                    std::function<void(E&)> Element::Handlers::*tunnelSlot,
                    std::function<void(E&)> Element::Handlers::*bubbleSlot,
                    bool (Element::*hook)(E&) = nullptr) {
    if (hit == nullptr) return false;
    if (TunnelDispatch(hit, ev, tunnelSlot)) return true;
    return BubbleDispatch(hit, ev, bubbleSlot, hook);
}

}  // namespace

// 移入/移出：悬停链差分直达（不冒泡）。
// 离开旧链中不在新链的元素 → 移出；进入新链中不在旧链的元素 → 移入。
// （移到子元素上不算离开父元素：父元素仍在两条链里。）
void DispatchHoverTransitions(Element* previous, Element* current, const Vec2& pt) {
    for (Element* e = previous; e != nullptr; e = e->Parent()) {
        if (InChain(e, current)) continue;         // 仍在链上（如移到子元素）→ 不触发移出
        const Element::Handlers* h = e->HandlersPtr();
        if (h == nullptr || !h->leave) continue;  // 无绑定直接跳过
        PointerEvent ev;
        ev.source = e;
        ev.pos = pt;
        h->leave(ev);
    }
    for (Element* e = current; e != nullptr; e = e->Parent()) {
        if (InChain(e, previous)) continue;        // 早就在链上 → 不重复触发移入
        const Element::Handlers* h = e->HandlersPtr();
        if (h == nullptr || !h->enter) continue;
        PointerEvent ev;
        ev.source = e;
        ev.pos = pt;
        h->enter(ev);
    }
}

void DispatchPointerMove(Element* hit, const Vec2& pt) {
    PointerEvent ev;
    ev.pos = pt;
    PhasedDispatch(hit, ev, &Element::Handlers::tunnelMove,
                   &Element::Handlers::move, &Element::HandlePointerMove);
}

void DispatchPointerDown(Element* hit, const Vec2& pt) {
    PointerEvent ev;
    ev.pos = pt;
    PhasedDispatch(hit, ev, &Element::Handlers::tunnelDown,
                   &Element::Handlers::down, &Element::HandlePointerDown);
}

void DispatchPointerUp(Element* hit, const Vec2& pt) {
    PointerEvent ev;
    ev.pos = pt;
    PhasedDispatch(hit, ev, &Element::Handlers::tunnelUp,
                   &Element::Handlers::up, &Element::HandlePointerUp);
}

bool DispatchWheel(Element* hit, const Vec2& pt, float delta, bool horizontal) {
    WheelEvent ev;
    ev.pos = pt;
    ev.delta = delta;
    ev.horizontal = horizontal;
    return PhasedDispatch(hit, ev, &Element::Handlers::tunnelWheel,
                          &Element::Handlers::wheel);
}

// ---------------------------------------------------------------------------
// 键盘事件（M5.9）：从焦点元素出发，隧道（根 → 焦点）+ 冒泡（焦点 → 根）
// ---------------------------------------------------------------------------

bool DispatchKeyDown(Element* target, int key, bool ctrl, bool shift, bool alt) {
    KeyEvent ev;
    ev.key = key;
    ev.ctrl = ctrl;
    ev.shift = shift;
    ev.alt = alt;
    return PhasedDispatch(target, ev, &Element::Handlers::tunnelKeyDown,
                          &Element::Handlers::keyDown, &Element::HandleKeyDown);
}

bool DispatchKeyUp(Element* target, int key, bool ctrl, bool shift, bool alt) {
    KeyEvent ev;
    ev.key = key;
    ev.ctrl = ctrl;
    ev.shift = shift;
    ev.alt = alt;
    return PhasedDispatch(target, ev, &Element::Handlers::tunnelKeyUp,
                          &Element::Handlers::keyUp, &Element::HandleKeyUp);
}

bool DispatchChar(Element* target, wchar_t ch) {
    KeyEvent ev;
    ev.ch = ch;
    return PhasedDispatch(target, ev, &Element::Handlers::tunnelChar,
                          &Element::Handlers::ch, &Element::HandleChar);
}

}  // namespace dui
