// ============================================================================
//  DUI/UI/TextBox.h — 文本编辑控件（TextBox 单行 / TextArea 多行 / 密码掩码）
//
//  能力：文本编辑、光标与选区、剪贴板（Ctrl+C/X/V/A）、撤销/重做（Ctrl+Z/Y）、
//        鼠标点击定位光标与拖选、密码掩码（●）、多行自动换行与上下移动光标、
//        内容滚动（光标跟随可见）。
//
//  焦点：可聚焦（IsFocusable() = true）。键盘事件经焦点元素冒泡送达本控件，
//        控件消费后不再向上冒泡（Element 的 Handle* 钩子；Element.h 契约）。
//
//  外观：选区底色 = Style::SelectionColor，光标色 = Style::CaretColor
//        （未设时取文本色），文本色 = Style::Color。
// ============================================================================
#pragma once

#include <functional>
#include <vector>

#include "UI/Element.h"

namespace dui {

class TextBox : public Element {
public:
    explicit TextBox(bool password = false);
    ~TextBox() override;

    // ---- 内容 ----
    const String& GetText() const noexcept { return text_; }
    void SetText(const String& s);
    void SetText(const wchar_t* s);
    void Clear();

    // ---- 光标与选区 ----
    uint32_t Caret() const noexcept { return caret_; }
    void SetCaret(uint32_t index);
    uint32_t SelectionStart() const noexcept { return caret_ < anchor_ ? caret_ : anchor_; }
    uint32_t SelectionEnd() const noexcept { return caret_ < anchor_ ? anchor_ : caret_; }
    bool HasSelection() const noexcept { return caret_ != anchor_; }
    String SelectedText() const;
    void SelectAll();
    void Deselect();

    // ---- 语义事件（与元素级 OnKeyDown 等绑定并存，互不覆盖）----
    // 文本变化：参数为新文本（掩码不影响真实文本）
    void OnTextChanged(std::function<void(const String&)> fn) { onTextChanged_ = std::move(fn); }
    // 回车提交（单行 TextBox；多行 TextArea 的回车插入换行）
    void OnSubmit(std::function<void()> fn) { onSubmit_ = std::move(fn); }

    bool IsPassword() const noexcept { return password_; }
    // 切换密码模式（掩码 ●）；真实文本不变
    void SetPassword(bool on);
    bool IsMultiline() const noexcept { return multiline_; }

protected:
    // 排版测量（缓存段落；排版对象 = 掩码后的显示文本）
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;
    // 自绘：选区 → 文本 → 光标
    void OnPaint(graphics::Painter& painter, const StyleContext& ctx, float scale) override;
    // 内容裁剪（文本溢出裁到内容盒）
    bool ContentClipRect(Rect& out) const override;
    // 焦点变化：取得焦点时把光标放到文本末尾
    void OnFocusChanged(bool focused) override;
    // 闪烁节拍：光标持续闪烁（框架定时器驱动，500ms 一次）
    bool IsBlinkable() const override { return true; }
    bool OnBlink() override;
    // 插入点（光标底部）：IME 候选窗口定位用
    bool InsertionPoint(Vec2& outDip) const override;

    // 控件内部事件钩子
    bool HandleKeyDown(KeyEvent& ev) override;
    bool HandleChar(KeyEvent& ev) override;
    bool HandlePointerDown(PointerEvent& ev) override;
    bool HandlePointerMove(PointerEvent& ev) override;
    bool HandlePointerUp(PointerEvent& ev) override;

    bool multiline_ = false;   // 多行（TextArea）

private:
    // ---- 编辑核心 ----
    void InsertChar(wchar_t ch);
    void DeleteBackward();
    void DeleteForward();
    void MoveCaret(int delta, bool extend);       // 水平移动（±1 字符）
    void MoveCaretLine(int delta, bool extend);   // 上下移动（多行）
    void MoveToLineEdge(bool toEnd, bool extend, bool whole);
    void MoveTo(uint32_t index, bool extend);
    void Copy() const;
    void Cut();
    void Paste();
    void Undo();
    void Redo();
    void PushUndo();
    void NotifyChanged();
    void RebuildDisplay();
    void ResetBlinkPhase();   // 交互后光标立即可见（重置闪烁相位）

    // ---- 几何 ----
    Rect  ContentBox() const;                  // 内容盒（绝对 DIP）
    Vec2  TextOrigin() const;                  // 文本原点（含滚动偏移）
    uint32_t HitTestIndex(const Vec2& ptAbs) const;
    void EnsureCaretVisible();
    void ClampScroll();

    String   text_;          // 真实文本
    String   display_;       // 显示文本（密码模式为掩码）
    uint32_t caret_ = 0;     // 光标（选区活动端）
    uint32_t anchor_ = 0;    // 选区另一端（== caret_ 表示无选区）
    uint32_t dragAnchor_ = 0;
    bool     dragging_ = false;
    bool     password_ = false;
    bool     caretVisible_ = true;   // 光标闪烁相位（false = 隐藏相位）
    float    scrollX_ = 0.f;  // 内容滚动偏移（DIP）
    float    scrollY_ = 0.f;
    float    goalX_ = -1.f;   // 上下移动的目标列（-1 = 未记录）
    mutable void* para_ = nullptr;   // text::Paragraph（pimpl：公共头不引入 dwrite）

    struct Snapshot {
        String   text;
        uint32_t caret = 0;
        uint32_t anchor = 0;
    };
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;

    std::function<void(const String&)> onTextChanged_;
    std::function<void()> onSubmit_;
};

// 多行文本编辑（自动换行；回车插入换行；上下方向键移动光标）
class TextArea : public TextBox {
public:
    TextArea();
};

}  // namespace dui
