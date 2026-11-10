// ============================================================================
//  DUI/UI/Text.cpp — Text 的实现（DWrite 排版测量与段落释放）
// ============================================================================

#include "UI/Text.h"

#include "Text/TextEngine.h"

namespace dui {

Vec2 Text::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    text::Engine& en = text::Engine::Get();
    if (!en.Ready()) {
        // DWrite 不可用：M3 占位估算兜底（CJK 1em、其余 0.55em）
        float units = 0.f;
        for (const wchar_t ch : text_) units += (ch >= 0x2E80) ? 1.0f : 0.55f;
        return {units * ctx.fontSize, ctx.fontSize * ctx.lineHeight};
    }
    const float maxW = contentAvail.x > 0.f ? contentAvail.x : 1e6f;   // 不限宽
    text::ShapeInput in;
    in.text = text_.c_str();
    in.fontSize = ctx.fontSize;
    in.weight = dui::DWriteWeight(ResolveProperty(this, Prop::FontWeight, ctx).u);
    in.lineHeight = ctx.lineHeight;
    in.maxWidth = maxW;
    in.family = ctx.family;
    para_ = en.Shape(static_cast<text::Paragraph*>(para_), in);
    return para_ != nullptr ? static_cast<text::Paragraph*>(para_)->size : Vec2{};
}

Text::~Text() {
    text::Engine::Get().Discard(static_cast<text::Paragraph*>(para_));
}

}  // namespace dui
