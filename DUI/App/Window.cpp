// ============================================================================
//  DUI/App/Window.cpp — dui::Window 实现（UI 门面 → NativeWindow 平台壳）
// ============================================================================

#include "App/Window.h"

#include "App/NativeWindow.h"

namespace dui {

Window::Window(const WindowOptions& options) : options_(options) {
    root_ = std::make_unique<Panel>();
}

Window::~Window() = default;

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

}  // namespace dui
