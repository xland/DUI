// ============================================================================
//  DUI/Core/Screen.h — 显示器信息查询（轻量工具，无状态）
//
//  把 Win32 的 EnumDisplayMonitors / MonitorFromWindow / GetMonitorInfo /
//  GetDpiForMonitor 收成一处，业务不必 include windows.h 即可做多屏定位、
//  工作区避让与 DPI 换算。
//
//  坐标单位（重要）：
//    · Px  字段是**物理像素**、虚拟桌面全局坐标（可能为负，如主屏左侧扩展屏）。
//    · Dip 字段是把该屏的物理坐标按**该屏自身 DPI** 换算的结果。混合 DPI 的
//      多屏系统里各屏 DIP 坐标系互不连续，**不可跨屏相加 / 拼接**；
//      需要跨屏几何时一律用 Px 字段（见 VirtualBounds）。
// ============================================================================
#pragma once

#include <windows.h>

#include <vector>

#include "Core/Core.h"

namespace dui {

class Window;   // App/Window.h：FromWindow 重载用，实现文件里取 HWND

// 单个显示器的几何与 DPI（快照；屏幕热插拔 / DPI 变化后重新查询即可）
struct ScreenInfo {
    HMONITOR monitor = nullptr;   // 原生句柄，仅供高级用途；框架内部不持有
    Rect     boundsPx;            // 整个显示器（含任务栏），物理像素全局坐标
    Rect     workAreaPx;          // 工作区（排除任务栏），物理像素全局坐标
    Rect     boundsDip;           // boundsPx 按本屏 DPI 换算
    Rect     workAreaDip;         // workAreaPx 按本屏 DPI 换算
    float    dpi = 96.f;          // 本屏有效 DPI（MDT_EFFECTIVE_DPI）
    bool     primary = false;     // 是否主显示器
};

class Screen {
public:
    // 所有显示器（EnumDisplayMonitors 顺序，主屏通常在前但不保证）。
    static std::vector<ScreenInfo> All();

    // 主显示器；系统无显示器时返回 dpi=96、各矩形为 0 的默认 ScreenInfo。
    static ScreenInfo Primary();

    // 物理像素点所在显示器；点不在任何屏上（负坐标的空域）时返回最近屏。
    static ScreenInfo FromPoint(float xPx, float yPx);

    // 窗口所在显示器（MONITOR_DEFAULTTONEAREST）。窗口尚未创建时返回最近屏。
    static ScreenInfo FromWindow(const Window& window);
    static ScreenInfo FromWindow(HWND hwnd);

    // 所有显示器的并集，物理像素全局坐标（混合 DPI 下唯一可跨屏相加的口径）。
    static Rect VirtualBounds();

    // 工作区便捷入口：返回 s.workAreaDip（需要物理像素直接用 s.workAreaPx）。
    static Rect WorkArea(const ScreenInfo& s) noexcept { return s.workAreaDip; }

    // 按指定屏的 DPI 换算；传入的 dip/px 是该屏局部量，不涉及跨屏坐标拼接。
    static float DipToPx(const ScreenInfo& s, float dip) noexcept { return dip * s.dpi / 96.f; }
    static float PxToDip(const ScreenInfo& s, float px) noexcept { return px * 96.f / s.dpi; }
};

}  // namespace dui
