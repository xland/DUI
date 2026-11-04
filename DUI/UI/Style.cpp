// ============================================================================
//  DUI/UI/Style.cpp — Style 的实现（链式构造 + 声明集查询）
//  级联解析在 StyleResolve.cpp；元素实现在 Element.cpp。
// ============================================================================

#include "UI/Style.h"

#include <algorithm>

namespace dui {

// ---------------------------------------------------------------------------
// 声明集查询
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
// 链式构造
// ---------------------------------------------------------------------------

StyleValue& Style::Ensure(Prop p) {
    for (StyleValue& v : decls_) {
        if (v.prop == p) return v;
    }
    StyleValue nv;
    nv.prop = p;
    decls_.push_back(nv);
    return decls_.back();
}

Style& Style::SetU(Prop p, uint32_t u) {
    Ensure(p).u = u;
    return *this;
}

Style& Style::SetF(Prop p, float f) {
    Ensure(p).f = f;
    return *this;
}

Style& Style::Display(dui::Display v)              { return SetU(Prop::Display, static_cast<uint32_t>(v)); }
Style& Style::FlexDirection(dui::FlexDirection v)  { return SetU(Prop::FlexDirection, static_cast<uint32_t>(v)); }
Style& Style::Width(float dip)                     { return SetF(Prop::Width, dip); }
Style& Style::Height(float dip)                    { return SetF(Prop::Height, dip); }
Style& Style::MinWidth(float dip)                  { return SetF(Prop::MinWidth, dip); }
Style& Style::MaxWidth(float dip)                  { return SetF(Prop::MaxWidth, dip); }
Style& Style::FlexGrow(float v)                    { return SetF(Prop::FlexGrow, v); }
Style& Style::FlexShrink(float v)                  { return SetF(Prop::FlexShrink, v); }
Style& Style::JustifyContent(dui::Justify v)       { return SetU(Prop::JustifyContent, static_cast<uint32_t>(v)); }
Style& Style::AlignItems(dui::Align v)             { return SetU(Prop::AlignItems, static_cast<uint32_t>(v)); }
Style& Style::FlexWrap(dui::FlexWrap v)            { return SetU(Prop::FlexWrap, static_cast<uint32_t>(v)); }
Style& Style::Background(dui::Color c)             { return SetU(Prop::BackgroundColor, c); }
Style& Style::Color(dui::Color c)                  { return SetU(Prop::Color, c); }
Style& Style::FontSize(float dip)                  { return SetF(Prop::FontSize, dip); }
Style& Style::FontWeight(dui::FontWeight v)        { return SetU(Prop::FontWeight, static_cast<uint32_t>(v)); }
Style& Style::FontFamily(const wchar_t* name) { Ensure(Prop::FontFamily).family = name; return *this; }
Style& Style::LineHeight(float multiple)      { return SetF(Prop::LineHeight, multiple); }
Style& Style::Opacity(float v)                { return SetF(Prop::Opacity, v); }
Style& Style::SelectionColor(dui::Color c)   { return SetU(Prop::SelectionColor, c); }
Style& Style::CaretColor(dui::Color c)       { return SetU(Prop::CaretColor, c); }
Style& Style::Gap(float dip)                  { return SetF(Prop::Gap, dip); }

Style& Style::TrackWidth(float dip)            { return SetF(Prop::ScrollbarTrackWidth, dip); }
Style& Style::TrackHeight(float dip)           { return SetF(Prop::ScrollbarTrackHeight, dip); }
Style& Style::TrackBackground(dui::Color c)    { return SetU(Prop::ScrollbarTrackBackgroundColor, c); }
Style& Style::TrackCornerRadius(float dip)     { return SetF(Prop::ScrollbarTrackCornerRadius, dip); }
Style& Style::TrackBorderColor(dui::Color c)   { return SetU(Prop::ScrollbarTrackBorderColor, c); }
Style& Style::TrackBorderWidth(float dip)      { return SetF(Prop::ScrollbarTrackBorderWidth, dip); }
Style& Style::ThumbWidth(float dip)            { return SetF(Prop::ScrollbarThumbWidth, dip); }
Style& Style::ThumbHeight(float dip)           { return SetF(Prop::ScrollbarThumbHeight, dip); }
Style& Style::ThumbBackground(dui::Color c)    { return SetU(Prop::ScrollbarThumbBackgroundColor, c); }
Style& Style::ThumbCornerRadius(float dip)     { return SetF(Prop::ScrollbarThumbCornerRadius, dip); }
Style& Style::ThumbBorderColor(dui::Color c)   { return SetU(Prop::ScrollbarThumbBorderColor, c); }
Style& Style::ThumbBorderWidth(float dip)      { return SetF(Prop::ScrollbarThumbBorderWidth, dip); }

Style& Style::Padding(float all) {
    return Padding(Thickness(all));
}

Style& Style::Padding(const Thickness& t) {
    // Thickness 四边内嵌 StyleValue（f = left，extra = top/right/bottom）
    StyleValue& v = Ensure(Prop::Padding);
    v.f = t.left;
    v.extra[0] = t.top;
    v.extra[1] = t.right;
    v.extra[2] = t.bottom;
    return *this;
}

Style& Style::CornerRadius(float dip) {
    return CornerRadius(Thickness(dip));
}

Style& Style::CornerRadius(const Thickness& t) {
    StyleValue& v = Ensure(Prop::CornerRadius);
    v.f = t.left;
    v.extra[0] = t.top;
    v.extra[1] = t.right;
    v.extra[2] = t.bottom;
    return *this;
}

Style& Style::Transition(Prop prop, uint32_t durationMs, Easing easing) {
    // 同 (元素, 属性) 唯一：后声明替换（05-Animation §3.2）
    transitions_.erase(
        std::remove_if(transitions_.begin(), transitions_.end(),
                       [prop](const TransitionSpec& t) { return t.prop == prop; }),
        transitions_.end());
    TransitionSpec t;
    t.prop = prop;
    t.durationMs = durationMs;
    t.easing = easing;
    transitions_.push_back(t);
    return *this;
}

Style& Style::When(State s, const Style& variant) {
    for (auto& [state, style] : states_) {
        if (state == s) { style = variant; return *this; }   // 同状态只留一份
    }
    states_.emplace_back(s, variant);
    return *this;
}

}  // namespace dui
