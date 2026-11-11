// ============================================================================
//  DUI/Core/Dispatcher.h — UI 线程任务队列 + 后台线程池（07-Threading §3/§4）
//
//  线程规则（07-Threading §3.1 唯一铁律）：后台线程只碰纯数据；触碰 UI 的任务
//  一律经 Post 回投 UI 线程。UI Dispatcher 由 Application::Run 安装并排空。
//
//  实现注记：PostMessage 唤醒需要 HWND（07-Threading D4），本模块因此含少量
//  Win32。「Core 不依赖 Win32」约束已于 2026-11-11 取消（剪贴板下沉 Core 同批），
//  不再用 pimpl 隔离，成员直接进头文件。
// ============================================================================
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include "Core.h"

namespace dui {

// 后台固定线程池（07-Threading D2：并发度 = min(4, 硬件并发)，FIFO）
class ThreadPool {
public:
    static ThreadPool& Shared();
    void Post(std::function<void()> fn);   // 线程安全

private:
    ThreadPool();
    ~ThreadPool();

    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> threads;
    bool stop = false;
};

// UI 线程任务队列 + 泵。每轮消息循环由 Application 排空（[调度] 步）。
class Dispatcher {
public:
    Dispatcher() = default;
    ~Dispatcher() = default;

    // 线程安全：fn 在 UI 线程执行（下次 Drain 时）
    void Post(std::function<void()> fn);
    // 仅 UI 线程调用：执行当前积压任务（执行期间新 Post 的进下一轮）
    void Drain();
    // Application::Run 安装唤醒目标（跨线程 Post 后 PostMessage 唤醒消息泵）
    void SetWakeTarget(HWND hwnd, UINT wakeMessage);

    static Dispatcher* Ui();           // 进程级 UI Dispatcher（未安装返回 nullptr）
    static void SetUi(Dispatcher* d);  // UI 线程安装/卸载

private:
    std::mutex mtx;
    std::vector<std::function<void()>> queue;
    HWND hwnd = nullptr;
    UINT wakeMsg = 0;
};

}  // namespace dui
