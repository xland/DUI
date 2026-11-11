// ============================================================================
//  CustomFont/main.cpp — 自定义字体文件（图标字体）演示
//
//  加载工程内未安装的 iconfont.ttf（图标字体，iconfont.cn 导出），
//  在窗口中用该字体族渲染一排字体图标 + 图标名。
//
//  字体文件随构建拷到 exe 目录（vcxproj PostBuildEvent），
//  运行时按 exe 所在目录定位加载。
//
//  格式：仅 ttf / otf / ttc —— DirectWrite 只认 SFNT 容器；同目录的
//  iconfont.woff2 不被支持（Web 字体封装，需先用 woff2/fontTools 转成 ttf）。
//
//  注意：框架公共头不含 windows.h，wWinMain 入口类型由本文件自带。
// ============================================================================

#include <windows.h>

#include <cwchar>

#include "DUI/DUI.h"

namespace {

struct IconItem {
    const wchar_t* name;
    wchar_t        glyph;
};

// 码表来自随字体包附带的 iconfont.json（font_class → unicode）
constexpr IconItem kIcons[] = {
    {L"star",     0xEBE1},
    {L"guanli",   0xE798},
    {L"map",      0xE694},
    {L"kefu",     0xE603},
    {L"order",    0xE61B},
    {L"edit",     0xE652},
    {L"check",    0xE60D},
    {L"uncheck",  0xE605},
    {L"cart",     0xE602},
    {L"discover", 0xE601},
    {L"me",       0xE614},
    {L"shop",     0xE600},
    {L"rmb",      0xE6AC},
};

// exe 所在目录 + 文件名（PostBuildEvent 把 iconfont.ttf 拷到 exe 目录）
dui::String FontPathInExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    dui::String path(buf);
    const size_t slash = path.find_last_of(L'\\');
    if (slash != dui::String::npos) path.resize(slash + 1);
    return path + L"iconfont.ttf";
}

class CustomFontWindow final : public dui::Window {
public:
    CustomFontWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI CustomFont",
              .width  = 760.f,
              .height = 420.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .Gap(16.f)
            .Padding(24.f)
            .Background(0xFFF5F5F5));

        root->AddChild<dui::Text>(L"自定义字体图标（iconfont.ttf）", dui::Style()
            .FontSize(20.f).Color(0xFF1F2937).FlexShrink(0.f));

        // 加载工程内字体文件，拿字体族名（= "iconfont"）。
        // FontFamily 样式只存 wchar_t* 指针（不拷字符串），族名串必须活过整个
        // 使用期 —— 局部 String 出函数即悬空 → 图标变方框，故用 static。
        static const dui::String family = dui::LoadFontFamily(FontPathInExeDir().c_str());
        if (family.empty()) {
            root->AddChild<dui::Text>(L"字体加载失败（iconfont.ttf 不在 exe 目录）",
                                      dui::Style().FontSize(14.f).Color(0xFFB91C1C));
            return;
        }

        root->AddChild<dui::Text>(L"字体族：iconfont · 用 Style().FontFamily 指定后即可渲染图标码位",
                                  dui::Style().FontSize(13.f).Color(0xFF6B7280).FlexShrink(0.f));
        root->AddChild<dui::Text>(L"支持格式：ttf / otf / ttc（DirectWrite 只认 SFNT）；woff2 需先转换",
                                  dui::Style().FontSize(12.f).Color(0xFF9CA3AF).FlexShrink(0.f));

        // 图标流式排列：单行 Row + FlexWrap(Wrap)，宽度不够自动换行，图标本体用 iconfont 族 + 大字号
        dui::Panel* grid = root->AddChild<dui::Panel>(dui::Style()
            .Gap(20.f));
        wchar_t glyph[2] = {};
        dui::Panel* line = grid->AddChild<dui::Panel>(dui::Style()
            .FlexDirection(dui::FlexDirection::Row)
            .FlexWrap(dui::FlexWrap::Wrap)
            .Gap(28.f));
        for (int idx = 0; idx < 13; ++idx) {
            dui::Panel* cell = line->AddChild<dui::Panel>(dui::Style()
                .FlexDirection(dui::FlexDirection::Column)
                .AlignItems(dui::Align::Center)
                .Gap(6.f));
            glyph[0] = kIcons[idx].glyph;
            cell->AddChild<dui::Text>(glyph, dui::Style()
                .FontFamily(family.c_str())
                .FontSize(32.f)
                .Color(0xFF2563EB));
            cell->AddChild<dui::Text>(kIcons[idx].name, dui::Style()
                .FontSize(12.f)
                .Color(0xFF6B7280));
        }
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::Application app;
    CustomFontWindow window;
    return app.Run(window);
}
