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

CanvasItem MakeItem(CanvasItem::Type type, Color color, float width) {
    CanvasItem it;
    it.type = type;
    it.color = color;
    it.width = width;
    return it;
}

}  // namespace

void Canvas::Clear() noexcept {
    items_.clear();
    InvalidatePaint();
}

void Canvas::AddLine(Vec2 p0, Vec2 p1, Color color, float width) {
    CanvasItem it = MakeItem(CanvasItem::Type::Line, color, width);
    it.points.push_back(p0);
    it.points.push_back(p1);
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddPolyline(const Vec2* points, uint32_t count, Color color, float width) {
    if (points == nullptr || count < 2) return;
    CanvasItem it = MakeItem(CanvasItem::Type::Polyline, color, width);
    it.points.assign(points, points + count);
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddPolygon(const Vec2* points, uint32_t count, Color color, float width) {
    if (points == nullptr || count < 2) return;
    CanvasItem it = MakeItem(CanvasItem::Type::Polygon, color, width);
    it.points.assign(points, points + count);
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRect(const Rect& r, Color color) {
    CanvasItem it = MakeItem(CanvasItem::Type::Rect, color, 0.f);
    it.rect = r;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRectOutline(const Rect& r, Color color, float width) {
    CanvasItem it = MakeItem(CanvasItem::Type::Rect, color, width);
    it.rect = r;
    it.radius = 0.f;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRoundRect(const Rect& r, float radius, Color color) {
    CanvasItem it = MakeItem(CanvasItem::Type::RoundRect, color, 0.f);
    it.rect = r;
    it.radius = radius;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddRoundRectOutline(const Rect& r, float radius, Color color, float width) {
    CanvasItem it = MakeItem(CanvasItem::Type::RoundRect, color, width);
    it.rect = r;
    it.radius = radius;
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddEllipse(Vec2 center, float rx, float ry, Color color) {
    CanvasItem it = MakeItem(CanvasItem::Type::Ellipse, color, 0.f);
    it.center = center;
    it.radii = {rx, ry};
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddEllipseOutline(Vec2 center, float rx, float ry, Color color, float width) {
    CanvasItem it = MakeItem(CanvasItem::Type::Ellipse, color, width);
    it.center = center;
    it.radii = {rx, ry};
    items_.push_back(std::move(it));
    InvalidatePaint();
}

void Canvas::AddCircle(Vec2 center, float r, Color color) {
    AddEllipse(center, r, r, color);
}

void Canvas::AddCircleOutline(Vec2 center, float r, Color color, float width) {
    AddEllipseOutline(center, r, r, color, width);
}

void Canvas::AddArc(Vec2 center, float r, float startDeg, float endDeg, Color color, float width) {
    const float sweep = endDeg - startDeg;
    const int steps = static_cast<int>(std::ceil(std::fabs(sweep) / kArcStepDeg));
    if (steps < 1) return;
    CanvasItem it = MakeItem(CanvasItem::Type::Polyline, color, width);
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

    for (const CanvasItem& it : items_) {
        switch (it.type) {
        case CanvasItem::Type::Line: {
            const Vec2& a = it.points[0];
            const Vec2& c = it.points[1];
            p.StrokeLine({ox + a.x, oy + a.y}, {ox + c.x, oy + c.y}, it.color, it.width, scale);
            break;
        }
        case CanvasItem::Type::Polyline:
        case CanvasItem::Type::Polygon: {
            const uint32_t n = static_cast<uint32_t>(it.points.size());
            const uint32_t segs = (it.type == CanvasItem::Type::Polygon) ? n : (n - 1);
            for (uint32_t i = 0; i < segs; ++i) {
                const Vec2& a = it.points[i];
                const Vec2& c = it.points[(i + 1) % n];
                p.StrokeLine({ox + a.x, oy + a.y}, {ox + c.x, oy + c.y}, it.color, it.width, scale);
            }
            break;
        }
        case CanvasItem::Type::Rect:
        case CanvasItem::Type::RoundRect:
            if (it.width > 0.f) {
                p.StrokeRoundRect({ox + it.rect.x, oy + it.rect.y, it.rect.w, it.rect.h},
                                  it.radius, it.width, it.color, scale);
            } else {
                p.FillRoundedRect({ox + it.rect.x, oy + it.rect.y, it.rect.w, it.rect.h},
                                  it.radius, it.color, 1.f, scale);
            }
            break;
        case CanvasItem::Type::Ellipse: {
            const Vec2 c{ox + it.center.x, oy + it.center.y};
            if (it.width > 0.f) {
                p.StrokeEllipse(c, it.radii.x, it.radii.y, it.color, it.width, scale);
            } else {
                p.FillEllipse(c, it.radii.x, it.radii.y, it.color, scale);
            }
            break;
        }
        }
    }
}

}  // namespace dui
