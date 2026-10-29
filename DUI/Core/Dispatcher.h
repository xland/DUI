// ============================================================================
//  DUI/Core/Dispatcher.h — UI 线程任务队列 + 后台线程池（07-Threading §3/§4）
//
//  线程规则（07-Threading §3.1 唯一铁律）：后台线程只碰纯数据；触碰 UI 的任务
//  一律经 Post 回投 UI 线程。UI Dispatcher 由 Application::Run 安装并排空。
//
//  实现注记：PostMessage 唤醒需要 HWND（07-Threading D4），本模块因此含少量
//  Win32 —— 与 01-Layering「Core 不依赖 Win32」的偏差记录在 Todo（M4）；
//  pimpl 把 windows.h 挡在 .cpp，DUI.h 公共头不带 windows.h 的约束不破。
// ============================================================================
#pragma once

#include <functional>
#include <memory>

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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// UI 线程任务队列 + 泵。每轮消息循环由 Application 排空（[调度] 步）。
class Dispatcher {
public:
    Dispatcher();
    ~Dispatcher();

    // 线程安全：fn 在 UI 线程执行（下次 Drain 时）
    void Post(std::function<void()> fn);
    // 仅 UI 线程调用：执行当前积压任务（执行期间新 Post 的进下一轮）
    void Drain();
    // Application::Run 安装唤醒目标（跨线程 Post 后 PostMessage 唤醒消息泵）
    void SetWakeTarget(void* hwnd, unsigned wakeMessage);

    static Dispatcher* Ui();           // 进程级 UI Dispatcher（未安装返回 nullptr）
    static void SetUi(Dispatcher* d);  // UI 线程安装/卸载

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dui
