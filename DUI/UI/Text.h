// ============================================================================
//  DUI/UI/Text.h — 文本控件与可点击文本（实现拆在 UI/Text.cpp）
//  排版经 text::Engine（pimpl：公共头不引入 dwrite），M5 接入。
// ============================================================================

#pragma once

#include "UI/Element.h"

namespace dui {

class Text : public Element {
public:
    Text() = default;
    explicit Text(const wchar_t* text) : text_(text ? text : L"") {}
    ~Text() override;    // 释放段落排版缓存（Text.cpp，经 text::Engine）

    void SetText(const wchar_t* text) { text_ = text ? text : L""; MarkDirty(kFlagLayout); }
    void SetText(const String& s)     { text_ = s; MarkDirty(kFlagLayout); }
    const String& Text_() const noexcept { return text_; }

    Kind GetKind() const noexcept override { return Kind::Text; }

    // 段落排版结果（text::Paragraph 的 pimpl，生命周期归本元素）。
    // 测量在 MeasureContent 内刷新；App 层收集时经此句柄绘制（M5）。
    void* ParaHandle() const noexcept { return para_; }

protected:
    // M5：DWrite 排版结果（11-Text §3）；DWrite 不可用时退回 M3 占位估算
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;

private:
    String text_;
    mutable void* para_ = nullptr;   // text::Paragraph（pimpl：公共头不引入 dwrite）
};

// 可点击文本控件（M4：hover/pressed 状态位由输入维护、焦点 + Tab）
class Button : public Text {
public:
    using Text::Text;
};

}  // namespace dui
