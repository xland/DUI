// ============================================================================
//  DUI/App/Window.cpp — dui::Window 实现（UI 门面 → NativeWindow 平台壳）
// ============================================================================

#include "App/Window.h"

#include "App/NativeWindow.h"

namespace dui {

Window::Window(const WindowOptions& options) : options_(options) {
    root_ = std::make_unique<Panel>();
    root_->SetWindowTree(this);   // 根面板及其子树挂上本窗口
}

Window::~Window() {
    // HWND 销毁时系统会自动撤销绑定其上的 timer，但业务闭包捕获的对象可能
    // 先于 HWND 析构，故这里主动停表并释放，避免闭包多活一帧。
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) {
        for (const auto& kv : timers_) native->StopTimer(kv.first);
    }
    timers_.clear();
}

HWND Window::NativeHandle() const noexcept {
    if (native_ == nullptr) return nullptr;
    return static_cast<NativeWindow*>(native_)->Handle();
}

bool Window::OnDragEnterFiles(const std::vector<String>& paths, Vec2 ptDip) {
    Vec2 local{};
    Element* hit = HitTest(root_.get(), ptDip, &local);
    return DispatchDragOverFiles(hit, local, paths, true);
}

bool Window::OnDragOverFiles(const std::vector<String>& paths, Vec2 ptDip) {
    Vec2 local{};
    Element* hit = HitTest(root_.get(), ptDip, &local);
    return DispatchDragOverFiles(hit, local, paths, false);
}

void Window::OnDragLeaveFiles() {
    DispatchDragLeaveFiles(root_.get());
}

void Window::OnDropFiles(const std::vector<String>& paths, Vec2 ptDip) {
    DispatchDragLeaveFiles(root_.get());   // 清掉悬停预览态
    Vec2 local{};
    Element* hit = HitTest(root_.get(), ptDip, &local);
    DispatchDropFiles(hit, local, paths);
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

// ---- 可见性与几何（转发到 NativeWindow）----
void Window::Show() {
    if (auto* n = static_cast<NativeWindow*>(native_)) n->Show();
}
void Window::Hide() {
    if (auto* n = static_cast<NativeWindow*>(native_)) n->Hide();
}
bool Window::IsVisible() const noexcept {
    auto* n = static_cast<NativeWindow*>(native_);
    return n != nullptr && n->IsVisible();
}
void Window::Activate() {
    if (auto* n = static_cast<NativeWindow*>(native_)) n->Activate();
}
Rect Window::BoundsPx() const {
    auto* n = static_cast<NativeWindow*>(native_);
    return n != nullptr ? n->BoundsPx() : Rect{};
}
void Window::SetBoundsPx(Rect r) {
    if (auto* n = static_cast<NativeWindow*>(native_)) n->SetBoundsPx(r);
}
void Window::MoveToPx(float xPx, float yPx) {
    if (auto* n = static_cast<NativeWindow*>(native_)) n->MoveToPx(xPx, yPx);
}
void Window::Resize(float wDip, float hDip) {
    if (auto* n = static_cast<NativeWindow*>(native_)) n->Resize(wDip, hDip);
}
void Window::Center() {
    if (auto* n = static_cast<NativeWindow*>(native_)) n->CenterOnScreen();
}

void Window::SetTopmost(bool on) {
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->SetTopmost(on);
}

void Window::SetMouseThrough(bool on) {
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->SetMouseThrough(on);
}

void Window::SetExcludeFromCapture(bool on) {
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->SetExcludeFromCapture(on);
}

void Window::FlashFrame(bool on, unsigned count, unsigned intervalMs) {
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->FlashFrame(on, count, intervalMs);
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
constexpr uintptr_t kTickTimerId = 1;   // 内部动画节拍独占；业务 id 从 2 起
}

unsigned Window::SetTimer(unsigned ms, std::function<void()> fn) {
    if (ms == 0) ms = 1;
    auto* native = static_cast<NativeWindow*>(native_);
    if (native == nullptr || !fn) return 0;   // OnCreate 之前 / 空回调：不挂空转

    // 分配 id：跳过 0/1（保留）与已占用项；溢出绕回时继续找空位
    unsigned id = nextTimerId_;
    for (;;) {
        if (id < 2) id = 2;
        if (timers_.find(id) == timers_.end()) break;
        ++id;
    }
    nextTimerId_ = id + 1;

    native->StartTimer(id, ms);
    timers_.emplace(id, std::move(fn));
    return id;
}

void Window::KillTimer(unsigned id) noexcept {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    auto* native = static_cast<NativeWindow*>(native_);
    if (native != nullptr) native->StopTimer(id);
    timers_.erase(it);
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
    if (id == kTickTimerId && tickTimer_ != 0) {
        AnimationClock::Instance().Tick(::GetTickCount64());
        SyncTickTimer();      // 闪烁结束 / 动画移除 → 及时停表；周期变化 → 及时换挡
        return;
    }
    const auto it = timers_.find(static_cast<unsigned>(id));
    if (it == timers_.end()) return;
    // 先拷贝再执行：回调里常会 KillTimer(自身 id) 实现一次性定时器，
    // 那会 erase 容器中的闭包 —— 直接 it->second() 等于执行中被析构（UB）。
    auto fn = it->second;
    fn();
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

}  // namespace dui
