// ============================================================================
//  DUI/UI/Canvas.h — 2D 绘图控件（Canvas）
//
//  往画布上追加 2D 图元（直线 / 折线 / 多边形 / 矩形 / 圆角矩形 /
//  圆 / 椭圆 / 弧），收集期由框架按图元列表绘制（Element::OnPaint 钩子，
//  ADR-018 定制点 2）。
//
//  坐标：局部 DIP，原点 = 画布左上角（与父元素偏移无关）；线宽单位 DIP。
//  绘制：矩形 / 圆角矩形走矩形批，线段与椭圆走形状批（SDF + 1 像素过渡带 AA）。
//
//  未支持：多边形填充（GPU 侧无解析式 AA 填充路径，暂只提供描边）。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "Core/Core.h"
#include "UI/UI.h"

namespace dui {

// 画布图元：Canvas 持有一份列表，收集期逐项转成 Painter 指令
struct CanvasItem {
    enum class Type : uint8_t {
        Line,      // 直线（points: 2 点）
        Polyline,  // 折线（points: n 点，不闭合；圆弧亦折线化后存入）
        Polygon,   // 多边形（points: n 点，闭合描边）
        Rect,      // 矩形（width > 0 描边，否则填充）
        RoundRect, // 圆角矩形（width > 0 描边，否则填充）
        Ellipse,   // 椭圆 / 圆（width > 0 描边，否则填充）
    };

    Type   type = Type::Line;
    Color  color = 0xFF000000;
    float  width = 1.f;        // 描边宽度（DIP）；填充图元忽略
    Rect   rect;               // Rect / RoundRect：外框
    float  radius = 0.f;       // RoundRect：圆角半径（DIP）
    Vec2   center;             // Ellipse：中心
    Vec2   radii;              // Ellipse：(rx, ry)
    std::vector<Vec2> points;  // Line / Polyline / Polygon：顶点（局部 DIP）
};

// 2D 绘图控件：既可作为容器放子元素，也可纯作绘图表面
class Canvas : public Panel {
public:
    // ---- 图元追加（局部 DIP 坐标）----
    void Clear() noexcept;
    void AddLine(Vec2 p0, Vec2 p1, Color color, float width = 1.f);
    void AddPolyline(const Vec2* points, uint32_t count, Color color, float width = 1.f);
    void AddPolygon(const Vec2* points, uint32_t count, Color color, float width = 1.f);
    void AddRect(const Rect& r, Color color);
    void AddRectOutline(const Rect& r, Color color, float width = 1.f);
    void AddRoundRect(const Rect& r, float radius, Color color);
    void AddRoundRectOutline(const Rect& r, float radius, Color color, float width = 1.f);
    void AddEllipse(Vec2 center, float rx, float ry, Color color);
    void AddEllipseOutline(Vec2 center, float rx, float ry, Color color, float width = 1.f);
    void AddCircle(Vec2 center, float r, Color color);
    void AddCircleOutline(Vec2 center, float r, Color color, float width = 1.f);
    void AddArc(Vec2 center, float r, float startDeg, float endDeg, Color color, float width = 1.f);

    const std::vector<CanvasItem>& Items() const noexcept { return items_; }

protected:
    void OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) override;

private:
    std::vector<CanvasItem> items_;
};

}  // namespace dui
