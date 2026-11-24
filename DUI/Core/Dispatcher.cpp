// ============================================================================
//  DUI/Core/Dispatcher.cpp — Dispatcher / ThreadPool 实现（07-Threading）
// ============================================================================

#include "Core/Dispatcher.h"

#include <atomic>

namespace dui {

namespace {
// PostUi / Foreground / ScheduleThen / Emit 都会在后台线程读，SetUi 在 UI 线程
// 写；用原子指针避免数据竞争（release/acquire 保证安装/卸载对读者可见）。
std::atomic<Dispatcher*> g_uiDispatcher{nullptr};
}

// ---------------------------------------------------------------------------
// ThreadPool（07-Threading D2：固定池，FIFO）
// ---------------------------------------------------------------------------

ThreadPool::ThreadPool() {
    unsigned n = std::thread::hardware_concurrency();
    if (n == 0 || n > 4) n = 4;
    for (unsigned i = 0; i < n; ++i) {
        threads.emplace_back([this] {
            for (;;) {
                std::function<void()> fn;
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [this] { return stop || !queue.empty(); });
                    if (stop && queue.empty()) return;
                    fn = std::move(queue.front());
                    queue.pop_front();
                }
                fn();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        const std::lock_guard<std::mutex> lock(mtx);
        stop = true;
    }
    cv.notify_all();
    // 必须 join：worker 捕获 [this] 访问 mtx/cv/queue/stop，detach 后成员析构
    // 而 worker 仍在跑 → UAF。worker 循环在 stop && queue.empty() 时退出，
    // 所以 join 会把已排队的任务执行完再返回。
    for (std::thread& t : threads) {
        if (t.joinable()) t.join();
    }
}

ThreadPool& ThreadPool::Shared() {
    static ThreadPool pool;
    return pool;
}

void ThreadPool::Post(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> lock(mtx);
        queue.push_back(std::move(fn));
    }
    cv.notify_one();
}

// ---------------------------------------------------------------------------
// Dispatcher（UI 队列 + PostMessage 唤醒，07-Threading D4）
// ---------------------------------------------------------------------------

void Dispatcher::SetWakeTarget(HWND target, UINT wakeMessage) {
    hwnd = target;
    wakeMsg = wakeMessage;
}

void Dispatcher::SetWakeThread(DWORD threadId) noexcept {
    wakeThread = threadId;
}

void Dispatcher::Post(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> lock(mtx);
        queue.push_back(std::move(fn));
    }
    // 唤醒消息泵（消息合并由泵天然完成）：有窗口投窗口，无窗口（全部销毁、
    // 退出策略为不退出）投线程。两条都失败时任务仍留在队列，下轮 Drain 取走。
    if (hwnd != nullptr) {
        PostMessageW(hwnd, wakeMsg, 0, 0);
    } else if (wakeThread != 0) {
        PostThreadMessageW(wakeThread, wakeMsg, 0, 0);
    }
}

void Dispatcher::Drain() {
    std::vector<std::function<void()>> batch;
    {
        const std::lock_guard<std::mutex> lock(mtx);
        batch.swap(queue);
    }
    for (std::function<void()>& fn : batch) fn();   // 执行期间新 Post 的进下一轮
}

Dispatcher* Dispatcher::Ui() { return g_uiDispatcher.load(std::memory_order_acquire); }

void Dispatcher::SetUi(Dispatcher* d) { g_uiDispatcher.store(d, std::memory_order_release); }

Dispatcher& Dispatcher::Shared() noexcept {
    static Dispatcher inst;
    return inst;
}

}  // namespace dui
