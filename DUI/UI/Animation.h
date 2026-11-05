// ============================================================================
//  DUI/UI/Animation.h — 命令式属性动画（05-Animation）
//
//  只有一种启动方式：element->Animate(属性标签, 目标值, AnimConfig)。
//  动画值写入样式级联第 1 层（最高优先级），不污染 Set() 本地覆盖；
//  时间源复用 UI/Image.h 的 AnimationClock（帧动画 / 光标闪烁 / 属性动画
//  共用 App 层唯一一个窗口定时器）。
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "Core/Threading.h"
#include "UI/Style.h"

namespace dui {

class Element;

// ---------------------------------------------------------------------------
// 强类型属性标签：目标值类型由标签决定，编译期拒绝不可动画的属性。
// 目前只开放 Paint 类标量（float / Color）；布局、文本类属性每帧重排代价高。
// ---------------------------------------------------------------------------
template <class T>
struct AnimProp {
    Prop prop;
};

inline constexpr AnimProp<float> Opacity{Prop::Opacity};
inline constexpr AnimProp<Color> BackgroundColor{Prop::BackgroundColor};
inline constexpr AnimProp<Color> TextColor{Prop::Color};
inline constexpr AnimProp<Color> SelectionColor{Prop::SelectionColor};
inline constexpr AnimProp<Color> CaretColor{Prop::CaretColor};

// 聚合：支持 C++20 指派初始化 { .durationMs = 200, .easing = dui::Easing::EaseOut }
struct AnimConfig {
    uint32_t durationMs = 200;
    Easing   easing     = Easing::EaseOut;
};

namespace detail {

struct AnimState {
    Prop     prop = Prop::Unknown;
    Element* element = nullptr;
    Lifetime lifetime;                       // 元素析构置否 → 句柄自动失效
    std::function<void()> onFinished;
    bool running   = true;
    bool completed = false;
    bool cancelled = false;
};

struct AnimSlot {
    Prop     prop = Prop::Unknown;
    StyleValue from{};
    StyleValue to{};
    bool     color = false;                  // 颜色按 ARGB 通道插值，其余按 float 插值
    uint64_t startMs = 0;                   // 0 = 首次 tick 时取当前时刻
    uint32_t durationMs = 0;
    Easing   easing = Easing::EaseOut;
    bool     running = true;                 // false = 已完成，终值仍留在动画槽
    std::shared_ptr<AnimState> state;
};

// 统一 ticker 与句柄的桥接入口（实现在 UI/Animation.cpp）
void RegisterAnimatedElement(Element* e) noexcept;
void UnregisterAnimatedElement(Element* e) noexcept;
bool HasActiveAnimations() noexcept;
void TickAnimations(uint64_t nowMs);
void CancelAnimation(const std::shared_ptr<AnimState>& state) noexcept;

}  // namespace detail

// ---------------------------------------------------------------------------
// 动画句柄：可拷贝；Cancel 幂等；元素销毁后任何操作都是 no-op。
// ---------------------------------------------------------------------------
class AnimationHandle {
public:
    AnimationHandle() = default;

    // 完成回调（UI 线程、tick 内调用）。动画已正常完成后注册则立即调用一次；
    // 已取消或元素已销毁则忽略。重复注册以后者覆盖前者。
    AnimationHandle& OnFinished(std::function<void()> fn);

    // 取消并回落基值；幂等。
    void Cancel() noexcept;

private:
    friend class Element;
    explicit AnimationHandle(std::shared_ptr<detail::AnimState> state)
        : state_(std::move(state)) {}

    std::shared_ptr<detail::AnimState> state_;
};

}  // namespace dui
