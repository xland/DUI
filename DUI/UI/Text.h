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

    void SetText(const wchar_t* text) { text_ = text ? text : L""; ClearSelectionState(); MarkDirty(kFlagLayout); }
    void SetText(const String& s)     { text_ = s; ClearSelectionState(); MarkDirty(kFlagLayout); }
    const String& Text_() const noexcept { return text_; }

    Kind GetKind() const noexcept override { return Kind::Text; }

    // ------------------------------------------------------------------
    // 文本选择（M5.63；CSS user-select 子集）
    //   样式 `user-select: true` 打开后：可拖选、双击选词、三击选段、Ctrl+C 复制。
    //   默认关闭（不允许选择）——与浏览器一致。
    //   选区落在**排版结果**上（截断生效时是截断后的那段）。
    // ------------------------------------------------------------------
    // 是否允许选择（user-select 样式为真）
    bool Selectable() const;
    uint32_t SelectionStart() const noexcept { return selAnchor_ < selCaret_ ? selAnchor_ : selCaret_; }
    uint32_t SelectionEnd()   const noexcept { return selAnchor_ < selCaret_ ? selCaret_ : selAnchor_; }
    bool     HasSelection()   const noexcept { return selAnchor_ != selCaret_; }
    String   SelectedText() const;
    void     SelectAll();
    void     Deselect();

    // 可聚焦：user-select 打开时允许点击取得焦点（用于接收 Ctrl+C 等键盘事件）
    bool IsFocusable() const noexcept override { return focusable_ || Selectable(); }
    // 不进 Tab 链：可选中的文本不抢 Tab 位（CSS tabindex=-1 语义）
    bool Tabbable() const noexcept override { return !Selectable(); }

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

    // 文本原点（排版原点，绝对 DIP）：内容盒左上 + textAlign 的整体平移。
    // 绘制（App/Frame.cpp）与命中测试共用同一份计算 —— 保证选区高亮的位置
    // 与鼠标命中位置严格一致（textAlign 居中 / 右对齐时尤其关键）。
    // 带 ctx 的重载供绘制热路径用（O(1) 解析继承值），无 ctx 的供命中测试用。
    Vec2 TextOriginDip() const;
    Vec2 TextOriginDip(const StyleContext& ctx) const;

protected:
    // M5：DWrite 排版结果（11-Text §3）；DWrite 不可用时退回 M3 占位估算
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;

    // 首行基线（相对内容盒顶部，DIP）：供 align-items: baseline 对齐用
    float FirstBaseline() const override { return baseline_; }

    // ---- 文本选择（M5.63）----
    // 指针形态：可选中的文本用 I-beam
    bool WantsTextCursor() const override { return Selectable(); }
    // 失焦清选区（浏览器行为：选区随焦点消失）
    void OnFocusChanged(bool focused) override;
    // 控件内部事件钩子（拖选 / 双击选词 / 三击选段 / 键盘）
    bool HandlePointerDown(PointerEvent& ev) override;
    bool HandlePointerMove(PointerEvent& ev) override;
    bool HandlePointerUp(PointerEvent& ev) override;
    bool HandleKeyDown(KeyEvent& ev) override;

private:
    // ---- 选择相关 ----
    uint32_t HitTestIndex(const Vec2& ptAbs) const;   // 绝对 DIP → 字符索引
    Vec2     TextOriginWith(uint32_t textAlign) const; // 文本原点（给定对齐值）
    void     SelectWordAt(uint32_t index);             // 双击：选词
    void     SelectLineAt(uint32_t index);             // 三击：选段（整行）
    void     MoveCaret(int dir, bool extend, bool byWord);
    void     MoveToLineEdge(bool toEnd, bool extend);
    void     CopySelection() const;
    void     ClearSelectionState() noexcept;           // 文本变更 / 失焦：清选区
    uint32_t TextLength() const;                      // 当前排版结果的字符数
    const String& CurrentText() const;                // 当前排版结果的文本

    String text_;
    mutable void* fullPara_ = nullptr;      // 全文排版（测量 / 前缀测量用）
    mutable void* truncPara_ = nullptr;     // 截断后排版（ellipsis 生效时绘制用）
    mutable void* ellipsisPara_ = nullptr;  // "…" 宽度测量缓存
    mutable bool  truncated_ = false;       // 当前生效的是截断段落
    mutable float baseline_ = -1.f;          // 首行基线（-1 = 尚无排版结果）
    mutable float singleLineHeight_ = 0.f;   // 单行行高（内容高度下限；0 = 尚无排版结果）
    mutable float inkCenter_ = 0.f;          // 墨迹中心（相对基线，向上为正）
    mutable bool  inkValid_ = false;         // 墨迹中心是否有效

    // ---- 选区状态（M5.63）----
    uint32_t selAnchor_ = 0;    // 选区固定端（按下时的位置 / 双击三击的词首行首）
    uint32_t selCaret_ = 0;    // 选区活动端（跟随鼠标）
    bool     selecting_ = false;   // 正在拖选
    uint32_t clickCount_ = 0;   // 连击计数（2 = 选词，3 = 选段）
    uint64_t lastClickMs_ = 0;  // 上次按下时刻（判连击）
    Vec2     lastClickPos_{};   // 上次按下位置（判连击）
    Vec2     pressPos_{};       // 本次按下位置（判是否算拖动）
};

// 可点击文本控件（M4：hover/pressed 状态位由输入维护、焦点 + Tab）
class Button : public Text {
public:
    using Text::Text;
};

}  // namespace dui
