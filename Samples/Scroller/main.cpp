// ============================================================================
//  Samples/Scroller/main.cpp — 滚动容器控件（dui::Scroller）示例
//
//  演示三种用法：
//    1. 双向滚动：横向卡片行（卡片比视口高 → 横向 + 纵向都有滚动条）
//    2. 纵向滚动：长列表（内容超出视口 → 纵向滚动条）
//    3. 嵌套滚动：外层纵向容器内嵌两个横向容器（裁剪区域取交集）
//
//  交互：滚轮纵向滚动 · Shift + 滚轮横向滚动 · 可直接拖动滚动条滑块。
// ============================================================================

#include <windows.h>   // 框架公共头不含 windows.h，入口类型由本文件自带
#include <cwchar>

#include "DUI/DUI.h"

namespace {

constexpr dui::Color kInk   = 0xFF1F2937;
constexpr dui::Color kMuted = 0xFF6B7280;
constexpr dui::Color kCard  = 0xFFFFFFFF;
constexpr dui::Color kRow   = 0xFFF1F5F9;

constexpr dui::Color kPalette[] = {
    0xFFDBEAFE, 0xFFDCFCE7, 0xFFFEF3C7,
    0xFFFCE7F3, 0xFFEDE9FE, 0xFFCFFAFE,
};

// 追加一条带序号的文本（Text 持有字符串副本）
dui::Text* AddNumbered(dui::Panel* parent, const wchar_t* prefix, int index,
                       dui::StyleBuilder style) {
    wchar_t buf[64] = {};
    std::swprintf(buf, 64, L"%s %02d", prefix, index);
    return parent->AddChild<dui::Text>(buf, style);
}

class ScrollerWindow final : public dui::Window {
public:
    ScrollerWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI Scroller",
              .width  = 880.f,     // DIP
              .height = 660.f,
          }) {}

protected:
    void OnCreate() override {   // 原生窗口已建、尚未显示
        dui::Panel* root = Root();
        root->SetStyle(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Column)
            .Gap(12.f)
            .Padding(16.f)
            .Background(0xFFF3F4F6));

        root->AddChild<dui::Text>(L"DUI Scroller — 滚动容器控件", dui::StyleBuilder()
            .FontSize(20.f).Color(kInk).FlexShrink(0.f));
        root->AddChild<dui::Text>(
            L"滚轮纵向滚动 · Shift + 滚轮横向滚动 · 可直接拖动滚动条滑块",
            dui::StyleBuilder().FontSize(13.f).Color(kMuted).FlexShrink(0.f));

        AddTwoAxis(root);
        AddList(root);
        AddNested(root);
    }

private:
    // 1) 双向滚动：横向卡片行（卡片比视口高 → 横向 + 纵向都有滚动条）
    void AddTwoAxis(dui::Panel* root) {
        dui::Scroller* sc = root->AddChild<dui::Scroller>(dui::StyleBuilder()
            .FlexDirection(dui::FlexDirection::Row)
            .Height(150.f)
            .FlexShrink(0.f)
            .Gap(10.f)
            .Padding(10.f)
            .CornerRadius(10.f)
            .Background(kCard));
        for (int i = 0; i < 6; ++i) {
            dui::Panel* card = sc->AddChild<dui::Panel>(dui::StyleBuilder()
                .Width(150.f).Height(220.f)
                .Padding(10.f)
                .CornerRadius(8.f)
                .Background(kPalette[i % 6]));
            AddNumbered(card, L"卡片", i + 1, dui::StyleBuilder().FontSize(14.f).Color(kInk));
        }
    }

    // 2) 纵向滚动：长列表
    void AddList(dui::Panel* root) {
        dui::Scroller* sc = root->AddChild<dui::Scroller>(dui::StyleBuilder()
            .FlexGrow(1.f)
            .Gap(8.f)
            .Padding(10.f)
            .CornerRadius(10.f)
            .Background(kCard));
        for (int i = 0; i < 20; ++i) {
            dui::Panel* row = sc->AddChild<dui::Panel>(dui::StyleBuilder()
                .Padding(10.f)
                .CornerRadius(6.f)
                .Background(kRow));
            AddNumbered(row, L"列表项", i + 1, dui::StyleBuilder().FontSize(14.f).Color(kInk));
        }
    }

    // 3) 嵌套滚动：外层纵向容器内嵌两个横向容器
    void AddNested(dui::Panel* root) {
        dui::Scroller* outer = root->AddChild<dui::Scroller>(dui::StyleBuilder()
            .Height(190.f)
            .FlexShrink(0.f)
            .Gap(10.f)
            .Padding(10.f)
            .CornerRadius(10.f)
            .Background(kCard));
        for (int line = 0; line < 2; ++line) {
            dui::Scroller* inner = outer->AddChild<dui::Scroller>(dui::StyleBuilder()
                .FlexDirection(dui::FlexDirection::Row)
                .Height(120.f)
                .Gap(10.f));
            for (int i = 0; i < 8; ++i) {
                dui::Panel* cell = inner->AddChild<dui::Panel>(dui::StyleBuilder()
                    .Width(140.f).Height(90.f)
                    .Padding(8.f)
                    .CornerRadius(6.f)
                    .Background(kPalette[(line * 8 + i) % 6]));
                AddNumbered(cell, L"格子", line * 8 + i + 1,
                            dui::StyleBuilder().FontSize(13.f).Color(kInk));
            }
        }
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    ScrollerWindow window;
    return app.Run(window);
}
