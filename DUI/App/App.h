// ============================================================================
//  DUI/App/App.h — 应用运行时与窗口基类（01-Layering §2 App；12-Windowing）
//
//  窗口形态（已定案）：
//    · 使用者继承 dui::Window，构造期只声明窗口形态（标题/尺寸/Chrome），
//      不创建原生窗口；Application::Run(window) 接管创建 → OnCreate → Show
//      → 消息循环 → 退出码。
//    · OnCreate：原生窗口已建、尚未显示；Root() 可用，构建过程不可见。
//    · 无边框/自定义标题栏 = 构造里换 Chrome + OnCreate 里加标题栏
//      （12-Windowing §6）。
//
//  M1 骨架：Run 返回 0 并打印提示；M2 落地 Platform.Window + 消息循环 +
//  交换链，M5 落地 OnDpiChanged 完整流程。
// ============================================================================

#pragma once

#include <memory>

#include "UI/UI.h"

namespace dui {

// 窗口装饰（12-Windowing §6：无边框 + 自定义标题栏的同一扩展点）
enum class Chrome : uint8_t {
    System,       // 系统标题栏 + 边框
    Borderless,   // 无边框，业务自绘标题栏（M5）
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

protected:
    // 原生窗口已创建、尚未显示：唯一搭界面的地方（构建期间窗口不可见）
    virtual void OnCreate() {}
    virtual void OnDpiChanged(const DpiChanged&) {}
    virtual bool OnClosing() { return true; }   // 返回 false 阻止关闭（M2）

    Panel* Root() noexcept { return root_.get(); }

private:
    friend class Application;

    WindowOptions options_;
    float scale_ = 1.f;
    std::unique_ptr<Panel> root_;
};

class Application {
public:
    Application();
    ~Application();

    // 建 HWND → OnCreate → Show → 消息循环 → 退出码；窗口创建失败返回非 0，
    // 原因经 LastError() 查询（12-Windowing / App 契约）。
    int Run(Window& window);

    const String& LastError() const noexcept { return lastError_; }

private:
    String lastError_;
};

}  // namespace dui
