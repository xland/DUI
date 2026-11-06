// ============================================================================
//  DUI/UI/Animation.cpp — 命令式属性动画（05-Animation）
//
//  · Element::Animate 启动补间，动画值写入 Element::animated_（级联第 1 层）
//  · 统一 ticker（AnimationClock）调 detail::TickAnimations 推进所有活动元素
//  · 句柄以 shared_ptr<AnimState> + Lifetime 令牌实现幂等取消与元素销毁失效
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
                       bool color, float t) {
    StyleValue v = from;
    if (color) v.u = LerpColor(from.u, to.u, t);
    else v.f = from.f + (to.f - from.f) * t;
    return v;
}

bool AnyRunning(const std::vector<detail::AnimSlot>& anims) {
    for (const detail::AnimSlot& a : anims) {
        if (a.running) return true;
    }
    return false;
}

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

void CancelAnimation(const std::shared_ptr<AnimState>& state) noexcept {
    if (!state || !state->lifetime || state->cancelled) return;
    state->cancelled = true;
    state->running = false;
    state->completed = false;
    state->onFinished = nullptr;

    Element* e = state->element;
    e->anims_.erase(
        std::remove_if(e->anims_.begin(), e->anims_.end(),
                       [&state](const AnimSlot& a) { return a.state == state; }),
        e->anims_.end());
    e->animated_.erase(
        std::remove_if(e->animated_.begin(), e->animated_.end(),
                       [&state](const StyleValue& v) { return v.prop == state->prop; }),
        e->animated_.end());
    e->MarkDirty(PropFlagsOf(state->prop));
    if (!AnyRunning(e->anims_)) UnregisterAnimatedElement(e);
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Element：启动与推进
// ---------------------------------------------------------------------------

AnimationHandle Element::StartAnimation(Prop p, bool color, const StyleValue& to,
                                        const AnimConfig& cfg) {
    // 同 (元素, 属性) 唯一：旧动画（含已完成、仍保留终值者）整体作废，
    // 不触发其完成回调；新动画的起点自然取到当前显示值。
    for (auto it = anims_.begin(); it != anims_.end(); ++it) {
        if (it->prop != p) continue;
        if (it->state) {
            it->state->running = false;
            it->state->cancelled = true;
            it->state->completed = false;
            it->state->onFinished = nullptr;
        }
        anims_.erase(it);
        break;
    }

    auto state = std::make_shared<detail::AnimState>();
    state->prop = p;
    state->element = this;
    state->lifetime = Lifetime();

    detail::AnimSlot slot;
    slot.prop = p;
    slot.from = ResolveProperty(this, p);   // 遍历外路径，含层 1 当前显示值
    slot.to = to;
    slot.color = color;
    slot.durationMs = cfg.durationMs != 0 ? cfg.durationMs : 1u;
    slot.easing = cfg.easing;
    slot.state = state;

    anims_.push_back(std::move(slot));
    detail::RegisterAnimatedElement(this);
    return AnimationHandle(std::move(state));
}

void Element::TickAnimations(uint64_t nowMs) {
    std::vector<std::shared_ptr<detail::AnimState>> finished;
    for (detail::AnimSlot& a : anims_) {
        if (!a.running) continue;
        if (a.startMs == 0) a.startMs = nowMs;   // 首拍 t = 0，从起点开始
        const uint64_t elapsed = nowMs - a.startMs;
        float t = static_cast<float>(elapsed) / static_cast<float>(a.durationMs);
        if (t > 1.f) t = 1.f;

        const StyleValue v = Interpolate(a.from, a.to, a.color, ApplyEasing(a.easing, t));
        bool found = false;
        for (StyleValue& cur : animated_) {
            if (cur.prop == a.prop) { cur = v; found = true; break; }
        }
        if (!found) animated_.push_back(v);
        MarkDirty(PropFlagsOf(a.prop));

        if (t >= 1.f) {
            a.running = false;                  // 终值保留在 animated_（fill forwards）
            a.state->running = false;
            a.state->completed = true;
            finished.push_back(a.state);
        }
    }

    if (!AnyRunning(anims_)) detail::UnregisterAnimatedElement(this);

    // 回调统一延后到本元素的推进结束之后，避免重入打乱 anims_ 遍历
    for (const auto& state : finished) {
        if (state->cancelled || !state->lifetime || !state->onFinished) continue;
        auto fn = std::move(state->onFinished);
        state->onFinished = nullptr;
        fn();
    }
}

// ---------------------------------------------------------------------------
// AnimationHandle
// ---------------------------------------------------------------------------

AnimationHandle& AnimationHandle::OnFinished(std::function<void()> fn) {
    if (!state_ || !state_->lifetime || state_->cancelled) return *this;
    if (state_->running) {
        state_->onFinished = std::move(fn);
    } else if (state_->completed && fn) {
        fn();   // 已正常完成：注册即通知
    }
    return *this;
}

void AnimationHandle::Cancel() noexcept {
    detail::CancelAnimation(state_);
}

}  // namespace dui
