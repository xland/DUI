// ============================================================================
//  DUI/UI/Event.cpp — 命中测试与最小输入支持（06-Events，M3 子集）
//
//  M3 范围：按 Rect 的最深命中、hover/pressed 状态链维护（含祖先，CSS 直觉）、
//  Click 合成（按下抬起同元素）。双阶段路由、指针捕获、拖出阈值在 M4 落地。
//
//  e.source 语义（按两个示例的用法实现）：处理器宿主元素 —— 点击落在子元素上
//  时冒泡到绑定处理器者，e.source 即该元素（06-Events §4.3 的"命中目标"以
//  示例为准收敛为此语义）。
// ============================================================================

#include "UI/UI.h"

namespace dui {

namespace {

bool InRect(const Rect& r, const Vec2& p) {
    return p.x >= r.x && p.x <= r.Right() && p.y >= r.y && p.y <= r.Bottom();
}

Element* HitTestInner(Element* e, const Vec2& pt) {
    const std::vector<Element*>& cs = e->Children();
    for (auto it = cs.rbegin(); it != cs.rend(); ++it) {   // 后绘制者在上：倒序命中
        Element* c = *it;
        const Rect& b = c->Bounds();
        if (b.w > 0.f && b.h > 0.f && InRect(b, pt)) {
            return HitTestInner(c, Vec2{pt.x - b.x, pt.y - b.y});
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

}  // namespace dui
