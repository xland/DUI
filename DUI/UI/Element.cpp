// ============================================================================
//  DUI/UI/Element.cpp — Element 的实现（树结构、状态位、本地覆盖、标脏）
//  布局 / 级联解析 / 命中测试分别在 Layout.cpp / StyleResolve.cpp / Event.cpp。
// ============================================================================

#include "UI/Element.h"

namespace dui {

Element::~Element() {
    lifetime_.Kill();   // 生命周期令牌置否：延迟回投的回调据此丢弃（06-Events D7）
    for (Element* c : children_) delete c;
}

void Element::AttachChild(Element* child) {
    child->parent_ = this;
    children_.push_back(child);
    MarkDirty(kFlagLayout);   // 子项集合变化 → 重排
}

void Element::SetOverride(Prop p, float f, uint32_t u) {
    for (StyleValue& v : overrides_) {
        if (v.prop == p) { v.f = f; v.u = u; MarkDirty(PropFlagsOf(p)); return; }
    }
    StyleValue nv;
    nv.prop = p;
    nv.f = f;
    nv.u = u;
    overrides_.push_back(nv);
    MarkDirty(PropFlagsOf(p));
}

void Element::SetState(State s, bool on) {
    const uint32_t mask = static_cast<uint32_t>(s);
    const uint32_t next = on ? (states_ | mask) : (states_ & ~mask);
    if (next == states_) return;   // 相同值幂等：不标脏、不重建过渡（03-Style §6.2）
    states_ = next;
    // 标脏 = 该状态变体中各属性的失效标志并集（03-Style §7）；无变体则无视觉变化
    uint32_t flags = 0;
    for (const Style* st = style_; st != nullptr; st = st->Parent()) {
        if (const Style* variant = st->StateVariant(s)) {
            for (const StyleValue& d : variant->decls_) flags |= PropFlagsOf(d.prop);
        }
    }
    if (flags != 0) MarkDirty(flags);
}

void Element::MarkDirty(uint32_t flags) {
    if (flags == 0) return;
    dirty_ |= flags;
    for (Element* a = this; a != nullptr && !a->subtreeDirty_; a = a->parent_) {
        a->subtreeDirty_ = true;
    }
}

// ---------------------------------------------------------------------------
// Measure：容器默认 = 子项沿主轴聚合（04-Layout §4.1/§6.1）
// ---------------------------------------------------------------------------

Vec2 Element::MeasureContent(const StyleContext& ctx, const Vec2& avail) const {
    const uint32_t dir = ResolveProperty(this, Prop::FlexDirection, ctx).u;
    const float gap = ResolveProperty(this, Prop::Gap, ctx).f;
    const bool row = dir == static_cast<uint32_t>(FlexDirection::Row);

    Vec2 total{};
    bool first = true;
    for (Element* c : children_) {
        if (ResolveProperty(c, Prop::Display, ctx).u == static_cast<uint32_t>(Display::None)) continue;
        const Vec2 cs = MeasureElement(c, avail, ctx);
        if (row) {
            total.x += cs.x + (first ? 0.f : gap);
            if (cs.y > total.y) total.y = cs.y;
        } else {
            total.y += cs.y + (first ? 0.f : gap);
            if (cs.x > total.x) total.x = cs.x;
        }
        first = false;
    }
    return total;
}

}  // namespace dui
