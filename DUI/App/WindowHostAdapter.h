// ============================================================================
//  DUI/App/WindowHostAdapter.h — 平台事件 → dui::Window 事件的适配器（内部头）
//
//  Platform 层不依赖 App/UI（01-Layering §1），因此由本层做桥接。
//  Window / App 的 friend（前置声明见 App.h / Window.h），可访问它们的私有成员。
// ============================================================================

#pragma once

#include "App/App.h"
#include "App/NativeWindowHost.h"

namespace dui {

class WindowHostAdapter final : public NativeWindowHost {
public:
    WindowHostAdapter(App& app, Window& window, const NativeWindow& native,
                      graphics::Device& device);

    // 窗口销毁 → 交给 App 决定"是否最后一个"（最后一个才 PostQuitMessage）
    bool OnWindowDestroyed() override;

    void SetFramePending(bool* flag) noexcept;
    // Device::Init 完成后才允许就地 RenderFrame：native->Create 里的 SetWindowPos
    // 会同步派发 WM_SIZE，那时 device 尚未 Init（dev_ 为空），渲染会崩在上传纹理。
    void SetDeviceReady() noexcept;

    void OnNativeCreated(HWND hwnd, float initialScale) override;
    bool OnCloseRequest() override;
    // DPI 变化的**前置**：先把缩放系数换过来（尺寸调整还没发生，不重排）——
    // 窗口层在 SetWindowPos 之前调用，否则它同步发出的 WM_SIZE 会用旧缩放
    // 算出错误的客户区 DIP 尺寸，布局盒缩小一圈（12-Windowing §7.2）。
    void OnDpiScaleChanging(float newScale) override;
    void OnDpiChanged(float newScale, const Rect& suggestedDip,
                      bool clientDipChanged) override;

    void OnPointerMove(Vec2 pt) override;
    void OnPointerButton(Vec2 pt, PointerButton button, bool down) override;
    void OnMouseWheel(Vec2 pt, float delta, bool horizontal) override;
    bool OnDragEnterFiles(const std::vector<String>& paths, Vec2 ptDip) override;
    bool OnDragOverFiles(const std::vector<String>& paths, Vec2 ptDip) override;
    void OnDragLeaveFiles() override;
    void OnDropFiles(const std::vector<String>& paths, Vec2 ptDip) override;
    void OnPointerLeave() override;

    void OnKeyDown(int key, bool ctrl, bool shift, bool alt) override;
    void OnKeyUp(int key, bool ctrl, bool shift, bool alt) override;
    void OnChar(wchar_t ch) override;
    // 非客户区命中（WM_NCHITTEST）：框架默认值 → 交给业务覆盖。
    bool OnNcHitTest(Vec2 pt, int& hit) override;
    // 指针形态：命中链就近者胜（CSS cursor 继承同效）
    bool OnSetCursor(Vec2 pt) override;
    void OnTimer(uintptr_t id) override;
    void OnResized() override;
    void OnActivated(bool active) override;

    // 取"应清焦点"标志（消费一次）：最小化不算失活，保留焦点
    bool TakeDeactivate();

private:
    void RequestFrame() noexcept;
    // 框架默认的非客户区命中（仅无边框窗口有效）
    HitTestCode DefaultNcHitTest(Vec2 pt) const;
    float ResizeBorderDip() const;

    float pendingOldScale_ = 0.f;   // DPI 变化前的缩放（OnDpiScaleChanging 留档）

    App& app_;
    Window&  window_;
    const NativeWindow& native_;
    graphics::Device& device_;
    bool*    framePending_ = nullptr;
    bool     deviceReady_ = false;   // Device::Init 完成前禁止就地渲染（建窗期 WM_SIZE）
    Element* hover_ = nullptr;
    Element* pressed_ = nullptr;
    Vec2     pressPt_{};   // 按下位置（DIP），Click 拖出阈值判定用
    Vec2     lastPt_{};    // 最近一次指针位置（DIP），移出事件用
    Scroller* dragging_ = nullptr;      // 正在拖拽的滚动条（滑块）
    bool      dragVertical_ = false;
    bool      pendingDeactivate_ = false;   // 窗口失活待处理（延迟一轮判定最小化）
};

}  // namespace dui
