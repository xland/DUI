// ============================================================================
//  DUI/Core/Dispatcher.cpp — Dispatcher / ThreadPool 实现（07-Threading）
// ============================================================================

#include "Core/Dispatcher.h"

namespace dui {

namespace {
Dispatcher* g_uiDispatcher = nullptr;   // 仅 UI 线程读写（SetUi 安装于 Run）
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
    for (std::thread& t : threads) t.detach();   // 进程退出随 OS 回收（R6 排空列 P1）
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

void Dispatcher::Post(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> lock(mtx);
        queue.push_back(std::move(fn));
    }
    // 唤醒消息泵（消息合并由泵天然完成；窗口已毁时 Post 失败，任务留待下轮）
    if (hwnd != nullptr) PostMessageW(hwnd, wakeMsg, 0, 0);
}

void Dispatcher::Drain() {
    std::vector<std::function<void()>> batch;
    {
        const std::lock_guard<std::mutex> lock(mtx);
        batch.swap(queue);
    }
    for (std::function<void()>& fn : batch) fn();   // 执行期间新 Post 的进下一轮
}

Dispatcher* Dispatcher::Ui() { return g_uiDispatcher; }

void Dispatcher::SetUi(Dispatcher* d) { g_uiDispatcher = d; }

}  // namespace dui
