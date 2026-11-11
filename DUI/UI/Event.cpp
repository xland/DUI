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

// 写出命中点的"命中元素所在单元空间"坐标（可空指针 —— 不关心时零成本）
inline void SetLocal(Vec2* out, const Vec2& v) {
    if (out != nullptr) *out = v;
}

// 命中测试：ptAbs 是"未变换布局空间"的客户区绝对 DIP（与 Bounds() 同空间）。
// 子元素带 transform 时，先把指针逆变换到该元素的局部空间再判断；逐层施加
// 逆矩阵等价于整体 T⁻¹，嵌套自动成立（10-Rendering §6.3）。
// inFixedLayer：当前在窗口级 Fixed 子树内。普通树遍历跳过 Fixed（已由 HitTest
// 用窗口坐标优先命中；Scroller 的 +off 换算会误命中窗口坐标的 Fixed Bounds）。
// outLocal：命中点换算到"命中元素所在单元空间"的坐标。滚动容器内的元素，
//   它的 Bounds 在单元空间（= 客户区空间 + 滚动偏移），命中点同样要加偏移；
//   命中容器自身（滚动条 / padding 区）时取容器自身空间的坐标。
//   控件据此把 ev.pos 与自己的 Bounds 相减即得局部坐标（06-Events §3）。
Element* HitTestInner(Element* e, const Vec2& ptAbs, Vec2* outLocal,
                      bool inFixedLayer = false, uint32_t depth = 0) {
    const std::vector<Element*>& cs = e->Children();
    auto TryHit = [&](Element* c) -> Element* {
        if (!inFixedLayer && FixedCount() != 0 && IsFixed(c)) return nullptr;
        const Transform2D t = ElementTransform(c);
        if (!t.IsInvertible()) return nullptr;   // Scale → 0：元素缩没，不可命中
        const Vec2 qc = t.Inverse().Apply(ptAbs);
        const Rect& b = c->Bounds();
        if (b.w <= 0.f || b.h <= 0.f || !InRect(b, qc)) return nullptr;
        // visibility: hidden → 自身不可命中；子树里显式 visible 的元素仍可命中
        const bool hidden = c->Hidden();
        if (c->GetKind() == Kind::Scroller) {
            auto* sc = static_cast<Scroller*>(c);
            bool vertical = false;
            // 滚动条区域优先命中滚动容器本身（否则会穿透到被裁剪的内容）
            if (sc->HitScrollBar(qc, vertical)) { SetLocal(outLocal, qc); return hidden ? nullptr : c; }
            // 视口外（padding 区）同样归容器；视口内换算到内容坐标再命中子元素
            const Rect vp = sc->Viewport();
            if (!InRect(vp, qc)) { SetLocal(outLocal, qc); return hidden ? nullptr : c; }
            const Vec2 off = sc->ScrollOffset();
            // 内容空间 = 单元空间 + 滚动偏移（子项 Bounds 在单元空间，命中点要
            // 加偏移才对得上；绘制侧则由 Painter 原点减偏移，两边互为逆运算）
            Element* hit = HitTestInner(c, Vec2{qc.x + off.x, qc.y + off.y}, outLocal,
                                        inFixedLayer, depth + 1);
            if (hit == c) {   // 无子项命中 → 命中容器自身
                SetLocal(outLocal, qc);
                return hidden ? nullptr : c;
            }
            return hit;
        }
        Element* hit = HitTestInner(c, qc, outLocal, inFixedLayer, depth + 1);
        if (hit == c) {       // 同上
            SetLocal(outLocal, qc);
            return hidden ? nullptr : c;
        }
        return hit;
    };
    // 与 Frame.cpp 两趟绘制对应：流内子项先画、脱离流后画 → 倒序即先命中脱离流。
    // 启用锚点 / zIndex 时按绘制侧同一有序列表（含跨层提升到本 CB 的 Absolute）倒序，
    // 值大者后画、先命中。
    if (UseOrderedOutOfFlow()) {
        const std::vector<Element*>& ordered = ZOrderedOutOfFlow(e, depth);
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            if (Element* hit = TryHit(*it)) return hit;
        }
    } else {
        for (auto it = cs.rbegin(); it != cs.rend(); ++it) {
            if (!IsOutOfFlow(*it)) continue;
            if (Element* hit = TryHit(*it)) return hit;
        }
    }
    for (auto it = cs.rbegin(); it != cs.rend(); ++it) {
        if (IsOutOfFlow(*it)) continue;
        if (Element* hit = TryHit(*it)) return hit;
    }
    return e;
}

// 窗口级 Fixed 层：遍历顺序与 Frame.cpp 的 Collect 两趟一致（流内子项先、
// Absolute 子树后；Fixed 在树序原位记录且不再深入），保证绘制 Z 序与倒序命中一致。
// Display::None 子树不进入；遇到 Fixed 收集整棵子树（子树内的 Fixed 不再单独提升）。
void CollectFixedLayer(Element* e, std::vector<Element*>& out, uint32_t depth) {
    if (ResolveProperty(e, Prop::Display).u == static_cast<uint32_t>(Display::None)) return;
    if (IsFixed(e)) { out.push_back(e); return; }
    const std::vector<Element*>& cs = e->Children();
    for (Element* c : cs)
        if (!IsOutOfFlow(c) || IsFixed(c)) CollectFixedLayer(c, out, depth + 1);
    // 与 Frame.cpp 的第二趟同序：否则 Absolute 子树里的 Fixed 收集顺序会漂移。
    if (UseOrderedOutOfFlow()) {
        for (Element* c : ZOrderedOutOfFlow(e, depth))
            if (!IsFixed(c)) CollectFixedLayer(c, out, depth + 1);
    } else {
        for (Element* c : cs)
            if (IsOutOfFlow(c) && !IsFixed(c)) CollectFixedLayer(c, out, depth + 1);
    }
}

// 祖先内容裁剪（ContentClipRect）仍约束 Fixed；Scroller 视口不在此列 ——
// 视口 scissor 是 Collector 专属机制，通用版 Fixed 要绕过它。
bool WithinAncestorClips(Element* e, const Vec2& pt) {
    for (Element* a = e->Parent(); a != nullptr; a = a->Parent()) {
        Rect r{};
        if (a->ContentClipRect(r) && !InRect(r, pt)) return false;
    }
    return true;
}

}  // namespace

// ---- 指针捕获服务（Element::SetCapture / ReleaseCapture 的入口）-------------
void SetCaptureElement(Element* e) noexcept { g_capture = e; }
Element* CapturedElement() noexcept { return g_capture; }
void ClearCaptureIf(Element* e) noexcept { if (g_capture == e) g_capture = nullptr; }

Element* HitTest(Element* root, const Vec2& pt, Vec2* outLocal) {
    if (root == nullptr) return nullptr;
    const Transform2D t = ElementTransform(root);
    if (!t.IsInvertible()) return nullptr;
    const Vec2 q = t.Inverse().Apply(pt);
    if (!InRect(root->Bounds(), q)) return nullptr;

    // 窗口级 Fixed 层先命中（后画者在上）。布局期计数为 0 时整段跳过：
    // 不使用 Fixed 的应用与原来完全一样，零额外遍历。
    if (FixedCount() != 0) {
        static std::vector<Element*> fixed;   // 跨调用复用，稳态零分配
        fixed.clear();
        CollectFixedLayer(root, fixed, 0);
        for (auto it = fixed.rbegin(); it != fixed.rend(); ++it) {
            Element* f = *it;
            const Transform2D ft = ElementTransform(f);   // 自身 transform 仍生效
            if (!ft.IsInvertible()) continue;
            const Vec2 fq = ft.Inverse().Apply(pt);
            if (!InRect(f->Bounds(), fq)) continue;
            if (!WithinAncestorClips(f, pt)) continue;
            // Fixed 层以窗口为原点绘制（不受祖先滚动 / transform 影响）
            Element* hit = HitTestInner(f, fq, outLocal, true, 0);
            if (hit == f) SetLocal(outLocal, fq);
            return hit;
        }
    }
    Element* hit = HitTestInner(root, q, outLocal);
    if (hit == root) SetLocal(outLocal, q);
    return hit;
}

// 客户区坐标 → 元素所在单元空间：沿祖先链逐层应用 transform 逆变换，进入
// 滚动容器的内容时加其滚动偏移（与 HitTest 的换算规则完全一致）。
// 指针捕获期间 Move/Up 直达捕获者（未必是命中元素），用它在捕获者空间定位。
Vec2 ClientToLocal(Element* e, const Vec2& clientPt) {
    if (e == nullptr) return clientPt;
    static std::vector<Element*> chain;   // 跨调用复用，稳态零分配
    chain.clear();
    for (Element* p = e; p != nullptr; p = p->Parent()) chain.push_back(p);
    // chain = e → ... → 根；倒序遍历即 根 → ... → e
    Vec2 pt = clientPt;
    for (size_t i = chain.size(); i-- > 0;) {
        Element* c = chain[i];
        const Transform2D t = ElementTransform(c);
        if (!t.IsInvertible()) continue;
        pt = t.Inverse().Apply(pt);
        // 进入滚动容器的子项前加滚动偏移（元素自身的偏移不加）
        if (c->GetKind() == Kind::Scroller && i > 0) {
            const Vec2 off = static_cast<Scroller*>(c)->ScrollOffset();
            pt = Vec2{pt.x + off.x, pt.y + off.y};
        }
    }
    return pt;
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

// Click：按下/抬起同元素合成的语义事件（06-Events §3）。与指针事件共用
// PointerEvent 及双阶段路由；不经过控件内部 Handle* 钩子。
void DispatchClick(Element* hit, const Vec2& pt, PointerButton button) {
    PointerEvent ev;
    ev.pos = pt;
    ev.button = button;
    PhasedDispatch(hit, ev, &Element::Handlers::tunnelClick,
                   &Element::Handlers::click);
}

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

void DispatchPointerDown(Element* hit, const Vec2& pt, PointerButton button) {
    PointerEvent ev;
    ev.pos = pt;
    ev.button = button;
    PhasedDispatch(hit, ev, &Element::Handlers::tunnelDown,
                   &Element::Handlers::down, &Element::HandlePointerDown);
}

void DispatchPointerUp(Element* hit, const Vec2& pt, PointerButton button) {
    PointerEvent ev;
    ev.pos = pt;
    ev.button = button;
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
