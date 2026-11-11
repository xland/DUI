// ============================================================================
//  DUI/Core/Menu.h — 系统原生弹出菜单（TrackPopupMenuEx；Core 层 OS 服务）
//
//  只做弹出菜单：不提供菜单栏、快捷键解析，也不自动挂钩 WM_CONTEXTMENU ——
//  何时弹出、弹在哪由业务决定。菜单是 USER32 的独立窗口，不受宿主窗口
//  WS_EX_NOREDIRECTIONBITMAP / DirectComposition 合成路径影响。
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "Core/Core.h"

namespace dui {

// 链式构造：每次 ShowMenu 都按当前状态重建一份原生菜单。复选项的勾选状态
// 由业务保存，AddCheck 的回调回传切换后的新值（多个复选项可同时勾上）。
class Menu {
public:
    Menu& Add(const wchar_t* text, std::function<void()> onClick);
    Menu& Separator();
    Menu& AddSubMenu(const wchar_t* text, const Menu& sub);
    Menu& AddCheck(const wchar_t* text, bool checked,
                   std::function<void(bool)> onToggle);

    // 内部：供 Window::ShowMenu 构建原生菜单（业务无需访问）
    struct Item {
        enum class Kind : uint8_t { Command, Separator, SubMenu, Check };
        String text;
        Kind   kind = Kind::Command;
        bool   checked = false;
        std::function<void()>       onClick;
        std::function<void(bool)>   onToggle;
        std::shared_ptr<const Menu> sub;
    };
    const std::vector<Item>& Items() const noexcept { return items_; }

private:
    std::vector<Item> items_;
};

// 弹出菜单（同步模态；TrackPopupMenuEx）。owner 为 HWND（void* 传递），
// posScreenPx 为屏幕物理像素坐标。菜单弹出期间系统接管消息泵。
void ShowPopupMenu(const Menu& menu, void* owner, Vec2 posScreenPx);

}  // namespace dui
