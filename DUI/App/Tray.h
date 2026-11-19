// ============================================================================
//  DUI/App/Tray.h — 系统托盘（Shell_NotifyIcon；M5.78）
//
//  自包含：内部建一个 message-only 窗口接收托盘回调消息，不占用 Application
//  的窗口列表（不影响 WindowCount / 退出策略）。销毁顺序安全：析构即撤图标、
//  关消息窗口，之后回调不再到达。
//
//  用途：配合 Application::SetQuitOnLastWindowClosed(false) 实现"关窗不退出、
//  托盘常驻、点图标再唤出窗口"。
// ============================================================================

#pragma once

#include <functional>

#include <windows.h>

#include "Core/Core.h"
#include "App/Menu.h"

namespace dui {

class Tray {
public:
    Tray();
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // 图标与提示。icon 可为 nullptr（用系统默认应用图标）；这一重载不接管
    // icon 的所有权（不 DestroyIcon），调用方自行管理其生命周期。
    // 返回 false 时查 LastError()。重复 Show 会先移除旧图标。
    bool Show(HICON icon, const wchar_t* tip);

    // 便捷加载：从 .ico 文件或内存中的 .ico 文件字节创建图标，Tray 接管其
    // 所有权（替换旧图标 / 析构时 DestroyIcon），调用方无需管理。加载成功后
    // 若托盘已显示则立即换图；失败查 LastError()。data 只在调用期间使用。
    bool SetIconFromFile(const wchar_t* filePath);
    bool SetIconFromMemory(const void* data, size_t size);

    // 用 SetIconFromFile/SetIconFromMemory 加载的图标显示；未加载过则用系统
    // 默认图标。等价于 Show(已加载图标, tip)。
    bool Show(const wchar_t* tip);

    void Hide() noexcept;
    bool Visible() const noexcept { return visible_; }

    // 提示文本（图标已显示时可更新）。
    bool SetTip(const wchar_t* tip);

    // 弹菜单用的 owner 句柄（= 内部 message-only 窗口），传给 dui::ShowPopupMenu，
    // 让右键菜单正确关联到托盘。ShowPopupMenu 的 owner 不可为 nullptr。
    void* Owner() const noexcept { return hwnd_; }

    // 左键单击 / 双击 / 右键回调（均在 UI 线程）。右键不自动弹菜单 ——
    // 业务在 OnContextMenu 里调 ShowPopupMenu(menu, tray.Owner(), posScreen)。
    Tray& OnClick(std::function<void()> fn) { onClick_ = std::move(fn); return *this; }
    Tray& OnDoubleClick(std::function<void()> fn) { onDoubleClick_ = std::move(fn); return *this; }
    Tray& OnContextMenu(std::function<void(Vec2 posScreenPx)> fn) {
        onContextMenu_ = std::move(fn); return *this;
    }

    const String& LastError() const noexcept { return lastError_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool EnsureWindow();

    HWND hwnd_ = nullptr;
    HICON ownedIcon_ = nullptr;  // SetIconFromFile/Memory 加载的图标，本类接管
    UINT id_ = 0;              // NOTIFYICONDATAW.uID（本实例内固定为 1）
    bool visible_ = false;
    String lastError_;
    std::function<void()> onClick_;
    std::function<void()> onDoubleClick_;
    std::function<void(Vec2)> onContextMenu_;
    POINT lastClickPos_{};     // 右键坐标（屏幕物理像素）
};

}  // namespace dui
