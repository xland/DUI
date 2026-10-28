// ============================================================================
//  DUI/Core/Threading.h — 协程异步最小集（07-Threading §4）
//
//  M1 骨架：Task 在创建时**同步**跑完，Background/Foreground 立即返回，
//  Then 立即回调 —— 只保证示例可编译、语义可读。真正的 Dispatcher、线程池、
//  UI 线程回投在 M4 落地（07-Threading P0：Background/Foreground awaiter +
//  Task<T> 状态机 + Lifetime 安全回投），届时签名不变。
//
//  awaiter 约定：await_ready = true 表示"本 stub 不挂起"。
// ============================================================================

#pragma once

#include <coroutine>
#include <functional>
#include <memory>
#include <exception>
#include <utility>

#include "Core.h"

namespace dui {

// ---- 元素生命周期令牌（06-Events D7 / 07-Threading §4.3）----
//  M2 接入延迟删除队列：元素销毁时对应令牌置 false，回投回调据此丢弃结果。
struct Lifetime {
    explicit operator bool() const noexcept { return alive_; }
    bool alive_ = true;
};

// ---- 线程切换点（M1 stub：不真正切线程）----
struct ThreadSwitchAwaiter {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

inline ThreadSwitchAwaiter Background() noexcept { return {}; }   // 切到线程池（M4）
inline ThreadSwitchAwaiter Foreground() noexcept { return {}; }   // 切回 UI 线程（M4）

// ---- Task<T>：M1 为"同步完成"的退化协程 ----
//  状态用 shared_ptr 承载：final_suspend 为 never，协程帧立即销毁，
//  结果必须先于帧销毁拷出到 Task 持有的 state 中。
template <class T>
class Task {
public:
    struct promise_type {
        std::shared_ptr<T> state = std::make_shared<T>();

        Task get_return_object() noexcept { return Task(state); }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_value(T v) { *state = std::move(v); }
        void unhandled_exception() noexcept { std::terminate(); }
    };

    // M1 stub：协程已同步完成，立即回调；M4 改为"完成时经 Dispatcher 回投 UI 线程"。
    // 回调参数即 T（T 已是 Result<U> 时回调收 Result<U>，不再二次包裹）。
    Task& Then(std::function<void(T)> fn) {
        if (fn) fn(std::move(*state_));
        return *this;
    }

    void Start() {}   // M1：无异步可启动；M4：把协程句柄交给 Dispatcher

private:
    explicit Task(std::shared_ptr<T> state) noexcept : state_(std::move(state)) {}
    std::shared_ptr<T> state_;
};

}  // namespace dui
