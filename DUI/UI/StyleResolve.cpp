// ============================================================================
//  DUI/UI/StyleResolve.cpp — 级联解析与失效（03-Style §5/§7，15-Properties §5）
//
//  级联顺序（首次命中即返回）：
//    2 状态变体（固定优先级 disabled > pressed > selected > focused > hover）
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
    // 层 2：状态变体。逐属性独立仲裁：按优先级从高到低，首个设置该属性者胜。
    for (const Style* s = e->StylePtr(); s != nullptr; s = s->Parent()) {
        for (State st : kStatePriority) {
            if (!e->HasState(st)) continue;
            const Style* variant = s->StateVariant(st);
            if (variant == nullptr) continue;
            if (const StyleValue* found = variant->Find(p)) {
                out = *found;
                return true;
            }
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
    case Prop::FlexShrink:   return MakeF(p, 1.f);   // 15-Properties §4.4
    case Prop::FlexDirection: return MakeU(p, static_cast<uint32_t>(FlexDirection::Column));
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
    case Prop::TextAlign:
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
        return kFlagLayout;
    default:
        return kFlagLayout;   // 未知属性按最坏情况处理
    }
}

void ClearDirtyTree(Element* root) {
    if (root == nullptr) return;
    root->dirty_ = 0;
    root->subtreeDirty_ = false;
    for (Element* c : root->children_) ClearDirtyTree(c);
}

}  // namespace dui
