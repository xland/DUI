// ============================================================================
//  DUI/UI/Style.cpp — Style 的实现（链式构造 + 声明集查询）
//  级联解析在 StyleResolve.cpp；元素实现在 Element.cpp。
// ============================================================================

#include "UI/Style.h"

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

const std::vector<GridTrack>* Style::FindTracks(Prop p) const noexcept {
    const bool cols = p == Prop::GridTemplateColumns;
    for (const Style* s = this; s != nullptr; s = s->parent_) {
        const std::vector<GridTrack>& t = cols ? s->gridCols_ : s->gridRows_;
        if (!t.empty()) return &t;
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
Style& Style::TranslationX(float dip)              { return SetF(Prop::TranslationX, dip); }
Style& Style::TranslationY(float dip)              { return SetF(Prop::TranslationY, dip); }
Style& Style::Rotation(float deg)                  { return SetF(Prop::Rotation, deg); }
Style& Style::Scale(float v)                       { return SetF(Prop::Scale, v); }
Style& Style::MinWidth(float dip)                  { return SetF(Prop::MinWidth, dip); }
Style& Style::MaxWidth(float dip)                  { return SetF(Prop::MaxWidth, dip); }
Style& Style::FlexGrow(float v)                    { return SetF(Prop::FlexGrow, v); }
Style& Style::FlexShrink(float v)                  { return SetF(Prop::FlexShrink, v); }
Style& Style::JustifyContent(dui::Justify v)       { return SetU(Prop::JustifyContent, static_cast<uint32_t>(v)); }
Style& Style::AlignItems(dui::Align v)             { return SetU(Prop::AlignItems, static_cast<uint32_t>(v)); }
Style& Style::FlexWrap(dui::FlexWrap v)            { return SetU(Prop::FlexWrap, static_cast<uint32_t>(v)); }
// Grid：占位声明让 Prop 在级联里可见，变长轨道实体存 vector（push_back 避免异常展开实例化）
Style& Style::GridTemplateColumns(std::initializer_list<GridTrack> tracks) {
    gridCols_.clear();
    for (const GridTrack& t : tracks) gridCols_.push_back(t);
    return SetU(Prop::GridTemplateColumns, 1);
}
Style& Style::GridTemplateRows(std::initializer_list<GridTrack> tracks) {
    gridRows_.clear();
    for (const GridTrack& t : tracks) gridRows_.push_back(t);
    return SetU(Prop::GridTemplateRows, 1);
}
// 定位（04-Layout §4.3）：Left/Top/Right/Bottom 只对定位元素生效，Static 下忽略
Style& Style::Position(dui::Position v)            { return SetU(Prop::Position, static_cast<uint32_t>(v)); }
Style& Style::Left(float dip)                      { return SetF(Prop::Left, dip); }
Style& Style::Top(float dip)                       { return SetF(Prop::Top, dip); }
Style& Style::Right(float dip)                     { return SetF(Prop::Right, dip); }
Style& Style::Bottom(float dip)                    { return SetF(Prop::Bottom, dip); }
// zIndex（04-Layout §4.3）：仅 Absolute 生效；负值可用，u 槽按 int32 位型存取
Style& Style::ZIndex(int32_t v)                    { return SetU(Prop::ZIndex, static_cast<uint32_t>(v)); }
Style& Style::Background(dui::Color c)             { return SetU(Prop::BackgroundColor, c); }
Style& Style::Color(dui::Color c)                  { return SetU(Prop::Color, c); }
Style& Style::FontSize(float dip)                  { return SetF(Prop::FontSize, dip); }
Style& Style::FontWeight(dui::FontWeight v)        { return SetU(Prop::FontWeight, static_cast<uint32_t>(v)); }
Style& Style::FontFamily(const wchar_t* name) { Ensure(Prop::FontFamily).family = name; return *this; }
Style& Style::LineHeight(float multiple)      { return SetF(Prop::LineHeight, multiple); }
Style& Style::TextAlign(dui::TextAlign v)     { return SetU(Prop::TextAlign, static_cast<uint32_t>(v)); }
Style& Style::Opacity(float v)                { return SetF(Prop::Opacity, v); }
Style& Style::SelectionColor(dui::Color c)   { return SetU(Prop::SelectionColor, c); }
Style& Style::CaretColor(dui::Color c)       { return SetU(Prop::CaretColor, c); }
Style& Style::Cursor(dui::Cursor v)          { return SetU(Prop::Cursor, static_cast<uint32_t>(v)); }
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

Style& Style::Margin(float all) {
    return Margin(Thickness(all));
}

Style& Style::Margin(const Thickness& t) {
    StyleValue& v = Ensure(Prop::Margin);
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

}  // namespace dui
