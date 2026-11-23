// ============================================================================
//  DUI/App/NativeWindow.h — 窗口壳服务（内部头，不进 DUI.h）
//
//  HWND 封装 + 窗口过程：事件经 NativeWindowHost 抽象上抛，由本层的
//  WindowHostAdapter（App/App.cpp）适配到 dui::Window。
//
//  窗口每实例状态（含 this）存 GWLP_USERDATA（12-Windowing：单窗口单缩放）。
// ============================================================================

#pragma once

#include <windows.h>

#include "Core/Core.h"
#include "App/DropTarget.h"
#include "App/NativeWindowHost.h"

namespace dui {

class NativeWindow {
public:
    NativeWindow() = default;
    ~NativeWindow();

    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    // 创建（保持隐藏）→ 按窗口所在显示器 DPI 定最终尺寸 → OnNativeCreated。
    // 失败时返回 false 并填 error（无异常约定，ADR-007）。
    // borderless = true → 无系统边框（WS_POPUP）+ DWM 阴影，业务自绘标题栏。
    //   注意：无边框时 widthDip/heightDip 就是**整窗**尺寸（客户区覆盖全窗，
    //   WM_NCCALCSIZE 处理），不再做 AdjustWindowRect 边框补偿。
    // doubleClickMaximize = false → 无边框窗口双击拖动区不最大化（吃掉
    //   WM_NCLBUTTONDBLCLK）；true = 系统原生行为（默认）。
    bool Create(const wchar_t* title, float widthDip, float heightDip,
                NativeWindowHost& host, String& error, bool borderless = false,
                bool doubleClickMaximize = true);

    void Show();
    // 把窗口移到所在显示器工作区正中（尺寸不变）。显示前调用，构建过程仍不可见。
    // 用 MonitorFromWindow → MONITORINFO.rcWork，跨屏 / 任务栏 / DPI 都按物理像素算。
    void CenterOnScreen();
    bool Valid() const noexcept { return hwnd_ != nullptr; }
    HWND Handle() const noexcept { return hwnd_; }
    float Scale() const noexcept { return scale_; }

    // 窗口操作（无边框窗口的自绘标题栏按钮需要）
    void Minimize();                // 最小化
    void MaximizeOrRestore();       // 最大化 / 从最大化还原
    void RequestClose();            // 请求关闭（走 WM_CLOSE → 业务可拦截）
    bool IsMaximized() const;       // 当前是否最大化

    // 窗口定时器（上层按需启停；节拍经 NativeWindowHost::OnTimer 上抛）
    void StartTimer(uintptr_t id, unsigned ms);
    void StopTimer(uintptr_t id);

    // 隐形系统 caret（IME 插入点）：IME 依 GetCaretPos() 把候选窗口定位到
    // 光标处。我们自绘光标，故另建一个不可见的系统 caret 专门报告插入点。
    //
    // 高度不可为 1px：IME 依 caret 的**底边**摆放候选窗口，零高度插入点会让
    // 输入法把候选窗下移一整行。高度变化时重建位图。
    void EnsureImeCaret();
    void UpdateImeCaretPos(int pxX, int pyY, int pxHeight);
    void RemoveImeCaret();

    // 指针形态（鼠标光标）：idc = Windows 预定义光标资源（IDC_ARROW / IDC_IBEAM …）
    void SetCursorShape(LPCWSTR idc) const;

    Vec2 ClientSizePx() const;      // 客户区物理像素
    Vec2 ClientSizeDip() const;     // 客户区 DIP（12-Windowing §3）
    // 已最小化（IsIconic）：客户区尺寸为 0 → 不能进帧（App 层据此停表/阻塞）
    bool IsMinimized() const noexcept;

    // 应用退出时先摘掉宿主再析构：否则 DestroyWindow 同步发出的 WM_DESTROY
    // 会回调已析构的宿主（多窗口 App 的记录清理路径）
    void DetachHost() noexcept { host_ = nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static bool EnsureClassRegistered(String& error);
    Vec2 DipFromLParam(LPARAM lp) const noexcept;

    HWND  hwnd_ = nullptr;
    float scale_ = 1.f;
    bool   borderless_ = false;   // 无边框（业务自绘标题栏与边框）
    bool   dblClickMaximize_ = true;   // 无边框窗口双击拖动区是否最大化（原生行为）
    NativeWindowHost* host_ = nullptr;
    DropTarget* dropTarget_ = nullptr;   // 独立 COM 对象（M5.85；另持一引用）
    static float ScaleOf(void* ctx) noexcept;   // DropTarget 的 scale 提供者
    HBITMAP caretBmp_ = nullptr;   // 隐形系统 caret 的位图（1×N 全透明）
    int caretHeight_ = 0;         // 当前 caret 位图高度（变化时重建）
};

}  // namespace dui
