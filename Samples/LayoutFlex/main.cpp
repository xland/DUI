// ============================================================================
//  LayoutFlex/main.cpp — Flex 布局能力演示
//
//  覆盖能力（04-Layout）：FlexDirection Row/Column、Gap、Padding、
//  FlexGrow（剩余空间分配）、FlexShrink + MinWidth（收缩）、
//  JustifyContent / AlignItems（主轴/交叉轴对齐）、嵌套容器。
//
//  构建产物（Build 布局，DUI.slnx 根目录相对）：
//    中间产物  Build\Temp\LayoutFlex\Debug|Release\
//    最终产物  Build\LayoutFlex\Debug|Release\LayoutFlex.exe
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// 分区小标题
void NewCaption(dui::Panel* parent, const wchar_t* text) {
    parent->AddChild<dui::Text>(text, dui::StyleBuilder()
        .FontSize(12.f)
        .Color(0xFF777777));
}

// 演示用色块：标签文字在块内双轴居中。width/height/grow/shrink ≤0 表示不设置
dui::Panel* NewBox(dui::Panel* parent, const wchar_t* label, dui::Color bg,
                   float width, float height, float grow = 0.f, float shrink = 0.f) {
    dui::StyleBuilder b = dui::StyleBuilder()
        .FlexDirection(dui::FlexDirection::Column)
        .JustifyContent(dui::Justify::Center)
        .AlignItems(dui::Align::Center)
        .Background(bg)
        .CornerRadius(4.f)
        .Color(0xFFFFFFFF)
        .Height(height);
    if (width > 0.f) b.Width(width);
    if (grow > 0.f) b.FlexGrow(grow);
    if (shrink > 0.f) {
        b.FlexShrink(shrink);
        b.MinWidth(0.f);   // 允许收缩到内容以下（04-Layout §5）
    }
    dui::Panel* box = parent->AddChild<dui::Panel>(b);
    box->AddChild<dui::Text>(label);
    return box;
}

class LayoutWindow final : public dui::Window {
public:
    LayoutWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"LayoutFlex · 布局能力演示",
              .width  = 900.f,
              .height = 640.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Column)
            .Padding(16.f)
            .Gap(10.f)
            .Background(0xFFF3F3F3)
            .Color(0xFF222222)
            .FontSize(13.f));

        root->AddChild<dui::Text>(L"Flex 布局能力演示", dui::StyleBuilder()
            .FontSize(18.f)
            .FontWeight(dui::FontWeight::SemiBold)
            .Color(0xFF333333));

        // ---- FlexGrow：剩余空间按 grow 比例分配（三等分） -------------------
        NewCaption(root, L"FlexGrow：三等分");
        dui::Panel* rowA = root->AddChild<dui::Panel>(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        NewBox(rowA, L"grow=1", 0xFF4472C4, 0.f, 48.f, 1.f);
        NewBox(rowA, L"grow=1", 0xFF5B9BD5, 0.f, 48.f, 1.f);
        NewBox(rowA, L"grow=1", 0xFF4472C4, 0.f, 48.f, 1.f);

        // ---- 定宽 + 弹性：固定尺寸项不参与分配，弹性项吃满剩余 -------------
        NewCaption(root, L"定宽 + 弹性：中间项 FlexGrow 吃满剩余空间");
        dui::Panel* rowB = root->AddChild<dui::Panel>(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        NewBox(rowB, L"width=140", 0xFF70AD47, 140.f, 48.f);
        NewBox(rowB, L"grow=1", 0xFF548235, 0.f, 48.f, 1.f);
        NewBox(rowB, L"width=100", 0xFF70AD47, 100.f, 48.f);

        // ---- FlexShrink：超出的固定宽度按 shrink × 尺寸加权收缩 ------------
        NewCaption(root, L"FlexShrink：3 × 320 超出容器，收缩至放得下");
        dui::Panel* rowC = root->AddChild<dui::Panel>(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        NewBox(rowC, L"shrink=1", 0xFFED7D31, 320.f, 48.f, 0.f, 1.f);
        NewBox(rowC, L"shrink=1", 0xFFFFC000, 320.f, 48.f, 0.f, 1.f);
        NewBox(rowC, L"shrink=1", 0xFFED7D31, 320.f, 48.f, 0.f, 1.f);

        // ---- 对齐：容器内主轴/交叉轴双居中 ---------------------------------
        NewCaption(root, L"JustifyContent + AlignItems：双轴居中");
        dui::Panel* rowD = root->AddChild<dui::Panel>(dui::StyleBuilder()
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center)
            .Height(110.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f));
        rowD->AddChild<dui::Text>(L"水平垂直居中", dui::StyleBuilder()
            .FontSize(20.f)
            .FontWeight(dui::FontWeight::SemiBold));

        // ---- Gap：子项间距（主轴方向） -------------------------------------
        NewCaption(root, L"Gap：子项间距 16");
        dui::Panel* rowE = root->AddChild<dui::Panel>(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(16.f));
        NewBox(rowE, L"80", 0xFF7030A0, 80.f, 48.f);
        NewBox(rowE, L"80", 0xFF9C59D0, 80.f, 48.f);
        NewBox(rowE, L"80", 0xFF7030A0, 80.f, 48.f);
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    LayoutWindow window;
    return app.Run(window);
}
