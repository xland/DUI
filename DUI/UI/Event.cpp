// ============================================================================
//  DUI/UI/Event.cpp — 命中测试与最小输入支持（06-Events，M3 子集）
//
//  M3 范围：按 Rect 的最深命中、hover/pressed 状态链维护（含祖先，CSS 直觉）、
//  Click 合成（按下抬起同元素）。双阶段路由、指针捕获、拖出阈值在 M4 落地。
//  M5.8 追加：元素级指针事件（移入/移出/移动/按下/弹起/滚轮）——
//    移入/移出按"悬停链"差分直达；移动/按下/弹起/滚轮冒泡，handled 可终止；
//    派发只经过有绑定者的元素（无绑定直接跳过），不做防抖与节流。
//  M5.9 追加：元素级键盘事件（KeyDown/KeyUp/Char）——先到焦点元素再冒泡；
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

bool InRect(const Rect& r, const Vec2& p) {
    return p.x >= r.x && p.x <= r.Right() && p.y >= r.y && p.y <= r.Bottom();
}

// 命中测试：全程用客户区绝对坐标（Bounds() 是绝对坐标，勿混用相对坐标）
Element* HitTestInner(Element* e, const Vec2& ptAbs) {
    const std::vector<Element*>& cs = e->Children();
    for (auto it = cs.rbegin(); it != cs.rend(); ++it) {   // 后绘制者在上：倒序命中
        Element* c = *it;
        const Rect& b = c->Bounds();
        if (b.w > 0.f && b.h > 0.f && InRect(b, ptAbs)) {
            if (c->GetKind() == Kind::Scroller) {
                auto* sc = static_cast<Scroller*>(c);
                bool vertical = false;
                // 滚动条区域优先命中滚动容器本身（否则会穿透到被裁剪的内容）
                if (sc->HitScrollBar(ptAbs, vertical)) return c;
                // 视口外（padding 区）同样归容器；视口内换算到内容坐标再命中子元素
                const Rect vp = sc->Viewport();
                if (!InRect(vp, ptAbs)) return c;
                const Vec2 off = sc->ScrollOffset();
                return HitTestInner(c, Vec2{ptAbs.x + off.x, ptAbs.y + off.y});
            }
            return HitTestInner(c, ptAbs);
        }
    }
    return e;
}

}  // namespace

Element* HitTest(Element* root, const Vec2& pt) {
    if (root == nullptr) return nullptr;
    if (!InRect(root->Bounds(), pt)) return nullptr;
    return HitTestInner(root, pt);
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

void DispatchClick(Element* hit) {
    for (Element* e = hit; e != nullptr; e = e->Parent()) {
        if (!e->onClick_) continue;
        ClickEvent ev;
        ev.source = e;
        e->onClick_(ev);
        if (ev.handled) break;   // 冒泡终止（06-Events §5）
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
    BubbleDispatch(hit, ev, &Element::Handlers::move, &Element::HandlePointerMove);
}

void DispatchPointerDown(Element* hit, const Vec2& pt) {
    PointerEvent ev;
    ev.pos = pt;
    BubbleDispatch(hit, ev, &Element::Handlers::down, &Element::HandlePointerDown);
}

void DispatchPointerUp(Element* hit, const Vec2& pt) {
    PointerEvent ev;
    ev.pos = pt;
    BubbleDispatch(hit, ev, &Element::Handlers::up, &Element::HandlePointerUp);
}

bool DispatchWheel(Element* hit, const Vec2& pt, float delta, bool horizontal) {
    WheelEvent ev;
    ev.pos = pt;
    ev.delta = delta;
    ev.horizontal = horizontal;
    return BubbleDispatch(hit, ev, &Element::Handlers::wheel);
}

// ---------------------------------------------------------------------------
// 键盘事件（M5.9）：先到焦点元素，再沿祖先链冒泡
// ---------------------------------------------------------------------------

bool DispatchKeyDown(Element* target, int key, bool ctrl, bool shift, bool alt) {
    KeyEvent ev;
    ev.key = key;
    ev.ctrl = ctrl;
    ev.shift = shift;
    ev.alt = alt;
    return BubbleDispatch(target, ev, &Element::Handlers::keyDown, &Element::HandleKeyDown);
}

bool DispatchKeyUp(Element* target, int key, bool ctrl, bool shift, bool alt) {
    KeyEvent ev;
    ev.key = key;
    ev.ctrl = ctrl;
    ev.shift = shift;
    ev.alt = alt;
    return BubbleDispatch(target, ev, &Element::Handlers::keyUp, &Element::HandleKeyUp);
}

bool DispatchChar(Element* target, wchar_t ch) {
    KeyEvent ev;
    ev.ch = ch;
    return BubbleDispatch(target, ev, &Element::Handlers::ch, &Element::HandleChar);
}

}  // namespace dui
