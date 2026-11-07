// ============================================================================
//  DUI/App/App.cpp — Application / Window 运行时
//
//  M2：Platform.Window（HWND 封装）+ 消息循环 + OnCreate/Show 时序（App.h 契约、
//  12-Windowing §6）。交换链在 M3，OnDpiChanged 完整流程在 M5。
// ============================================================================

#include "App/App.h"

#include "Graphics/Graphics.h"
#include "Platform/Window.h"
#include "UI/Scroller.h"

#include <ole2.h>

#include <cstdio>

namespace dui {

namespace {

// 光标形态 → Windows 预定义光标资源（映射放在 App 层：Platform 不依赖 UI 层）
LPCWSTR ToWinCursor(dui::Cursor c) {
    switch (c) {
    case dui::Cursor::IBeam:      return IDC_IBEAM;
    case dui::Cursor::Hand:       return IDC_HAND;
    case dui::Cursor::Wait:       return IDC_WAIT;
    case dui::Cursor::Crosshair:  return IDC_CROSS;
    case dui::Cursor::Move:       return IDC_SIZEALL;
    case dui::Cursor::SizeAll:    return IDC_SIZEALL;
    case dui::Cursor::NotAllowed: return IDC_NO;
    case dui::Cursor::Help:       return IDC_HELP;
    default:                      return IDC_ARROW;
    }
}

}  // namespace

// 平台事件 → dui::Window 事件的适配器（Window 的 friend，App.h 声明）。
// Platform 层不依赖 App/UI（01-Layering §1），因此由本层做桥接。
// 注意：必须与 App.h 的 friend 声明同为 dui::WindowHostAdapter（不能进匿名命名空间）。
class WindowHostAdapter final : public platform::NativeWindowHost {
public:
    WindowHostAdapter(Window& window, const platform::NativeWindow& native,
                      graphics::Device& device)
        : window_(window), native_(native), device_(device) {}

    void SetFramePending(bool* flag) noexcept { framePending_ = flag; }

    void OnNativeCreated(HWND, float initialScale) override {
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
        if (dragging_ != nullptr) {          // 滑块拖拽中：只更新滑块位置
            dragging_->UpdateBarDrag(pt);
            RequestFrame();
            return;
        }
        if (Element* cap = CapturedElement()) {
            // 捕获期间：不命中测试、不更新悬停链，Move 直达捕获者（仍从其开始冒泡）
            if (hover_ != nullptr) {
                UpdateHoverChain(hover_, nullptr);
                DispatchHoverTransitions(hover_, nullptr, pt);
                hover_ = nullptr;
            }
            lastPt_ = pt;
            DispatchPointerMove(cap, pt);
            RequestFrame();
            return;
        }
        Element* hit = HitTest(window_.Root(), pt);
        UpdateHoverChain(hover_, hit);
        DispatchHoverTransitions(hover_, hit, pt);   // 移入/移出（悬停链差分）
        hover_ = hit;
        lastPt_ = pt;
        DispatchPointerMove(hit, pt);                 // 移动（冒泡）
        window_.SyncImeCaret();                       // 拖选移动光标 → 同步 IME 插入点
        RequestFrame();
    }

    void OnPointerButton(Vec2 pt, bool down) override {
        if (down) {
            // 命中滚动条 → 进入滑块拖拽（不参与 Click 合成，也不派发元素事件）
            Element* hit = HitTest(window_.Root(), pt);
            Scroller* sc = FindScroller(hit);
            bool vertical = false;
            if (sc != nullptr && sc->HitScrollBar(pt, vertical)) {
                sc->BeginBarDrag(vertical, pt);
                dragging_ = sc;
                dragVertical_ = vertical;
                RequestFrame();
                return;
            }
            pressPt_ = pt;
            pressed_ = hit;
            SetPressedChain(hit, true);
            // 点击取得焦点（命中可聚焦元素或其可聚焦祖先；06-Events §6）
            if (Element* f = FindFocusable(hit)) SetFocus(f);
            DispatchPointerDown(hit, pt);              // 按下（冒泡）
        } else {
            if (dragging_ != nullptr) {
                dragging_->EndBarDrag();
                dragging_ = nullptr;
                RequestFrame();
                return;
            }
            Element* cap = CapturedElement();
            Element* hit = cap != nullptr ? cap : HitTest(window_.Root(), pt);
            SetPressedChain(pressed_, false);
            DispatchPointerUp(hit, pt);                // 弹起（冒泡）
            if (cap != nullptr) {
                SetCaptureElement(nullptr);            // 抬起自动释放捕获（拖拽不合成 Click）
                Element* cur = HitTest(window_.Root(), pt);
                UpdateHoverChain(hover_, cur);         // 恢复悬停链到当前指针位置
                DispatchHoverTransitions(hover_, cur, pt);
                hover_ = cur;
            } else {
                // Click 合成：按下抬起同元素且拖出在阈值内（06-Events §3：4 dip）
                constexpr float kDragThreshold = 4.f;
                if (pressed_ != nullptr && hit == pressed_ &&
                    pt.x >= pressPt_.x - kDragThreshold && pt.x <= pressPt_.x + kDragThreshold &&
                    pt.y >= pressPt_.y - kDragThreshold && pt.y <= pressPt_.y + kDragThreshold) {
                    DispatchClick(pressed_);
                }
            }
            pressed_ = nullptr;
        }
        window_.SyncImeCaret();   // 点击定位光标 → 同步 IME 插入点
        RequestFrame();
    }

    // 滚轮：先派发元素级 Wheel（冒泡，handled 可拦截）；无人处理时由滚动容器消费
    // （沿祖先链找最近的"该轴还可滚"的滚动容器：内层滚不动 → 冒泡外层；
    //   纵向滚轮，Shift + 滚轮横向）
    void OnMouseWheel(Vec2 pt, float delta, bool horizontal) override {
        Element* hit = HitTest(window_.Root(), pt);
        if (DispatchWheel(hit, pt, delta, horizontal)) {   // 已被元素处理
            RequestFrame();
            return;
        }
        Scroller* sc = nullptr;
        for (Element* p = hit; p != nullptr; p = p->Parent()) {
            if (p->GetKind() != Kind::Scroller) continue;
            Scroller* s = static_cast<Scroller*>(p);
            const float max = horizontal ? s->MaxScrollX() : s->MaxScrollY();
            if (max > 0.f) { sc = s; break; }
        }
        if (sc == nullptr) return;
        constexpr float kWheelStep = 48.f;   // 每格滚动量（DIP）
        if (horizontal) sc->ScrollBy(-delta * kWheelStep, 0.f);
        else            sc->ScrollBy(0.f, -delta * kWheelStep);
        RequestFrame();
    }

    void OnPointerLeave() override {
        UpdateHoverChain(hover_, nullptr);
        DispatchHoverTransitions(hover_, nullptr, lastPt_);   // 全部移出
        hover_ = nullptr;
        RequestFrame();
    }

    // ---- 键盘输入（M5.9）：先到焦点元素，再沿祖先链冒泡 ----

    void OnKeyDown(int key, bool ctrl, bool shift, bool alt) override {
        Element* target = FocusedElement();
        bool handled = false;
        if (target != nullptr) handled = DispatchKeyDown(target, key, ctrl, shift, alt);
        // 默认行为：Tab / Shift+Tab 切换焦点（控件未消费时）
        if (!handled && key == VK_TAB) {
            FocusNext(window_.Root(), shift);
            RequestFrame();
        }
        window_.SyncImeCaret();   // 光标可能已移动 → 同步 IME 插入点
    }

    void OnKeyUp(int key, bool ctrl, bool shift, bool alt) override {
        Element* target = FocusedElement();
        if (target != nullptr) DispatchKeyUp(target, key, ctrl, shift, alt);
    }

    void OnChar(wchar_t ch) override {
        Element* target = FocusedElement();
        if (target != nullptr) DispatchChar(target, ch);
        window_.SyncImeCaret();   // 输入后光标右移 → 同步 IME 插入点
    }

    // 指针形态：命中链就近者胜 —— 元素指定了 cursor 就用它；没指定则沿链向上取
    // 父级的（与 CSS 的 cursor 继承同效）；都没指定再看是否为编辑控件（I-beam）。
    // 非客户区命中：转发给业务（Chrome::Borderless 的标题栏 / 边框热区）
    bool OnNcHitTest(Vec2 pt, int& hit) override {
        HitTestCode code = HitTestCode::Client;
        if (!window_.OnNcHitTest(pt, code)) return false;
        hit = static_cast<int>(code);
        return true;
    }

    // 指针形态：命中链就近者胜 —— 元素指定了 cursor 就用它；没指定则沿链向上取
    // 父级的（与 CSS 的 cursor 继承同效）；都没指定再看是否为编辑控件（I-beam）。
    bool OnSetCursor(Vec2 pt) override {
        Element* hit = HitTest(window_.Root(), pt);
        for (Element* e = hit; e != nullptr; e = e->Parent()) {
            StyleValue v;
            if (TryResolveElement(e, Prop::Cursor, v)) {
                native_.SetCursorShape(ToWinCursor(static_cast<dui::Cursor>(v.u)));
                return true;
            }
            if (e->WantsTextCursor()) {
                native_.SetCursorShape(IDC_IBEAM);
                return true;
            }
        }
        return false;   // 默认箭头
    }

    // 定时器节拍（统一 ticker）：转发到窗口，由 AnimationClock 分发
    void OnTimer(uintptr_t id) override {
        window_.OnTimer(id);
    }

    void OnResized() override {
        window_.Root()->InvalidateLayout();   // 客户区尺寸变化 → 重排
        // 拖拽边框期间 Windows 在模态循环里连续派发 WM_SIZE，主消息循环被挂起，
        // framePending 要等拖拽结束才被消费 —— 帧必须就地渲染，内容才能实时变化
        Application::RenderFrame(window_, native_, device_);
        window_.OnStateChanged();            // 尺寸 / 最大化状态变化（业务自绘按钮用）
    }

    // 窗口激活状态变化：失活 → 焦点离开窗口（编辑控件失焦，光标停止闪烁）。
    // 延迟一轮判定：最小化也会发 WM_ACTIVATE(WA_INACTIVE)，但最小化应保留焦点
    // （还原后可直接继续输入）—— 下一轮 WM_SIZE 已到达，IsIconic 才是准的。
    void OnActivated(bool active) override {
        if (active) {
            pendingDeactivate_ = false;
            return;
        }
        // 系统对话框弹出期间宿主失活不是「切到别的应用」：不清编辑控件焦点
        // （17-Dialogs §6.2；dialogDepth_ 由 App/Dialog.cpp 在调用前后维护）。
        if (window_.dialogDepth_ > 0) return;
        pendingDeactivate_ = true;
    }

    // 取"应清焦点"标志（消费一次）：最小化不算失活，保留焦点
    bool TakeDeactivate() {
        if (!pendingDeactivate_) return false;
        pendingDeactivate_ = false;
        return !native_.IsMinimized();
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
    Vec2     lastPt_{};    // 最近一次指针位置（DIP），移出事件用
    Scroller* dragging_ = nullptr;      // 正在拖拽的滚动条（滑块）
    bool      dragVertical_ = false;
    bool      pendingDeactivate_ = false;   // 窗口失活待处理（延迟一轮判定最小化）
};

Window::Window(const WindowOptions& options) : options_(options) {
    root_ = std::make_unique<Panel>();
}

Window::~Window() = default;

void* Window::NativeHandle() const noexcept {
    if (native_ == nullptr) return nullptr;
    return static_cast<void*>(static_cast<platform::NativeWindow*>(native_)->Handle());
}

// 窗口操作（无边框窗口的自绘标题栏按钮）
void Window::Minimize() {
    auto* native = static_cast<platform::NativeWindow*>(native_);
    if (native != nullptr) native->Minimize();
}

void Window::MaximizeOrRestore() {
    auto* native = static_cast<platform::NativeWindow*>(native_);
    if (native != nullptr) native->MaximizeOrRestore();
}

void Window::Close() {
    auto* native = static_cast<platform::NativeWindow*>(native_);
    if (native != nullptr) native->RequestClose();
}

bool Window::IsMaximized() const {
    auto* native = static_cast<platform::NativeWindow*>(native_);
    return native != nullptr && native->IsMaximized();
}

Vec2 Window::ClientSizeDip() const {
    auto* native = static_cast<platform::NativeWindow*>(native_);
    if (native == nullptr) return {};
    return native->ClientSizeDip();
}

// ---------------------------------------------------------------------------
// UI 统一节拍（光标闪烁 + GIF 帧动画 + 命令式属性动画）：
//   App 层只保留一个窗口定时器；是否需要节拍、周期多少由 AnimationClock 决定，
//   tick 经它分发给帧动画、光标闪烁与属性补间。空闲 / 最小化时停表。
// ---------------------------------------------------------------------------

namespace {
constexpr uintptr_t kTickTimerId = 1;
}

void Window::SyncTickTimer() {
    auto* native = static_cast<platform::NativeWindow*>(native_);
    if (native == nullptr) return;
    auto& clock = AnimationClock::Instance();
    const bool need = clock.NeedsTick() && !native->IsMinimized();
    if (need) {
        const unsigned ms = clock.TickPeriodMs();
        if (tickTimer_ == 0 || tickPeriodMs_ != ms) {
            native->StartTimer(kTickTimerId, ms);   // 同 id 重设即换周期
            tickTimer_ = kTickTimerId;
            tickPeriodMs_ = ms;
        }
    } else if (tickTimer_ != 0) {
        native->StopTimer(tickTimer_);
        tickTimer_ = 0;
        tickPeriodMs_ = 0;
        clock.Reset();        // 下次启动重新计闪烁相位
    }
}

void Window::OnTimer(uintptr_t id) {
    if (id != tickTimer_) return;
    AnimationClock::Instance().Tick(::GetTickCount64());
    SyncTickTimer();          // 闪烁结束 / 动画移除 → 及时停表；周期变化 → 及时换挡
}

// IME 插入点：UI 层给插入点矩形（光标所在行：行顶→基线）→ 平台层把隐形系统
// caret 移过去。IME 依 GetCaretPos() 定位候选窗口；系统 caret 不可见，故与自绘
// 光标不冲突。caret 高度必须覆盖到基线，否则输入法会把候选窗下移一整行。
// 注意：这里不碰任何 IMM API —— 在 IME 消息处理期间调用 ImmGetContext 会重建
// 输入上下文、打断正在进行的组合（中文打不出来）。
void Window::SyncImeCaret() {
    auto* native = static_cast<platform::NativeWindow*>(native_);
    if (native == nullptr) return;
    Rect dip{};
    if (!QueryInsertionPoint(dip)) {
        native->RemoveImeCaret();
        return;
    }
    const int px = static_cast<int>(dip.x * scale_ + 0.5f);
    const int py = static_cast<int>(dip.y * scale_ + 0.5f);
    const int ph = static_cast<int>(dip.h * scale_ + 0.5f);
    native->UpdateImeCaretPos(px, py, ph);
}

Application::Application() = default;
Application::~Application() = default;

int Application::Run(Window& window) {
    lastError_.clear();

    // UI 线程的 COM 单元必须是 STA（17-Dialogs §9）：Common Item Dialog 与
    // Shell 扩展是 STA 组件，MTA 下 IFileDialog::Show 会在创建窗口前死锁。
    // OleInitialize 同时建立 STA 与 OLE（Shell 拖放/扩展需要）；必须早于 WIC
    // 的 CoInitializeEx(MTA)（后者在已 STA 的线程上只返回 RPC_E_CHANGED_MODE，
    // COM 仍可用）。进程级持有，不配对 Uninitialize（与 ImageCodec.cpp 一致）。
    const HRESULT uiCom = OleInitialize(nullptr);
    if (uiCom == RPC_E_CHANGED_MODE) {
        std::printf("[DUI] COM already initialized as MTA on UI thread; "
                    "system file dialogs will report an error\n");
    }

    graphics::Device device;
    platform::NativeWindow native;
    WindowHostAdapter host(window, native, device);

    // 建 HWND（隐藏、按显示器 DPI 定尺寸）
    if (!native.Create(window.options_.title, window.options_.width,
                       window.options_.height, host, lastError_,
                       window.options_.chrome == Chrome::Borderless)) {
        return 1;   // lastError_ 已填（App.h 契约：失败返回非 0）
    }
    window.native_ = &native;   // 平台窗口对象（定时器等平台能力）

    // 初始居中：仍在 OnCreate / Show 之前，构建过程不可见（App.h 契约）
    if (window.options_.center) native.CenterOnScreen();

    if (!device.Init(native.Handle(), lastError_)) {
        std::printf("[DUI] device init failed: %ls\n", lastError_.c_str());
        return 1;
    }

    // UI Dispatcher（07-Threading）：后台完成回投 → PostMessage 唤醒 → [调度] 排空
    constexpr unsigned kWakeMessage = WM_APP;   // 窗口过程仅借它唤醒泵，无副作用
    dispatcher_.SetWakeTarget(native.Handle(), kWakeMessage);
    Dispatcher::SetUi(&dispatcher_);

    // 剪贴板：平台实现注入 UI 层（UI 不依赖 Platform，01-Layering §1）
    SetClipboardService(&platform::GetClipboardText, &platform::SetClipboardText);

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
        // 窗口失活（切到别的应用）→ 清焦点：编辑控件随之失焦 —— 选区消失、
        // 光标停止闪烁。最小化不在此列（保留焦点，还原后可直接继续输入）。
        if (host.TakeDeactivate()) {
            SetFocus(nullptr);
            SetCaptureElement(nullptr);   // 失活时可能收不到抬起消息 → 防捕获残留
        }
        window.SyncTickTimer();      // UI 统一节拍：按需启停 / 换周期（渲染前同步）
        // 最小化：客户区尺寸为 0 → RenderFrame 直接返回且**不清脏标记**，若此时
        // 仍按 dirty 进帧，主循环永远进不了 WaitMessage → 退化成忙转（CPU 100%）。
        // 光标闪烁节拍每 500ms 标一次脏，故只在文本框聚焦时暴露。
        // 最小化期间直接阻塞等消息；恢复时由 WM_SIZE → OnResized 触发重排 + 重绘。
        if (!native.IsMinimized() && (framePending || window.Root()->SubtreeDirty())) {
            framePending = false;
            RenderFrame(window, native, device);
        } else {
            WaitMessage();
        }
    }
}

}  // namespace dui
