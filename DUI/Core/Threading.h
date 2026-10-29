// ============================================================================
//  DUI/Core/Threading.h — 协程异步（07-Threading §4，M4 真实实现）
//
//  · Task<T>：eager 协程（创建即运行到首个挂起点）；Then 回调一律回投 UI 线程
//  · Background()/Foreground()：协程切换点（命名空间函数，调用点读作"动作"）
//  · Lifetime：元素存活令牌；Element 析构置否，回投回调据此安全丢弃
//
//  线程铁律（07-Threading §3.1）：后台线程只碰纯数据；触碰 UI 一律 Post 回投。
// ============================================================================

#pragma once

#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "Core.h"
#include "Dispatcher.h"

namespace dui {

// ---- 元素生命周期令牌（06-Events D7 / 07-Threading §4.3）----
// 默认存活；Element 析构置否。令牌可拷贝（共享同一标志）。
struct Lifetime {
    Lifetime() : flag_(std::make_shared<bool>(true)) {}
    explicit operator bool() const noexcept { return *flag_; }
    void Kill() noexcept { *flag_ = false; }

private:
    std::shared_ptr<bool> flag_;
};

// ---- 协程切换点（07-Threading §4.2）----------------------------------------

// 切到后台线程池：挂起当前协程，由池线程恢复
struct BackgroundAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const {
        ThreadPool::Shared().Post([h] { h.resume(); });
    }
    void await_resume() const noexcept {}
};
inline BackgroundAwaiter Background() noexcept { return {}; }

// 切回 UI 线程：经 Dispatcher 回投（未安装/已退出则任务丢弃，协程不再恢复）
struct ForegroundAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const {
        Dispatcher* ui = Dispatcher::Ui();
        if (ui != nullptr) ui->Post([h] { h.resume(); });
    }
    void await_resume() const noexcept {}
};
inline ForegroundAwaiter Foreground() noexcept { return {}; }

// ---- Task<T> ---------------------------------------------------------------

namespace detail {

template <class T>
struct TaskState {
    std::mutex mtx;
    T value{};
    std::function<void(T)> then;
    bool done = false;
};

// 把 Then 回调调度到 UI 线程（Then 回投 UI 线程，Todo M4）
template <class T>
void ScheduleThen(const std::shared_ptr<TaskState<T>>& st) {
    Dispatcher* ui = Dispatcher::Ui();
    if (ui == nullptr) return;
    ui->Post([st] {
        std::function<void(T)> fn;
        {
            const std::lock_guard<std::mutex> lock(st->mtx);
            fn = std::move(st->then);
            st->then = nullptr;   // 消费即清：重复调度不会重复执行
        }
        if (fn) fn(st->value);
    });
}

}  // namespace detail

template <class T>
class Task {
public:
    struct promise_type;

    // 完成回调：fn 在 UI 线程执行（经 Dispatcher）。完成前或后注册均可。
    Task& Then(std::function<void(T)> fn) {
        bool done = false;
        {
            const std::lock_guard<std::mutex> lock(state_->mtx);
            state_->then = std::move(fn);
            done = state_->done;
        }
        if (done) detail::ScheduleThen(state_);   // 已完成：直接调度
        return *this;
    }

    // eager 语义：协程在创建时已运行至首个挂起点；保留入口签名（规格冻结）
    void Start() {}

private:
    explicit Task(const std::shared_ptr<detail::TaskState<T>>& s) noexcept : state_(s) {}

    std::shared_ptr<detail::TaskState<T>> state_;
};

template <class T>
struct Task<T>::promise_type {
    std::shared_ptr<detail::TaskState<T>> state =
        std::make_shared<detail::TaskState<T>>();

    Task get_return_object() noexcept { return Task(state); }
    std::suspend_never initial_suspend() noexcept { return {}; }   // eager
    // 完成后协程帧即销毁；回调只触 TaskState（shared_ptr 存活），与帧无关
    std::suspend_never final_suspend() noexcept { return {}; }

    void return_value(T v) {
        bool need = false;
        {
            const std::lock_guard<std::mutex> lock(state->mtx);
            state->value = std::move(v);
            state->done = true;
            need = state->then != nullptr;
        }
        if (need) detail::ScheduleThen(state);
    }

    void unhandled_exception() noexcept { std::terminate(); }   // 无异常约定（ADR-007）
};

}  // namespace dui
