// ============================================================================
//  DUI/UI/Scroller.cpp — 滚动容器：偏移夹紧、滚动条几何与绘制、滑块拖拽
// ============================================================================

#include "UI/Scroller.h"

#include <algorithm>

#include "Graphics/Graphics.h"

namespace dui {

namespace {

// 滚动条外观（基础版：固定尺寸，不接入样式系统）
constexpr float kBarThickness = 10.f;    // 条厚（DIP）
constexpr float kBarGap = 2.f;          // 与视口边缘间距（DIP）
constexpr float kMinThumb = 28.f;       // 滑块最小长度（DIP）
constexpr Color kTrackColor = 0x1F000000;
constexpr Color kThumbColor = 0x73000000;

bool InRect(const Rect& r, const Vec2& p) {
    return p.x >= r.x && p.x <= r.Right() && p.y >= r.y && p.y <= r.Bottom();
}

}  // namespace

// ---------------------------------------------------------------------------
// 偏移
// ---------------------------------------------------------------------------

void Scroller::SetScrollOffset(Vec2 v) {
    const Vec2 clamped{
        std::clamp(v.x, 0.f, MaxScrollX()),
        std::clamp(v.y, 0.f, MaxScrollY()),
    };
    if (clamped.x == offset_.x && clamped.y == offset_.y) return;
    offset_ = clamped;
    InvalidatePaint();   // 滚动只重绘不重排（04-Layout §6.1）
}

void Scroller::ScrollBy(float dx, float dy) {
    SetScrollOffset(Vec2{offset_.x + dx, offset_.y + dy});
}

void Scroller::ClampScroll() {
    const float mx = MaxScrollX();
    const float my = MaxScrollY();
    if (offset_.x > mx || offset_.x < 0.f || offset_.y > my || offset_.y < 0.f) {
        offset_.x = std::clamp(offset_.x, 0.f, mx);
        offset_.y = std::clamp(offset_.y, 0.f, my);
    }
}

// ---------------------------------------------------------------------------
// 视口与内容
// ---------------------------------------------------------------------------

Rect Scroller::Viewport() const {
    const Rect b = Bounds();   // 绝对 DIP（Arrange 结果）
    return Rect{b.x + pad_.left, b.y + pad_.top,
                b.w - pad_.left - pad_.right, b.h - pad_.top - pad_.bottom};
}

float Scroller::MaxScrollX() const {
    const Rect v = Viewport();
    return std::max(0.f, contentSize_.x - v.w);
}

float Scroller::MaxScrollY() const {
    const Rect v = Viewport();
    return std::max(0.f, contentSize_.y - v.h);
}

// ---------------------------------------------------------------------------
// 滚动条几何
// ---------------------------------------------------------------------------

bool Scroller::VerticalBarVisible() const {
    return MaxScrollY() > 0.f;
}

bool Scroller::HorizontalBarVisible() const {
    return MaxScrollX() > 0.f;
}

Rect Scroller::VerticalTrackRect() const {
    const Rect v = Viewport();
    return Rect{v.Right() - kBarThickness - kBarGap, v.y + kBarGap,
                kBarThickness, std::max(0.f, v.h - kBarGap * 2.f)};
}

Rect Scroller::VerticalThumbRect() const {
    const Rect t = VerticalTrackRect();
    const float max = MaxScrollY();
    if (max <= 0.f || t.h <= 0.f) return t;
    const float ratio = Viewport().h / std::max(1.f, contentSize_.y);
    const float len = std::clamp(t.h * ratio, std::min(kMinThumb, t.h), t.h);
    const float pos = t.y + (offset_.y / max) * (t.h - len);
    return Rect{t.x, pos, t.w, len};
}

Rect Scroller::HorizontalTrackRect() const {
    const Rect v = Viewport();
    return Rect{v.x + kBarGap, v.Bottom() - kBarThickness - kBarGap,
                std::max(0.f, v.w - kBarGap * 2.f), kBarThickness};
}

Rect Scroller::HorizontalThumbRect() const {
    const Rect t = HorizontalTrackRect();
    const float max = MaxScrollX();
    if (max <= 0.f || t.w <= 0.f) return t;
    const float ratio = Viewport().w / std::max(1.f, contentSize_.x);
    const float len = std::clamp(t.w * ratio, std::min(kMinThumb, t.w), t.w);
    const float pos = t.x + (offset_.x / max) * (t.w - len);
    return Rect{pos, t.y, len, t.h};
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------

bool Scroller::HitScrollBar(const Vec2& pt, bool& vertical) const {
    if (VerticalBarVisible() && InRect(VerticalTrackRect(), pt)) {
        vertical = true;
        return true;
    }
    if (HorizontalBarVisible() && InRect(HorizontalTrackRect(), pt)) {
        vertical = false;
        return true;
    }
    return false;
}

void Scroller::BeginBarDrag(bool vertical, Vec2 pt) {
    dragActive_ = true;
    dragVertical_ = vertical;
    dragStartPt_ = vertical ? pt.y : pt.x;
    dragStartOffset_ = vertical ? offset_.y : offset_.x;

    // 点在轨道空白处 → 直接跳页（Web 常见行为）
    const Rect track = vertical ? VerticalTrackRect() : HorizontalTrackRect();
    const Rect thumb = vertical ? VerticalThumbRect() : HorizontalThumbRect();
    const float p = vertical ? pt.y : pt.x;
    const float thumbStart = vertical ? thumb.y : thumb.x;
    const float thumbEnd = vertical ? thumb.Bottom() : thumb.Right();
    if (p < thumbStart || p > thumbEnd) {
        const float trackLen = vertical ? track.h : track.w;
        const float thumbLen = vertical ? thumb.h : thumb.w;
        const float max = vertical ? MaxScrollY() : MaxScrollX();
        const float travel = std::max(1.f, trackLen - thumbLen);
        const float frac = ((p - (vertical ? track.y : track.x)) - thumbLen * 0.5f) / travel;
        const float target = dragStartOffset_ + std::clamp(frac, 0.f, 1.f) * max;
        if (vertical) SetScrollOffset(Vec2{offset_.x, target});
        else SetScrollOffset(Vec2{target, offset_.y});
        dragStartOffset_ = target;   // 跳页后以新位置为拖拽起点
    }
}

void Scroller::UpdateBarDrag(Vec2 pt) {
    if (!dragActive_) return;
    const Rect track = dragVertical_ ? VerticalTrackRect() : HorizontalTrackRect();
    const Rect thumb = dragVertical_ ? VerticalThumbRect() : HorizontalThumbRect();
    const float trackLen = dragVertical_ ? track.h : track.w;
    const float thumbLen = dragVertical_ ? thumb.h : thumb.w;
    const float max = dragVertical_ ? MaxScrollY() : MaxScrollX();
    if (max <= 0.f) return;
    const float travel = std::max(1.f, trackLen - thumbLen);
    const float delta = (dragVertical_ ? pt.y : pt.x) - dragStartPt_;
    const float target = dragStartOffset_ + delta / travel * max;
    if (dragVertical_) SetScrollOffset(Vec2{offset_.x, target});
    else SetScrollOffset(Vec2{target, offset_.y});
}

void Scroller::EndBarDrag() {
    dragActive_ = false;
}

// ---------------------------------------------------------------------------
// 测量与绘制
// ---------------------------------------------------------------------------

Vec2 Scroller::MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const {
    const StyleValue pv = ResolveProperty(this, Prop::Padding, ctx);
    pad_ = {pv.f, pv.extra[0], pv.extra[1], pv.extra[2]};

    // 主轴不限（内容可溢出 → 可滚动），交叉轴受视口约束（列表项拉满视口宽）
    const bool row = ResolveProperty(this, Prop::FlexDirection, ctx).u ==
                     static_cast<uint32_t>(FlexDirection::Row);
    const Vec2 avail = row ? Vec2{1e6f, contentAvail.y} : Vec2{contentAvail.x, 1e6f};
    const Vec2 total = Element::MeasureContent(ctx, avail);
    natSize_ = total;
    return total;
}

void Scroller::PaintScrollBars(graphics::Painter& p, float scale) {
    if (VerticalBarVisible()) {
        p.FillRoundedRect(VerticalTrackRect(), kBarThickness * 0.5f, kTrackColor, 1.f, scale);
        p.FillRoundedRect(VerticalThumbRect(), kBarThickness * 0.5f, kThumbColor, 1.f, scale);
    }
    if (HorizontalBarVisible()) {
        p.FillRoundedRect(HorizontalTrackRect(), kBarThickness * 0.5f, kTrackColor, 1.f, scale);
        p.FillRoundedRect(HorizontalThumbRect(), kBarThickness * 0.5f, kThumbColor, 1.f, scale);
    }
}

}  // namespace dui
