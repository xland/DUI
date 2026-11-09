// ============================================================================
//  DUI/UI/Icon.h — 内置矢量图标（框架自绘，不依赖字体 / 位图）
//  9 个图样全部由 Painter 形状指令绘制，归一化到元素盒，因此尺寸随
//  Style().Width/Height 自由变化，描边宽度按盒尺寸等比缩放。
// ============================================================================

#pragma once

#include "UI/Element.h"

namespace dui {

// 内置图标名称（强类型，编译期校验）
enum class IconName : uint8_t {
    None,
    Check, CheckChecked,
    Radio, RadioChecked,
    Minimize, Restore, Maximize, Close,
    Search,
};

// 底层矢量绘制：dipBox 为绝对 DIP 矩形，color 通常取自 Prop::Color。
// 供 Icon 控件与后续的 CheckButton / RadioButton 复用。
void DrawIcon(graphics::Painter& p, IconName name, const Rect& dipBox,
              Color color, float scale);

class Icon : public Element {
public:
    Icon() = default;
    explicit Icon(IconName name) : name_(name) {}

    IconName Name() const noexcept { return name_; }
    Icon& SetIcon(IconName name);

    void OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) override;

protected:
    // 不设 Width/Height 时的自然尺寸（DIP）；显式尺寸由布局覆盖
    Vec2 MeasureContent(const StyleContext&, const Vec2&) const override { return {16.f, 16.f}; }

private:
    IconName name_ = IconName::None;
};

}  // namespace dui
