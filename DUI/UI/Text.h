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
    // text-overflow: ellipsis 时返回**截断后**的段落（未截断则即全文段落）。
    void* ParaHandle() const noexcept { return truncated_ ? truncPara_ : fullPara_; }

    // 单行行高（DIP）："内容决定尺寸" 的最小高度（CSS min-height:auto 的近似）。
    // 布局收缩时用它给文本类元素设下限，避免文字被压到盒子外。
    float SingleLineHeight() const noexcept { return singleLineHeight_; }

    // 墨迹中心（相对**内容盒顶** = 排版原点，DIP）：单行文本用它做"视觉居中"补偿。
    // 行盒居中 ≠ 视觉居中 —— 雅黑汉字的墨迹重心比行盒中心低 1~2 DIP（见 TextEngine）。
    // 无墨迹数据（多行 / 测量失败）时返回行盒中心 → 补偿为 0（退化为纯行盒居中）。
    float InkCenterFromBoxTop() const noexcept {
        return inkValid_ ? baseline_ - inkCenter_ : singleLineHeight_ * 0.5f;
    }

protected:
    // M5：DWrite 排版结果（11-Text §3）；DWrite 不可用时退回 M3 占位估算
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;

    // 首行基线（相对内容盒顶部，DIP）：供 align-items: baseline 对齐用
    float FirstBaseline() const override { return baseline_; }

private:
    String text_;
    mutable void* fullPara_ = nullptr;      // 全文排版（测量 / 前缀测量用）
    mutable void* truncPara_ = nullptr;     // 截断后排版（ellipsis 生效时绘制用）
    mutable void* ellipsisPara_ = nullptr;  // "…" 宽度测量缓存
    mutable bool  truncated_ = false;       // 当前生效的是截断段落
    mutable float baseline_ = -1.f;          // 首行基线（-1 = 尚无排版结果）
    mutable float singleLineHeight_ = 0.f;   // 单行行高（内容高度下限；0 = 尚无排版结果）
    mutable float inkCenter_ = 0.f;          // 墨迹中心（相对基线，向上为正）
    mutable bool  inkValid_ = false;         // 墨迹中心是否有效
};

// 可点击文本控件（M4：hover/pressed 状态位由输入维护、焦点 + Tab）
class Button : public Text {
public:
    using Text::Text;
};

}  // namespace dui
