// ============================================================================
//  DUI/App/App.cpp — Application / Window 运行时
//
//  M2：Platform.Window（HWND 封装）+ 消息循环 + OnCreate/Show 时序（App.h 契约、
//  12-Windowing §6）。交换链在 M3，OnDpiChanged 完整流程在 M5。
// ============================================================================

#include "App/App.h"

#include "Graphics/Graphics.h"
#include "Platform/Window.h"

#include <cstdio>

namespace dui {

// 平台事件 → dui::Window 事件的适配器（Window 的 friend，App.h 声明）。
// Platform 层不依赖 App/UI（01-Layering §1），因此由本层做桥接。
// 注意：必须与 App.h 的 friend 声明同为 dui::WindowHostAdapter（不能进匿名命名空间）。
class WindowHostAdapter final : public platform::NativeWindowHost {
public:
    WindowHostAdapter(Window& window, const platform::NativeWindow& native,
                      graphics::Device& device)
        : window_(window), native_(native), device_(device) {}

    void SetFramePending(bool* flag) noexcept { framePending_ = flag; }

    void OnNativeCreated(HWND hwnd, float initialScale) override {
        window_.native_ = hwnd;
        window_.scale_ = initialScale;
        std::printf("[DUI] window created, scale = %.2f\n", initialScale);
    }

    bool OnCloseRequest() override {
        return window_.OnClosing();
    }

    void OnDpiChanged(float newScale, const Rect& suggestedDip, bool clientDipChanged) override {
        const float oldScale = window_.scale_;
        window_.scale_ = newScale;
        // §4-5 分级失效：DIP 尺寸不变 → 只重绘不重排；字形按新 pixelSize 重光栅
        // （图集按 (字体, 字形, 像素尺寸) 缓存，新档位按需生成，旧档位 LRU 淘汰）
        std::printf("[DUI] DPI changed: %.2f -> %.2f (%s)\n", oldScale, newScale,
                    clientDipChanged ? "layout invalidated" : "repaint only");
        if (clientDipChanged) window_.Root()->InvalidateLayout();
        else window_.Root()->InvalidatePaint();
        window_.OnDpiChanged(DpiChanged{oldScale, newScale, suggestedDip});
        // §4-7：重算鼠标当前位置（物理 → DIP），避免 hover 状态错位
        const HWND hwnd = static_cast<HWND>(window_.NativeHandle());
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        Element* hit = HitTest(window_.Root(), Vec2{pt.x / newScale, pt.y / newScale});
        UpdateHoverChain(hover_, hit);
        hover_ = hit;
        RequestFrame();
    }

    // ---- 指针输入（M3 最小形态；归一化事件与完整路由在 M4）----

    void OnPointerMove(Vec2 pt) override {
        Element* hit = HitTest(window_.Root(), pt);
        UpdateHoverChain(hover_, hit);
        hover_ = hit;
        RequestFrame();
    }

    void OnPointerButton(Vec2 pt, bool down) override {
        Element* hit = HitTest(window_.Root(), pt);
        if (down) {
            pressPt_ = pt;
            pressed_ = hit;
            SetPressedChain(hit, true);
        } else {
            SetPressedChain(pressed_, false);
            // Click 合成：按下抬起同元素且拖出在阈值内（06-Events §3：4 dip）
            constexpr float kDragThreshold = 4.f;
            if (pressed_ != nullptr && hit == pressed_ &&
                pt.x >= pressPt_.x - kDragThreshold && pt.x <= pressPt_.x + kDragThreshold &&
                pt.y >= pressPt_.y - kDragThreshold && pt.y <= pressPt_.y + kDragThreshold) {
                DispatchClick(pressed_);
            }
            pressed_ = nullptr;
        }
        RequestFrame();
    }

    void OnPointerLeave() override {
        UpdateHoverChain(hover_, nullptr);
        hover_ = nullptr;
        RequestFrame();
    }

    void OnResized() override {
        window_.Root()->InvalidateLayout();   // 客户区尺寸变化 → 重排
        // 拖拽边框期间 Windows 在模态循环里连续派发 WM_SIZE，主消息循环被挂起，
        // framePending 要等拖拽结束才被消费 —— 帧必须就地渲染，内容才能实时变化
        Application::RenderFrame(window_, native_, device_);
    }

private:
    void RequestFrame() noexcept {
        if (framePending_ != nullptr) *framePending_ = true;
    }

    Window&  window_;
    const platform::NativeWindow& native_;
    graphics::Device& device_;
    bool*    framePending_ = nullptr;
    Element* hover_ = nullptr;
    Element* pressed_ = nullptr;
    Vec2     pressPt_{};   // 按下位置（DIP），Click 拖出阈值判定用
};

Window::Window(const WindowOptions& options) : options_(options) {
    root_ = std::make_unique<Panel>();
}

Window::~Window() = default;

Application::Application() = default;
Application::~Application() = default;

int Application::Run(Window& window) {
    lastError_.clear();

    graphics::Device device;
    platform::NativeWindow native;
    WindowHostAdapter host(window, native, device);

    // 建 HWND（隐藏、按显示器 DPI 定尺寸）
    if (!native.Create(window.options_.title, window.options_.width,
                       window.options_.height, host, lastError_)) {
        return 1;   // lastError_ 已填（App.h 契约：失败返回非 0）
    }

    if (!device.Init(native.Handle(), lastError_)) {
        std::printf("[DUI] device init failed: %ls\n", lastError_.c_str());
        return 1;
    }

    // UI Dispatcher（07-Threading）：后台完成回投 → PostMessage 唤醒 → [调度] 排空
    constexpr unsigned kWakeMessage = WM_APP;   // 窗口过程仅借它唤醒泵，无副作用
    dispatcher_.SetWakeTarget(native.Handle(), kWakeMessage);
    Dispatcher::SetUi(&dispatcher_);

    // 原生窗口已建、尚未显示：业务在这里搭界面，构建过程不可见
    window.OnCreate();
    native.Show();

    bool framePending = true;   // 首帧
    host.SetFramePending(&framePending);

    // 按需渲染循环：[调度] 排空 → 有失效才进帧 → 空闲 WaitMessage（10-Rendering R1）
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                Dispatcher::SetUi(nullptr);
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        dispatcher_.Drain();
        if (framePending || window.Root()->SubtreeDirty()) {
            framePending = false;
            RenderFrame(window, native, device);
        } else {
            WaitMessage();
        }
    }
}

}  // namespace dui
