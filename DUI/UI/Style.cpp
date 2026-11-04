// ============================================================================
//  DUI/UI/Style.cpp — Style 与 StyleBuilder 的实现
//  级联解析在 StyleResolve.cpp；元素实现在 Element.cpp。
// ============================================================================

#include "UI/Style.h"

#include <algorithm>

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

StyleBuilder& StyleBuilder::TrackWidth(float dip)            { return SetF(Prop::ScrollbarTrackWidth, dip); }
StyleBuilder& StyleBuilder::TrackHeight(float dip)           { return SetF(Prop::ScrollbarTrackHeight, dip); }
StyleBuilder& StyleBuilder::TrackBackground(dui::Color c)    { return SetU(Prop::ScrollbarTrackBackgroundColor, c); }
StyleBuilder& StyleBuilder::TrackCornerRadius(float dip)     { return SetF(Prop::ScrollbarTrackCornerRadius, dip); }
StyleBuilder& StyleBuilder::TrackBorderColor(dui::Color c)   { return SetU(Prop::ScrollbarTrackBorderColor, c); }
StyleBuilder& StyleBuilder::TrackBorderWidth(float dip)      { return SetF(Prop::ScrollbarTrackBorderWidth, dip); }
StyleBuilder& StyleBuilder::ThumbWidth(float dip)            { return SetF(Prop::ScrollbarThumbWidth, dip); }
StyleBuilder& StyleBuilder::ThumbHeight(float dip)           { return SetF(Prop::ScrollbarThumbHeight, dip); }
StyleBuilder& StyleBuilder::ThumbBackground(dui::Color c)    { return SetU(Prop::ScrollbarThumbBackgroundColor, c); }
StyleBuilder& StyleBuilder::ThumbCornerRadius(float dip)     { return SetF(Prop::ScrollbarThumbCornerRadius, dip); }
StyleBuilder& StyleBuilder::ThumbBorderColor(dui::Color c)   { return SetU(Prop::ScrollbarThumbBorderColor, c); }
StyleBuilder& StyleBuilder::ThumbBorderWidth(float dip)      { return SetF(Prop::ScrollbarThumbBorderWidth, dip); }

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

}  // namespace dui
