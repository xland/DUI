// ============================================================================
//  DUI/App/NativeWindowHost.h — 平台窗口 → 上层的事件接口（内部头，不进 DUI.h）
//
//  NativeWindow 只依赖本接口，不认识 App / UI 层；唯一实现者是 App.cpp 里的
//  WindowHostAdapter（dui::Window / App 的 friend）。
//  默认实现在 NativeWindowHost.cpp，头文件只保留声明。
// ============================================================================

#pragma once

#include <windows.h>      // HWND

#include <cstdint>
#include <vector>

#include "Core/Core.h"      // Vec2 / Rect / String / PointerButton
#include "App/DropTarget.h" // IDropTargetHost

namespace dui {

// 平台窗口 → 上层的事件接口（App 层实现；protected 虚函数由宿主回调）
class NativeWindowHost : public IDropTargetHost {
public:
    virtual ~NativeWindowHost();

    // HWND 已创建（隐藏）、尺寸已按显示器 DPI 定好 —— 此时上层可搭界面
    virtual void OnNativeCreated(HWND hwnd, float initialScale) = 0;
    // 返回 false 阻止关闭
    virtual bool OnCloseRequest() = 0;
    // 窗口已销毁（WM_DESTROY）：返回 true → 平台层 PostQuitMessage 退出应用。
    // 单窗口宿主返回 true（原行为）；多窗口宿主在还有窗口存活时返回 false。
    virtual bool OnWindowDestroyed();
    // 12-Windowing §4：newScale = HIWORD(wParam)/96；suggestedDip 为系统建议
    // 矩形（整窗物理坐标）按 newScale 换算的 DIP 值；clientDipChanged =
    // 客户区 DIP 尺寸是否变化（§4-5 分级失效：不变只重绘，不重排）
    virtual void OnDpiChanged(float newScale, const Rect& suggestedDip, bool clientDipChanged) = 0;
    // DPI 变化的**前置**通知：在窗口尺寸调整（SetWindowPos）**之前**发出，
    // 让上层先把缩放系数换过来 —— SetWindowPos 会同步发出 WM_SIZE，那时若
    // 缩放还是旧值，WM_SIZE 会用旧缩放算出错误的客户区 DIP 尺寸，布局盒
    // 缩小一圈（12-Windowing §7.2）。这里只换系数，不触发重排。
    virtual void OnDpiScaleChanging(float newScale);
    // 指针输入（M3：物理坐标已换算为窗口客户区 DIP；完整归一化在 M4）
    virtual void OnPointerMove(Vec2 ptDip) = 0;
    // button = 触发的按键；down = 按下 / 弹起（左键 / 右键 / 中键都转发按下 + 抬起）
    virtual void OnPointerButton(Vec2 ptDip, PointerButton button, bool down) = 0;
    virtual void OnPointerLeave() = 0;
    // 滚轮：ptDip = 指针位置（客户区 DIP）；delta = 滚轮格数（正 = 向上滚）
    // horizontal = true 时应横向滚动（Shift + 滚轮由平台层判定）
    virtual void OnMouseWheel(Vec2 ptDip, float delta, bool horizontal) = 0;
    // 键盘输入（M5.9）：key = VK_* 虚拟键码；ctrl/shift/alt = 修饰键状态
    virtual void OnKeyDown(int key, bool ctrl, bool shift, bool alt) = 0;
    virtual void OnKeyUp(int key, bool ctrl, bool shift, bool alt) = 0;
    // 字符输入（WM_CHAR / WM_IME_CHAR；IME 提交亦走此路）
    virtual void OnChar(wchar_t ch) = 0;
    // 指针形态（WM_SETCURSOR）：返回 true 表示上层已设置光标（不再走默认箭头）
    virtual bool OnSetCursor(Vec2 ptDip);
    // 非客户区命中（WM_NCHITTEST）：ptDip = 客户区 DIP；返回 true 表示上层已判定
    // 命中区（hit 取 HTCAPTION / HTLEFT 等），系统据此接管"拖标题栏移动 /
    // 拖边框缩放"。仅 Borderless 模式有意义。
    virtual bool OnNcHitTest(Vec2 ptDip, int& hit);
    // 拖放（M5.85，IDropTarget）：paths = 文件完整路径；ptDip = 悬停/释放点客户区 DIP。
    // 覆盖 IDropTargetHost 的纯虚，给默认实现以免其它 NativeWindowHost 实现被迫改动。
    // Enter/Over 返回 true 表示接受（光标变 COPY，否则 NONE）。
    bool OnDragEnterFiles(const std::vector<String>& paths, Vec2 ptDip) override;
    bool OnDragOverFiles(const std::vector<String>& paths, Vec2 ptDip) override;
    void OnDragLeaveFiles() override;
    void OnDropFiles(const std::vector<String>& paths, Vec2 ptDip) override;
    // 定时器节拍（WM_TIMER；id 由上层分配并自行管理）
    virtual void OnTimer(uintptr_t id);
    // 窗口获得/失去激活（WM_ACTIVATE）：上层据此让编辑控件失焦（光标停止闪烁）
    virtual void OnActivated(bool active);
    virtual void OnResized() = 0;
};

}  // namespace dui
