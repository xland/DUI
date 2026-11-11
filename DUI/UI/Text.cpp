// ============================================================================
//  DUI/UI/Text.cpp — Text 的实现（DWrite 排版测量与段落释放）
// ============================================================================

#include "UI/Text.h"

#include "Graphics/TextEngine.h"

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
    const bool ellipsis =
        ctx.textOverflow == static_cast<uint32_t>(dui::TextOverflow::Ellipsis);

    text::ShapeInput in;
    in.text = text_.c_str();
    in.fontSize = ctx.fontSize;
    in.weight = dui::DWriteWeight(ResolveProperty(this, Prop::FontWeight, ctx).u);
    in.lineHeight = ctx.lineHeight;
    in.maxWidth = maxW;
    in.family = ctx.family;
    in.letterSpacing = ctx.letterSpacing;   // 字距（继承属性）
    in.wordSpacing = ctx.wordSpacing;       // 词距（继承属性）
    // 斜体（继承属性，走 ctx）/ 文本装饰（不继承，走元素自身链）
    in.italic = ResolveProperty(this, Prop::FontStyle, ctx).u ==
                static_cast<uint32_t>(dui::FontStyle::Italic);
    const uint32_t deco = ResolveProperty(this, Prop::TextDecoration, ctx).u;
    in.underline = dui::HasDecoration(deco, dui::TextDecoration::Underline);
    in.strike = dui::HasDecoration(deco, dui::TextDecoration::LineThrough);

    if (!ellipsis) {
        truncated_ = false;
        fullPara_ = en.Shape(static_cast<text::Paragraph*>(fullPara_), in);
        const text::Paragraph* para = static_cast<text::Paragraph*>(fullPara_);
        baseline_ = para != nullptr ? para->baseline : -1.f;
        return para != nullptr ? para->size : Vec2{};
    }

    // ---- text-overflow: ellipsis（单行；M5.56）--------------------------------
    // ① 单行排版（不折行）→ 得全文自然宽。省略号语义要求"一行放不下"，
    //    若照常折行就会变成多行，永远走不到省略号分支。
    in.noWrap = true;
    fullPara_ = en.Shape(static_cast<text::Paragraph*>(fullPara_), in);
    const text::Paragraph* full = static_cast<text::Paragraph*>(fullPara_);
    if (full == nullptr) { truncated_ = false; return Vec2{}; }
    baseline_ = full->baseline;
    if (full->size.x <= maxW) { truncated_ = false; return full->size; }   // 放得下

    // ② 省略号宽度 + "能放下的最长前缀"（簇边界安全）→ 截断文本再排一次
    const float ellipsisW =
        en.MeasureEllipsis(static_cast<text::Paragraph*>(ellipsisPara_), in);
    const uint32_t fit = en.FitPrefixChars(full, maxW - ellipsisW);
    String clipped = text_.substr(0, fit);
    clipped += L"…";
    in.text = clipped.c_str();
    truncPara_ = en.Shape(static_cast<text::Paragraph*>(truncPara_), in);
    truncated_ = true;
    const text::Paragraph* tp = static_cast<text::Paragraph*>(truncPara_);
    return tp != nullptr ? tp->size : full->size;
}

Text::~Text() {
    text::Engine& en = text::Engine::Get();
    en.Discard(static_cast<text::Paragraph*>(fullPara_));
    en.Discard(static_cast<text::Paragraph*>(truncPara_));
    en.Discard(static_cast<text::Paragraph*>(ellipsisPara_));
}

}  // namespace dui
