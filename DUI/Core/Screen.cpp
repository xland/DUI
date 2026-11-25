// ============================================================================
//  DUI/Core/Screen.cpp — 显示器信息查询实现
// ============================================================================

#include "Core/Screen.h"

#include "App/Window.h"   // FromWindow(const Window&) → NativeHandle()

#include <shellscalingapi.h>   // GetDpiForMonitor（Win8.1+）

#pragma comment(lib, "shcore.lib")

namespace dui {
namespace {

Rect FromWinRect(const RECT& r) noexcept {
    return Rect{static_cast<float>(r.left), static_cast<float>(r.top),
                static_cast<float>(r.right - r.left),
                static_cast<float>(r.bottom - r.top)};
}

float DpiOf(HMONITOR mon) noexcept {
    UINT dx = 96, dy = 96;
    if (mon != nullptr &&
        SUCCEEDED(::GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) && dx > 0) {
        return static_cast<float>(dx);
    }
    return 96.f;
}

ScreenInfo MakeInfo(HMONITOR mon) {
    ScreenInfo info;
    info.monitor = mon;
    info.dpi = DpiOf(mon);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon != nullptr && ::GetMonitorInfoW(mon, &mi)) {
        info.boundsPx = FromWinRect(mi.rcMonitor);
        info.workAreaPx = FromWinRect(mi.rcWork);
        info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    }
    const float k = 96.f / info.dpi;
    info.boundsDip = Rect{info.boundsPx.x * k, info.boundsPx.y * k,
                          info.boundsPx.w * k, info.boundsPx.h * k};
    info.workAreaDip = Rect{info.workAreaPx.x * k, info.workAreaPx.y * k,
                            info.workAreaPx.w * k, info.workAreaPx.h * k};
    return info;
}

BOOL CALLBACK EnumCb(HMONITOR mon, HDC, LPRECT, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<ScreenInfo>*>(lparam);
    out->push_back(MakeInfo(mon));
    return TRUE;   // 继续枚举
}

}  // namespace

std::vector<ScreenInfo> Screen::All() {
    std::vector<ScreenInfo> out;
    ::EnumDisplayMonitors(nullptr, nullptr, &EnumCb, reinterpret_cast<LPARAM>(&out));
    return out;
}

ScreenInfo Screen::Primary() {
    return MakeInfo(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
}

ScreenInfo Screen::FromPoint(float xPx, float yPx) {
    const POINT pt{static_cast<LONG>(xPx), static_cast<LONG>(yPx)};
    return MakeInfo(::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST));
}

ScreenInfo Screen::FromWindow(const Window& window) {
    return FromWindow(window.NativeHandle());
}

ScreenInfo Screen::FromWindow(HWND hwnd) {
    return MakeInfo(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
}

Rect Screen::VirtualBounds() {
    struct Ctx { RECT rect; bool first; } ctx{{}, true};
    ::EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (::GetMonitorInfoW(mon, &mi)) {
                if (c->first) { c->rect = mi.rcMonitor; c->first = false; }
                else ::UnionRect(&c->rect, &c->rect, &mi.rcMonitor);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    if (ctx.first) return {};
    return FromWinRect(ctx.rect);
}

}  // namespace dui
