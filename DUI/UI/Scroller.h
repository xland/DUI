// ============================================================================
//  DUI/UI/Scroller.h — 滚动容器控件
//
//  内容按自然尺寸排列（可溢出视口）；滚动不改布局结果，只改绘制坐标：
//  收集期把内容切成独立绘制单元（Painter 原点偏移 = -滚动偏移），
//  并用 scissor 裁到视口内（App/Frame.cpp）；滚动条由本控件自绘。
//
//  交互：滚轮（纵向；Shift+滚轮横向）与滚动条滑块拖拽（App 层路由）。
// ============================================================================
#pragma once

#include <cstdint>

#include "Core/Core.h"
#include "UI/UI.h"

namespace dui {

namespace graphics {
class Painter;
}

// 滚动容器：支持纵向/横向滚动条
class Scroller : public Panel {
public:
    Kind GetKind() const noexcept override { return Kind::Scroller; }

    // ---- 滚动偏移（DIP）----
    Vec2 ScrollOffset() const noexcept { return offset_; }
    void SetScrollOffset(Vec2 v);
    void ScrollBy(float dx, float dy);

    // ---- 视口与内容 ----
    Rect Viewport() const;                 // 内容盒（绝对 DIP）
    Vec2 ContentSize() const noexcept { return contentSize_; }   // 内容实际范围（布局后）
    Vec2 NaturalSize() const noexcept { return natSize_; }       // 内容自然尺寸（布局用）
    float MaxScrollX() const;
    float MaxScrollY() const;

    // ---- 滚动条几何（绝对 DIP）----
    bool VerticalBarVisible() const;
    bool HorizontalBarVisible() const;
    Rect VerticalTrackRect() const;
    Rect VerticalThumbRect() const;
    Rect HorizontalTrackRect() const;
    Rect HorizontalThumbRect() const;

    // ---- 交互（App 层调用）----
    // 点是否落在滚动条上（vertical=true → 纵向条）
    bool HitScrollBar(const Vec2& ptDip, bool& vertical) const;
    void BeginBarDrag(bool vertical, Vec2 ptDip);
    void UpdateBarDrag(Vec2 ptDip);
    void EndBarDrag();

    // 收敛越界偏移（内容变短 / 视口变大后调用）
    void ClampScroll();

    // 布局期写入：内容实际范围（子项外接盒，相对内容盒原点）
    void SetContentExtent(Vec2 size) noexcept { contentSize_ = size; }

    // 绘制滚动条（App 层收集期调用；压在内容之上）
    void PaintScrollBars(graphics::Painter& p, float scale);

protected:
    // 内容自然尺寸：主轴不限（可溢出），交叉轴受视口约束（列表项拉满视口宽）
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;

private:
    Vec2 offset_{};
    mutable Vec2 contentSize_{};   // 内容实际范围（布局后写入）
    mutable Vec2 natSize_{};       // 内容自然尺寸（测量期写入）
    mutable Thickness pad_{};
    bool  dragActive_ = false;
    bool  dragVertical_ = false;
    float dragStartPt_ = 0.f;
    float dragStartOffset_ = 0.f;
};

}  // namespace dui
