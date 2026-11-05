// ============================================================================
//  DUI/App/App.h — 应用运行时与窗口基类（01-Layering §2 App；12-Windowing）
//
//  窗口形态（已定案）：
//    · 使用者继承 dui::Window，构造期只声明窗口形态（标题/尺寸/Chrome），
//      不创建原生窗口；Application::Run(window) 接管创建 → OnCreate → Show
//      → 消息循环 → 退出码。
//    · OnCreate：原生窗口已建、尚未显示；Root() 可用，构建过程不可见。
//    · 无边框/自定义标题栏 = 构造里换 Chrome + OnCreate 里加标题栏
//      （12-Windowing §6.1）。
//
//  M1 骨架已被 M2 替换：Platform.Window（HWND）+ 消息循环已落地；M5 落地
//  OnDpiChanged 完整流程（交换链重建 / 字形重光栅）。
// ============================================================================

#pragma once

#include <cstdint>
#include <memory>

#include "Core/Dispatcher.h"
#include "UI/UI.h"

namespace dui {

namespace platform {
class NativeWindow;      // App/Frame.cpp 组装层使用（App.h 不带 windows.h）
}
namespace graphics {
class Device;
}

// 窗口装饰（12-Windowing §6.1：无边框 + 自定义标题栏的同一扩展点）
enum class Chrome : uint8_t {
    System,       // 系统标题栏 + 边框
    Borderless,   // 无边框，业务自绘标题栏（DWM 阴影 + 拖动移动 + 拖边框缩放）
};

// 非客户区命中码（对应 Win32 的 HT*；公共头不含 windows.h，故在此自定义）
enum class HitTestCode : int {
    Client = 1,        // HTCLIENT：普通客户区
    Caption = 2,       // HTCAPTION：拖动可移动窗口
    Left = 10,         // HTLEFT：左边缘可缩放
    Right = 11,        // HTRIGHT
    Top = 12,          // HTTOP
    TopLeft = 13,      // HTTOPLEFT
    TopRight = 14,     // HTTOPRIGHT
    Bottom = 15,       // HTBOTTOM
    BottomLeft = 16,   // HTBOTTOMLEFT
    BottomRight = 17,  // HTBOTTOMRIGHT
};

struct DpiChanged {
    float oldScale = 1.f;
    float newScale = 1.f;
    Rect  suggestedDip;    // 系统建议的新窗口矩形（已换算 DIP，12-Windowing §4）
};

struct WindowOptions {
    const wchar_t* title = L"";
    float width = 800.f;         // DIP
    float height = 600.f;        // DIP
    Chrome chrome = Chrome::System;
};

class Window {
public:
    explicit Window(const WindowOptions& options = {});
    virtual ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 12-Windowing §9-3：scale = dpi / 96；窗口级（多窗口可各在不同 DPI 显示器）
    float Scale()   const noexcept { return scale_; }
    float DipToPx(float dip) const noexcept { return dip * scale_; }
    float PxToDip(float px)  const noexcept { return px / scale_; }

    // 窗口操作（无边框窗口的自绘标题栏按钮需要）
    void Minimize();              // 最小化
    void MaximizeOrRestore();     // 最大化 / 从最大化还原
    void Close();                 // 请求关闭（走 WM_CLOSE → OnClosing 可拦截）
    bool IsMaximized() const;     // 当前是否最大化

    // 客户区尺寸（DIP）：自绘边框热区判定用（OnNcHitTest）
    Vec2 ClientSizeDip() const;

protected:
    // 原生窗口已创建、尚未显示：唯一搭界面的地方（构建期间窗口不可见）
    virtual void OnCreate() {}
    virtual void OnDpiChanged(const DpiChanged&) {}
    virtual bool OnClosing() { return true; }   // 返回 false 阻止关闭（M2）

    // 非客户区命中（仅 Chrome::Borderless 有意义）：ptDip = 客户区 DIP。
    // 返回 true → 采用 hit：Caption = 拖动可移动窗口；Left/Right/Top/Bottom/
    // 四个角 = 拖边框可缩放（由系统接管，业务不必自己处理鼠标与移动逻辑）。
    virtual bool OnNcHitTest(Vec2 ptDip, HitTestCode& hit) {
        (void)ptDip; (void)hit;
        return false;
    }
    // 窗口状态变化（尺寸改变 / 最大化还原）：业务可刷新自绘按钮的图标等
    virtual void OnStateChanged() {}

    Panel* Root() noexcept { return root_.get(); }

private:
    friend class Application;
    friend class WindowHostAdapter;    // App.cpp：平台事件 → 本类事件的适配器

    WindowOptions options_;
    float scale_ = 1.f;
    void* native_ = nullptr;           // platform::NativeWindow*（平台窗口对象；公共头不带 windows.h）
    std::unique_ptr<Panel> root_;
    uintptr_t blinkTimer_ = 0;         // 光标闪烁节拍定时器（0 = 未启动）

    // M2 内部用（M3 交换链创建也需要）；不进公共 API
    void* NativeHandle() const noexcept;   // HWND（App.cpp 定义）

    // 光标闪烁节拍（内部）：按 UI 层需求启停窗口定时器
    void SyncBlinkTimer();
    void OnTimer(uintptr_t id);
    // IME 插入点：把系统 caret（隐形）移到焦点控件的光标处 —— IME 依
    // GetCaretPos() 把候选窗口定位到光标下方。
    void SyncImeCaret();
};

class WindowHostAdapter;                 // App.cpp：平台事件适配器（拖拽就地渲染用）

class Application {
public:
    Application();
    ~Application();

    // 建 HWND → OnCreate → Show → 消息循环 → 退出码；窗口创建失败返回非 0，
    // 原因经 LastError() 查询（12-Windowing / App 契约）。
    int Run(Window& window);

    const String& LastError() const noexcept { return lastError_; }

private:
    friend class WindowHostAdapter;      // App.cpp：拖拽尺寸变化的就地渲染（OnResized）

    // 帧路径（App/Frame.cpp）：尺寸同步 → 失效检查 → 重排 → 收集 → 光栅 → Present。
    // static：不依赖实例状态，供 Run 主循环与 OnResized 就地渲染共用
    static void RenderFrame(Window& window, const platform::NativeWindow& native, graphics::Device& device);

    Dispatcher dispatcher_;   // UI 任务队列（07-Threading：[调度] 步排空）
    String lastError_;
};

}  // namespace dui
