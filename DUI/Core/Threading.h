// ============================================================================
//  DUI/Core/Threading.h — 协程异步（07-Threading §4，M4 真实实现）
//
//  · Task<T> / Task<void>：eager 协程（创建即运行到首个挂起点）；Then 回投 UI 线程
//  · Background()/Foreground()：协程切换点（命名空间函数，调用点读作"动作"）
//  · Lifetime：元素存活令牌；Element 析构置否，回投回调据此安全丢弃
//  · PostUi(life, fn)：后台任务带令牌回投 UI；元素销毁 / UI 退出时自动丢弃
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

// ---- 后台 → UI 的安全回投（07-Threading §4.3）------------------------------
// 事件处理器里取 life = element->Lifetime()（必须在 UI 线程取），后台任务用它
// 把界面更新投回主线程：元素已销毁或 UI Dispatcher 已卸载时任务直接丢弃。
// fn 在 UI 线程执行，可安全触碰元素树；后台线程仍只处理纯数据。
inline void PostUi(Lifetime life, std::function<void()> fn) {
    Dispatcher* ui = Dispatcher::Ui();
    if (ui == nullptr) return;
    ui->Post([life = std::move(life), fn = std::move(fn)] {
        if (life) fn();   // 检查与元素析构同在 UI 线程，无需加锁
    });
}

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

// 切回 UI 线程：经 Dispatcher 回投。UI 已退出时无法回投，直接销毁挂起的
// 协程帧（避免永不恢复导致帧泄漏）；正常路径下 h.resume() 发生在 UI 线程。
struct ForegroundAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const {
        Dispatcher* ui = Dispatcher::Ui();
        if (ui == nullptr) { h.destroy(); return; }
        ui->Post([h] { h.resume(); });
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

// void 特化：无返回值，Then 回调不带参数（co_return; / 函数自然结束）
template <>
struct TaskState<void> {
    std::mutex mtx;
    std::function<void()> then;
    bool done = false;
};

template <>
inline void ScheduleThen<void>(const std::shared_ptr<TaskState<void>>& st) {
    Dispatcher* ui = Dispatcher::Ui();
    if (ui == nullptr) return;
    ui->Post([st] {
        std::function<void()> fn;
        {
            const std::lock_guard<std::mutex> lock(st->mtx);
            fn = std::move(st->then);
            st->then = nullptr;
        }
        if (fn) fn();
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

// ---- Task<void>：无结果的协程（co_return; 或函数自然结束）-------------------
// 单独特化，避免为 void 设计占位值与回调签名；Then 回调不带参数。
template <>
class Task<void> {
public:
    struct promise_type {
        std::shared_ptr<detail::TaskState<void>> state =
            std::make_shared<detail::TaskState<void>>();

        Task get_return_object() noexcept { return Task(state); }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }

        void return_void() {
            bool need = false;
            {
                const std::lock_guard<std::mutex> lock(state->mtx);
                state->done = true;
                need = state->then != nullptr;
            }
            if (need) detail::ScheduleThen(state);
        }
        void unhandled_exception() noexcept { std::terminate(); }
    };

    Task& Then(std::function<void()> fn) {
        bool done = false;
        {
            const std::lock_guard<std::mutex> lock(state_->mtx);
            state_->then = std::move(fn);
            done = state_->done;
        }
        if (done) detail::ScheduleThen(state_);
        return *this;
    }
    void Start() {}

private:
    explicit Task(const std::shared_ptr<detail::TaskState<void>>& s) noexcept : state_(s) {}
    std::shared_ptr<detail::TaskState<void>> state_;
};

}  // namespace dui
