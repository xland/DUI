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
    // 滚轮：ptDip = 指针位置（客户区 DIP）；delta = 滚轮格数（正 = 向上滚）
    // horizontal = true 时应横向滚动（Shift + 滚轮由平台层判定）
    virtual void OnMouseWheel(Vec2 ptDip, float delta, bool horizontal) = 0;
    // 键盘输入（M5.9）：key = VK_* 虚拟键码；ctrl/shift/alt = 修饰键状态
    virtual void OnKeyDown(int key, bool ctrl, bool shift, bool alt) = 0;
    virtual void OnKeyUp(int key, bool ctrl, bool shift, bool alt) = 0;
    // 字符输入（WM_CHAR / WM_IME_CHAR；IME 提交亦走此路）
    virtual void OnChar(wchar_t ch) = 0;
    // 定时器节拍（WM_TIMER；id 由上层分配并自行管理）
    virtual void OnTimer(uintptr_t id) { (void)id; }
    virtual void OnResized() = 0;
};

// 剪贴板（CF_UNICODETEXT）：App 层注入到 UI 层（UI 不依赖 Platform）
bool GetClipboardText(String& out);
bool SetClipboardText(const String& text);

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

    // 窗口定时器（上层按需启停；节拍经 NativeWindowHost::OnTimer 上抛）
    void StartTimer(uintptr_t id, unsigned ms);
    void StopTimer(uintptr_t id);

    // 隐形系统 caret（IME 插入点）：IME 依 GetCaretPos() 把候选窗口定位到
    // 光标处。我们自绘光标，故另建一个不可见的系统 caret 专门报告插入点。
    void EnsureImeCaret();
    void UpdateImeCaretPos(int pxX, int pyY);
    void RemoveImeCaret();

    Vec2 ClientSizePx() const;      // 客户区物理像素
    Vec2 ClientSizeDip() const;     // 客户区 DIP（12-Windowing §3）

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static bool EnsureClassRegistered(String& error);
    Vec2 DipFromLParam(LPARAM lp) const noexcept;

    HWND  hwnd_ = nullptr;
    float scale_ = 1.f;
    NativeWindowHost* host_ = nullptr;
    HBITMAP caretBmp_ = nullptr;   // 隐形系统 caret 的位图（1×1 全透明）
};

}  // namespace dui::platform
