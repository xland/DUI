// ============================================================================
//  DUI/UI/UI.cpp — 样式与元素的实现
//  级联解析 / 布局 / 命中测试分别在 StyleResolve.cpp / Layout.cpp / Event.cpp。
// ============================================================================

#include "UI/UI.h"

#include "Text/TextEngine.h"

namespace dui {

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

const StyleValue* Style::Find(Prop p) const {
    for (const Style* s = this; s != nullptr; s = s->parent_) {
        for (const StyleValue& v : s->decls_) {
            if (v.prop == p) return &v;
        }
    }
    return nullptr;
}

const Style* Style::StateVariant(State s) const {
    for (const auto& [state, style] : states_) {
        if (state == s) return &style;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// StyleBuilder
// ---------------------------------------------------------------------------

StyleValue& StyleBuilder::Ensure(Prop p) {
    for (StyleValue& v : style_.decls_) {
        if (v.prop == p) return v;
    }
    StyleValue nv;
    nv.prop = p;
    style_.decls_.push_back(nv);
    return style_.decls_.back();
}

StyleBuilder& StyleBuilder::SetU(Prop p, uint32_t u) {
    Ensure(p).u = u;
    return *this;
}

StyleBuilder& StyleBuilder::SetF(Prop p, float f) {
    Ensure(p).f = f;
    return *this;
}

StyleBuilder& StyleBuilder::Display(dui::Display v)              { return SetU(Prop::Display, static_cast<uint32_t>(v)); }
StyleBuilder& StyleBuilder::FlexDirection(dui::FlexDirection v)  { return SetU(Prop::FlexDirection, static_cast<uint32_t>(v)); }
StyleBuilder& StyleBuilder::Width(float dip)                     { return SetF(Prop::Width, dip); }
StyleBuilder& StyleBuilder::Height(float dip)                    { return SetF(Prop::Height, dip); }
StyleBuilder& StyleBuilder::MinWidth(float dip)                  { return SetF(Prop::MinWidth, dip); }
StyleBuilder& StyleBuilder::MaxWidth(float dip)                  { return SetF(Prop::MaxWidth, dip); }
StyleBuilder& StyleBuilder::FlexGrow(float v)                    { return SetF(Prop::FlexGrow, v); }
StyleBuilder& StyleBuilder::FlexShrink(float v)                  { return SetF(Prop::FlexShrink, v); }
StyleBuilder& StyleBuilder::JustifyContent(dui::Justify v)       { return SetU(Prop::JustifyContent, static_cast<uint32_t>(v)); }
StyleBuilder& StyleBuilder::AlignItems(dui::Align v)             { return SetU(Prop::AlignItems, static_cast<uint32_t>(v)); }
StyleBuilder& StyleBuilder::Background(dui::Color c)             { return SetU(Prop::BackgroundColor, c); }
StyleBuilder& StyleBuilder::Color(dui::Color c)                  { return SetU(Prop::Color, c); }
StyleBuilder& StyleBuilder::FontSize(float dip)                  { return SetF(Prop::FontSize, dip); }
StyleBuilder& StyleBuilder::FontWeight(dui::FontWeight v)        { return SetU(Prop::FontWeight, static_cast<uint32_t>(v)); }
StyleBuilder& StyleBuilder::FontFamily(const wchar_t* name) { Ensure(Prop::FontFamily).family = name; return *this; }
StyleBuilder& StyleBuilder::LineHeight(float multiple)      { return SetF(Prop::LineHeight, multiple); }
StyleBuilder& StyleBuilder::Opacity(float v)                { return SetF(Prop::Opacity, v); }
StyleBuilder& StyleBuilder::Gap(float dip)                  { return SetF(Prop::Gap, dip); }

StyleBuilder& StyleBuilder::Padding(float all) {
    return Padding(Thickness(all));
}

StyleBuilder& StyleBuilder::Padding(const Thickness& t) {
    // Thickness 四边内嵌 StyleValue（f = left，extra = top/right/bottom）
    StyleValue& v = Ensure(Prop::Padding);
    v.f = t.left;
    v.extra[0] = t.top;
    v.extra[1] = t.right;
    v.extra[2] = t.bottom;
    return *this;
}

StyleBuilder& StyleBuilder::CornerRadius(float dip) {
    return CornerRadius(Thickness(dip));
}

StyleBuilder& StyleBuilder::CornerRadius(const Thickness& t) {
    StyleValue& v = Ensure(Prop::CornerRadius);
    v.f = t.left;
    v.extra[0] = t.top;
    v.extra[1] = t.right;
    v.extra[2] = t.bottom;
    return *this;
}

StyleBuilder& StyleBuilder::Transition(Prop prop, uint32_t durationMs, Easing easing) {
    for (const TransitionSpec& t : style_.transitions_) {
        if (t.prop == prop) {
            // 同 (元素, 属性) 唯一：后声明替换（05-Animation §3.2）
            style_.transitions_.erase(
                std::remove_if(style_.transitions_.begin(), style_.transitions_.end(),
                               [prop](const TransitionSpec& t) { return t.prop == prop; }),
                style_.transitions_.end());
            break;
        }
    }
    TransitionSpec t;
    t.prop = prop;
    t.durationMs = durationMs;
    t.easing = easing;
    style_.transitions_.push_back(t);
    return *this;
}

StyleBuilder& StyleBuilder::When(State s, const StyleBuilder& variant) {
    for (auto& [state, style] : style_.states_) {
        if (state == s) { style = variant.style_; return *this; }   // 同状态只留一份
    }
    style_.states_.emplace_back(s, variant.style_);
    return *this;
}

// ---------------------------------------------------------------------------
// Element
// ---------------------------------------------------------------------------

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

Vec2 Text::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    text::Engine& en = text::Engine::Get();
    if (!en.Ready()) {
        // DWrite 不可用：M3 占位估算兜底（CJK 1em、其余 0.55em）
        float units = 0.f;
        for (const wchar_t ch : text_) units += (ch >= 0x2E80) ? 1.0f : 0.55f;
        return {units * ctx.fontSize, ctx.fontSize * ctx.lineHeight};
    }
    const float maxW = contentAvail.x > 0.f ? contentAvail.x : 1e6f;   // 不限宽
    const uint32_t weight = ResolveProperty(this, Prop::FontWeight, ctx).u;
    para_ = en.Shape(static_cast<text::Paragraph*>(para_), text_.c_str(),
                     ctx.fontSize, weight, ctx.lineHeight, maxW, ctx.family);
    return para_ != nullptr ? static_cast<text::Paragraph*>(para_)->size : Vec2{};
}

Text::~Text() {
    text::Engine::Get().Discard(static_cast<text::Paragraph*>(para_));
}

}  // namespace dui
