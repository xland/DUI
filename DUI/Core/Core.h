// ============================================================================
//  DUI/Core/Core.h — 无平台依赖的基础类型（01-Layering §2 Core）
//
//  约束（ADR-007 / 00-Overview §3）：
//    · 无异常：错误经 Result<T> 传递
//    · 无 RTTI：属性用整数 ID，值用 tagged union（本文件不涉及）
//    · 公开 API 边界用标准库容器/字符串（01-Layering Core.Contents）
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace dui {

// ---- 字符串：UTF-16 为主（ADR-026；字面量 L"" 与 DWrite/Win32 零转码）----
using String = std::wstring;

// ---- 颜色：0xAARRGGBB，按 sRGB 直觉书写；管线全程预乘 Alpha（ADR-013）----
using Color = uint32_t;

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

struct Rect {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;

    float Right()  const noexcept { return x + w; }
    float Bottom() const noexcept { return y + h; }
};

// ---- 四边厚度（15-Properties §4.2；12-Windowing §7.1-4 的 Px 单位后续加入）----
//  聚合形式：{all} / {horizontal, vertical（CSS 顺序：第一个是垂直）} / {l, t, r, b}
struct Thickness {
    float left = 0.f;
    float top = 0.f;
    float right = 0.f;
    float bottom = 0.f;

    Thickness() = default;
    explicit Thickness(float all) noexcept
        : left(all), top(all), right(all), bottom(all) {}
    Thickness(float vertical, float horizontal) noexcept   // CSS 顺序：先上下后左右
        : left(horizontal), right(horizontal), top(vertical), bottom(vertical) {}
    Thickness(float l, float t, float r, float b) noexcept
        : left(l), top(t), right(r), bottom(b) {}
};

// ---- 无异常错误传递（ADR-007）：默认构造 = 失败/空 ----
template <class T>
class [[nodiscard]] Result {
public:
    Result() = default;
    Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : ok_(true), value_(std::move(value)) {}

    explicit operator bool() const noexcept { return ok_; }
    const T& Value() const noexcept { return value_; }
    T&       Value() noexcept { return value_; }

private:
    bool ok_ = false;
    T    value_{};
};

}  // namespace dui
