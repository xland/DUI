// ============================================================================
//  Layout/main.cpp — 布局能力演示
//
//  覆盖能力（04-Layout）：FlexDirection Row/Column、Gap、Padding、
//  FlexGrow（剩余空间分配）、FlexShrink + MinWidth（收缩）、
//  JustifyContent / AlignItems（主轴/交叉轴对齐）、嵌套容器、
//  Grid（Fixed/Flex/Auto/Percent 轨道 + 自动放置 + 显式行列 + 跨格）、
//  JustifyContent 六取值、alignSelf / alignItems::End·Baseline、
//  FlexBasis、rowGap/columnGap、visibility / overflow / letterSpacing。
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
              .height = 900.f,
          }) {}

protected:
    void OnCreate() override {
        // 内容比窗口高 → 放进 Scroller：直接挂在根 Column 上时 flex-shrink 会把
        // 每一块压扁（内容被压缩、文字却不被裁剪 → 文字溢出块外）。
        dui::Panel* host = Root();
        host->SetStyle(dui::Style().Background(0xFFF3F3F3));
        dui::Scroller* scroll = host->AddChild<dui::Scroller>(dui::Style()
            .FlexGrow(1.f));   // 视口高度由剩余空间决定（内容超出则在视口内滚动）
        dui::Panel* root = scroll->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Padding(16.f)
            .Gap(10.f)
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

        // ---- JustifyContent 六取值：剩余空间怎么分 --------------------------
        NewCaption(root, L"JustifyContent：Start / Center / End / SpaceBetween / SpaceAround / SpaceEvenly");
        dui::Panel* justRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        const dui::Justify justs[] = {dui::Justify::Start, dui::Justify::Center, dui::Justify::End,
                                      dui::Justify::SpaceBetween, dui::Justify::SpaceAround,
                                      dui::Justify::SpaceEvenly};
        for (int i = 0; i < 6; ++i) {
            dui::Panel* cell = justRow->AddChild<dui::Panel>(dui::Style()
                .Width(140.f).Height(52.f)
                .Background(0xFFFFFFFF).CornerRadius(4.f)
                .JustifyContent(justs[i])
                .Padding(4.f));
            NewBox(cell, L"1", 0xFF4472C4, 26.f, 26.f);
            NewBox(cell, L"2", 0xFF70AD47, 26.f, 26.f);
        }

        // ---- 交叉轴：Start 贴顶 / alignSelf 覆盖 / End 贴底 / Baseline 基线 ----
        NewCaption(root, L"AlignItems：Start 贴顶 · alignSelf 覆盖 · End 贴底 · Baseline 按首行基线");
        dui::Panel* crossRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(14.f)
            .Height(92.f)
            .Background(0xFFE7E6E6).CornerRadius(4.f)
            .AlignItems(dui::Align::Start));      // 父级：全部贴顶
        crossRow->AddChild<dui::Panel>(dui::Style()
            .Width(70.f).Height(28.f).Background(0xFF4472C4).CornerRadius(4.f));
        crossRow->AddChild<dui::Panel>(dui::Style()
            .Width(80.f).Height(28.f).Background(0xFFED7D31).CornerRadius(4.f)
            .AlignSelf(dui::Align::End));         // 单项覆盖：贴底
        crossRow->AddChild<dui::Text>(L"16 号", dui::Style().FontSize(16.f));
        crossRow->AddChild<dui::Text>(L"28 号基线对齐", dui::Style().FontSize(28.f));

        // ---- FlexBasis：主轴基准尺寸 ------------------------------------------
        NewCaption(root, L"FlexBasis：basis=60 + grow=1 → 按基准起算再分剩余");
        dui::Panel* basisRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        for (int i = 0; i < 3; ++i) {
            dui::Panel* b = basisRow->AddChild<dui::Panel>(dui::Style()
                .FlexBasis(60.f).FlexGrow(1.f).Height(46.f)
                .Background(0xFF4472C4).CornerRadius(4.f)
                .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center));
            b->AddChild<dui::Text>(L"basis 60", dui::Style().Color(0xFFFFFFFF));
        }

        // ---- 换行 + 行列间距 --------------------------------------------------
        NewCaption(root, L"FlexWrap + columnGap 12 / rowGap 20：换行后列间距与行间距分开");
        dui::Panel* wrapBox = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .FlexWrap(dui::FlexWrap::Wrap)
            .ColumnGap(12.f).RowGap(20.f)
            .Width(320.f)
            .Padding(8.f)
            .Background(0xFFE7E6E6).CornerRadius(4.f));
        for (int i = 0; i < 6; ++i)
            NewBox(wrapBox, L"item", 0xFF4472C4, 80.f, 40.f);

        // ---- 可见性 / 溢出裁剪 / 字距 -----------------------------------------
        NewCaption(root, L"Visibility 隐藏但占位 · Overflow 裁剪到内容盒 · LetterSpacing 字距");
        dui::Panel* miscRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(12.f)
            .AlignItems(dui::Align::Start));
        dui::Panel* hiddenBox = miscRow->AddChild<dui::Panel>(dui::Style()
            .Width(120.f).Height(58.f)
            .Background(0xFF4472C4).CornerRadius(4.f)
            .Visibility(dui::Visibility::Hidden));   // 占位但不画
        hiddenBox->AddChild<dui::Text>(L"看不见", dui::Style().Color(0xFFFFFFFF));
        dui::Panel* clipBox = miscRow->AddChild<dui::Panel>(dui::Style()
            .Width(220.f).Height(58.f)
            .Background(0xFFFFFFFF).CornerRadius(4.f)
            .Overflow(dui::Overflow::Hidden)           // 内容裁到盒内
            .Padding(6.f));
        clipBox->AddChild<dui::Text>(L"overflow hidden：这段文字会超出盒子，但被裁掉了。",
                                     dui::Style().Color(0xFF222222));
        miscRow->AddChild<dui::Text>(L"字距 4 DIP", dui::Style()
            .LetterSpacing(4.f).Color(0xFF222222));
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::Application app;
    LayoutWindow window;
    return app.Run(window);
}
