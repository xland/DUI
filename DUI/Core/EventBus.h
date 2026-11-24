// ============================================================================
//  DUI/Core/EventBus.h — 全局自定义事件总线（进程级，按事件名广播）
//
//  CustomEvent 只携带 type + detail；没有元素树路由（无 target / 冒泡 / 阶段）。
//  监听方可为元素（带 Lifetime，销毁自动解绑）或纯逻辑；发射方任意逻辑 / 线程。
//  Emit 异步回投 UI 线程（下一轮 Dispatcher::Drain 执行）。
// ============================================================================

#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <utility>

#include "Core/Core.h"
#include "Core/Threading.h"

namespace dui {

struct CustomEvent {
    String   type;
    std::any detail;

    // 取强类型负载。类型不符返回 nullptr（无异常），监听器据此静默跳过。
    template <class T>
    const T* Get() const noexcept { return std::any_cast<T>(&detail); }
};

// 内部：注册已包装的监听器。life 非空时元素销毁自动失效。
// 返回 id（>0），供 Off 解绑。UI 线程调用。
uint64_t AddCustomListener(const String& type, std::function<void(const CustomEvent&)> fn,
                           Lifetime life = {});

// 解绑指定监听器（UI 线程调用）。
bool OffCustomListener(uint64_t id) noexcept;

// 注册：fn 直接收强类型负载 const T&。返回 id 供 Off（元素注册可忽略）。
// 第三个参数为元素 Lifetime —— Element::On 会传入自身令牌，销毁后自动失效。
template <class T, class Fn>
uint64_t On(const String& type, Fn&& fn, Lifetime life = {}) {
    return AddCustomListener(type,
        [fn = std::forward<Fn>(fn)](const CustomEvent& ev) {
            if (const T* p = ev.Get<T>()) fn(*p);
        }, life);
}

// 成员函数形式：On<T>(type, ctx, &Ctx::method, life)
template <class T, class Ctx>
uint64_t On(const String& type, Ctx* ctx, void (Ctx::*fn)(const T&), Lifetime life = {}) {
    return On<T>(type, [ctx, fn](const T& v) { (ctx->*fn)(v); }, life);
}

// 发射：任意线程可调用；监听器在下轮 UI Drain 执行，负载按值拷贝。
// 同一事件名的监听器按注册顺序触发，类型不符者静默跳过。
void Emit(const String& type, std::any detail = {});

template <class T>
void Emit(const String& type, T&& value) {
    Emit(type, std::any(std::forward<T>(value)));
}

}  // namespace dui
