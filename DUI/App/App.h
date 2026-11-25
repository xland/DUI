// ============================================================================
//  DUI/App/App.h — 应用运行时 App（01-Layering §2 App）
//
//  窗口基类 dui::Window 在 App/Window.h（公共 API，DUI.h 一并暴露）。
//  M1 骨架已被 M2 替换：Platform.Window（HWND）+ 消息循环已落地；M5 落地
//  OnDpiChanged 完整流程（交换链重建 / 字形重光栅）。
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <windows.h>      // 公共 API 直接使用 HWND 等 Win32 类型

// winuser.h 在 UNICODE 下把 MessageBox 定义成 MessageBoxW；公共头引入 Win32 头后
// 必须清掉该宏，否则 Window::MessageBox 声明会被替换成 MessageBoxW，与下面的
// 兼容别名重复。App/Dialog.h 末尾另有一处同样的兜底（应对业务先包含 windows.h）。
#ifdef MessageBox
#undef MessageBox
#endif

#include "Core/Dispatcher.h"
#include "App/Dialog.h"
#include "App/Menu.h"
#include "UI/UI.h"
#include "App/Window.h"

namespace dui {

class NativeWindow;          // App/NativeWindow.h：App/Frame.cpp 组装层使用（避免依赖内部头）
namespace graphics {
class Device;
}

class WindowHostAdapter;                 // App.cpp：平台事件适配器（拖拽就地渲染用）

class App {
public:
    App();
    ~App();

    // 单窗口便捷入口（兼容既有代码）：AddWindow(window) + Run() 的语法糖。
    // 建 HWND → OnCreate → Show → 消息循环 → 退出码；创建失败返回非 0，
    // 原因经 LastError() 查询（12-Windowing / App 契约）。
    int Run(Window& window);

    // ---- 多窗口（M5.77）----
    // 注册窗口：建 HWND + 独立 Device + 宿主，并立即 OnCreate / Show。
    // 成功后由随后的 Run() 统一驱动；失败返回 false（原因见 LastError）。
    // 注意：一个进程应当只有一个 App 实例（Dispatcher::Ui() 是进程级）。
    bool AddWindow(Window& window);
    // 统一消息循环：驱动所有已注册窗口；最后一个窗口销毁时退出。
    int  Run();
    // 已注册且尚未销毁的窗口数
    size_t WindowCount() const noexcept;

    // ---- 退出策略（M5.78）----
    // 最后一个窗口销毁时是否自动结束消息循环。默认 true（原行为）。
    //   true  → 最后一个窗口关闭 → 自动退出，Run() 返回。
    //   false → 最后一个窗口关闭后 Run() 仍继续（窗口数为 0），进程存活：
    //           用于系统托盘常驻 / 无窗口后台阶段；业务随后调 Quit() 退出。
    // 注意：即使为 false，业务仍需在某个时刻调 Quit()，否则 Run() 永不返回。
    void SetQuitOnLastWindowClosed(bool on) noexcept { quitOnLastWindowClosed_ = on; }
    bool QuitOnLastWindowClosed() const noexcept { return quitOnLastWindowClosed_; }

    // 请求结束消息循环，exitCode 成为 Run() 的返回值（默认 0）。
    // 可从任意线程调用（线程安全地投递 WM_QUIT）；无窗口阶段同样有效。
    void Quit(int exitCode = 0) noexcept;

    // ---- 应用级定时器（M5.78，窗口无关）----
    // 与窗口无关：全部窗口销毁后仍周期触发。回调在 UI 线程消息循环里执行。
    // ms 为周期（夹紧到 >=1），返回 id（>0）传给 KillTimer 取消；每次调用
    // 分配新 id（不会取消旧定时器）。要一次性触发就在 fn 里 KillTimer(自身 id)。
    // fn 不要做重活（与 OnTimer 同属 UI 线程节拍）。
    // 注意：回调若捕获窗口 / 控件指针，窗口销毁后本定时器仍触发 → UAF。
    // 这类需求请用 Window::SetTimer（绑定 HWND，随窗口销毁自动失效）。
    unsigned SetTimer(unsigned ms, std::function<void()> fn);
    void     KillTimer(unsigned id) noexcept;

    const String& LastError() const noexcept { return lastError_; }

private:
    friend class WindowHostAdapter;      // App.cpp：宿主回调多窗口生命周期

    struct WindowRecord;                 // 定义在 App.cpp（含 Device / host，不进公共头）

    // 帧路径（App/Frame.cpp）：尺寸同步 → 失效检查 → 重排 → 收集 → 光栅 → Present。
    // static：不依赖实例状态，供 Run 主循环与 OnResized 就地渲染共用
    static void RenderFrame(Window& window, const NativeWindow& native, graphics::Device& device);

    // WM_DESTROY：标记记录待清理，返回 true 表示"最后一个窗口且策略允许 → 退出"
    bool OnWindowDestroyed(WindowHostAdapter* host);

    // 应用定时器：由 ::SetTimer(nullptr, id, ms, ...) 投递的 WM_TIMER 触发，
    // 在 UI 线程主循环里按 id 找到回调执行（见 App.cpp）。
    void DispatchAppTimer(unsigned id);

    std::vector<std::unique_ptr<WindowRecord>> windows_;
    String lastError_;

    bool quitOnLastWindowClosed_ = true;
    // key = ::SetTimer(nullptr,...) 返回的系统 timer id（WM_TIMER 的 wParam）。
    std::unordered_map<unsigned, std::function<void()>> timers_;
};

}  // namespace dui
