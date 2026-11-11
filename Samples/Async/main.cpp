// ============================================================================
//  Async/main.cpp — 异步示例：点击按钮 → 子线程执行任务 → 过程中更新界面
//
//  一个协程把线程切换写成线性代码：co_await Background() 切到子线程，
//  co_await Foreground() 切回 UI 线程；只有后者之后才能碰界面。
//  每步 Sleep 一下，既是模拟耗时计算，也给 UI 留出排空队列 + 重绘的时间。
// ============================================================================

#include <windows.h>

#include <string>

#include "DUI/DUI.h"

namespace {

static const dui::Style kButton = dui::Style()
    .Background(0xFF374151).Color(0xFFFFFFFF)
    .Padding(8.f).CornerRadius(4.f)
    .Cursor(dui::Cursor::Hand);

// 耗时任务：eager 协程创建时在 UI 线程跑到第一个 co_await，所以令牌在这里取。
dui::Task<void> Work(dui::Text* status) {
    auto life = status->Lifetime();
    for (int i = 1; i <= 10; ++i) {
        co_await dui::Background();       // 切到子线程
        Sleep(600);                        // 耗时计算（只碰纯数据）
        dui::String text = L"处理中  " + std::to_wstring(i * 10) + L"%";
        co_await dui::Foreground();       // 切回 UI 线程
        if (!life) co_return;             // 元素已销毁 → 正常退出，不再碰 status
        status->SetText(text);
    }
    status->SetText(L"完成：10 步全部结束");
}

class AsyncWindow final : public dui::Window {
public:
    AsyncWindow()
        : dui::Window(dui::WindowOptions{
              .title  = L"DUI 异步示例",
              .width  = 560.f,
              .height = 280.f,
          }) {}

protected:
    void OnCreate() override {
        dui::Panel* root = Root();
        root->SetStyle(dui::Style()
            .Gap(16.f).Padding(24.f)
            .Background(0xFFF5F5F5));

        root->AddChild<dui::Text>(L"点击按钮：任务在子线程跑 10 步，每步切回 UI 更新下面的文字",
                                  dui::Style().FontSize(16.f));
        dui::Button* start = root->AddChild<dui::Button>(L"开始任务", kButton);
        dui::Text* status = root->AddChild<dui::Text>(L"等待开始",
                                  dui::Style().FontSize(22.f).Height(48.f));

        start->OnClick([status](dui::PointerEvent&) {
            Work(status);   // eager 协程：创建即运行，不用 Start()/Then()
        });
    }
};

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    dui::Application app;
    AsyncWindow window;
    return app.Run(window);
}
