// ============================================================================
//  Layout/main.cpp — 布局能力演示
//
//  覆盖能力（04-Layout）：FlexDirection Row/Column、Gap、Padding、
//  FlexGrow（剩余空间分配）、FlexShrink + MinWidth（收缩）、
//  JustifyContent / AlignItems（主轴/交叉轴对齐）、嵌套容器、
//  Grid（Fixed/Flex/Auto/Percent 轨道 + 自动放置 + 显式行列 + 跨格）。
//  样式相关示例（Margin / TextAlign / 运行时 Set）见 Samples/Style；
//  定位相关示例（Relative/Absolute/Fixed/zIndex）见 Samples/Position。
//
//  构建产物（Build 布局，DUI.slnx 根目录相对）：
//    中间产物  Build\Temp\Layout\Debug|Release\
//    最终产物  Build\Layout\Debug|Release\Layout.exe
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// 分区小标题
void NewCaption(dui::Panel* parent, const wchar_t* text) {
    parent->AddChild<dui::Text>(text, dui::Style()
        .FontSize(12.f)
        .Color(0xFF777777));
}

// 演示用色块：标签文字在块内双轴居中。width/height/grow/shrink ≤0 表示不设置
dui::Panel* NewBox(dui::Panel* parent, const wchar_t* label, dui::Color bg,
                   float width, float height, float grow = 0.f, float shrink = 0.f) {
    dui::Style b = dui::Style()
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
              .title  = L"Layout · 布局能力演示",
              .width  = 900.f,
              .height = 800.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Padding(16.f)
            .Gap(10.f)
            .Background(0xFFF3F3F3)
            .Color(0xFF222222)
            .FontSize(13.f));

        root->AddChild<dui::Text>(L"Flex 布局能力演示", dui::Style()
            .FontSize(18.f)
            .FontWeight(dui::FontWeight::SemiBold)
            .Color(0xFF333333));

        // ---- FlexGrow：剩余空间按 grow 比例分配（三等分） -------------------
        NewCaption(root, L"FlexGrow：三等分");
        dui::Panel* rowA = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        NewBox(rowA, L"grow=1", 0xFF4472C4, 0.f, 48.f, 1.f);
        NewBox(rowA, L"grow=1", 0xFF5B9BD5, 0.f, 48.f, 1.f);
        NewBox(rowA, L"grow=1", 0xFF4472C4, 0.f, 48.f, 1.f);

        // ---- 定宽 + 弹性：固定尺寸项不参与分配，弹性项吃满剩余 -------------
        NewCaption(root, L"定宽 + 弹性：中间项 FlexGrow 吃满剩余空间");
        dui::Panel* rowB = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        NewBox(rowB, L"width=140", 0xFF70AD47, 140.f, 48.f);
        NewBox(rowB, L"grow=1", 0xFF548235, 0.f, 48.f, 1.f);
        NewBox(rowB, L"width=100", 0xFF70AD47, 100.f, 48.f);

        // ---- FlexShrink：超出的固定宽度按 shrink × 尺寸加权收缩 ------------
        NewCaption(root, L"FlexShrink：3 × 320 超出容器，收缩至放得下");
        dui::Panel* rowC = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        NewBox(rowC, L"shrink=1", 0xFFED7D31, 320.f, 48.f, 0.f, 1.f);
        NewBox(rowC, L"shrink=1", 0xFFFFC000, 320.f, 48.f, 0.f, 1.f);
        NewBox(rowC, L"shrink=1", 0xFFED7D31, 320.f, 48.f, 0.f, 1.f);

        // ---- 对齐：容器内主轴/交叉轴双居中 ---------------------------------
        NewCaption(root, L"JustifyContent + AlignItems：双轴居中");
        dui::Panel* rowD = root->AddChild<dui::Panel>(dui::Style()
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center)
            .Height(110.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f));
        rowD->AddChild<dui::Text>(L"水平垂直居中", dui::Style()
            .FontSize(20.f)
            .FontWeight(dui::FontWeight::SemiBold));

        // ---- Gap：子项间距（主轴方向） -------------------------------------
        NewCaption(root, L"Gap：子项间距 16");
        dui::Panel* rowE = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(16.f));
        NewBox(rowE, L"80", 0xFF7030A0, 80.f, 48.f);
        NewBox(rowE, L"80", 0xFF9C59D0, 80.f, 48.f);
        NewBox(rowE, L"80", 0xFF7030A0, 80.f, 48.f);

        // ---- Grid：四类轨道 + 自动放置 + 显式行列 + 跨格 --------------------
        NewCaption(root, L"Grid：Fixed / Flex / Auto / Percent 轨道 + 自动放置 + 显式跨格");
        dui::Panel* grid = root->AddChild<dui::Panel>(dui::Style()
            .Display(dui::Display::Grid)
            .GridTemplateColumns({dui::Fixed(120.f), dui::Flex(1.f),
                                  dui::Auto, dui::Percent(20.f)})
            .GridTemplateRows({dui::Fixed(52.f), dui::Flex(1.f)})
            .Gap(8.f)
            .Height(140.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f));

        // 显式放置 + 跨两列：先添加（先占位），其余按序自动填空
        dui::Panel* wide = grid->AddChild<dui::Panel>(dui::Style()
            .Background(0xFFC00000).CornerRadius(4.f).Color(0xFFFFFFFF)
            .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center));
        wide->Set(dui::Prop::GridRow, 1);
        wide->Set(dui::Prop::GridColumn, 1);
        wide->Set(dui::Prop::GridColumnSpan, 2);
        wide->AddChild<dui::Text>(L"显式 row1 col1 · 跨 2 列");

        const wchar_t* labels[] = {
            L"Fixed 120", L"Flex 1", L"Auto", L"Percent 20", L"auto #5", L"auto #6",
        };
        const dui::Color colors[] = {
            0xFF4472C4, 0xFF70AD47, 0xFFED7D31, 0xFF7030A0, 0xFF5B9BD5, 0xFF548235,
        };
        for (int i = 0; i < 6; ++i) {
            dui::Panel* cell = grid->AddChild<dui::Panel>(dui::Style()
                .Background(colors[i]).CornerRadius(4.f).Color(0xFFFFFFFF)
                .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center));
            cell->AddChild<dui::Text>(labels[i]);
        }
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    LayoutWindow window;
    return app.Run(window);
}
