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
// 开放 Paint 类标量（float / Color）与 Paint 级 transform 标量；布局尺寸标签
// （Width / Height）每帧重排，仅作对照用（05-Animation §2.1 / §3）。
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
// Paint 级 transform：平移 DIP / 旋转度 / 等比倍率（级联层 1 生效，标脏 kFlagPaint）
inline constexpr AnimProp<float> TranslationX{Prop::TranslationX};
inline constexpr AnimProp<float> TranslationY{Prop::TranslationY};
inline constexpr AnimProp<float> Rotation{Prop::Rotation};
inline constexpr AnimProp<float> Scale{Prop::Scale};
// 布局尺寸（每帧 kFlagLayout → 整棵树重新 Measure/Arrange）
inline constexpr AnimProp<float> Width{Prop::Width};
inline constexpr AnimProp<float> Height{Prop::Height};
// 定位偏移（04-Layout §4.3）：仅 Relative/Absolute/Fixed 生效，同样每帧重排
inline constexpr AnimProp<float> Left{Prop::Left};
inline constexpr AnimProp<float> Top{Prop::Top};
// 四边厚度（四边同时补间，共用同一条时间轴与缓动；kFlagLayout → 每帧重排）
inline constexpr AnimProp<Thickness> Padding{Prop::Padding};
inline constexpr AnimProp<Thickness> Margin{Prop::Margin};
// 四角圆角（四角同时补间；kFlagPaint → 只重绘，不重排）
inline constexpr AnimProp<CornerRadii> CornerRadius{Prop::CornerRadius};
// 边框宽度（四边同时补间；kFlagPaint → 只重绘，不重排）
inline constexpr AnimProp<Thickness> BorderWidth{Prop::BorderWidth};

// 值的插值形态：决定补间时哪些通道参与插值
enum class AnimKind : uint8_t {
    Scalar,   // 单标量（f）
    Color,    // ARGB 四通道（u）
    Quad,     // 四通道（f + extra[0..2]）：Thickness 四边 / CornerRadii 四角
};

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
    AnimKind kind = AnimKind::Scalar;        // 插值形态：标量 / 颜色 / 四通道
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
