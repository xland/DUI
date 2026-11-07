// ============================================================================
//  Dialog/MacroCompat.cpp — 编译期守卫：MessageBox 与 Win32 同名宏
//
//  与 main.cpp 相反的包含顺序：DUI.h 在前、windows.h 在后（宏重新生效）。
//  此时 `w.MessageBox(...)` 会被预处理成 `w.MessageBoxW(...)`，由
//  Window::MessageBoxW 兼容别名接住（17-Dialogs §4.4）。
//  本文件不参与运行，只为让两种 TU 顺序都在 Debug/Release 下链接通过。
// ============================================================================

#include "DUI/DUI.h"
#include <windows.h>

dui::MessageResult DialogMacroCompatCheck(dui::Window& window) {
    return window.MessageBox(dui::MessageOptions{
        .text = L"",
        .title = L"宏兼容检查",
        .icon = dui::MessageIcon::None,
        .buttons = dui::MessageButtons::Ok,
    });
}
