// ============================================================================
//  DUI/Core/Menu.cpp — 系统原生弹出菜单
//
//  HMENU 由 CreatePopupMenu 构建，子菜单挂在同一棵菜单树上；命令 ID 全局连续
//  编号，TrackPopupMenuEx(TPM_RETURNCMD) 返回后按 ID 查回调并调用。
//  DestroyMenu 递归销毁整棵树，故子菜单无需单独释放。
//  Window::ShowMenu 成员方法（owner 句柄 / dialogDepth_）在 App/Dialog.cpp。
// ============================================================================

#include "Core/Menu.h"

#include <windows.h>

#include <utility>

namespace dui {

Menu& Menu::Add(const wchar_t* text, std::function<void()> onClick) {
    Item it;
    it.text = text;
    it.kind = Item::Kind::Command;
    it.onClick = std::move(onClick);
    items_.push_back(std::move(it));
    return *this;
}

Menu& Menu::Separator() {
    Item it;
    it.kind = Item::Kind::Separator;
    items_.push_back(std::move(it));
    return *this;
}

Menu& Menu::AddSubMenu(const wchar_t* text, const Menu& sub) {
    Item it;
    it.text = text;
    it.kind = Item::Kind::SubMenu;
    it.sub = std::make_shared<Menu>(sub);   // 拷贝定格：之后改动 sub 不影响本菜单
    items_.push_back(std::move(it));
    return *this;
}

Menu& Menu::AddCheck(const wchar_t* text, bool checked,
                     std::function<void(bool)> onToggle) {
    Item it;
    it.text = text;
    it.kind = Item::Kind::Check;
    it.checked = checked;
    it.onToggle = std::move(onToggle);
    items_.push_back(std::move(it));
    return *this;
}

namespace {

using Command = std::function<void()>;

void AppendItems(HMENU h, const Menu& m, std::vector<Command>& cmds) {
    for (const Menu::Item& it : m.Items()) {
        switch (it.kind) {
        case Menu::Item::Kind::Separator:
            ::AppendMenuW(h, MF_SEPARATOR, 0, nullptr);
            break;
        case Menu::Item::Kind::SubMenu: {
            HMENU sub = ::CreatePopupMenu();
            AppendItems(sub, *it.sub, cmds);
            ::AppendMenuW(h, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), it.text.c_str());
            break;
        }
        case Menu::Item::Kind::Command:
        case Menu::Item::Kind::Check: {
            const UINT id = static_cast<UINT>(cmds.size()) + 1;
            if (it.kind == Menu::Item::Kind::Check) {
                cmds.push_back([fn = it.onToggle, checked = it.checked] {
                    if (fn) fn(!checked);
                });
            } else {
                cmds.push_back(it.onClick);
            }
            UINT flags = MF_STRING;
            if (it.kind == Menu::Item::Kind::Check && it.checked) flags |= MF_CHECKED;
            ::AppendMenuW(h, flags, id, it.text.c_str());
            break;
        }
        }
    }
}

void TrackMenu(const Menu& menu, HWND hwnd, const POINT& pt) {
    HMENU h = ::CreatePopupMenu();
    std::vector<Command> cmds;
    AppendItems(h, menu, cmds);
    const int cmd = static_cast<int>(::TrackPopupMenuEx(
        h, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, hwnd, nullptr));
    ::DestroyMenu(h);
    if (cmd > 0 && static_cast<size_t>(cmd) <= cmds.size()) {
        Command fn = std::move(cmds[static_cast<size_t>(cmd) - 1]);
        if (fn) fn();
    }
}

}  // namespace

void ShowPopupMenu(const Menu& menu, void* owner, Vec2 posScreenPx) {
    if (owner == nullptr) return;
    POINT pt{static_cast<LONG>(posScreenPx.x + 0.5f),
             static_cast<LONG>(posScreenPx.y + 0.5f)};
    TrackMenu(menu, static_cast<HWND>(owner), pt);
}

}  // namespace dui
