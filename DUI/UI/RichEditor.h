// ============================================================================
//  DUI/UI/RichEditor.h — 富文本控件（RichEditor.md §3.1 D1；P2 只读 + P3 编辑）
//
//  数据是 richtext::RichDoc（纯模型），本控件只做「块级堆叠 + 行内 run 排版」：
//    · MeasureContent 返回文档总高 —— 外层 Scroller 据此出滚动条
//    · OnPaint 逐块绘制：块背景 / 引用左边框 / 文字（含 run 高亮盒）/
//      列表标记 / 代码块（等宽 + 行号）/ 图片占位 / 分割线，最后画选区与光标
//  不生成块级子元素，也不画滚动条（D1：视口与滚动条归外层 Scroller）。
//
//  P3 编辑（与 TextBox 同构的做法）：
//    · 光标/选区 = 两个 {可编辑文本块序号, 段内 UTF-16 偏移}
//    · 可编辑块 = 段落 / 标题 / 引用 / 列表项 / 代码块；分割线只读；
//      代码块的内容在 block.code，可进入编辑（回车 = 块内换行，不拆块）；
//      图片不是可编辑文本，但可作为光标定位点：图前（offset 0）/ 图后（offset 1），
//      点图像右侧落在图后，此时 Backspace 删除该图
//    · 回车分裂块（D6）；段首退格与上一块合并；列表内回车新增同级项，
//      空列表项上再回车退出列表
//    · Ctrl+A/C/X/V/Z/Y；撤销为整文档快照（连续输入合并成一步）
//    · IME 走系统隐形 caret（InsertionPoint），不含内联 preedit
// ============================================================================

#pragma once

#include <memory>

#include "UI/RichDoc.h"
#include "UI/Panel.h"

namespace dui {

class RichEditor : public Panel {
public:
    RichEditor();
    ~RichEditor() override;

    // ---- 文档 ----
    void SetDoc(const richtext::RichDoc& doc);   // 值语义持有；清空撤销栈
    const richtext::RichDoc& Doc() const noexcept;

    // 排版后的文档总高（DIP，不含 padding）
    float ContentHeight() const noexcept;

    // ---- 编辑开关 ----
    void SetEditable(bool on);                   // 默认 true（P3）
    bool IsEditable() const noexcept { return editable_; }
    bool HasSelection() const noexcept;

protected:
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;
    void OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) override;
    bool ContentClipRect(Rect& out) const override;

    // ---- 编辑相关钩子（Element 契约）----
    void OnFocusChanged(bool focused) override;
    bool IsBlinkable() const override { return editable_; }
    bool OnBlink() override;
    bool InsertionPoint(Rect& outRect) const override;
    bool WantsTextCursor() const override { return true; }

    bool HandlePointerDown(PointerEvent& ev) override;
    bool HandlePointerMove(PointerEvent& ev) override;
    bool HandlePointerUp(PointerEvent& ev) override;
    bool HandleKeyDown(KeyEvent& ev) override;
    bool HandleChar(KeyEvent& ev) override;

private:
    struct Impl;
    mutable std::unique_ptr<Impl> impl_;   // MeasureContent 内要写排版缓存

    Vec2 ContentOrigin() const;            // 内容盒左上（绝对 DIP）
    void RevealCaret();                    // 光标跟随：让外层 Scroller 滚到可见

    bool editable_ = true;
};

}  // namespace dui
