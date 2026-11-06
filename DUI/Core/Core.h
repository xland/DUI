// ============================================================================
//  DUI/Core/Core.h — 无平台依赖的基础类型（01-Layering §2 Core）
//
//  约束（ADR-007 / 00-Overview §3）：
//    · 无异常：错误经 Result<T> 传递
//    · 无 RTTI：属性用整数 ID，值用 tagged union（本文件不涉及）
//    · 公开 API 边界用标准库容器/字符串（01-Layering Core.Contents）
// ============================================================================

#pragma once

#include <cmath>
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

// ---- 2D 仿射变换（Paint 级 transform；10-Rendering §6.3）------------------
//  Apply(x,y) = (a*x + c*y + tx, b*x + d*y + ty)；(a,b) / (c,d) 为 x / y 基向量像。
//  A * B 表示"先 B 后 A"，与函数复合 A(B(p)) 一致。默认构造 = 恒等变换。
//  只描述线性部分 + 平移（无透视）；DPI 与滚动原点的换算由 Graphics.Painter 处理。
struct Transform2D {
    float a = 1.f, b = 0.f;
    float c = 0.f, d = 1.f;
    float tx = 0.f, ty = 0.f;

    Vec2 Apply(Vec2 p) const noexcept {
        return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
    }

    Transform2D operator*(const Transform2D& r) const noexcept {
        return {a * r.a + c * r.b, b * r.a + d * r.b,
                a * r.c + c * r.d, b * r.c + d * r.d,
                a * r.tx + c * r.ty + tx, b * r.tx + d * r.ty + ty};
    }

    float Determinant() const noexcept { return a * d - b * c; }
    bool IsIdentity() const noexcept {
        return a == 1.f && b == 0.f && c == 0.f && d == 1.f && tx == 0.f && ty == 0.f;
    }
    bool IsInvertible() const noexcept { return std::abs(Determinant()) > 1e-6f; }

    Transform2D Inverse() const noexcept {
        const float det = Determinant();
        if (std::abs(det) <= 1e-6f) return {};   // 调用方须先查 IsInvertible
        Transform2D r{d / det, -b / det, -c / det, a / det, 0.f, 0.f};
        r.tx = -(r.a * tx + r.c * ty);
        r.ty = -(r.b * tx + r.d * ty);
        return r;
    }

    // 把 DIP 空间的矩阵共轭到像素空间：S_s · T · S_(1/s)。等比缩放与线性部分
    // 可交换，故只有平移需要乘 s（10-Rendering §6.3）。
    Transform2D Scaled(float s) const noexcept { return {a, b, c, d, tx * s, ty * s}; }

    static Transform2D Translation(float x, float y) noexcept {
        return {1.f, 0.f, 0.f, 1.f, x, y};
    }
    static Transform2D Scaling(float s) noexcept { return {s, 0.f, 0.f, s, 0.f, 0.f}; }
    // y 轴向下的屏幕坐标系：正角度 = 视觉顺时针
    static Transform2D Rotation(float deg) noexcept {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
        const float rad = deg * kDegToRad;
        const float cs = std::cos(rad), sn = std::sin(rad);
        return {cs, sn, -sn, cs, 0.f, 0.f};
    }
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
