// ============================================================================
//  DUI/UI/Element.cpp — Element 的实现（树结构、状态位、本地覆盖、标脏）
//  布局 / 级联解析 / 命中测试分别在 Layout.cpp / StyleResolve.cpp / Event.cpp。
// ============================================================================

#include "UI/Element.h"

#include <algorithm>

#include "UI/Focus.h"

namespace dui {

Element::~Element() {
    ClearFocusIf(this);     // 焦点元素被销毁 → 清焦点（避免全局焦点悬空）
    ClearCaptureIf(this);   // 捕获目标被销毁 → 清捕获（避免悬空）
    lifetime_.Kill();   // 生命周期令牌置否：延迟回投的回调据此丢弃（06-Events D7）
    detail::UnregisterAnimatedElement(this);   // 动画句柄随元素销毁自动失效
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
    if (next == states_) return;   // 相同值幂等：不标脏、不触发任何动画（03-Style §6.2）
    states_ = next;
    // 标脏 = 该状态变体中各属性的失效标志并集（03-Style §7）；无变体则无视觉变化
    MarkDirty(StateVariantFlags(s));
}

const Style* Element::StateVariant(State s) const noexcept {
    for (const auto& [state, style] : stateVariants_) {
        if (state == s) return &style;
    }
    return nullptr;
}

uint32_t Element::StateVariantFlags(State s) const {
    const Style* variant = StateVariant(s);
    if (variant == nullptr) return 0;
    uint32_t flags = 0;
    for (const StyleValue& d : variant->decls_) flags |= PropFlagsOf(d.prop);
    return flags;
}

Element& Element::When(State s, const Style& variant) {
    Style* slot = nullptr;
    for (auto& [state, style] : stateVariants_) {
        if (state == s) { slot = &style; break; }
    }
    if (slot != nullptr) *slot = variant;      // 同状态只留一份，后设覆盖先设
    else stateVariants_.emplace_back(s, variant);
    // 创建后设置、且当前已处于该状态时立即生效
    if (HasState(s)) MarkDirty(StateVariantFlags(s));
    return *this;
}

void Element::MarkDirty(uint32_t flags) {
    if (flags == 0) return;
    dirty_ |= flags;
    for (Element* a = this; a != nullptr && !a->subtreeDirty_; a = a->parent_) {
        a->subtreeDirty_ = true;
    }
}

// ---------------------------------------------------------------------------
// 焦点（06-Events §6）：焦点唯一，挂在元素上
// ---------------------------------------------------------------------------

void Element::Focus() { SetFocus(this); }

void Element::Blur() {
    if (IsFocused(this)) SetFocus(nullptr);
}

bool Element::HasFocus() const noexcept { return IsFocused(this); }

// 指针捕获（06-Events §5）：状态存于 UI/Event.cpp，抬起由 App 层释放
void Element::SetCapture() { SetCaptureElement(this); }

void Element::ReleaseCapture() {
    if (HasCapture()) SetCaptureElement(nullptr);
}

bool Element::HasCapture() const noexcept { return CapturedElement() == this; }

// ---------------------------------------------------------------------------
// Measure：容器默认 = 子项沿主轴聚合（04-Layout §4.1/§6.1）
// ---------------------------------------------------------------------------

Vec2 Element::MeasureContent(const StyleContext& ctx, const Vec2& avail) const {
    const uint32_t dir = ResolveProperty(this, Prop::FlexDirection, ctx).u;
    const uint32_t wrap = ResolveProperty(this, Prop::FlexWrap, ctx).u;
    const float gap = ResolveProperty(this, Prop::Gap, ctx).f;
    const bool row = dir == static_cast<uint32_t>(FlexDirection::Row);

    // 换行流式布局（Row + Wrap）：按可用主轴宽贪心断行——
    // 自然主轴 = 最长行宽，交叉轴 = 各行高之和（行间同 gap）。
    // avail 不受限（1e6 透传）时不换行，走下方单行聚合。
    if (row && wrap == static_cast<uint32_t>(FlexWrap::Wrap) && avail.x < 1e5f) {
        Vec2 total{};
        float lineW = 0.f, lineH = 0.f;
        bool first = true;
        for (Element* c : children_) {
            if (ResolveProperty(c, Prop::Display, ctx).u == static_cast<uint32_t>(Display::None)) continue;
            const Vec2 cs = MeasureElement(c, avail, ctx);
            const float add = cs.x + (first ? 0.f : gap);
            if (!first && lineW + add > avail.x) {   // 放不下 → 断行
                if (lineW > total.x) total.x = lineW;
                total.y += lineH + gap;
                lineW = cs.x;                        // 新行首项不带前导 gap
                lineH = cs.y;
            } else {
                lineW += add;
                if (cs.y > lineH) lineH = cs.y;
            }
            first = false;
        }
        if (lineW > total.x) total.x = lineW;
        total.y += lineH;
        return total;
    }

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
