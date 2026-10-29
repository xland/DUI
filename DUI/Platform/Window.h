// ============================================================================
//  DUI/Platform/Window.h — 平台窗口封装（内部头，不进 DUI.h，公共 API 无 windows.h）
//
//  分层（01-Layering §1）：Platform 是最底层，不得反向依赖 App/UI ——
//  窗口事件经 NativeWindowHost 抽象上抛，由 App 层适配到 dui::Window
//  （适配器在 App/App.cpp）。
//
//  窗口每实例状态（含 this）存 GWLP_USERDATA（12-Windowing：单窗口单缩放）。
// ============================================================================

#pragma once

#include <windows.h>

#include "Core/Core.h"

namespace dui::platform {

// 平台窗口 → 上层的事件接口（App 层实现；protected 虚函数由宿主回调）
class NativeWindowHost {
public:
    virtual ~NativeWindowHost() = default;

    // HWND 已创建（隐藏）、尺寸已按显示器 DPI 定好 —— 此时上层可搭界面
    virtual void OnNativeCreated(HWND hwnd, float initialScale) = 0;
    // 返回 false 阻止关闭
    virtual bool OnCloseRequest() = 0;
    // 12-Windowing §4：newScale = HIWORD(wParam)/96；suggestedDip 为系统建议
    // 矩形（整窗物理坐标）按 newScale 换算的 DIP 值；clientDipChanged =
    // 客户区 DIP 尺寸是否变化（§4-5 分级失效：不变只重绘，不重排）
    virtual void OnDpiChanged(float newScale, const Rect& suggestedDip, bool clientDipChanged) = 0;
    // 指针输入（M3：物理坐标已换算为窗口客户区 DIP；完整归一化在 M4）
    virtual void OnPointerMove(Vec2 ptDip) = 0;
    virtual void OnPointerButton(Vec2 ptDip, bool down) = 0;   // M3：仅左键
    virtual void OnPointerLeave() = 0;
    virtual void OnResized() = 0;
};

class NativeWindow {
public:
    NativeWindow() = default;
    ~NativeWindow();

    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    // 创建（保持隐藏）→ 按窗口所在显示器 DPI 定最终尺寸 → OnNativeCreated。
    // 失败时返回 false 并填 error（无异常约定，ADR-007）。
    bool Create(const wchar_t* title, float widthDip, float heightDip,
                NativeWindowHost& host, String& error);

    void Show();
    bool Valid() const noexcept { return hwnd_ != nullptr; }
    HWND Handle() const noexcept { return hwnd_; }
    float Scale() const noexcept { return scale_; }

    Vec2 ClientSizePx() const;      // 客户区物理像素
    Vec2 ClientSizeDip() const;     // 客户区 DIP（12-Windowing §3）

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static bool EnsureClassRegistered(String& error);
    Vec2 DipFromLParam(LPARAM lp) const noexcept;

    HWND  hwnd_ = nullptr;
    float scale_ = 1.f;
    NativeWindowHost* host_ = nullptr;
};

}  // namespace dui::platform
