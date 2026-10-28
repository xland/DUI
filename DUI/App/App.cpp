// ============================================================================
//  DUI/App/App.cpp — Application / Window 的 M1 骨架
//  M2 落地：Platform.Window（HWND 封装）+ 消息循环 + 交换链 + OnCreate/Show 时序。
// ============================================================================

#include "App/App.h"

#include <cstdio>

namespace dui {

Window::Window(const WindowOptions& options) : options_(options) {
    root_ = std::make_unique<Panel>();
}

Window::~Window() = default;

Application::Application() = default;
Application::~Application() = default;

int Application::Run(Window& window) {
    lastError_.clear();
    (void)window;

    // M2：创建平台窗口 → window.OnCreate() → Show → 消息循环 → 退出码。
    // M1：API 骨架验证 —— 两个示例（Samples/main.cpp、Samples/ListSelect.cpp）
    // 应能编译链接并执行到此处。
    std::puts("[DUI] M1 skeleton: window/message-loop not implemented yet (M2).");
    return 0;
}

}  // namespace dui
