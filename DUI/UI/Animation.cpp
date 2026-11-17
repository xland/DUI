// ============================================================================
//  DUI/UI/Animation.cpp — 命令式属性动画（05-Animation）
//
//  · Element::Animate 只**创建**动画（不推进），AnimationHandle::Start() 才启动
//  · 统一 ticker（AnimationClock）调 detail::TickAnimations 推进所有活动元素
//  · 句柄以 shared_ptr<AnimShared> + Lifetime 令牌实现幂等控制与元素销毁失效
// ============================================================================

#include "UI/UI.h"

#include <algorithm>

namespace dui {

namespace {

struct RegEntry {
    Element* element;
    Lifetime lifetime;
};

// 有活动补间的元素（tick 遍历入口）；完成或取消后自动注销
std::vector<RegEntry> g_active;

float ApplyEasing(Easing e, float t) {
    switch (e) {
    case Easing::EaseIn:    return t * t;
    case Easing::EaseOut:   return 1.f - (1.f - t) * (1.f - t);
    case Easing::EaseInOut: return t < 0.5f ? 2.f * t * t
                                            : 1.f - 2.f * (1.f - t) * (1.f - t);
    default:                return t;   // Linear
    }
}

uint32_t LerpChannel(uint32_t a, uint32_t b, float t) {
    const uint32_t ca = a & 0xFFu;
    const int d = static_cast<int>(b & 0xFFu) - static_cast<int>(ca);
    return static_cast<uint32_t>(static_cast<int>(ca) + d * t + 0.5f);
}

uint32_t LerpColor(uint32_t from, uint32_t to, float t) {
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out |= LerpChannel(from >> shift, to >> shift, t) << shift;
    }
    return out;
}

StyleValue Interpolate(const StyleValue& from, const StyleValue& to,
                       AnimKind kind, float t) {
    StyleValue v = from;
    switch (kind) {
    case AnimKind::Color:
        v.u = LerpColor(from.u, to.u, t);
        break;
    case AnimKind::Quad:
        // 四通道（Thickness 四边 / CornerRadii 四角）：各通道独立线性插值，
        // 共用同一条时间轴与缓动（CSS transition 的"同一属性一条时间轴"语义）
        v.f = from.f + (to.f - from.f) * t;
        for (int i = 0; i < 3; ++i)
            v.extra[i] = from.extra[i] + (to.extra[i] - from.extra[i]) * t;
        break;
    default:
        v.f = from.f + (to.f - from.f) * t;
        break;
    }
    return v;
}

bool AnyRunning(const std::vector<detail::AnimSlot>& anims) {
    for (const detail::AnimSlot& a : anims) {
        if (a.phase == dui::AnimPhase::Running) return true;
    }
    return false;
}

// 事件触发：回调**保留**（循环的每一轮、重播都要再次触发），只拷贝一份再调用，
// 避免回调里重新注册时把正在执行的 std::function 销毁掉。
void FireStarted(const std::shared_ptr<detail::AnimShared>& s) {
    if (!s || !s->lifetime || !s->onStarted) return;
    const std::function<void()> fn = s->onStarted;
    fn();
}

void FirePaused(const std::shared_ptr<detail::AnimShared>& s) {
    if (!s || !s->lifetime || !s->onPaused) return;
    const std::function<void()> fn = s->onPaused;
    fn();
}

void FireStopped(const std::shared_ptr<detail::AnimShared>& s) {
    if (!s || !s->lifetime || !s->onStopped) return;
    const std::function<void()> fn = s->onStopped;
    fn();
}

void FireFinished(const std::shared_ptr<detail::AnimShared>& s) {
    if (!s || !s->lifetime || !s->onFinished) return;
    const std::function<void()> fn = s->onFinished;
    fn();
}

// 推进期间产生的事件（延后到本元素本轮补间推进结束后按序派发）
enum class EvKind : uint8_t { Started, Finished };

struct DeferredEvent {
    EvKind kind = EvKind::Finished;
    std::shared_ptr<detail::AnimShared> shared;
};

// 掉帧跨过多轮时最多展开 4 轮事件（再多就折叠，避免回调风暴）
constexpr uint32_t kMaxLoopEventRounds = 4;

}  // namespace

// ---------------------------------------------------------------------------
// 注册表：统一 ticker 只遍历"有活动补间"的元素
// ---------------------------------------------------------------------------

namespace detail {

void RegisterAnimatedElement(Element* e) noexcept {
    for (const RegEntry& r : g_active) {
        if (r.element == e) return;
    }
    g_active.push_back(RegEntry{e, e->Lifetime()});
}

void UnregisterAnimatedElement(Element* e) noexcept {
    g_active.erase(
        std::remove_if(g_active.begin(), g_active.end(),
                       [e](const RegEntry& r) { return r.element == e; }),
        g_active.end());
}

bool HasActiveAnimations() noexcept {
    return !g_active.empty();
}

void TickAnimations(uint64_t nowMs) {
    // 快照：完成回调里可以启动/取消动画、甚至销毁元素，都不能打乱本次遍历
    const std::vector<RegEntry> snapshot = g_active;
    for (const RegEntry& r : snapshot) {
        if (r.lifetime) r.element->TickAnimations(nowMs);
    }
}

void CancelAnimation(const std::shared_ptr<AnimShared>& shared) noexcept {
    if (!shared || !shared->lifetime) return;
    if (shared->phase == AnimPhase::Cancelled) return;
    shared->phase = AnimPhase::Cancelled;
    shared->onStarted = nullptr;
    shared->onPaused = nullptr;
    shared->onStopped = nullptr;
    shared->onFinished = nullptr;

    Element* e = shared->element;
    e->anims_.erase(
        std::remove_if(e->anims_.begin(), e->anims_.end(),
                       [&shared](const AnimSlot& a) { return a.shared == shared; }),
        e->anims_.end());
    e->animated_.erase(
        std::remove_if(e->animated_.begin(), e->animated_.end(),
                       [&shared](const StyleValue& v) { return v.prop == shared->prop; }),
        e->animated_.end());
    e->MarkDirty(PropFlagsOf(shared->prop));
    if (!AnyRunning(e->anims_)) UnregisterAnimatedElement(e);
}

// ---------------------------------------------------------------------------
// 句柄发来的控制命令：Start / Pause / Continue / Stop / Loop
// ---------------------------------------------------------------------------
void ControlAnimation(const std::shared_ptr<AnimShared>& shared, AnimCommand cmd,
                      uint32_t times) noexcept {
    if (!shared || !shared->lifetime) return;
    Element* e = shared->element;

    // 找本句柄对应的槽：同 (元素, 属性) 被新动画替换后，旧槽已消失 → 句柄失效
    AnimSlot* slot = nullptr;
    for (AnimSlot& a : e->anims_) {
        if (a.shared == shared) { slot = &a; break; }
    }
    if (slot == nullptr) return;

    const uint64_t now = ::GetTickCount64();
    switch (cmd) {
    case AnimCommand::Start:
    case AnimCommand::Loop: {
        // Start = 从头重播（时间轴归零、回到动画起点）；Loop = 从头重播 + 循环
        if (slot->phase == AnimPhase::Cancelled) return;
        if (cmd == AnimCommand::Loop) {
            slot->looping = true;
            slot->loopTotal = times;          // 0 = 无限循环
            slot->loopRemaining = times;
        } else if (slot->looping) {
            slot->loopRemaining = slot->loopTotal;   // 重播：轮数重新计数
        }
        if (slot->phase == AnimPhase::Idle) {
            slot->from = ResolveProperty(e, slot->prop);   // 首次启动：起点取当前显示值
        }
        slot->elapsedMs = 0;
        slot->startMs = now;
        slot->phase = AnimPhase::Running;
        shared->phase = AnimPhase::Running;
        // 立刻把值写回起点：重播时避免先闪一下上一轮的终值
        e->SetAnimatedValue(slot->from);
        RegisterAnimatedElement(e);
        FireStarted(shared);
        break;
    }

    case AnimCommand::Pause:
        if (slot->phase != AnimPhase::Running) return;
        slot->elapsedMs += now - slot->startMs;         // 冻结已推进时长
        slot->phase = AnimPhase::Paused;
        shared->phase = AnimPhase::Paused;
        if (!AnyRunning(e->anims_)) UnregisterAnimatedElement(e);   // 停表
        FirePaused(shared);
        break;

    case AnimCommand::Continue:
        if (slot->phase == AnimPhase::Paused) {
            slot->startMs = now;                        // 从断点继续（累计时长不变）
            slot->phase = AnimPhase::Running;
            shared->phase = AnimPhase::Running;
            RegisterAnimatedElement(e);
            FireStarted(shared);
        } else if (slot->phase == AnimPhase::Idle) {
            ControlAnimation(shared, AnimCommand::Start);   // 未启动 → 等同 Start
        }
        // 已是终态（Completed / Stopped / Cancelled）→ 不再执行
        break;

    case AnimCommand::Stop: {
        if (slot->phase == AnimPhase::Completed ||
            slot->phase == AnimPhase::Stopped ||
            slot->phase == AnimPhase::Cancelled) return;
        // 立即落到终值（fill forwards：终值留在 animated_）
        e->SetAnimatedValue(slot->to);
        slot->phase = AnimPhase::Stopped;
        shared->phase = AnimPhase::Stopped;
        slot->looping = false;                          // Stop 同时结束循环模式
        slot->loopRemaining = 0;
        if (!AnyRunning(e->anims_)) UnregisterAnimatedElement(e);
        FireStopped(shared);
        FireFinished(shared);   // 值已到终态 = 完成
        break;
    }
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Element：创建与推进
// ---------------------------------------------------------------------------

AnimationHandle Element::StartAnimation(Prop p, AnimKind kind, const StyleValue& to,
                                        const AnimConfig& cfg) {
    // 同 (元素, 属性) 唯一：旧动画（含已完成、仍保留终值者）整体作废，
    // 不触发其回调；新动画的起点在 Start() 时取当前显示值。
    for (auto it = anims_.begin(); it != anims_.end(); ++it) {
        if (it->prop != p) continue;
        if (it->shared) {
            it->shared->phase = AnimPhase::Cancelled;
            it->shared->onStarted = nullptr;
            it->shared->onPaused = nullptr;
            it->shared->onStopped = nullptr;
            it->shared->onFinished = nullptr;
        }
        anims_.erase(it);
        break;
    }

    auto shared = std::make_shared<detail::AnimShared>();
    shared->prop = p;
    shared->element = this;
    shared->lifetime = Lifetime();
    shared->phase = AnimPhase::Idle;      // 已创建，未启动

    detail::AnimSlot slot;
    slot.prop = p;
    slot.to = to;
    slot.kind = kind;
    slot.durationMs = cfg.durationMs != 0 ? cfg.durationMs : 1u;
    slot.easing = cfg.easing;
    slot.phase = AnimPhase::Idle;
    slot.shared = shared;

    anims_.push_back(std::move(slot));
    // 未启动 → 不注册进 ticker（没有活动补间就不空转）
    return AnimationHandle(std::move(shared));
}

void Element::TickAnimations(uint64_t nowMs) {
    std::vector<DeferredEvent> events;
    for (detail::AnimSlot& a : anims_) {
        if (a.phase != AnimPhase::Running) continue;
        if (a.startMs == 0) a.startMs = nowMs;   // 首拍 t = 0，从起点开始
        const uint64_t total = a.durationMs;     // 已保证 >= 1
        uint64_t elapsed = a.elapsedMs + (nowMs - a.startMs);
        uint64_t rounds = 0;                      // 本拍内完成的轮数
        if (elapsed >= total) {
            rounds = elapsed / total;             // 掉帧可能跨过多轮
            elapsed = elapsed % total;            // 当前轮内的位置
        }

        if (rounds == 0) {
            float t = static_cast<float>(elapsed) / static_cast<float>(total);
            if (t > 1.f) t = 1.f;
            SetAnimatedValue(Interpolate(a.from, a.to, a.kind, ApplyEasing(a.easing, t)));
            continue;
        }

        // 本拍内至少跑完一轮：判断还有没有下一轮
        const bool more = a.looping && (a.loopRemaining == 0 || a.loopRemaining > rounds);
        if (!more) {
            // 最后一轮 → 落到终值（fill forwards：终值留在 animated_）
            SetAnimatedValue(a.to);
            a.phase = AnimPhase::Completed;
            a.shared->phase = AnimPhase::Completed;
            events.push_back(DeferredEvent{EvKind::Finished, a.shared});
            continue;
        }

        // 还有下一轮：值回到起点，按余量重新插值；轮数计数递减
        // （more 的条件保证 loopRemaining > rounds，转换安全）
        if (a.loopRemaining != 0) a.loopRemaining -= static_cast<uint32_t>(rounds);
        const float t = static_cast<float>(elapsed) / static_cast<float>(total);
        SetAnimatedValue(Interpolate(a.from, a.to, a.kind, ApplyEasing(a.easing, t)));
        a.elapsedMs = elapsed;                     // 下一拍从当前轮内的位置继续
        a.startMs = nowMs;
        // 事件：每轮"结束 → OnFinished"，下一轮"开始 → OnStarted"
        const uint32_t emit = rounds < kMaxLoopEventRounds
                                  ? static_cast<uint32_t>(rounds)
                                  : kMaxLoopEventRounds;
        for (uint32_t i = 0; i < emit; ++i) {
            events.push_back(DeferredEvent{EvKind::Finished, a.shared});
            events.push_back(DeferredEvent{EvKind::Started, a.shared});
        }
    }

    if (!AnyRunning(anims_)) detail::UnregisterAnimatedElement(this);

    // 回调统一延后到本元素的推进结束之后，避免重入打乱 anims_ 遍历
    for (const DeferredEvent& ev : events) {
        if (!ev.shared || ev.shared->phase == AnimPhase::Cancelled) continue;
        if (ev.kind == EvKind::Finished) {
            FireFinished(ev.shared);
        } else {
            // 期间被 Stop / Cancel → 不再开始新一轮
            if (ev.shared->phase != AnimPhase::Running) continue;
            FireStarted(ev.shared);
        }
    }
}

void Element::SetAnimatedValue(const StyleValue& v) {
    bool found = false;
    for (StyleValue& cur : animated_) {
        if (cur.prop == v.prop) { cur = v; found = true; break; }
    }
    if (!found) animated_.push_back(v);
    MarkDirty(PropFlagsOf(v.prop));
}

// ---------------------------------------------------------------------------
// AnimationHandle：事件注册与控制命令
// ---------------------------------------------------------------------------

AnimationHandle AnimationHandle::OnStarted(std::function<void()> fn) {
    if (shared_ && shared_->lifetime && shared_->phase != AnimPhase::Cancelled) {
        if (shared_->phase == AnimPhase::Running) {
            if (fn) fn();                       // 已在推进：注册即通知
        }
        // 回调保留：循环的每一轮、重播都要再次触发
        shared_->onStarted = std::move(fn);
    }
    return *this;
}

AnimationHandle AnimationHandle::OnPaused(std::function<void()> fn) {
    if (shared_ && shared_->lifetime && shared_->phase != AnimPhase::Cancelled) {
        if (shared_->phase == AnimPhase::Paused) {
            if (fn) fn();                       // 已暂停：注册即通知
        }
        shared_->onPaused = std::move(fn);
    }
    return *this;
}

AnimationHandle AnimationHandle::OnStopped(std::function<void()> fn) {
    if (shared_ && shared_->lifetime && shared_->phase != AnimPhase::Cancelled) {
        if (shared_->phase == AnimPhase::Stopped) {
            if (fn) fn();                       // 已被 Stop：注册即通知
        }
        shared_->onStopped = std::move(fn);
    }
    return *this;
}

AnimationHandle AnimationHandle::OnFinished(std::function<void()> fn) {
    if (shared_ && shared_->lifetime && shared_->phase != AnimPhase::Cancelled) {
        if (shared_->phase == AnimPhase::Completed || shared_->phase == AnimPhase::Stopped) {
            if (fn) fn();                       // 已完成：注册即通知
        }
        shared_->onFinished = std::move(fn);
    }
    return *this;
}

void AnimationHandle::Start() noexcept {
    if (shared_) detail::ControlAnimation(shared_, detail::AnimCommand::Start);
}

void AnimationHandle::Pause() noexcept {
    if (shared_) detail::ControlAnimation(shared_, detail::AnimCommand::Pause);
}

void AnimationHandle::Continue() noexcept {
    if (shared_) detail::ControlAnimation(shared_, detail::AnimCommand::Continue);
}

void AnimationHandle::Stop() noexcept {
    if (shared_) detail::ControlAnimation(shared_, detail::AnimCommand::Stop);
}

void AnimationHandle::Loop(uint32_t times) noexcept {
    if (shared_) detail::ControlAnimation(shared_, detail::AnimCommand::Loop, times);
}

void AnimationHandle::Cancel() noexcept {
    detail::CancelAnimation(shared_);
}

AnimPhase AnimationHandle::Phase() const noexcept {
    if (!shared_ || !shared_->lifetime) return AnimPhase::Cancelled;
    return shared_->phase;
}

bool AnimationHandle::IsFinished() const noexcept {
    const AnimPhase p = Phase();
    return p == AnimPhase::Completed || p == AnimPhase::Stopped ||
           p == AnimPhase::Cancelled;
}

}  // namespace dui
