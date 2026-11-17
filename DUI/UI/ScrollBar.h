// ============================================================================
//  DUI/UI/ScrollBar.h — 滚动条几何与配色辅助（UI 层内部共享，不进公共头 DUI.h）
//
//  谁在用：
//    · Scroller           —— 容器滚动（纵向 / 横向）
//    · TextBox / TextArea —— 自绘文本的内部滚动（M5.74）
//  为什么抽出来：两者的轨道 / 滑块几何与配色规则完全一致（同一套样式属性
//  ScrollbarTrack* / ScrollbarThumb*），放一处才不会走偏。
//
//  这里只放**纯几何**（输入 → 输出矩形）与配色辅助；滚动偏移、拖拽状态机
//  留在各自控件里（Scroller 有锚点表与元素级滚动，TextBox 有光标跟随可见）。
//
//  外观约定（与 Scroller 一致）：滚动条**浮在内容之上**（overlay），不占布局
//  宽度 —— 内容盒宽度不因滚动条出现而变，故不会触发"滚动条出现 → 文字重排 →
//  滚动条消失"的布局抖动。
// ============================================================================
#pragma once

#include <algorithm>

#include "Core/Core.h"

namespace dui {
namespace sb {

// 滑块最小长度（DIP）：太短就点不中
constexpr float kMinThumb = 28.f;

// 滑块悬停加深：只提 alpha（RGB 不变）
constexpr float kThumbHoverBoost = 1.6f;

inline Color BoostAlpha(Color c, float k) {
    const uint32_t a = (c >> 24) & 0xFFu;
    const uint32_t na = std::min<uint32_t>(255u, static_cast<uint32_t>(a * k + 0.5f));
    return (c & 0x00FFFFFFu) | (na << 24);
}

// 纵向轨道：贴视口右缘，纵向铺满视口高（不留内缩间隙）
inline Rect VerticalTrackRect(const Rect& viewport, float thickness) {
    return Rect{viewport.Right() - thickness, viewport.y, thickness, std::max(0.f, viewport.h)};
}

// 横向轨道：贴视口下缘，横向铺满视口宽
inline Rect HorizontalTrackRect(const Rect& viewport, float thickness) {
    return Rect{viewport.x, viewport.Bottom() - thickness, std::max(0.f, viewport.w), thickness};
}

// 纵向滑块：长度 = 轨道长 × (视口长 / 内容长)，位置按滚动偏移比例分布
inline Rect VerticalThumbRect(const Rect& track, float thumbThickness,
                               float contentLen, float viewportLen, float offset) {
    const float max = std::max(0.f, contentLen - viewportLen);
    if (max <= 0.f || track.h <= 0.f) return track;
    const float ratio = viewportLen / std::max(1.f, contentLen);
    const float len = std::clamp(track.h * ratio, std::min(kMinThumb, track.h), track.h);
    const float pos = track.y + (offset / max) * (track.h - len);
    return Rect{track.x + (track.w - thumbThickness) * 0.5f, pos, thumbThickness, len};
}

// 横向滑块（与纵向对称）
inline Rect HorizontalThumbRect(const Rect& track, float thumbThickness,
                                 float contentLen, float viewportLen, float offset) {
    const float max = std::max(0.f, contentLen - viewportLen);
    if (max <= 0.f || track.w <= 0.f) return track;
    const float ratio = viewportLen / std::max(1.f, contentLen);
    const float len = std::clamp(track.w * ratio, std::min(kMinThumb, track.w), track.w);
    const float pos = track.x + (offset / max) * (track.w - len);
    return Rect{pos, track.y + (track.h - thumbThickness) * 0.5f, len, thumbThickness};
}

}  // namespace sb
}  // namespace dui
