// ============================================================================
//  DUI/UI/StyleResolve.cpp — 级联解析与失效（03-Style §5/§7，15-Properties §5）
//
//  级联顺序（首次命中即返回）：
//    1 动画值（命令式 Element::Animate 写入 Element::animated_）
//    2 状态变体（元素持有；固定优先级 disabled > pressed > selected > focused > hover）
//    3 元素本地覆盖（Set）
//    4 元素引用的 Style 派生链（由近及远）
//    5 父计算值（仅继承属性，经 StyleContext 遍历期下传）
//    6/7 主题默认 / 属性表内建默认（M3 合并为 BuiltinDefault）
// ============================================================================

#include "UI/UI.h"

namespace dui {

namespace {

// 多状态合并优先级（03-Style §6.2 已定），从高到低
constexpr State kStatePriority[5] = {
    State::Disabled, State::Pressed, State::Selected, State::Focused, State::Hover,
};

bool IsInherited(Prop p) {
    switch (p) {
    case Prop::Color:
    case Prop::FontSize:
    case Prop::FontWeight:
    case Prop::FontFamily:
    case Prop::LineHeight:
    case Prop::TextAlign:
    case Prop::Cursor:   // 光标：父元素设了，子元素未设则沿用（同 CSS 的 cursor 继承）
        return true;
    default:
        return false;
    }
}

StyleValue MakeU(Prop p, uint32_t u) {
    StyleValue v;
    v.prop = p;
    v.u = u;
    return v;
}

StyleValue MakeF(Prop p, float f) {
    StyleValue v;
    v.prop = p;
    v.f = f;
    return v;
}

}  // namespace

// ---- 层 2/3/4：元素自身链（状态变体 → 本地覆盖 → Style 派生链）------------

bool TryResolveElement(const Element* e, Prop p, StyleValue& out) {
    // 层 1：动画值。遍历内（StyleContext）与遍历外（Style::Resolve）都经过此处，
    // 保证两条读路径同帧一致（05-Animation §3）。
    for (const StyleValue& v : e->animated_) {
        if (v.prop == p) { out = v; return true; }
    }
    // 层 2：状态变体（Element::When 挂在元素上）。逐属性独立仲裁：
    // 按优先级从高到低，首个设置该属性者胜；变体自身仍可派生（Find 沿其 parent_ 回溯）。
    for (State st : kStatePriority) {
        if (!e->HasState(st)) continue;
        const Style* variant = e->StateVariant(st);
        if (variant == nullptr) continue;
        if (const StyleValue* found = variant->Find(p)) {
            out = *found;
            return true;
        }
    }
    // 层 3：本地覆盖
    for (const StyleValue& v : e->overrides_) {
        if (v.prop == p) {
            out = v;
            return true;
        }
    }
    // 层 4：Style 派生链（由近及远，Style::Find 已实现）
    if (const Style* s = e->StylePtr()) {
        if (const StyleValue* found = s->Find(p)) {
            out = *found;
            return true;
        }
    }
    return false;
}

// ---- 层 5/6/7：继承回退 + 内建默认 ----------------------------------------

bool TryResolve(const Element* e, Prop p, const StyleContext& ctx, StyleValue& out) {
    if (TryResolveElement(e, p, out)) return true;
    switch (p) {   // 层 5：父计算值（仅继承属性；ctx 始终携带）
    case Prop::Color:      out = MakeU(p, ctx.color);      return true;
    case Prop::FontSize:   out = MakeF(p, ctx.fontSize);   return true;
    case Prop::LineHeight: out = MakeF(p, ctx.lineHeight); return true;
    case Prop::FontWeight: out = MakeU(p, ctx.fontWeight); return true;
    case Prop::FontFamily: out.family = ctx.family;        return true;   // nullptr = 引擎默认
    default:
        return false;   // 非继承属性：交给内建默认（层 7）
    }
}

// 层 7：属性表内建默认（主题层 6 在 M3 未启用，值即默认主题）
StyleValue BuiltinDefault(Prop p) {
    switch (p) {
    case Prop::Color:        return MakeU(p, 0xFF000000);
    case Prop::FontSize:     return MakeF(p, 14.f);
    case Prop::LineHeight:   return MakeF(p, 1.2f);
    case Prop::Opacity:      return MakeF(p, 1.f);
    case Prop::Scale:        return MakeF(p, 1.f);   // transform：1 = 原始大小（不能走 default 的 0）
    // TranslationX / TranslationY / Rotation 默认 0，由 default 分支给出
    // 文本编辑：选区默认半透明蓝；光标色默认取文本色（控件内特判）
    case Prop::SelectionColor: return MakeU(p, 0x803399FF);
    case Prop::FlexShrink:   return MakeF(p, 1.f);   // 15-Properties §4.4
    case Prop::FlexDirection: return MakeU(p, static_cast<uint32_t>(FlexDirection::Column));
    // 滚动条：厚 10 DIP，轨道/滑块半透明黑；直角、无边框（圆角/边框走 0 兜底）
    case Prop::ScrollbarTrackWidth:
    case Prop::ScrollbarTrackHeight:
    case Prop::ScrollbarThumbWidth:
    case Prop::ScrollbarThumbHeight:        return MakeF(p, 10.f);
    case Prop::ScrollbarTrackBackgroundColor: return MakeU(p, 0x1F000000);
    case Prop::ScrollbarThumbBackgroundColor: return MakeU(p, 0x73000000);
    default:
        return MakeU(p, 0);   // Flex / 透明 / 0 尺寸
    }
}

StyleValue ResolveProperty(const Element* e, Prop p, const StyleContext& ctx) {
    StyleValue v;
    if (!TryResolve(e, p, ctx, v)) v = BuiltinDefault(p);
    return v;
}

StyleValue ResolveProperty(const Element* e, Prop p) {
    StyleValue v;
    if (TryResolveElement(e, p, v)) return v;
    if (IsInherited(p)) {   // 遍历外读继承属性：向上找最近设置者（15-Properties §5.2）
        for (const Element* a = e->Parent(); a != nullptr; a = a->Parent()) {
            if (TryResolveElement(a, p, v)) return v;
        }
    }
    return BuiltinDefault(p);
}

StyleContext StyleContext::Child(const Element* e) const {
    StyleContext c = *this;
    StyleValue v;
    if (TryResolveElement(e, Prop::Color, v))      c.color = v.u;
    if (TryResolveElement(e, Prop::FontSize, v))   c.fontSize = v.f;
    if (TryResolveElement(e, Prop::LineHeight, v)) c.lineHeight = v.f;
    if (TryResolveElement(e, Prop::FontWeight, v)) c.fontWeight = v.u;
    if (TryResolveElement(e, Prop::FontFamily, v)) c.family = v.family;   // nullptr 透传引擎默认
    return c;
}

// 遍历外读路径（公共 API，签名冻结）
Color Style::Resolve(const Element* element, Prop p) {
    if (element == nullptr) return 0;
    return ResolveProperty(element, p).u;
}

// ---- 失效标志（15-Properties §3；M3 将 Text 并入 Layout）-------------------

uint32_t PropFlagsOf(Prop p) {
    switch (p) {
    // Paint：仅重绘
    case Prop::Color:
    case Prop::BackgroundColor:
    case Prop::CornerRadius:
    case Prop::Opacity:
    // Paint 级 transform：只重收集顶点，不触发 Measure/Arrange
    case Prop::TranslationX:
    case Prop::TranslationY:
    case Prop::Rotation:
    case Prop::Scale:
    case Prop::TextAlign:
    // 文本编辑外观
    case Prop::SelectionColor:
    case Prop::CaretColor:
    // 滚动条外观（不改布局，几何每帧按属性重算）
    case Prop::ScrollbarTrackWidth:
    case Prop::ScrollbarTrackHeight:
    case Prop::ScrollbarTrackBackgroundColor:
    case Prop::ScrollbarTrackCornerRadius:
    case Prop::ScrollbarTrackBorderColor:
    case Prop::ScrollbarTrackBorderWidth:
    case Prop::ScrollbarThumbWidth:
    case Prop::ScrollbarThumbHeight:
    case Prop::ScrollbarThumbBackgroundColor:
    case Prop::ScrollbarThumbCornerRadius:
    case Prop::ScrollbarThumbBorderColor:
    case Prop::ScrollbarThumbBorderWidth:
        return kFlagPaint;
    // Layout：需重新布局（含文本类排版属性，M5 拆出 Text 层）
    case Prop::FontSize:
    case Prop::FontWeight:
    case Prop::FontFamily:
    case Prop::LineHeight:
    case Prop::Padding:
    case Prop::Margin:
    case Prop::Width:
    case Prop::Height:
    case Prop::MinWidth:
    case Prop::MaxWidth:
    case Prop::Display:
    case Prop::FlexDirection:
    case Prop::FlexGrow:
    case Prop::FlexShrink:
    case Prop::Gap:
    case Prop::JustifyContent:
    case Prop::AlignItems:
    case Prop::FlexWrap:
        return kFlagLayout;
    case Prop::Cursor:
        return kFlagNone;   // 光标形态不影响布局也不影响绘制
    default:
        return kFlagLayout;   // 未知属性按最坏情况处理
    }
}

// ---- Paint 级变换（10-Rendering §6.3）------------------------------------
// T(p) = c + t + M(p − c)：c = 元素盒中心（布局 DIP），t = 平移 DIP，
// M = R(θ) · Scale(s)。变换定义在布局空间、与滚动原点无关；像素空间版本
// 经 Transform2D::Scaled(scale) 得到。ResolveProperty 的遍历外路径含级联
// 层 1 动画值，故绘制与命中测试读到的是同一帧的变换。
Transform2D ElementTransform(const Element* e) noexcept {
    const Rect b = e->Bounds();
    const Vec2 c{b.x + b.w * 0.5f, b.y + b.h * 0.5f};
    const StyleValue tx = ResolveProperty(e, Prop::TranslationX);
    const StyleValue ty = ResolveProperty(e, Prop::TranslationY);
    const StyleValue rot = ResolveProperty(e, Prop::Rotation);
    const StyleValue sc = ResolveProperty(e, Prop::Scale);
    return Transform2D::Translation(c.x + tx.f, c.y + ty.f)
         * Transform2D::Rotation(rot.f)
         * Transform2D::Scaling(sc.f)
         * Transform2D::Translation(-c.x, -c.y);
}

void ClearDirtyTree(Element* root) {
    if (root == nullptr) return;
    root->dirty_ = 0;
    root->subtreeDirty_ = false;
    for (Element* c : root->children_) ClearDirtyTree(c);
}

}  // namespace dui
