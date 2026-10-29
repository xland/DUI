// ============================================================================
//  DUI/Core/Dispatcher.cpp — Dispatcher / ThreadPool 实现（07-Threading）
// ============================================================================

#include "Core/Dispatcher.h"

#include <condition_variable>
#include <deque>
#include <thread>
#include <vector>

#include <windows.h>

namespace dui {

namespace {
Dispatcher* g_uiDispatcher = nullptr;   // 仅 UI 线程读写（SetUi 安装于 Run）
}

// ---------------------------------------------------------------------------
// ThreadPool（07-Threading D2：固定池，FIFO）
// ---------------------------------------------------------------------------

struct ThreadPool::Impl {
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> threads;
    bool stop = false;
};

ThreadPool::ThreadPool() : impl_(new Impl) {
    unsigned n = std::thread::hardware_concurrency();
    if (n == 0 || n > 4) n = 4;
    for (unsigned i = 0; i < n; ++i) {
        impl_->threads.emplace_back([this] {
            for (;;) {
                std::function<void()> fn;
                {
                    std::unique_lock<std::mutex> lock(impl_->mtx);
                    impl_->cv.wait(lock, [this] {
                        return impl_->stop || !impl_->queue.empty();
                    });
                    if (impl_->stop && impl_->queue.empty()) return;
                    fn = std::move(impl_->queue.front());
                    impl_->queue.pop_front();
                }
                fn();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        const std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->stop = true;
    }
    impl_->cv.notify_all();
    for (std::thread& t : impl_->threads) t.detach();   // 进程退出随 OS 回收（R6 排空列 P1）
}

ThreadPool& ThreadPool::Shared() {
    static ThreadPool pool;
    return pool;
}

void ThreadPool::Post(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->queue.push_back(std::move(fn));
    }
    impl_->cv.notify_one();
}

// ---------------------------------------------------------------------------
// Dispatcher（UI 队列 + PostMessage 唤醒，07-Threading D4）
// ---------------------------------------------------------------------------

struct Dispatcher::Impl {
    std::mutex mtx;
    std::vector<std::function<void()>> queue;
    HWND hwnd = nullptr;
    UINT wakeMsg = 0;
};

Dispatcher::Dispatcher() : impl_(new Impl) {}
Dispatcher::~Dispatcher() = default;

void Dispatcher::SetWakeTarget(void* hwnd, unsigned wakeMessage) {
    impl_->hwnd = static_cast<HWND>(hwnd);
    impl_->wakeMsg = wakeMessage;
}

void Dispatcher::Post(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->queue.push_back(std::move(fn));
    }
    // 唤醒消息泵（消息合并由泵天然完成；窗口已毁时 Post 失败，任务留待下轮）
    if (impl_->hwnd != nullptr) PostMessageW(impl_->hwnd, impl_->wakeMsg, 0, 0);
}

void Dispatcher::Drain() {
    std::vector<std::function<void()>> batch;
    {
        const std::lock_guard<std::mutex> lock(impl_->mtx);
        batch.swap(impl_->queue);
    }
    for (std::function<void()>& fn : batch) fn();   // 执行期间新 Post 的进下一轮
}

Dispatcher* Dispatcher::Ui() { return g_uiDispatcher; }

void Dispatcher::SetUi(Dispatcher* d) { g_uiDispatcher = d; }

}  // namespace dui
