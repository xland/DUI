#include "UI/Icon.h"

#include <algorithm>

#include "Graphics/Graphics.h"
#include "UI/Style.h"

namespace dui {

namespace {

// 归一化坐标：u/v ∈ [0,1] 相对 dipBox
Vec2 At(const Rect& b, float u, float v) {
    return {b.x + b.w * u, b.y + b.h * v};
}

// 两段折线组成的对号（不依赖字体图元，放大仍是矢量线）
void StrokeCheck(graphics::Painter& p, const Rect& b, Color color, float lw, float scale) {
    const Vec2 a = At(b, 0.20f, 0.52f);
    const Vec2 m = At(b, 0.42f, 0.74f);
    const Vec2 c = At(b, 0.82f, 0.26f);
    p.StrokeLine(a, m, color, lw, scale);
    p.StrokeLine(m, c, color, lw, scale);
}

Rect Inset(const Rect& b, float k) {
    return {b.x + b.w * k, b.y + b.h * k, b.w * (1.f - 2.f * k), b.h * (1.f - 2.f * k)};
}

}  // namespace

Icon& Icon::SetIcon(IconName name) {
    if (name_ == name) return *this;
    name_ = name;
    InvalidatePaint();      // 只重绘，不重排
    return *this;
}

void Icon::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
    if (name_ == IconName::None) return;
    const Color color = ResolveProperty(this, Prop::Color, ctx).u;
    DrawIcon(p, name_, Bounds(), color, scale);
}

void DrawIcon(graphics::Painter& p, IconName name, const Rect& b, Color color, float scale) {
    if (name == IconName::None || b.w <= 0.f || b.h <= 0.f) return;
    const float s = std::min(b.w, b.h);
    const float lw = s * 0.10f;              // 描边随尺寸等比缩放
    // 9 个图样的中心线统一落在 [0.10, 0.90]；描边半宽 0.05 → 外沿 [0.05, 0.95]，
    // 同一 Width/Height 下视觉尺寸一致。
    const float radius = s * 0.06f;
    const Vec2 center{b.x + b.w * 0.5f, b.y + b.h * 0.5f};

    switch (name) {
    case IconName::Check:
        p.StrokeRoundRect(Inset(b, 0.10f), radius, lw, color, scale);
        break;
    case IconName::CheckChecked:
        p.StrokeRoundRect(Inset(b, 0.10f), radius, lw, color, scale);
        StrokeCheck(p, Inset(b, 0.10f), color, lw, scale);
        break;
    case IconName::Radio:
        p.StrokeEllipse(center, s * 0.40f, s * 0.40f, color, lw, scale);
        break;
    case IconName::RadioChecked:
        p.StrokeEllipse(center, s * 0.40f, s * 0.40f, color, lw, scale);
        p.FillEllipse(center, s * 0.20f, s * 0.20f, color, scale);
        break;
    case IconName::Minimize:
        p.StrokeLine(At(b, 0.10f, 0.50f), At(b, 0.90f, 0.50f), color, lw, scale);
        break;
    case IconName::Maximize:
        p.StrokeRoundRect(Inset(b, 0.10f), 0.f, lw, color, scale);
        break;
    case IconName::Restore: {
        // 后景右上方框被前景遮住，只画露出的上 / 右 / 左上段 / 下右段；
        // 分段线的圆头端点正好落在前景描边宽度内，接缝不露馅。
        p.StrokeLine(At(b, 0.32f, 0.10f), At(b, 0.90f, 0.10f), color, lw, scale);
        p.StrokeLine(At(b, 0.90f, 0.10f), At(b, 0.90f, 0.68f), color, lw, scale);
        p.StrokeLine(At(b, 0.32f, 0.10f), At(b, 0.32f, 0.32f), color, lw, scale);
        p.StrokeLine(At(b, 0.68f, 0.68f), At(b, 0.90f, 0.68f), color, lw, scale);
        // 前景完整直角方框（左下）
        p.StrokeRoundRect(Rect{b.x + b.w * 0.10f, b.y + b.h * 0.32f,
                               b.w * 0.58f, b.h * 0.58f},
                          0.f, lw, color, scale);
        break;
    }
    case IconName::Close:
        p.StrokeLine(At(b, 0.10f, 0.10f), At(b, 0.90f, 0.90f), color, lw, scale);
        p.StrokeLine(At(b, 0.90f, 0.10f), At(b, 0.10f, 0.90f), color, lw, scale);
        break;
    case IconName::Search:
        p.StrokeEllipse(At(b, 0.40f, 0.40f), s * 0.30f, s * 0.30f, color, lw, scale);
        p.StrokeLine(At(b, 0.61f, 0.61f), At(b, 0.90f, 0.90f), color, lw, scale);
        break;
    default:
        break;
    }
}

}  // namespace dui
