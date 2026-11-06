// ============================================================================
//  Style/main.cpp — 样式能力演示
//
//  覆盖能力（03-Style / 15-Properties）：样式复用与派生（Style::Derive）、
//  继承属性沿树下传、状态变体 When(State::Hover, …)、
//  级联优先级（Style → 本地 Set → 状态变体）、
//  视觉属性集（Background / Color / CornerRadius / Opacity / 字号字重 /
//  LineHeight / Padding / Cursor）、Margin（外边距盒，不塌陷）、
//  TextAlign（内容盒内对齐 + 继承）、运行时 Set 覆盖（Thickness / 字体族指针）。
//
//  布局相关示例见 Samples/Layout；定位相关示例见 Samples/Position。
//
//  构建产物（Build 布局，DUI.slnx 根目录相对）：
//    中间产物  Build\Temp\Style\Debug|Release\
//    最终产物  Build\Style\Debug|Release\Style.exe
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

// 可复用的静态样式：派生样式持它的裸指针（parent_），故必须是静态存储
// （03-Style §2）。样式里也可以写布局属性（FlexGrow 等），与视觉属性同层。
static const dui::Style kCard = dui::Style()
    .Height(52.f)
    .FlexGrow(1.f)
    .Padding(10.f)
    .CornerRadius(6.f)
    .Color(0xFFFFFFFF)
    .FontSize(14.f)
    .JustifyContent(dui::Justify::Center)
    .AlignItems(dui::Align::Center);

// 分区小标题
void NewCaption(dui::Panel* parent, const wchar_t* text) {
    parent->AddChild<dui::Text>(text, dui::Style()
        .FontSize(12.f)
        .Color(0xFF777777));
}

class StyleWindow final : public dui::Window {
public:
    StyleWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"Style · 样式能力演示",
              .width  = 900.f,
              .height = 1000.f,
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

        root->AddChild<dui::Text>(L"样式能力演示", dui::Style()
            .FontSize(18.f)
            .FontWeight(dui::FontWeight::SemiBold)
            .Color(0xFF333333));

        // ---- 复用与派生：派生只存差异，解析沿 parent_ 链回溯 ------------------
        NewCaption(root, L"复用与派生：一份静态 kCard + Style::Derive 只写差异");
        dui::Panel* cardRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        dui::Panel* cardA = cardRow->AddChild<dui::Panel>(dui::Style::Derive(kCard)
            .Background(0xFF4472C4));
        cardA->AddChild<dui::Text>(L"派生 A：只加蓝底");
        dui::Panel* cardB = cardRow->AddChild<dui::Panel>(dui::Style::Derive(kCard)
            .Background(0xFF70AD47).CornerRadius(18.f));
        cardB->AddChild<dui::Text>(L"派生 B：绿底 + 圆角覆盖");
        dui::Panel* cardC = cardRow->AddChild<dui::Panel>(dui::Style::Derive(kCard)
            .Background(0xFF7030A0).FontSize(18.f));
        cardC->AddChild<dui::Text>(L"派生 C：紫底 + 字号覆盖");

        // ---- 继承属性：父计算值沿树下传，子级可显式覆盖 ----------------------
        NewCaption(root, L"继承属性：FontSize / FontWeight / Color / FontFamily 沿树下传");
        dui::Panel* inheritBox = root->AddChild<dui::Panel>(dui::Style()
            .FontSize(16.f)
            .FontWeight(dui::FontWeight::SemiBold)
            .Color(0xFF2D7FF9)
            .FontFamily(L"SimSun")
            .Height(68.f)
            .Padding(8.f)
            .Gap(4.f)
            .Background(0xFFFFFFFF)
            .CornerRadius(4.f));
        inheritBox->AddChild<dui::Text>(L"子 Text 未设样式 → 继承 16 号半粗 / 蓝色 / 宋体");
        inheritBox->AddChild<dui::Text>(L"子 Text 显式覆盖字号与颜色", dui::Style()
            .FontSize(12.f)
            .Color(0xFF777777));

        // ---- 状态变体 + 光标形态 ---------------------------------------------
        NewCaption(root, L"When(State::Hover)：悬停换背景；Cursor 声明指针形态");
        dui::Panel* hover = root->AddChild<dui::Panel>(dui::Style()
            .Height(48.f)
            .Background(0xFF4472C4)
            .CornerRadius(6.f)
            .Color(0xFFFFFFFF)
            .Cursor(dui::Cursor::Hand)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        hover->When(dui::State::Hover, dui::Style()
            .Background(0xFFED7D31)
            .Color(0xFFFFFFFF));
        hover->AddChild<dui::Text>(L"悬停我：变体换橙底，指针为手型");

        // ---- 级联优先级：层 2 状态变体 > 层 3 本地覆盖 > 层 4 Style ----------
        NewCaption(root, L"级联顺序：静止看 Set（层 3），悬停看 When（层 2，优先级更高）");
        dui::Panel* cascade = root->AddChild<dui::Panel>(dui::Style()
            .Height(48.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f)
            .Color(0xFF222222)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        cascade->When(dui::State::Hover, dui::Style()
            .Background(0xFFED7D31)
            .Color(0xFFFFFFFF));
        cascade->Set(dui::Prop::BackgroundColor, 0xFF4472C4);
        cascade->AddChild<dui::Text>(L"静止：Set 覆盖的蓝底 ／ 悬停：When 变体的橙底");

        // ---- 视觉属性集 -------------------------------------------------------
        NewCaption(root, L"视觉属性：Background / Opacity / CornerRadius / 字号字重 / LineHeight / Padding");
        dui::Panel* visualRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        dui::Panel* visualA = visualRow->AddChild<dui::Panel>(dui::Style()
            .Width(280.f).Height(76.f)
            .Background(0xFF70AD47)
            .CornerRadius(14.f)
            .Opacity(0.55f)
            .Padding(10.f)
            .Color(0xFFFFFFFF));
        visualA->AddChild<dui::Text>(L"圆角 14 + 背景透明度 0.55 + Padding 10");
        dui::Panel* visualB = visualRow->AddChild<dui::Panel>(dui::Style()
            .Width(280.f).Height(76.f)
            .Background(0xFFFFFFFF)
            .CornerRadius(4.f)
            .Padding(10.f));
        visualB->AddChild<dui::Text>(L"18 号粗体 · 行高 1.6\n第二行仍按 1.6 倍行距", dui::Style()
            .FontSize(18.f)
            .FontWeight(dui::FontWeight::Bold)
            .LineHeight(1.6f));

        // ---- Margin：外边距盒参与父布局占位（不塌陷，与 gap 叠加） ------------
        NewCaption(root, L"Margin：外边距盒参与占位，不塌陷、与 gap 叠加");
        dui::Panel* marginRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f)
            .AlignItems(dui::Align::Center)
            .Height(88.f)
            .Background(0xFFE7E6E6)
            .CornerRadius(4.f));
        const wchar_t* marginLabels[] = {L"无 margin", L"margin 12", L"左右各 24"};
        const dui::Color marginColors[] = {0xFF4472C4, 0xFF70AD47, 0xFFED7D31};
        for (int i = 0; i < 3; ++i) {
            dui::Style box = dui::Style()
                .Width(140.f).Height(48.f)
                .Background(marginColors[i]).CornerRadius(4.f).Color(0xFFFFFFFF)
                .JustifyContent(dui::Justify::Center).AlignItems(dui::Align::Center);
            if (i == 1) box.Margin(12.f);                         // 四边同值
            if (i == 2) box.Margin(dui::Thickness(0.f, 24.f));     // CSS 顺序：上下 0 / 左右 24
            dui::Panel* cell = marginRow->AddChild<dui::Panel>(box);
            cell->AddChild<dui::Text>(marginLabels[i]);
        }

        // ---- TextAlign：内容盒内文本对齐（继承属性） ---------------------------
        NewCaption(root, L"TextAlign：Start / Center / End，以及父级设置后的继承");
        dui::Panel* alignRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .Gap(8.f));
        const wchar_t* alignLabels[] = {L"Start", L"Center", L"End", L"End（继承）"};
        const dui::TextAlign aligns[] = {dui::TextAlign::Start, dui::TextAlign::Center,
                                         dui::TextAlign::End};
        for (int i = 0; i < 4; ++i) {
            dui::Style cellStyle = dui::Style()
                .Width(200.f).Height(52.f)
                .Background(0xFFFFFFFF).CornerRadius(4.f)
                .Padding(8.f);
            if (i == 3) cellStyle.TextAlign(dui::TextAlign::End);   // 设在容器 → 子 Text 继承
            dui::Panel* cell = alignRow->AddChild<dui::Panel>(cellStyle);
            if (i < 3) cell->AddChild<dui::Text>(alignLabels[i], dui::Style().TextAlign(aligns[i]));
            else       cell->AddChild<dui::Text>(alignLabels[i]);
        }

        // ---- 运行时覆盖：Set 可改 Thickness 类属性与字体族 --------------------
        NewCaption(root, L"运行时 Set：点击色块切换 Padding / Margin / CornerRadius / FontFamily");
        dui::Panel* toggle = root->AddChild<dui::Panel>(dui::Style()
            .Height(56.f)
            .Background(0xFF4472C4)
            .CornerRadius(4.f)
            .Color(0xFFFFFFFF)
            .Padding(8.f)
            .JustifyContent(dui::Justify::Center)
            .AlignItems(dui::Align::Center));
        dui::Text* toggleLabel = toggle->AddChild<dui::Text>(L"点击切换：四边 16 / 宋体");
        toggle->OnClick([toggle, toggleLabel](dui::ClickEvent&) {
            const bool compact = dui::ResolveProperty(toggle, dui::Prop::CornerRadius).f < 10.f;
            if (compact) {
                toggle->Set(dui::Prop::Padding, dui::Thickness(16.f));       // Thickness 值
                toggle->Set(dui::Prop::Margin, dui::Thickness(0.f, 24.f));    // 上下 0 / 左右 24
                toggle->Set(dui::Prop::CornerRadius, 16.f);                   // 标量 → 四边同值
                toggleLabel->Set(dui::Prop::FontFamily, L"SimSun");           // 字面量，长期存活
            } else {
                toggle->Set(dui::Prop::Padding, dui::Thickness(8.f));
                toggle->Set(dui::Prop::Margin, dui::Thickness(0.f));
                toggle->Set(dui::Prop::CornerRadius, 4.f);
                toggleLabel->Set(dui::Prop::FontFamily, L"Microsoft YaHei");
            }
        });
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    StyleWindow window;
    return app.Run(window);
}
