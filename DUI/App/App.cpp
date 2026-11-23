// ============================================================================
//  DUI/App/App.cpp — App / Window 运行时
//
//  M2：Platform.Window（HWND 封装）+ 消息循环 + OnCreate/Show 时序（App.h 契约、
//  12-Windowing §6）。交换链在 M3，OnDpiChanged 完整流程在 M5。
// ============================================================================

#include "App/App.h"

#include "Graphics/Graphics.h"
#include "Graphics/Device.h"
#include "App/NativeWindow.h"
#include "App/WindowHostAdapter.h"

#include <ole2.h>

#include <cstdio>

namespace dui {

App::App() = default;
App::~App() = default;

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
// unique_ptr 管理的堆对象地址稳定 —— App::windows_ 扩容也不影响
// WindowHostAdapter::framePending_ 指向的 bool。
struct App::WindowRecord {
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

bool App::AddWindow(Window& window) {
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

bool App::OnWindowDestroyed(WindowHostAdapter* host) {
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

size_t App::WindowCount() const noexcept {
    size_t n = 0;
    for (const auto& rec : windows_) if (!rec->closed) ++n;
    return n;
}

void App::Quit(int exitCode) noexcept {
    // 经 UI 任务队列回投：PostQuitMessage 必须在 UI 线程执行才生效
    // （后台线程直接调只会投到自己队列）。Post 同时唤醒泵；无窗口时走线程唤醒。
    dispatcher_.Post([exitCode] { ::PostQuitMessage(exitCode); });
}

unsigned App::SetTimer(unsigned ms, std::function<void()> fn) {
    if (ms == 0) ms = 1;
    // hWnd=NULL：WM_TIMER 进 UI 线程消息队列，不依赖任何窗口；系统分配 id。
    const UINT_PTR id = ::SetTimer(nullptr, 0, ms, nullptr);
    if (id == 0) return 0;   // 创建失败（极少）
    timers_[static_cast<unsigned>(id)] = std::move(fn);
    return static_cast<unsigned>(id);
}

void App::KillTimer(unsigned id) noexcept {
    timers_.erase(id);
    ::KillTimer(nullptr, static_cast<UINT_PTR>(id));
}

void App::DispatchAppTimer(unsigned id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    it->second();   // 周期定时器：系统继续投递后续 WM_TIMER
}

int App::Run() {
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

int App::Run(Window& window) {
    lastError_.clear();
    if (!AddWindow(window)) return 1;
    return Run();
}

}  // namespace dui
