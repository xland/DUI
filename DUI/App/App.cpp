// ============================================================================
//  DUI/App/App.cpp — Application / Window 运行时
//
//  M2：Platform.Window（HWND 封装）+ 消息循环 + OnCreate/Show 时序（App.h 契约、
//  12-Windowing §6）。交换链在 M3，OnDpiChanged 完整流程在 M5。
// ============================================================================

#include "App/App.h"

#include "Graphics/Graphics.h"
#include "Graphics/Device.h"
#include "App/Window.h"
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
class WindowHostAdapter final : public NativeWindowHost {
public:
    WindowHostAdapter(Application& app, Window& window, const NativeWindow& native,
                      graphics::Device& device)
        : app_(app), window_(window), native_(native), device_(device) {}

    // 窗口销毁 → 交给 Application 决定"是否最后一个"（最后一个才 PostQuitMessage）
    bool OnWindowDestroyed() override { return app_.OnWindowDestroyed(this); }

    void SetFramePending(bool* flag) noexcept { framePending_ = flag; }

    // Device::Init 完成后才允许就地 RenderFrame：native->Create 里的 SetWindowPos
    // 会同步派发 WM_SIZE，那时 device 尚未 Init（dev_ 为空），渲染会崩在上传纹理。
    void SetDeviceReady() noexcept { deviceReady_ = true; }

    void OnNativeCreated(HWND, float initialScale) override {
        window_.scale_ = initialScale;
        std::printf("[DUI] window created, scale = %.2f\n", initialScale);
    }

    bool OnCloseRequest() override {
        return window_.OnClosing();
    }

    // DPI 变化的**前置**：先把缩放系数换过来（尺寸调整还没发生，不重排）——
    // 窗口层在 SetWindowPos 之前调用，否则它同步发出的 WM_SIZE 会用旧缩放
    // 算出错误的客户区 DIP 尺寸，布局盒缩小一圈（12-Windowing §7.2）。
    void OnDpiScaleChanging(float newScale) override {
        pendingOldScale_ = window_.scale_;   // 旧值留档（OnDpiChanged 要用）
        window_.scale_ = newScale;
    }

    void OnDpiChanged(float newScale, const Rect& suggestedDip, bool clientDipChanged) override {
        const float oldScale = pendingOldScale_ != 0.f ? pendingOldScale_ : window_.scale_;
        pendingOldScale_ = 0.f;
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
            DispatchPointerMove(cap, ClientToLocal(cap, pt));
            RequestFrame();
            return;
        }
        // 命中点换算到命中元素所在单元空间（滚动容器内含滚动偏移）——
        // 控件据此把 ev.pos 与自己的 Bounds 相减求局部坐标
        Vec2 local{};
        Element* hit = HitTest(window_.Root(), pt, &local);
        UpdateHoverChain(hover_, hit);
        DispatchHoverTransitions(hover_, hit, local);   // 移入/移出（悬停链差分）
        hover_ = hit;
        lastPt_ = pt;
        DispatchPointerMove(hit, local);                 // 移动（冒泡）
        window_.SyncImeCaret();                       // 拖选移动光标 → 同步 IME 插入点
        RequestFrame();
    }

    void OnPointerButton(Vec2 pt, PointerButton button, bool down) override {
        if (down) {
            if (button != PointerButton::Left) {
                // 非左键按下（目前只有右键）：**只派发元素级 PointerDown** ——
                // 不动 pressed 链 / 焦点 / 指针捕获 / IME 插入点：
                //   · Pressed 视觉态与焦点是左键语义（右键按下不该让按钮亮起、更不该抢焦点）
                //   · 覆盖 pressed_ 会让左键抬起时 Pressed 卡住、Click 合成失效
                //   · 窗口捕获服务于左键"拖出窗口外仍能收到抬起"，右键不做拖拽
                Vec2 local{};
                Element* hit = HitTest(window_.Root(), pt, &local);
                DispatchPointerDown(hit, local, button);
                RequestFrame();
                return;
            }
            // 命中滚动条 → 进入滑块拖拽（不参与 Click 合成，也不派发元素事件）
            Vec2 local{};
            Element* hit = HitTest(window_.Root(), pt, &local);
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
            // 点击取得焦点（命中可聚焦元素或其可聚焦祖先；06-Events §6）；
            // 没命中可聚焦元素（点窗口空白 / 点非可聚焦元素）→ **清焦点**（浏览器语义）：
            // 编辑控件随之失焦 → 选区消失、光标停闪。否则选区会留在一个"看起来仍聚焦着"
            // 的控件上（用户已不打算继续输入）；而看不见的选区会让后续 Delete/输入
            // 替换掉用户看不见的内容（与 TextBox 失焦清选区同一条理由）。
            if (Element* f = FindFocusable(hit)) SetFocus(f);
            else SetFocus(nullptr);
            DispatchPointerDown(hit, local, button);      // 按下（冒泡）
        } else if (button != PointerButton::Left) {
            // 非左键抬起：只派发元素级弹起，不动 pressed / 焦点 / Click 状态机
            Vec2 local{};
            Element* hit = HitTest(window_.Root(), pt, &local);
            DispatchPointerUp(hit, local, button);
        } else {
            if (dragging_ != nullptr) {
                dragging_->EndBarDrag();
                dragging_ = nullptr;
                RequestFrame();
                return;
            }
            Vec2 local{};
            Element* cap = CapturedElement();
            Element* hit = nullptr;
            if (cap != nullptr) {
                hit = cap;
                local = ClientToLocal(cap, pt);   // 捕获者空间（未必是命中元素）
            } else {
                hit = HitTest(window_.Root(), pt, &local);
            }
            SetPressedChain(pressed_, false);
            DispatchPointerUp(hit, local, button);           // 弹起（冒泡）
            if (cap != nullptr) {
                SetCaptureElement(nullptr);            // 抬起自动释放捕获（拖拽不合成 Click）
                Vec2 curLocal{};
                Element* cur = HitTest(window_.Root(), pt, &curLocal);
                UpdateHoverChain(hover_, cur);         // 恢复悬停链到当前指针位置
                DispatchHoverTransitions(hover_, cur, curLocal);
                hover_ = cur;
            } else {
                // Click 合成：按下抬起同元素且拖出在阈值内（06-Events §3：4 dip）
                constexpr float kDragThreshold = 4.f;
                if (pressed_ != nullptr && hit == pressed_ &&
                    pt.x >= pressPt_.x - kDragThreshold && pt.x <= pressPt_.x + kDragThreshold &&
                    pt.y >= pressPt_.y - kDragThreshold && pt.y <= pressPt_.y + kDragThreshold) {
                    DispatchClick(pressed_, local, PointerButton::Left);
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
        Vec2 local{};
        Element* hit = HitTest(window_.Root(), pt, &local);
        if (DispatchWheel(hit, local, delta, horizontal)) {   // 已被元素处理
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

    // 非客户区命中（WM_NCHITTEST）：框架默认值 → 交给业务覆盖。
    //   框架默认（仅无边框窗口）：① 缩放热区（resizable）② app-region 拖动区
    //                              ③ 客户区
    //   业务返回 true  → 采用 hit（业务可先改写框架默认值再返回）
    //   业务返回 false → 采用框架默认值
    // 有边框窗口：框架不介入（缩放由系统自带），业务不接管则交回 DefWindowProc。
    bool OnNcHitTest(Vec2 pt, int& hit) override {
        HitTestCode code = HitTestCode::Client;
        if (window_.options_.frameless) code = DefaultNcHitTest(pt);
        if (window_.OnNcHitTest(pt, code)) {
            hit = static_cast<int>(code);
            return true;
        }
        if (!window_.options_.frameless) return false;   // 有边框：交回系统默认
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
        // framePending 要等拖拽结束才被消费 —— 帧必须就地渲染，内容才能实时变化。
        // 但建窗时的 SetWindowPos 也同步派发 WM_SIZE，那时 Device 尚未 Init，
        // 就地渲染会崩；置脏交给随后正常帧路径（AddWindow 已设 framePending）。
        if (deviceReady_) {
            Application::RenderFrame(window_, native_, device_);
            window_.OnStateChanged();        // 尺寸 / 最大化状态变化（业务自绘按钮用）
        }
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

    float pendingOldScale_ = 0.f;   // DPI 变化前的缩放（OnDpiScaleChanging 留档）

    // 框架默认的非客户区命中（仅无边框窗口有效）：
    //   ① 缩放热区（resizable 且未最大化）→ HT* 缩放码
    //   ② app-region 拖动区（元素命中 + 继承链）→ HTCAPTION
    //   ③ 其余 → HTCLIENT
    // 顺序固定：**缩放热区优先于拖动区** —— 否则窗口上边那条窄带会被拖动区
    // 整条吃掉，只剩"移动窗口"、没有"缩放窗口"（12-Windowing §6.2）。
    HitTestCode DefaultNcHitTest(Vec2 pt) const {
        // ① 缩放热区：厚度取系统边框度量，与原生窗口同厚、随 DPI 自适应
        if (window_.options_.resizable && !window_.IsMaximized()) {
            const float b = ResizeBorderDip();
            if (b > 0.f) {
                const Vec2 size = window_.ClientSizeDip();
                const bool left   = pt.x < b;
                const bool right  = pt.x > size.x - b;
                const bool top    = pt.y < b;
                const bool bottom = pt.y > size.y - b;
                if (top && left)     return HitTestCode::TopLeft;
                if (top && right)    return HitTestCode::TopRight;
                if (bottom && left)  return HitTestCode::BottomLeft;
                if (bottom && right) return HitTestCode::BottomRight;
                if (top)    return HitTestCode::Top;
                if (left)   return HitTestCode::Left;
                if (right)  return HitTestCode::Right;
                if (bottom) return HitTestCode::Bottom;
            }
        }
        // ② app-region：命中元素 → 沿父链取最近设置者（继承属性，StyleResolve）
        //    Drag → 拖动窗口；NoDrag / None → 普通客户区
        Element* hit = HitTest(window_.Root(), pt);
        if (hit != nullptr) {
            const StyleValue v = ResolveProperty(hit, Prop::AppRegion);
            if (v.u == static_cast<uint32_t>(AppRegion::Drag)) return HitTestCode::Caption;
        }
        return HitTestCode::Client;
    }

    // 缩放热区厚度（DIP）：系统边框度量（SM_CXSIZEFRAME + SM_CXPADDEDBORDER，
    // 物理像素，按窗口 DPI 查询）÷ 缩放系数。与原生窗口的可缩放边框同厚。
    float ResizeBorderDip() const {
        const float scale = native_.Scale();
        if (scale <= 0.f) return 0.f;
        const UINT dpi = static_cast<UINT>(scale * 96.f + 0.5f);
        const int frame = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi);
        const int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        return static_cast<float>(frame + pad) / scale;
    }

    Application& app_;
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

Window::Window(const WindowOptions& options) : options_(options) {
    root_ = std::make_unique<Panel>();
}

Window::~Window() = default;

HWND Window::NativeHandle() const noexcept {
    if (native_ == nullptr) return nullptr;
    return static_cast<NativeWindow*>(native_)->Handle();
}

// 窗口操作（无边框窗口的自绘标题栏按钮）
void Window::Minimize() {
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->Minimize();
}

void Window::MaximizeOrRestore() {
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->MaximizeOrRestore();
}

void Window::Close() {
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->RequestClose();
}

bool Window::IsMaximized() const {
    auto* native = static_cast<NativeWindow*>(native_);
    return native != nullptr && native->IsMaximized();
}

Vec2 Window::ClientSizeDip() const {
    auto* native = static_cast<NativeWindow*>(native_);
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
    auto* native = static_cast<NativeWindow*>(native_);
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
    auto* native = static_cast<NativeWindow*>(native_);
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

namespace {

// UI 线程的 COM 单元必须是 STA（17-Dialogs §9）：Common Item Dialog 与
// Shell 扩展是 STA 组件，MTA 下 IFileDialog::Show 会在创建窗口前死锁。
// OleInitialize 同时建立 STA 与 OLE（Shell 拖放/扩展需要）；必须早于 WIC
// 的 CoInitializeEx(MTA)（后者在已 STA 的线程上只返回 RPC_E_CHANGED_MODE，
// COM 仍可用）。进程级持有，不配对 Uninitialize（与 ImageCodec.cpp 一致）。
void EnsureUiCom() {
    static bool done = false;
    if (done) return;
    done = true;
    const HRESULT uiCom = OleInitialize(nullptr);
    if (uiCom == RPC_E_CHANGED_MODE) {
        std::printf("[DUI] COM already initialized as MTA on UI thread; "
                    "system file dialogs will report an error\n");
    }
}

constexpr unsigned kWakeMessage = WM_APP;   // 窗口过程仅借它唤醒泵，无副作用

// 元素是否位于 root 子树内（含自身）。焦点与指针捕获是进程级全局唯一的，
// 多窗口下判断归属要沿 Parent 链走到窗口根。
bool IsWithin(Element* e, const Element* root) noexcept {
    for (; e != nullptr; e = e->Parent()) {
        if (e == root) return true;
    }
    return false;
}

}  // namespace

// 每窗口一份：Device / NativeWindow / 宿主各自独立（多窗口不同 DPI 显示器）。
// unique_ptr 管理的堆对象地址稳定 —— Application::windows_ 扩容也不影响
// WindowHostAdapter::framePending_ 指向的 bool。
struct Application::WindowRecord {
    Window* window = nullptr;
    std::unique_ptr<graphics::Device> device;
    std::unique_ptr<NativeWindow> native;
    std::unique_ptr<WindowHostAdapter> host;
    bool framePending = true;
    bool closed = false;        // WM_DESTROY 已到、记录待下一轮清理（见 OnWindowDestroyed）

    // 成员逆序析构：host → native → device。native 析构时 DestroyWindow 会同步
    // 发 WM_DESTROY，故必须在那之前摘掉宿主（本析构体先于成员析构执行）。
    ~WindowRecord() {
        if (native) native->DetachHost();
    }
};

bool Application::AddWindow(Window& window) {
    EnsureUiCom();   // OnCreate 里可能用 Image / Clipboard → 必须早于它
    auto rec = std::make_unique<WindowRecord>();
    rec->window = &window;
    rec->device = std::make_unique<graphics::Device>();
    rec->native = std::make_unique<NativeWindow>();
    rec->host = std::make_unique<WindowHostAdapter>(*this, window, *rec->native, *rec->device);

    // 建 HWND（隐藏、按显示器 DPI 定尺寸）
    if (!rec->native->Create(window.options_.title, window.options_.width,
                             window.options_.height, *rec->host, lastError_,
                             window.options_.frameless,
                             window.options_.doubleClickMaximize)) {
        return false;
    }
    window.native_ = rec->native.get();
    window.app_ = this;

    // 初始居中：仍在 OnCreate / Show 之前，构建过程不可见（App.h 契约）
    if (window.options_.center) rec->native->CenterOnScreen();

    if (!rec->device->Init(rec->native->Handle(), window.options_.transparent, lastError_)) {
        window.native_ = nullptr;
        return false;
    }
    // Device 就绪：此后 WM_SIZE（含 Show / 拖拽）触发的就地 RenderFrame 才安全。
    // Create 阶段的 SetWindowPos 早已同步派发过 WM_SIZE，那次被闸门挡下，
    // 本窗口的 framePending=true 保证随后正常帧补上首帧渲染。
    rec->host->SetDeviceReady();

    // 先登记再 OnCreate：业务在 OnCreate 里查 WindowCount() 能看到新窗口
    windows_.push_back(std::move(rec));
    WindowRecord& r = *windows_.back();

    // 后台完成回投 → PostMessage 唤醒泵 → [调度] 排空。唤醒目标 = 主窗口
    // （任一窗口收到该消息都足以让全局 PeekMessage 醒来；主窗口关闭后切换）。
    dispatcher_.SetWakeTarget(r.native->Handle(), kWakeMessage);

    window.OnCreate();
    r.native->Show();
    r.host->SetFramePending(&r.framePending);
    return true;
}

bool Application::OnWindowDestroyed(WindowHostAdapter* host) {
    // 只标记，不在消息派发中途 erase —— 此刻正在 host 的回调栈里，
    // 直接析构宿主会 use-after-free；记录留到 Run 循环下一轮开头清理。
    for (auto& rec : windows_) {
        if (rec->host.get() != host) continue;
        rec->window->native_ = nullptr;   // HWND 已销毁 → 防悬空
        rec->closed = true;
        break;
    }
    size_t alive = 0;
    for (const auto& rec : windows_) if (!rec->closed) ++alive;
    if (alive == 0) {
        // 最后一个窗口：退出与否由策略决定。
        //   true  → 平台层 PostQuitMessage(0)，消息循环收 WM_QUIT 退出。
        //   false → 不投退出消息；窗口数组清空，Run 继续（托盘 / 后台阶段），
        //           唤醒目标切到 UI 线程，保证后台 PostUi 仍能叫醒泵。
        if (quitOnLastWindowClosed_) return true;
        dispatcher_.SetWakeTarget(nullptr, kWakeMessage);
        dispatcher_.SetWakeThread(::GetCurrentThreadId());
        return false;
    }

    // 主窗口关了 → 唤醒目标切到下一个存活窗口
    bool wakeUpdated = false;
    for (const auto& rec : windows_) {
        if (rec->closed) continue;
        dispatcher_.SetWakeTarget(rec->native->Handle(), kWakeMessage);
        wakeUpdated = true;
        break;
    }
    (void)wakeUpdated;   // alive > 0 ⇒ 必有一个存活窗口
    return false;
}

size_t Application::WindowCount() const noexcept {
    size_t n = 0;
    for (const auto& rec : windows_) if (!rec->closed) ++n;
    return n;
}

void Application::Quit(int exitCode) noexcept {
    // 经 UI 任务队列回投：PostQuitMessage 必须在 UI 线程执行才生效
    // （后台线程直接调只会投到自己队列）。Post 同时唤醒泵；无窗口时走线程唤醒。
    dispatcher_.Post([exitCode] { ::PostQuitMessage(exitCode); });
}

unsigned Application::SetTimer(unsigned ms, std::function<void()> fn) {
    if (ms == 0) ms = 1;
    // hWnd=NULL：WM_TIMER 进 UI 线程消息队列，不依赖任何窗口；系统分配 id。
    const UINT_PTR id = ::SetTimer(nullptr, 0, ms, nullptr);
    if (id == 0) return 0;   // 创建失败（极少）
    timers_[static_cast<unsigned>(id)] = std::move(fn);
    return static_cast<unsigned>(id);
}

void Application::KillTimer(unsigned id) noexcept {
    timers_.erase(id);
    ::KillTimer(nullptr, static_cast<UINT_PTR>(id));
}

void Application::DispatchAppTimer(unsigned id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    it->second();   // 周期定时器：系统继续投递后续 WM_TIMER
}

int Application::Run() {
    EnsureUiCom();
    if (windows_.empty()) return 1;
    Dispatcher::SetUi(&dispatcher_);

    // 按需渲染循环：[调度] 排空 → 每个窗口各自有失效才进帧 → 空闲 WaitMessage。
    // PeekMessageW(nullptr) 收取**所有**窗口的消息；DispatchMessage 按 HWND
    // 的 GWLP_USERDATA 路由到对应 NativeWindow → 对应宿主（天然多实例隔离）。
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                Dispatcher::SetUi(nullptr);
                // 退出前清掉应用定时器（连同系统 timer），避免遗留到进程结束
                for (const auto& kv : timers_) ::KillTimer(nullptr, static_cast<UINT_PTR>(kv.first));
                timers_.clear();
                return static_cast<int>(msg.wParam);
            }
            // 应用级定时器：hWnd=NULL 的 WM_TIMER 不进任何窗口过程，在此直接分发
            if (msg.message == WM_TIMER && msg.hwnd == nullptr) {
                DispatchAppTimer(static_cast<unsigned>(msg.wParam));
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // 清理上一轮标记销毁的窗口（此刻不在任何宿主回调栈里）
        for (auto it = windows_.begin(); it != windows_.end();) {
            if ((*it)->closed) it = windows_.erase(it);
            else ++it;
        }

        dispatcher_.Drain();

        bool rendered = false;
        for (const auto& rec : windows_) {
            // 窗口失活（切到别的应用 / 别的窗口）→ 清焦点：编辑控件随之失焦 ——
            // 选区消失、光标停止闪烁。最小化不在此列（保留焦点）。
            // 焦点 / 捕获是进程级全局唯一（跨窗口共享），切换窗口时旧窗口失活、
            // 新窗口的点击才 SetFocus；按消息顺序失活先到，若此刻无差别清焦点会把
            // 新窗口刚设的焦点也清掉。故只清"仍属于失活窗口"的焦点 / 捕获。
            if (rec->host->TakeDeactivate()) {
                Element* root = rec->window->Root();
                if (Element* f = FocusedElement(); f != nullptr && IsWithin(f, root)) {
                    SetFocus(nullptr);
                }
                if (Element* c = CapturedElement(); c != nullptr && IsWithin(c, root)) {
                    SetCaptureElement(nullptr);   // 失活时可能收不到抬起消息 → 防捕获残留
                }
            }
            rec->window->SyncTickTimer();   // 每窗口按需启停 UI 统一节拍

            // 最小化：客户区尺寸为 0 → RenderFrame 直接返回且不清脏标记，若仍按
            // dirty 进帧，主循环永远进不了 WaitMessage → 退化成忙转（CPU 100%）。
            if (!rec->native->IsMinimized() &&
                (rec->framePending || rec->window->Root()->SubtreeDirty())) {
                rec->framePending = false;
                RenderFrame(*rec->window, *rec->native, *rec->device);   // 不等 vblank
                rendered = true;
            }
        }
        if (!rendered) WaitMessage();
    }
}

int Application::Run(Window& window) {
    lastError_.clear();
    if (!AddWindow(window)) return 1;
    return Run();
}

}  // namespace dui
