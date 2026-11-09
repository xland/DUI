// ============================================================================
//  Icon/main.cpp — 内置矢量图标示例
//
//  演示：9 个 dui::IconName 图样、Style().Width/Height 自由尺寸、
//        Color 继承 + When(State::Hover) 换色、SetIcon 运行时换图样。
//  图标为框架自绘矢量（Painter 形状指令），不依赖字体或位图资源。
//
//  构建产物：Build\Icon\Debug|Release\Icon.exe
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

static const dui::IconName kNames[] = {
    dui::IconName::Check, dui::IconName::CheckChecked,
    dui::IconName::Radio, dui::IconName::RadioChecked,
    dui::IconName::Minimize, dui::IconName::Restore,
    dui::IconName::Maximize, dui::IconName::Close, dui::IconName::Search,
};
static const wchar_t* kLabels[] = {
    L"Check", L"CheckChecked", L"Radio", L"RadioChecked", L"Minimize",
    L"Restore", L"Maximize", L"Close", L"Search",
};

void NewCaption(dui::Panel* parent, const wchar_t* text) {
    parent->AddChild<dui::Text>(text, dui::Style().FontSize(12.f).Color(0xFF777777));
}

class IconWindow final : public dui::Window {
public:
    IconWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"Icon · 内置矢量图标",
              .width  = 900.f,
              .height = 600.f,
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

        root->AddChild<dui::Text>(L"内置矢量图标", dui::Style()
            .FontSize(18.f).FontWeight(dui::FontWeight::SemiBold));

        // ---- 9 个图样一览 -----------------------------------------------------
        NewCaption(root, L"9 个内置图样（20×20）；名称用强类型 dui::IconName");
        for (int r = 0; r < 3; ++r) {
            dui::Panel* line = root->AddChild<dui::Panel>(dui::Style()
                .FlexDirection(dui::FlexDirection::Row)
                .Gap(8.f));
            for (int c = 0; c < 3; ++c) {
                const int i = r * 3 + c;
                dui::Panel* cell = line->AddChild<dui::Panel>(dui::Style()
                    .Width(200.f).Height(52.f)
                    .FlexDirection(dui::FlexDirection::Row)
                    .AlignItems(dui::Align::Center)
                    .Gap(12.f)
                    .Padding(dui::Thickness(6.f, 12.f))
                    .Background(0xFFFFFFFF)
                    .CornerRadius(4.f));
                cell->AddChild<dui::Icon>(dui::Style()
                    .Width(20.f).Height(20.f).Color(0xFF2563EB))
                    ->SetIcon(kNames[i]);
                cell->AddChild<dui::Text>(kLabels[i], dui::Style().FontSize(12.f));
            }
        }

        // ---- 尺寸：只由 Style().Width/Height 决定 -----------------------------
        NewCaption(root, L"尺寸：Style().Width/Height 自由定义，描边按尺寸等比缩放");
        dui::Panel* sizes = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .AlignItems(dui::Align::Center)
            .Gap(20.f)
            .Height(60.f));
        for (float sz : {16.f, 24.f, 32.f, 48.f}) {
            sizes->AddChild<dui::Icon>(dui::Style()
                .Width(sz).Height(sz).Color(0xFF1F2937))
                ->SetIcon(dui::IconName::Search);
        }

        // ---- 颜色：Color 继承 + hover 变体 ------------------------------------
        NewCaption(root, L"颜色：Color 是继承属性；When(State::Hover) 可直接换色");
        dui::Panel* colorRow = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .AlignItems(dui::Align::Center)
            .Gap(16.f)
            .Height(48.f)
            .Color(0xFF374151));
        dui::Icon* hoverIcon = colorRow->AddChild<dui::Icon>(dui::Style()
            .Width(32.f).Height(32.f));
        hoverIcon->SetIcon(dui::IconName::Close);
        hoverIcon->When(dui::State::Hover, dui::Style().Color(0xFFE81123));
        colorRow->AddChild<dui::Text>(L"悬停左侧图标变红（Color 由父级继承）");

        // ---- 运行时换图样 -----------------------------------------------------
        NewCaption(root, L"运行时：SetIcon 换图样（点击卡片在 check ↔ checkChecked 间切换）");
        dui::Panel* toggle = root->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .AlignItems(dui::Align::Center)
            .Gap(12.f)
            .Height(48.f)
            .Padding(8.f)
            .Background(0xFFFFFFFF)
            .CornerRadius(4.f)
            .Cursor(dui::Cursor::Hand));
        dui::Icon* sw = toggle->AddChild<dui::Icon>(dui::Style()
            .Width(24.f).Height(24.f).Color(0xFF2563EB));
        sw->SetIcon(dui::IconName::Check);
        dui::Text* hint = toggle->AddChild<dui::Text>(L"未选中");
        toggle->OnClick([sw, hint](dui::PointerEvent&) {
            const bool checked = sw->Name() == dui::IconName::CheckChecked;
            sw->SetIcon(checked ? dui::IconName::Check : dui::IconName::CheckChecked);
            hint->SetText(checked ? L"未选中" : L"已选中");
        });
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    IconWindow window;
    return app.Run(window);
}
