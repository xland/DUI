// ============================================================================
//  DUI/UI/Style.h — 样式核心：属性、状态、样式值、级联解析、StyleBuilder
//  （03-Style / 15-Properties 的运行时形态；解析实现在 UI/StyleResolve.cpp，
//   Style/StyleBuilder 实现在 UI/Style.cpp）
//
//  已定案并在此落形的关键契约：
//    · 静态外观（含布局属性）全部在 Style 里；Set(Prop) 只承载运行时动态值
//    · 用一次的样式内联（StyleBuilder 右值），共享/带变体的才起名
//    · 合并优先级 disabled > pressed > selected > focused > hover（03-Style §6.2）
// ============================================================================

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "Core/Core.h"

namespace dui {

// ---------------------------------------------------------------------------
// 属性与状态（15-Properties 属性主表的运行时形态；M3 引入编译期属性表）
// ---------------------------------------------------------------------------

enum class Prop : uint16_t {
    Unknown = 0,
    // 文本与字体
    Color, FontSize, FontWeight, LineHeight, TextAlign, FontFamily,
    // 盒模型
    Padding, Margin, CornerRadius, Opacity, BackgroundColor,
    // 尺寸
    Width, Height, MinWidth, MaxWidth,
    // 布局
    Display, FlexDirection, FlexShrink, FlexGrow, Gap, JustifyContent, AlignItems,
    // 滚动条（dui::Scroller 自绘轨道/滑块；宽度 = 纵向条厚，高度 = 横向条厚）
    ScrollbarTrackWidth, ScrollbarTrackHeight,
    ScrollbarTrackBackgroundColor, ScrollbarTrackCornerRadius,
    ScrollbarTrackBorderColor, ScrollbarTrackBorderWidth,
    ScrollbarThumbWidth, ScrollbarThumbHeight,
    ScrollbarThumbBackgroundColor, ScrollbarThumbCornerRadius,
    ScrollbarThumbBorderColor, ScrollbarThumbBorderWidth,
};

// 状态位（03-Style §6.2 已定全集）；disabled/selected 等写入方见文档
enum class State : uint32_t {
    Hover     = 1u << 0,
    Pressed   = 1u << 1,
    Focused   = 1u << 2,
    Disabled  = 1u << 3,
    Selected  = 1u << 4,
    ReadOnly  = 1u << 5,
    Invalid   = 1u << 6,
    Open      = 1u << 7,
};

enum class Display : uint8_t { Flex, Grid, None };
enum class FlexDirection : uint8_t { Row, Column };
enum class Justify : uint8_t { Start, Center };             // 主轴对齐（04-Layout §4.1，MVP 子集）
enum class Align : uint8_t { Stretch, Start, Center };      // 交叉轴对齐（默认 Stretch）
enum class FontWeight : uint8_t { Normal, Medium, SemiBold, Bold };
enum class Easing : uint8_t { Linear, EaseIn, EaseOut, EaseInOut };

// 样式值：prop + 主值（f/u）+ Thickness 扩展槽。
// 注：15-Properties §2 原案 StyleValue 为 8B 定长；M3 为内嵌四边 Thickness
// 扩到 20B（换 12B 变长 Decl 形态列为后续优化，03-Style §8）。
struct StyleValue {
    Prop     prop = Prop::Unknown;
    float    f = 0.f;        // 数值类主值；Thickness 的 left
    uint32_t u = 0;          // 枚举 / 颜色
    float    extra[3] = {};  // Thickness 的 top / right / bottom
    const wchar_t* family = nullptr;   // FontFamily：零拷贝共享，生命周期由调用方保证（建议字面量）
};

inline Thickness ValueAsThickness(const StyleValue& v) noexcept {
    return {v.f, v.extra[0], v.extra[1], v.extra[2]};
}

struct TransitionSpec {
    Prop     prop = Prop::Unknown;
    uint32_t durationMs = 0;
    Easing   easing = Easing::EaseOut;
};

// ---------------------------------------------------------------------------
// Style：不可变、可共享、可派生（03-Style §2）。构建期由 StyleBuilder 生成。
// ---------------------------------------------------------------------------
class Element;

class Style {
public:
    Style() = default;

    // 沿派生链由近及远查找（03-Style §5 第 4 层）；找不到返回 nullptr
    const StyleValue* Find(Prop p) const;

    // 状态变体（同一状态只保留一份 Decl，后设覆盖先设）
    const Style* StateVariant(State s) const;

    const Style* Parent() const noexcept { return parent_; }
    const char*  Name()   const noexcept { return name_; }

    // 遍历外读生效值（15-Properties §5.2；M3 接入动画槽与本地覆盖层）
    static Color Resolve(const Element* element, Prop p);

private:
    friend class StyleBuilder;
    friend class Element;     // SetState 需读状态变体的声明集计算失效标志

    std::vector<StyleValue>               decls_;
    std::vector<std::pair<State, Style>>  states_;
    std::vector<TransitionSpec>           transitions_;
    const Style* parent_ = nullptr;       // 派生基（生命周期由使用者保证为静态常量）
    const char*  name_ = nullptr;         // 注册表键 / 调试标识；匿名样式为 nullptr
};

// ---------------------------------------------------------------------------
// 级联解析与失效（实现在 UI/StyleResolve.cpp；03-Style §5 / 15-Properties §5）
// ---------------------------------------------------------------------------

// 遍历期继承态（15-Properties §5.1）：布局/绘制自上而下遍历时随递归下传
struct StyleContext {
    Color    color      = 0xFF000000;
    float    fontSize   = 14.f;
    float    lineHeight = 1.2f;
    uint32_t fontWeight = 0;
    const wchar_t* family = nullptr;   // 继承字体族；nullptr = 引擎默认（微软雅黑）

    // 进入元素 e 时计算子级继承态（继承属性：自身链命中则用，否则透传父值）
    StyleContext Child(const Element* e) const;
};

// 失效标志（15-Properties §3；M3 将 Text 并入 Layout）
enum PropFlags : uint32_t {
    kFlagNone   = 0,
    kFlagLayout = 1u << 0,
    kFlagPaint  = 1u << 1,
    kFlagText   = 1u << 2,
};

// 层 2/3/4（状态变体 → 本地覆盖 → Style 派生链）
bool TryResolveElement(const Element* e, Prop p, StyleValue& out);
// 层 2..5（TryResolveElement + 继承属性的父计算值）
bool TryResolve(const Element* e, Prop p, const StyleContext& ctx, StyleValue& out);
// 遍历内完整解析（层 2..7，含内建默认）
StyleValue ResolveProperty(const Element* e, Prop p, const StyleContext& ctx);
// 遍历外完整解析（继承属性向上找最近设置者，15-Properties §5.2）
StyleValue ResolveProperty(const Element* e, Prop p);
StyleValue BuiltinDefault(Prop p);
uint32_t PropFlagsOf(Prop p);

// ---------------------------------------------------------------------------
// StyleBuilder：链式构造，Build() 一次固化。When 的参数是 Builder 而非 Style
// （变体只是一份差异集，不注册、不被引用，见 03-Style §6.2）。
// ---------------------------------------------------------------------------
class StyleBuilder {
public:
    StyleBuilder() = default;
    explicit StyleBuilder(const Style& base) noexcept { style_.parent_ = &base; }  // 派生：只存差异
    explicit StyleBuilder(const char* name) noexcept { style_.name_ = name; }      // 具名：注册表键

    // 布局（静态外观的一部分，与视觉属性同层）
    // 注：参数类型写全限定 dui::Xxx —— 方法名与枚举名同名，类作用域内会被成员名遮蔽
    StyleBuilder& Display(dui::Display v);
    StyleBuilder& FlexDirection(dui::FlexDirection v);
    StyleBuilder& Width(float dip);
    StyleBuilder& Height(float dip);
    StyleBuilder& MinWidth(float dip);
    StyleBuilder& MaxWidth(float dip);
    StyleBuilder& FlexGrow(float v);
    StyleBuilder& FlexShrink(float v);
    StyleBuilder& JustifyContent(dui::Justify v);       // 主轴：剩余空间分配
    StyleBuilder& AlignItems(dui::Align v);             // 交叉轴：子项对齐

    // 视觉与文本
    StyleBuilder& Background(dui::Color c);
    StyleBuilder& Color(dui::Color c);
    StyleBuilder& FontSize(float dip);
    StyleBuilder& FontWeight(dui::FontWeight v);
    StyleBuilder& FontFamily(const wchar_t* name);      // 系统已安装字体族；指针需存活到样式使用期
    StyleBuilder& LineHeight(float multiple);
    StyleBuilder& Opacity(float v);                     // 0..1
    StyleBuilder& Padding(const Thickness& t);
    StyleBuilder& Padding(float all);                   // 四边同值
    StyleBuilder& Gap(float dip);
    StyleBuilder& CornerRadius(float dip);              // 四边同值
    StyleBuilder& CornerRadius(const Thickness& t);

    // 滚动条轨道 / 滑块（dui::Scroller 自绘部分）
    // 宽度 = 纵向条厚度，高度 = 横向条厚度；默认厚 10 DIP、直角、无边框
    StyleBuilder& TrackWidth(float dip);
    StyleBuilder& TrackHeight(float dip);
    StyleBuilder& TrackBackground(dui::Color c);
    StyleBuilder& TrackCornerRadius(float dip);
    StyleBuilder& TrackBorderColor(dui::Color c);
    StyleBuilder& TrackBorderWidth(float dip);
    StyleBuilder& ThumbWidth(float dip);
    StyleBuilder& ThumbHeight(float dip);
    StyleBuilder& ThumbBackground(dui::Color c);
    StyleBuilder& ThumbCornerRadius(float dip);
    StyleBuilder& ThumbBorderColor(dui::Color c);
    StyleBuilder& ThumbBorderWidth(float dip);

    // 动画与状态变体（05-Animation §3 / 03-Style §6.2）
    StyleBuilder& Transition(Prop prop, uint32_t durationMs, Easing easing);
    StyleBuilder& When(State s, const StyleBuilder& variant);

    Style Build() const { return style_; }

private:
    StyleValue& Ensure(Prop p);
    StyleBuilder& SetU(Prop p, uint32_t u);
    StyleBuilder& SetF(Prop p, float f);

    Style style_;
};

}  // namespace dui
