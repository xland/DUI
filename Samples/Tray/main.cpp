// ============================================================================
//  Tray/main.cpp — 系统托盘示例
//
//  演示：① 启动建窗 + 建托盘图标；② 关窗不退出（托盘常驻）；
//        ③ 左键托盘重新开窗；④ 右键托盘弹菜单（退出应用）。
//
//  图标：tray.ico 由工程 PostBuildEvent 拷到 exe 同目录，运行时按 exe 路径加载。
//  构建产物：Build\Tray\Debug|Release\Tray.exe
// ============================================================================

#include <windows.h>

#include <memory>
#include <vector>

#include "DUI/DUI.h"

namespace {

dui::App* g_app = nullptr;

// 窗口对象由本容器持有（App 只存 Window*，不负责释放）。
// 每次重新开窗都新建一个对象 —— 复用旧对象会让 OnCreate 重复往 root_ 加控件。
std::vector<std::unique_ptr<dui::Window>> g_windows;

class TrayWindow final : public dui::Window {
public:
    TrayWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"托盘示例",
              .width  = 460.f,
              .height = 300.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .FlexDirection(dui::FlexDirection::Column)
            .Padding(24.f)
            .Gap(12.f)
            .Background(0xFFF5F6F8)
            .Color(0xFF222222)
            .FontSize(13.f));

        root->AddChild<dui::Text>(L"系统托盘示例", dui::Style()
            .FontSize(20.f).FontWeight(dui::FontWeight::SemiBold));

        root->AddChild<dui::Text>(L"关掉这个窗口，应用不会退出，托盘图标仍在。", dui::Style());
        root->AddChild<dui::Text>(L"左键点托盘图标 → 重新打开窗口。", dui::Style());
        root->AddChild<dui::Text>(L"右键点托盘图标 → 菜单里可退出应用。", dui::Style());

        root->AddChild<dui::Panel>(dui::Style().FlexGrow(1.f));   // 弹性占位

        dui::Panel* hint = root->AddChild<dui::Panel>(dui::Style()
            .Padding(12.f)
            .CornerRadius(6.f)
            .Background(0xFFE8F0FE));
        hint->AddChild<dui::Text>(L"托盘图标不是窗口：框架用一个隐藏的 message-only 窗口接收回调消息，"
            L"它不占 WindowCount；应用不退出靠 SetQuitOnLastWindowClosed(false)。",
            dui::Style().Color(0xFF1E40AF));
    }
};

// 从 exe 同目录取图标路径（PostBuildEvent 已把 tray.ico 拷到那里）
dui::String IconPath() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    dui::String path(n > 0 ? buf : L"tray.ico");
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != dui::String::npos) path.resize(slash + 1);
    path += L"tray.ico";
    return path;
}

void OpenWindow() {
    auto w = std::make_unique<TrayWindow>();
    if (!g_app->AddWindow(*w)) return;   // 失败：LastError 见 g_app->LastError()
    g_windows.push_back(std::move(w));
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::App app;
    g_app = &app;
    app.SetQuitOnLastWindowClosed(false);   // 关掉所有窗口也不退出

    dui::Tray tray;
    tray.SetIconFromFile(IconPath().c_str());   // 框架接管图标；失败回退系统默认
    tray.Show(L"DUI 托盘示例");

    tray.OnClick([&] {
        if (app.WindowCount() == 0) OpenWindow();
    });

    tray.OnContextMenu([&](dui::Vec2 screenPx) {
        dui::Menu menu;
        menu.Add(L"打开窗口", [&] {
            if (app.WindowCount() == 0) OpenWindow();
        });
        menu.Add(L"退出应用", [&] { app.Quit(); });
        dui::ShowPopupMenu(menu, tray.Owner(), screenPx);
    });

    OpenWindow();          // 启动时先开一个窗口
    return app.Run();      // 关窗不返回；直到菜单里点"退出应用"
}
