// ============================================================================
//  DUI/UI/Canvas.cpp — Canvas 图元列表 → Painter 指令
// ============================================================================

#include "UI/Canvas.h"

#include <cmath>
#include <utility>

#include "Graphics/Graphics.h"

namespace dui {

namespace {

// 圆弧按折线近似：每 6° 一段（视觉上与 SDF 圆弧一致，代价可忽略）
constexpr float kArcStepDeg = 6.f;

CanvasItem MakeItem(CanvasItem::Type type, Color color, float width, Dash dash) {
    CanvasItem it;
    it.type = type;
    it.color = color;
    it.width = width;
    it.dash = dash;
    return it;
}

constexpr float kPi = 3.14159265358979f;

// 圆角矩形轮廓离散为折线（每角 8 段），供虚线沿中心线切段。
// 屏幕坐标 y 向下、角圆心在框内：左上 180°→270°（左边→顶边）、右上 270°→360°、
// 右下 0°→90°、左下 90°→180°；末尾补回首点闭合左边。
std::vector<Vec2> RoundRectToPath(const Rect& r, float radius) {
    const float lim = std::min(r.w, r.h) * 0.5f;
    const float rr = std::min(std::max(radius, 0.f), lim);
    const float x0 = r.x, y0 = r.y, x1 = r.Right(), y1 = r.Bottom();
    const int steps = 8;
    const float half = kPi * 0.5f;
    std::vector<Vec2> path;
    path.reserve(4 * (steps + 1) + 1);
    for (int i = 0; i <= steps; ++i) {
        const float a = kPi + half * (float(i) / steps);
        path.push_back({x0 + rr + rr * std::cos(a), y0 + rr + rr * std::sin(a)});
    }
    for (int i = 0; i <= steps; ++i) {
        const float a = kPi + half + half * (float(i) / steps);
        path.push_back({x1 - rr + rr * std::cos(a), y0 + rr + rr * std::sin(a)});
    }
    for (int i = 0; i <= steps; ++i) {
        const float a = half * (float(i) / steps);
        path.push_back({x1 - rr + rr * std::cos(a), y1 - rr + rr * std::sin(a)});
    }
    for (int i = 0; i <= steps; ++i) {
        const float a = half + half * (float(i) / steps);
        path.push_back({x0 + rr + rr * std::cos(a), y1 - rr + rr * std::sin(a)});
    }
    path.push_back(path.front());
    return path;
}

// 椭圆轮廓离散为折线（每 6° 一段，与 AddArc 同粒度）
std::vector<Vec2> EllipseToPath(Vec2 center, float rx, float ry) {
    const int steps = std::max(8, static_cast<int>(std::ceil(360.f / kArcStepDeg)));
    std::vector<Vec2> path;
    path.reserve(static_cast<size_t>(steps) + 1);
    const float kDeg2Rad = kPi / 180.f;
    for (int i = 0; i <= steps; ++i) {
        const float rad = kDeg2Rad * (360.f * i / steps);
        path.push_back({center.x + std::cos(rad) * rx, center.y + std::sin(rad) * ry});
    }
    return path;
}

}  // namespace

void Canvas::Clear() noexcept {
    items_.clear();
    InvalidatePaint();
}

void Canvas::AddLine(Vec2 p0, Vec2 p1, Color color, float width, Dash dash) {
    CanvasItem it = MakeItem(CanvasItem::Type::Line, color, width, dash);
    it.points.push_back(p0);
    it.points.push_back(p1);
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddPolyline(const Vec2* points, uint32_t count, Color color, float width, Dash dash) {
    if (points == nullptr || count < 2) return;
    CanvasItem it = MakeItem(CanvasItem::Type::Polyline, color, width, dash);
    it.points.assign(points, points + count);
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddPolygon(const Vec2* points, uint32_t count, Color color, float width, Dash dash) {
    if (points == nullptr || count < 2) return;
    CanvasItem it = MakeItem(CanvasItem::Type::Polygon, color, width, dash);
    it.points.assign(points, points + count);
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRect(const Rect& r, Color color) {
    CanvasItem it = MakeItem(CanvasItem::Type::Rect, color, 0.f, {});
    it.rect = r;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRectOutline(const Rect& r, Color color, float width, Dash dash) {
    CanvasItem it = MakeItem(CanvasItem::Type::Rect, color, width, dash);
    it.rect = r;
    it.radius = 0.f;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRoundRect(const Rect& r, float radius, Color color) {
    CanvasItem it = MakeItem(CanvasItem::Type::RoundRect, color, 0.f, {});
    it.rect = r;
    it.radius = radius;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRoundRectOutline(const Rect& r, float radius, Color color, float width, Dash dash) {
    CanvasItem it = MakeItem(CanvasItem::Type::RoundRect, color, width, dash);
    it.rect = r;
    it.radius = radius;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddEllipse(Vec2 center, float rx, float ry, Color color) {
    CanvasItem it = MakeItem(CanvasItem::Type::Ellipse, color, 0.f, {});
    it.center = center;
    it.radii = {rx, ry};
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddEllipseOutline(Vec2 center, float rx, float ry, Color color, float width, Dash dash) {
    CanvasItem it = MakeItem(CanvasItem::Type::Ellipse, color, width, dash);
    it.center = center;
    it.radii = {rx, ry};
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddCircle(Vec2 center, float r, Color color) {
    AddEllipse(center, r, r, color);
}

void Canvas::AddCircleOutline(Vec2 center, float r, Color color, float width, Dash dash) {
    AddEllipseOutline(center, r, r, color, width, dash);
}

void Canvas::AddArc(Vec2 center, float r, float startDeg, float endDeg,
                    Color color, float width, Dash dash) {
    const float sweep = endDeg - startDeg;
    const int steps = static_cast<int>(std::ceil(std::fabs(sweep) / kArcStepDeg));
    if (steps < 1) return;
    CanvasItem it = MakeItem(CanvasItem::Type::Polyline, color, width, dash);
    it.points.reserve(static_cast<size_t>(steps) + 1);
    const float kDeg2Rad = 3.14159265358979323846f / 180.f;
    for (int i = 0; i <= steps; ++i) {
        const float deg = startDeg + sweep * static_cast<float>(i) / static_cast<float>(steps);
        const float rad = deg * kDeg2Rad;
        it.points.push_back({center.x + std::cos(rad) * r, center.y + std::sin(rad) * r});
    }
    items_.push_back(std::move(it));
    InvalidatePaint();
}

// 收集期绘制：局部坐标 → 画布左上角为原点的绝对 DIP
void Canvas::OnPaint(graphics::Painter& p, const StyleContext&, float scale) {
    const Rect b = Bounds();
    const float ox = b.x;
    const float oy = b.y;

    // 描边图元的虚线走 CPU 切段：把局部点加画布原点偏移后交给公共工具。
    for (const CanvasItem& it : items_) {
        const bool isDashed = it.dash.on > 0.f && it.width > 0.f;
        switch (it.type) {
        case CanvasItem::Type::Line: {
            const Vec2 a{ox + it.points[0].x, oy + it.points[0].y};
            const Vec2 c{ox + it.points[1].x, oy + it.points[1].y};
            if (isDashed) {
                const Vec2 pts[2] = {a, c};
                graphics::StrokeDashedPolyline(p, pts, 2, it.color, it.width, it.dash, scale);
            } else {
                p.StrokeLine(a, c, it.color, it.width, scale);
            }
            break;
        }
        case CanvasItem::Type::Polyline:
        case CanvasItem::Type::Polygon: {
            const uint32_t n = static_cast<uint32_t>(it.points.size());
            const uint32_t segs = (it.type == CanvasItem::Type::Polygon) ? n : (n - 1);
            if (isDashed) {
                // 构造加偏移的折线，一次切段（跨段图案连续）。
                // Polygon 取 n 点再补首点闭合；Polyline 只取 n 点不闭合。
                std::vector<Vec2> path;
                path.reserve(n + 1);
                for (uint32_t i = 0; i < n; ++i)
                    path.push_back({ox + it.points[i].x, oy + it.points[i].y});
                if (it.type == CanvasItem::Type::Polygon)
                    path.push_back(path.front());
                graphics::StrokeDashedPolyline(p, path.data(), path.size(),
                                               it.color, it.width, it.dash, scale);
            } else {
                for (uint32_t i = 0; i < segs; ++i) {
                    const Vec2& a = it.points[i];
                    const Vec2& c = it.points[(i + 1) % n];
                    p.StrokeLine({ox + a.x, oy + a.y}, {ox + c.x, oy + c.y}, it.color, it.width, scale);
                }
            }
            break;
        }
        case CanvasItem::Type::Rect:
        case CanvasItem::Type::RoundRect: {
            const Rect r{ox + it.rect.x, oy + it.rect.y, it.rect.w, it.rect.h};
            if (it.width <= 0.f) {
                p.FillRoundedRect(r, it.radius, it.color, 1.f, scale);
            } else if (isDashed) {
                const std::vector<Vec2> path = RoundRectToPath(r, it.radius);
                graphics::StrokeDashedPolyline(p, path.data(), path.size(),
                                               it.color, it.width, it.dash, scale);
            } else {
                p.StrokeRoundRect(r, it.radius, it.width, it.color, scale);
            }
            break;
        }
        case CanvasItem::Type::Ellipse: {
            const Vec2 c{ox + it.center.x, oy + it.center.y};
            if (it.width <= 0.f) {
                p.FillEllipse(c, it.radii.x, it.radii.y, it.color, scale);
            } else if (isDashed) {
                const std::vector<Vec2> path = EllipseToPath(c, it.radii.x, it.radii.y);
                graphics::StrokeDashedPolyline(p, path.data(), path.size(),
                                               it.color, it.width, it.dash, scale);
            } else {
                p.StrokeEllipse(c, it.radii.x, it.radii.y, it.color, it.width, scale);
            }
            break;
        }
        }
    }
}

}  // namespace dui
