// ============================================================================
//  DUI/UI/UI.cpp — 样式与元素的 M1 实现
//  解析（级联合并、状态优先级、动画槽）、标脏、绘制在 M3 落地。
// ============================================================================

#include "UI/UI.h"

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

// M1 stub：只查元素引用的 Style 派生链（级联第 4 层）。
// M3 补齐：动画槽 → 状态变体 → 本地覆盖 → Style → 主题 → 内建默认（03-Style §5）。
Color Style::Resolve(const Element* element, Prop p) {
    if (element == nullptr) return 0;
    const Style* s = element->StylePtr();
    if (s == nullptr) return 0;
    if (const StyleValue* v = s->Find(p)) return v->u;
    return 0;
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
StyleBuilder& StyleBuilder::Background(dui::Color c)             { return SetU(Prop::BackgroundColor, c); }
StyleBuilder& StyleBuilder::Color(dui::Color c)                  { return SetU(Prop::Color, c); }
StyleBuilder& StyleBuilder::FontSize(float dip)                  { return SetF(Prop::FontSize, dip); }
StyleBuilder& StyleBuilder::FontWeight(dui::FontWeight v)        { return SetU(Prop::FontWeight, static_cast<uint32_t>(v)); }
StyleBuilder& StyleBuilder::LineHeight(float multiple)      { return SetF(Prop::LineHeight, multiple); }
StyleBuilder& StyleBuilder::Opacity(float v)                { return SetF(Prop::Opacity, v); }
StyleBuilder& StyleBuilder::Gap(float dip)                  { return SetF(Prop::Gap, dip); }

StyleBuilder& StyleBuilder::Padding(float all) {
    return Padding(Thickness(all));
}

StyleBuilder& StyleBuilder::Padding(const Thickness& t) {
    // M1：Thickness 四值打包进一个 StyleValue 的 f/u 不够用，先存到 CornerRadius 之外的
    // 专用槽（借 u 存指针不安全）。M3 引入正式 tagged union 后改为值存储。
    // 当前 stub：四值压缩到 f（left），仅供编译链接通过。
    StyleValue& v = Ensure(Prop::Padding);
    v.f = t.left;
    v.u = 0;
    return *this;
}

StyleBuilder& StyleBuilder::CornerRadius(float dip) {
    return CornerRadius(Thickness(dip));
}

StyleBuilder& StyleBuilder::CornerRadius(const Thickness& t) {
    StyleValue& v = Ensure(Prop::CornerRadius);
    v.f = t.left;   // stub 同上，M3 换 tagged union
    v.u = 0;
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
    for (Element* c : children_) delete c;
}

void Element::AttachChild(Element* child) {
    child->parent_ = this;
    children_.push_back(child);
    // M3: LayoutDirty 子树
}

void Element::SetOverride(Prop p, float f, uint32_t u) {
    for (StyleValue& v : overrides_) {
        if (v.prop == p) { v.f = f; v.u = u; return; }   // 本地覆盖内联容量：M3 收敛到 2
    }
    StyleValue nv;
    nv.prop = p;
    nv.f = f;
    nv.u = u;
    overrides_.push_back(nv);
    // M3: 按属性失效标志标脏
}

void Element::SetState(State s, bool on) {
    const uint32_t mask = static_cast<uint32_t>(s);
    const uint32_t next = on ? (states_ | mask) : (states_ & ~mask);
    if (next == states_) return;   // 相同值幂等：不标脏、不重建过渡（03-Style §6.2）
    states_ = next;
    // M3: 按该状态 Decl 的属性标志标脏（帧末合并）+ 过渡触发
}

}  // namespace dui
