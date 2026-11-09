// ============================================================================
//  Samples/Menu/main.cpp — 系统原生弹出菜单
//
//  演示：右键客户区弹菜单（PointerEvent::button == PointerButton::Right）、
//        普通项 / 分隔条 / 多级子菜单 / 多个可同时勾选的复选项。
//        框架不自动挂钩右键：何时弹、弹在哪由业务自己决定。
//
//  构建产物：Build\Menu\Debug|Release\Menu.exe
// ============================================================================

#include <windows.h>

#include "DUI/DUI.h"

namespace {

class MenuWindow final : public dui::Window {
public:
    MenuWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"Menu · 系统原生弹出菜单",
              .width  = 520.f,
              .height = 320.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Gap(12.f).Padding(24.f)
            .FontSize(14.f).Color(0xFF1F2933)
            .Background(0xFFF3F4F6));

        root->AddChild<dui::Text>(L"在窗口任意位置点右键，弹出系统原生菜单",
            dui::Style().FontSize(16.f).FontWeight(dui::FontWeight::SemiBold));
        status_ = root->AddChild<dui::Text>(L"最近操作：—",
            dui::Style().FontSize(13.f).Color(0xFF475569));
        state_ = root->AddChild<dui::Text>(L"",
            dui::Style().FontSize(13.f).Color(0xFF475569));

        root->OnPointerUp([this](dui::PointerEvent& ev) {
            if (ev.button == dui::PointerButton::Right) ShowMenu(BuildMenu(), ev.pos);
        });
        RefreshState();
    }

private:
    dui::Menu BuildMenu() {
        dui::Menu recent;
        recent.Add(L"撤销", [this] { Note(L"撤销"); })
              .Add(L"重做", [this] { Note(L"重做"); });

        dui::Menu file;
        file.Add(L"新建", [this] { Note(L"新建"); })
            .Separator()
            .AddSubMenu(L"最近操作", recent)
            .Separator()
            .Add(L"退出", [this] { Close(); });

        dui::Menu m;
        m.AddCheck(L"显示网格", grid_,  [this](bool on) { grid_ = on;  RefreshState(); })
         .AddCheck(L"显示标尺", ruler_, [this](bool on) { ruler_ = on; RefreshState(); })
         .Separator()
         .AddSubMenu(L"文件", file)
         .Separator()
         .Add(L"关于", [this] { Note(L"关于"); });
        return m;
    }

    void Note(const wchar_t* text) {
        status_->SetText(dui::String(L"最近操作：") + text);
    }

    void RefreshState() {
        state_->SetText(dui::String(L"复选状态：网格 ") + (grid_ ? L"开" : L"关") +
                        L"　标尺 " + (ruler_ ? L"开" : L"关"));
    }

    dui::Text* status_ = nullptr;
    dui::Text* state_  = nullptr;
    bool grid_  = false;
    bool ruler_ = false;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    dui::Application app;
    MenuWindow window;
    return app.Run(window);
}
