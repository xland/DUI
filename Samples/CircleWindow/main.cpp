// ============================================================================
//  Samples/CircleWindow/main.cpp — 圆形半透明窗口
//
//  演示：WindowOptions::transparent（DirectComposition + PREMULTIPLIED 合成）、
//        CreateEllipticRgn + SetWindowRgn 裁剪窗口有效区域、
//        dui::Canvas 画半透明圆盘与细线圆边框、
//        整个客户区都是 HTCAPTION（鼠标拖任意位置移动窗口）。
//
//  构建产物：Build\CircleWindow\Debug|Release\CircleWindow.exe
// ============================================================================

#include <windows.h>   // CreateEllipticRgn / SetWindowRgn / GetClientRect

#include "DUI/DUI.h"

namespace {

constexpr float      kSize   = 320.f;       // DIP，正方形窗口
constexpr dui::Color kFill   = 0xB01E293B;  // 半透明深蓝（alpha 0xB0）
constexpr dui::Color kBorder = 0xFF00FFFF;  // 不透明细线

// rgn 是 GDI 硬裁剪（像素级阶梯，无 AA，边界会内凹 1~2 px）。让它从窗口边界
// 让开 2 物理像素，自绘圆再内缩 6 DIP（覆盖阶梯内凹 + 1px AA 过渡带 + 余量），
// 两条边之间留出全透明间隙：可见轮廓完全由 Canvas 的 SDF 圆决定，rgn 的硬边
// 落在透明区里看不见，颗粒感就消失了。两个常量可按实际观感微调。
constexpr int   kRgnInsetPx   = 2;
constexpr float kDrawInsetDip = 6.f;

class CircleWindow final : public dui::Window {
public:
    CircleWindow()
        : dui::Window(dui::WindowOptions{
              .title       = L"CircleWindow · 圆形半透明窗口",
              .width       = kSize,
              .height      = kSize,
              .frameless   = true,
              .transparent = true,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column));

        // Canvas 铺满客户区：半透明填充圆 + 细线圆边框。transparent 窗口每帧
        // 清屏为全透明，圆外不画任何东西，所以圆形外观不依赖 SetWindowRgn 的
        // 视觉裁剪；rgn 只负责让圆外不接收鼠标。
        dui::Canvas* surface = root->AddChild<dui::Canvas>(dui::Style()
            .FlexGrow(1.f)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center)
            .Gap(8.f)
            .Color(0xFFFFFFFF)
            .FontSize(14.f));

        const dui::Vec2 center{kSize * 0.5f, kSize * 0.5f};
        const float r = kSize * 0.5f - kDrawInsetDip;   // 含描边外沿，整体落在 rgn 内侧
        surface->AddCircle(center, r, kFill);
        surface->AddCircleOutline(center, r, kBorder, 1.f);

        surface->AddChild<dui::Text>(L"CircleWindow", dui::Style()
            .FontSize(20.f).FontWeight(dui::FontWeight::SemiBold)
            .TextAlign(dui::TextAlign::Center));
        surface->AddChild<dui::Text>(L"半透明背景 · 细线圆边框", dui::Style()
            .FontSize(13.f).Color(0xFFD0E4FF)
            .TextAlign(dui::TextAlign::Center));
        surface->AddChild<dui::Text>(L"拖动任意位置移动窗口", dui::Style()
            .FontSize(12.f).Color(0xFF9FB6D0)
            .TextAlign(dui::TextAlign::Center));
        surface->AddChild<dui::Text>(L"Alt + F4 关闭", dui::Style()
            .FontSize(12.f).Color(0xFF9FB6D0)
            .TextAlign(dui::TextAlign::Center));

        ApplyCircleRegion();
    }

    void OnDpiChanged(const dui::DpiChanged&) override { ApplyCircleRegion(); }
    void OnStateChanged() override                     { ApplyCircleRegion(); }

    bool OnNcHitTest(dui::Vec2, dui::HitTestCode& hit) override {
        hit = dui::HitTestCode::Caption;   // 整个客户区都是标题栏，可拖任意位置
        return true;
    }

private:
    void ApplyCircleRegion() {
        HWND hwnd = NativeHandle();       // OnCreate 起有效
        RECT rc{};
        GetClientRect(hwnd, &rc);          // 物理像素，DPI 变化后必须重建
        // 让出 kRgnInsetPx 像素，硬裁剪边不与窗口边界相切；可见边缘交给自绘圆
        HRGN rgn = CreateEllipticRgn(kRgnInsetPx, kRgnInsetPx,
                                     rc.right - kRgnInsetPx, rc.bottom - kRgnInsetPx);
        SetWindowRgn(hwnd, rgn, TRUE);     // rgn 所有权移交系统，勿 DeleteObject
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    CircleWindow window;
    return app.Run(window);
}
