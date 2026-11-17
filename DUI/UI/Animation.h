// ============================================================================
//  DUI/UI/Animation.h — 命令式属性动画（05-Animation）
//
//  生命周期：Animate() 只**创建**动画（不执行）→ AnimationHandle::Start()
//  才真正推进；Pause() 冻结在当前值，Continue() 从当前值继续，Stop() 立即
//  落到终值，Cancel() 回落基值。
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
// 动画相位（Element::Animate 返回的句柄可查）
// ---------------------------------------------------------------------------
enum class AnimPhase : uint8_t {
    Idle = 0,     // 已创建，未启动
    Running,      // 推进中
    Paused,       // 已暂停（值冻结在当前）
    Completed,    // 自然跑到终点
    Stopped,      // Stop()：值已落在终值
    Cancelled,    // Cancel()：已回落基值
};

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

// 句柄与元素槽共享的记录：句柄侧（回调 / 相位）与元素侧（补间数据）共用一份
struct AnimShared {
    Prop     prop = Prop::Unknown;
    Element* element = nullptr;
    Lifetime lifetime;                       // 元素析构置否 → 句柄自动失效
    std::function<void()> onStarted;
    std::function<void()> onPaused;
    std::function<void()> onStopped;
    std::function<void()> onFinished;
    AnimPhase phase = AnimPhase::Idle;
};

struct AnimSlot {
    Prop     prop = Prop::Unknown;
    StyleValue from{};
    StyleValue to{};
    AnimKind kind = AnimKind::Scalar;        // 插值形态：标量 / 颜色 / 四通道
    uint64_t startMs = 0;                   // 当前推进段起点（绝对时刻）
    uint64_t elapsedMs = 0;                 // 已推进时长（暂停前累计 + 当前段）
    uint32_t durationMs = 0;
    Easing   easing = Easing::EaseOut;
    AnimPhase phase = AnimPhase::Idle;      // Idle = 已创建未启动
    bool     looping = false;               // 循环模式（Loop() 开启）
    uint32_t loopTotal = 0;                 // 循环总轮数；0 = 无限
    uint32_t loopRemaining = 0;             // 剩余轮数（含当前轮）；0 且 looping = 无限
    std::shared_ptr<AnimShared> shared;
};

// 句柄发来的控制命令
enum class AnimCommand : uint8_t { Start, Pause, Continue, Stop, Loop };

// 统一 ticker 与句柄的桥接入口（实现在 UI/Animation.cpp）
void RegisterAnimatedElement(Element* e) noexcept;
void UnregisterAnimatedElement(Element* e) noexcept;
bool HasActiveAnimations() noexcept;
void TickAnimations(uint64_t nowMs);
void CancelAnimation(const std::shared_ptr<AnimShared>& shared) noexcept;
void ControlAnimation(const std::shared_ptr<AnimShared>& shared, AnimCommand cmd,
                      uint32_t times = 0) noexcept;

}  // namespace detail

// ---------------------------------------------------------------------------
// 动画句柄：可拷贝；控制命令幂等；元素销毁后任何操作都是 no-op。
// ---------------------------------------------------------------------------
class AnimationHandle {
public:
    AnimationHandle() = default;

    // ---- 事件（UI 线程同步触发；返回副本便于链式调用）----
    // OnStarted：开始推进（首次 Start、重播、Continue 恢复，或循环的每一轮开始）。
    // OnPaused：暂停。
    // OnStopped：被 Stop() 打断（值已落在终值）。
    // OnFinished：到达终点（自然跑完，或 Stop() 立即完成；循环时每轮结束各触发一次）。
    // 注册时若对应状态已成立 → 立即调用一次；重复注册以后者覆盖前者；
    // 已取消 / 元素已销毁则忽略。回调会被保留 —— 循环的每一轮、重播都会再次触发。
    AnimationHandle OnStarted(std::function<void()> fn);
    AnimationHandle OnPaused(std::function<void()> fn);
    AnimationHandle OnStopped(std::function<void()> fn);
    AnimationHandle OnFinished(std::function<void()> fn);

    // ---- 控制 ----
    // Start：**从头重播** —— 时间轴归零、值回到动画起点，再重新推进。
    //        首次启动时起点取"当前显示值"；重播沿用原起点。
    // Pause：冻结在当前值（仅 Running 有效）。
    // Continue：从当前值继续（Paused → 恢复；Idle → 等同 Start；
    //           已是终态 → 不再执行）。
    // Stop：立即落到终值（等价"跳到末尾并完成"）；同时结束循环模式。
    // Loop：开启循环播放，并从头开始第一轮。times = 总播放轮数（0 = 无限循环）。
    //        每轮结束触发 OnFinished、下一轮开始触发 OnStarted；
    //        最后一轮结束后进入 Completed。
    // Cancel：取消并回落基值。
    void Start() noexcept;
    void Pause() noexcept;
    void Continue() noexcept;
    void Stop() noexcept;
    void Loop(uint32_t times = 0) noexcept;
    void Cancel() noexcept;

    // ---- 查询 ----
    AnimPhase Phase() const noexcept;
    bool IsRunning() const noexcept { return Phase() == AnimPhase::Running; }
    bool IsFinished() const noexcept;

private:
    friend class Element;
    explicit AnimationHandle(std::shared_ptr<detail::AnimShared> shared)
        : shared_(std::move(shared)) {}

    std::shared_ptr<detail::AnimShared> shared_;
};

}  // namespace dui
