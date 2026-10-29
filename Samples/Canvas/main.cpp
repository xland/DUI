// ============================================================================
//  Samples/Canvas/main.cpp — 2D 绘图控件（dui::Canvas）示例
//
//  演示 Canvas 的图元 API：直线 / 折线 / 多边形 / 矩形 / 圆角矩形 /
//  圆 / 椭圆 / 圆弧。坐标为画布局部 DIP，原点 = 画布左上角。
// ============================================================================

#include <windows.h>   // 框架公共头不含 windows.h，入口类型由本文件自带
#include "DUI/DUI.h"

namespace {

constexpr dui::Color kInk     = 0xFF1F2937;
constexpr dui::Color kAccent  = 0xFF2563EB;
constexpr dui::Color kRose    = 0xFFDB2777;
constexpr dui::Color kAmber   = 0xFFD97706;
constexpr dui::Color kGreen   = 0xFF16A34A;
constexpr dui::Color kGrid    = 0xFFCBD5E1;

// 画布内容：网格 + 各类 2D 图元
void DrawScene(dui::Canvas& c) {
    // 网格（大量直线：验证图元批的吞吐）
    for (int x = 0; x <= 560; x += 40) {
        c.AddLine({static_cast<float>(x), 0.f}, {static_cast<float>(x), 320.f}, kGrid, 1.f);
    }
    for (int y = 0; y <= 320; y += 40) {
        c.AddLine({0.f, static_cast<float>(y)}, {560.f, static_cast<float>(y)}, kGrid, 1.f);
    }

    // 矩形：填充 + 描边
    c.AddRect({24.f, 24.f, 120.f, 80.f}, 0xFFDBEAFE);
    c.AddRectOutline({24.f, 24.f, 120.f, 80.f}, kAccent, 2.f);

    // 圆角矩形：填充 + 描边
    c.AddRoundRect({176.f, 24.f, 120.f, 80.f}, 16.f, 0xFFDCFCE7);
    c.AddRoundRectOutline({176.f, 24.f, 120.f, 80.f}, 16.f, kGreen, 2.f);

    // 圆 / 椭圆：填充 + 描边
    c.AddCircle({360.f, 64.f}, 40.f, 0xFFFCE7F3);
    c.AddCircleOutline({360.f, 64.f}, 40.f, kRose, 2.f);
    c.AddEllipse({470.f, 64.f}, 56.f, 30.f, 0xFFFEF3C7);
    c.AddEllipseOutline({470.f, 64.f}, 56.f, 30.f, kAmber, 2.f);

    // 直线 + 折线
    c.AddLine({24.f, 140.f}, {200.f, 200.f}, kInk, 2.f);
    const dui::Vec2 zigzag[4] = {{240.f, 200.f}, {280.f, 140.f}, {320.f, 200.f}, {360.f, 150.f}};
    c.AddPolyline(zigzag, 4, kAccent, 3.f);

    // 多边形（闭合描边）
    const dui::Vec2 pentagon[5] = {
        {470.f, 130.f}, {520.f, 180.f}, {485.f, 215.f}, {435.f, 215.f}, {400.f, 180.f}};
    c.AddPolygon(pentagon, 5, kInk, 2.f);

    // 圆弧（上半 / 下半）
    c.AddArc({120.f, 260.f}, 34.f, 180.f, 360.f, kAccent, 3.f);
    c.AddArc({220.f, 260.f}, 34.f, 0.f, 180.f, kRose, 3.f);
}

class CanvasWindow final : public dui::Window {
public:
    CanvasWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI Canvas",
              .width  = 900.f,     // DIP
              .height = 640.f,
          }) {}

protected:
    void OnCreate() override {   // 原生窗口已建、尚未显示
        dui::Panel* root = Root();
        root->SetStyle(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Column)
            .Gap(10.f)
            .Padding(16.f)
            .Background(0xFFF3F4F6));

        root->AddChild<dui::Text>(L"DUI Canvas — 2D 绘图控件", dui::StyleBuilder()
            .FontSize(20.f).Color(0xFF111827));
        root->AddChild<dui::Text>(
            L"图元：直线 / 折线 / 多边形 / 矩形 / 圆角矩形 / 圆 / 椭圆 / 圆弧"
            L"（坐标为画布局部 DIP，原点 = 画布左上角）",
            dui::StyleBuilder().FontSize(13.f).Color(0xFF6B7280));

        dui::Canvas* canvas = root->AddChild<dui::Canvas>(dui::StyleBuilder()
            .FlexGrow(1.f)
            .Background(0xFFFFFFFF)
            .CornerRadius(10.f));
        DrawScene(*canvas);
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    CanvasWindow window;
    return app.Run(window);
}
