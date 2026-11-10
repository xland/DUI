// ============================================================================
//  DUI/UI/Style.h — 样式：属性、状态、级联解析与链式构造（合一的 Style）
//  （03-Style / 15-Properties 的运行时形态；解析实现在 UI/StyleResolve.cpp，
//   Style 链式方法实现在 UI/Style.cpp）
//
//  已定案并在此落形的关键契约：
//    · 静态外观（含布局属性）全部在 Style 里；Set(Prop) 只承载运行时动态值
//    · 用一次的样式内联（dui::Style() 链式临时），需要复用的才起名
//    · 状态变体归 Element::When（03-Style §6.2）：Style 是纯静态声明集，
//      不响应运行时状态位
// ============================================================================

#pragma once

#include <cstdint>
#include <initializer_list>
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
    // 文本编辑（TextBox/TextArea）：选区底色 / 光标色
    SelectionColor, CaretColor,
    // 指针形态（鼠标光标；语义同 CSS 的 cursor）
    Cursor,
    // 盒模型
    Padding, Margin, CornerRadius, Opacity, BackgroundColor,
    // 尺寸
    Width, Height, MinWidth, MaxWidth,
    // Paint 级变换（不改布局盒；10-Rendering §6.3）：平移 DIP / 旋转度 / 等比倍率
    TranslationX, TranslationY, Rotation, Scale,
    // 布局
    Display, FlexDirection, FlexShrink, FlexGrow, Gap, JustifyContent, AlignItems,
    // 布局（续）
    FlexWrap,
    // 滚动条（dui::Scroller 自绘轨道/滑块；宽度 = 纵向条厚，高度 = 横向条厚）
    ScrollbarTrackWidth, ScrollbarTrackHeight,
    ScrollbarTrackBackgroundColor, ScrollbarTrackCornerRadius,
    ScrollbarTrackBorderColor, ScrollbarTrackBorderWidth,
    ScrollbarThumbWidth, ScrollbarThumbHeight,
    ScrollbarThumbBackgroundColor, ScrollbarThumbCornerRadius,
    ScrollbarThumbBorderColor, ScrollbarThumbBorderWidth,
    // 定位（04-Layout §4.3）：追加在表尾，不重排已有属性 ID
    Position, Left, Top, Right, Bottom, ZIndex,
    // Grid（04-Layout §4.2）：轨道列表是变长声明，实体存 Style（见 FindTracks）；
    // 这两个 Prop 只作名义声明 / 失效标志，另四个是标量放置属性。
    GridTemplateColumns, GridTemplateRows, GridRow, GridColumn, GridRowSpan, GridColumnSpan,
    // 阴影（box-shadow 子集；10-Rendering）：f = blur(DIP)，extra[0/1] = offsetX/Y(DIP)，
    // u = 颜色 ARGB（0 = 无阴影）。只投元素边框盒，走 Shape 批 kind=3 的 SDF 衰减带。
    Shadow,
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

// Grid 轨道（04-Layout §4.2）：结构化定义，不做 CSS 字符串解析。
// value 语义：Fixed = DIP，Flex = 份数，Percent = 百分数（0..100），Auto 忽略。
enum class GridTrackKind : uint8_t { Fixed, Flex, Auto, Percent };
struct GridTrack {
    GridTrackKind kind  = GridTrackKind::Auto;
    float         value = 0.f;
};
inline GridTrack Fixed(float dip)        { return {GridTrackKind::Fixed, dip}; }
inline GridTrack Flex(float shares)      { return {GridTrackKind::Flex, shares}; }
inline GridTrack Percent(float percent)  { return {GridTrackKind::Percent, percent}; }
inline constexpr GridTrack Auto{GridTrackKind::Auto, 0.f};
enum class FlexDirection : uint8_t { Row, Column };
enum class FlexWrap : uint8_t { NoWrap, Wrap };              // 主轴放不下时换行（仅 Row；04-Layout §4.1 MVP）
enum class Position : uint8_t { Static, Relative, Absolute, Fixed };   // 04-Layout §4.3
enum class Justify : uint8_t { Start, Center };             // 主轴对齐（04-Layout §4.1，MVP 子集）
enum class Align : uint8_t { Stretch, Start, Center };      // 交叉轴对齐（默认 Stretch）
enum class FontWeight : uint8_t { Normal, Medium, SemiBold, Bold };
// dui::FontWeight（枚举序号 0-3）→ DWRITE_FONT_WEIGHT（100-1000）。
// 传给文本引擎的字重一律先过此函数：直接把 1/2/3 塞给 DWrite 属非法值，
// 就近匹配会选中家族里最细的 Light/Thin 面 → 文字全局偏细偏灰。
inline uint32_t DWriteWeight(uint32_t fontWeight) {
    switch (fontWeight) {
    case 1:  return 500;   // Medium
    case 2:  return 600;   // SemiBold
    case 3:  return 700;   // Bold
    default: return 400;   // Normal / 未设置
    }
}
// 文本在内容盒内的水平对齐（15-Properties §4.1）；继承属性，默认 Start
enum class TextAlign : uint8_t { Start, Center, End };
enum class Easing : uint8_t { Linear, EaseIn, EaseOut, EaseInOut };   // AnimConfig::easing（05-Animation §4）

// 鼠标光标形态（语义同 CSS 的 cursor；仅系统内置形态）
enum class Cursor : uint8_t {
    Arrow, IBeam, Hand, Wait, Crosshair, Move, SizeAll, NotAllowed, Help,
};

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

// 四边厚度类属性：f = left，extra = top/right/bottom。运行时 Set 传标量时
// 按四边同值展开，与 Style::Padding(float) / Margin(float) / CornerRadius(float) 一致。
// 注：CornerRadius 当前绘制（Frame.cpp::DrawBackground）只读 .f 作统一圆角，
// extra 三边暂未消费；四角独立半径需 Graphics 层支持，不属本次范围。
inline bool IsThicknessProp(Prop p) noexcept {
    return p == Prop::Padding || p == Prop::CornerRadius || p == Prop::Margin;
}

// ---------------------------------------------------------------------------
// Style：一份样式声明集，既是构建产物也是构建器——链式方法返回 *this，
// `dui::Style().Gap(20.f)` 的临时可直接传给 SetStyle / AddChild（按值拷贝持有）。
// 也可起名 / 派生后作为静态常量共享（03-Style §2）。
// ---------------------------------------------------------------------------
class Element;

class Style {
public:
    Style() = default;
    explicit Style(const char* name) noexcept { name_ = name; }   // 具名：注册表键 / 调试标识

    // 派生：只存与基样式的差异（解析沿 parent_ 链回溯）。不用构造函数表达——
    // Style(const Style&) 必须留给拷贝构造
    static Style Derive(const Style& base) noexcept {
        Style s;
        s.parent_ = &base;
        return s;
    }

    // 沿派生链由近及远查找（03-Style §5 第 4 层）；找不到返回 nullptr
    const StyleValue* Find(Prop p) const;

    const Style* Parent() const noexcept { return parent_; }
    const char*  Name()   const noexcept { return name_; }

    // 遍历外读生效值（15-Properties §5.2；含级联第 1 层动画值）
    static Color Resolve(const Element* element, Prop p);

    // ------------------------------------------------------------------
    // 链式构造（参数类型写全限定 dui::Xxx —— 方法名与枚举名同名，
    // 类作用域内会被成员名遮蔽）
    // ------------------------------------------------------------------

    // 布局（静态外观的一部分，与视觉属性同层）
    Style& Display(dui::Display v);
    Style& FlexDirection(dui::FlexDirection v);
    Style& Width(float dip);
    Style& Height(float dip);
    // Paint 级变换（不改布局盒；度 / 倍率 / DIP）
    Style& TranslationX(float dip);
    Style& TranslationY(float dip);
    Style& Rotation(float deg);
    Style& Scale(float v);
    Style& MinWidth(float dip);
    Style& MaxWidth(float dip);
    Style& FlexGrow(float v);
    Style& FlexShrink(float v);
    Style& JustifyContent(dui::Justify v);       // 主轴：剩余空间分配
    Style& AlignItems(dui::Align v);             // 交叉轴：子项对齐
    Style& FlexWrap(dui::FlexWrap v);            // 主轴放不下时断行（仅 Row）
    // Grid（04-Layout §4.2）：轨道列表是变长声明，实体存本对象内，
    // FindTracks 沿派生链由近及远找第一个非空定义。
    Style& GridTemplateColumns(std::initializer_list<GridTrack> tracks);
    Style& GridTemplateRows(std::initializer_list<GridTrack> tracks);
    const std::vector<GridTrack>* FindTracks(Prop p) const noexcept;
    // 定位（04-Layout §4.3）：Left/Top/Right/Bottom 单位 DIP，未设的一边 = Auto
    Style& Position(dui::Position v);
    Style& Left(float dip);
    Style& Top(float dip);
    Style& Right(float dip);
    Style& Bottom(float dip);
    // zIndex：同一 CB 内的 Absolute 按值升序绘制（同值按树序）；默认 0
    Style& ZIndex(int32_t v);

    // 视觉与文本
    Style& Background(dui::Color c);
    Style& Color(dui::Color c);
    Style& FontSize(float dip);
    Style& FontWeight(dui::FontWeight v);
    Style& FontFamily(const wchar_t* name);      // 系统已安装字体族；指针需存活到样式使用期
    Style& LineHeight(float multiple);
    Style& TextAlign(dui::TextAlign v);          // 内容盒内水平对齐（继承属性）
    Style& Opacity(float v);                     // 0..1
    // 文本编辑（TextBox/TextArea）
    Style& SelectionColor(dui::Color c);        // 选区底色（默认半透明蓝）
    Style& CaretColor(dui::Color c);            // 光标色（默认取文本色）
    // 指针形态（鼠标光标；语义同 CSS 的 cursor）
    // 生效范围 = 指针位于该元素上时；命中链就近者胜（未指定则沿链取父级的，
    // 与 CSS 的 cursor 继承同效）。不需要 hover 变体：指针不在元素上时，
    // 任何光标形态都不可见。
    Style& Cursor(dui::Cursor v);
    Style& Padding(const Thickness& t);
    Style& Padding(float all);                   // 四边同值
    Style& Margin(const Thickness& t);           // 外边距：占位但不属于元素盒（04-Layout §3）
    Style& Margin(float all);                    // 四边同值
    Style& Gap(float dip);
    Style& CornerRadius(float dip);              // 四边同值
    Style& CornerRadius(const Thickness& t);
    // 阴影（box-shadow 子集）：blur = 模糊半径 DIP，color 含 alpha（0 = 无阴影），
    // offsetX/Y = 偏移 DIP。只投元素边框盒（圆角矩形），SDF 衰减带近似高斯；
    // 不支持 inset / spread / 多阴影，也不随元素 Opacity 变化。
    Style& Shadow(float blurDip, dui::Color color, float offsetXDip = 0.f, float offsetYDip = 0.f);

    // 滚动条轨道 / 滑块（dui::Scroller 自绘部分）
    // 宽度 = 纵向条厚度，高度 = 横向条厚度；默认厚 10 DIP、直角、无边框
    Style& TrackWidth(float dip);
    Style& TrackHeight(float dip);
    Style& TrackBackground(dui::Color c);
    Style& TrackCornerRadius(float dip);
    Style& TrackBorderColor(dui::Color c);
    Style& TrackBorderWidth(float dip);
    Style& ThumbWidth(float dip);
    Style& ThumbHeight(float dip);
    Style& ThumbBackground(dui::Color c);
    Style& ThumbCornerRadius(float dip);
    Style& ThumbBorderColor(dui::Color c);
    Style& ThumbBorderWidth(float dip);

private:
    friend class Element;     // StateVariantFlags 需读状态变体声明集计算失效标志

    StyleValue& Ensure(Prop p);
    Style& SetU(Prop p, uint32_t u);
    Style& SetF(Prop p, float f);

    std::vector<StyleValue>               decls_;
    std::vector<GridTrack>                gridCols_;   // 04-Layout §4.2：变长轨道，随 Style 值拷贝
    std::vector<GridTrack>                gridRows_;
    const Style* parent_ = nullptr;       // 派生基（生命周期由使用者保证为静态常量）
    const char*  name_ = nullptr;         // 注册表键 / 调试标识；匿名样式为 nullptr
};

// ---------------------------------------------------------------------------
// 级联解析与失效（实现在 UI/StyleResolve.cpp；03-Style §5 / 15-Properties §5）
// ---------------------------------------------------------------------------

// Grid 轨道列表（04-Layout §4.2）：TrackList 是变长声明，不走 StyleValue；
// 沿 Style 派生链找第一个非空定义（不可动画、无状态变体、无本地覆盖）。
const std::vector<GridTrack>* GridTracksOf(const Element* e, Prop p);

// 遍历期继承态（15-Properties §5.1）：布局/绘制自上而下遍历时随递归下传
struct StyleContext {
    Color    color      = 0xFF000000;
    float    fontSize   = 14.f;
    float    lineHeight = 1.2f;
    uint32_t fontWeight = 0;
    uint32_t textAlign = 0;            // 继承的 dui::TextAlign（默认 Start）
    const wchar_t* family = nullptr;   // 继承字体族；nullptr = 引擎默认（微软雅黑）
    float    scale = 1.f;   // 遍历期 DPI 缩放（图像类控件据此把像素尺寸换算为 DIP）

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

// 层 1..4（动画值 → 状态变体 → 本地覆盖 → Style 派生链）
bool TryResolveElement(const Element* e, Prop p, StyleValue& out);
// 层 1..5（TryResolveElement + 继承属性的父计算值）
bool TryResolve(const Element* e, Prop p, const StyleContext& ctx, StyleValue& out);
// 遍历内完整解析（层 2..7，含内建默认）
StyleValue ResolveProperty(const Element* e, Prop p, const StyleContext& ctx);
// 遍历外完整解析（继承属性向上找最近设置者，15-Properties §5.2）
StyleValue ResolveProperty(const Element* e, Prop p);
StyleValue BuiltinDefault(Prop p);
uint32_t PropFlagsOf(Prop p);
// 脱离文档流（04-Layout §4.3）：布局 / 绘制 / 命中三处共用同一判断
bool IsOutOfFlow(const Element* e);
bool IsFixed(const Element* e);
// 布局期统计的 Fixed 元素数：命中测试据此短路窗口级 Fixed 预扫（零额外遍历）
void ResetFixedCount() noexcept;
void NoteFixed() noexcept;
uint32_t FixedCount() noexcept;
// 最近定位祖先（04-Layout §4.3）：沿 Parent 链找第一个 position != Static 的祖先，
// 找不到时退回根（初始包含块 = 窗口客户区）。
const Element* ContainingBlockOf(const Element* e);
// 布局期把每个 Absolute 登记到它的包含块名下，绘制 / 命中在包含块处统一消费。
void ResetAbsoluteAnchors() noexcept;
void NoteAbsoluteAnchor(const Element* cb, Element* e);
uint32_t AbsoluteCount() noexcept;
// zIndex（04-Layout §4.3）：只对 Absolute 生效，Fixed 恒为 0。
int32_t ZIndexOf(const Element* e);
// e 处应绘制的脱离流项按 zIndex 升序、同值按树序（稳定排序）返回。启用锚点后
// 数据源为"归属 e 的 Absolute（可跨中间容器）+ e 的直接 Fixed 子项"；缓冲按递归
// 深度分层跨帧复用，热路径零分配；depth 由遍历方逐层下传。
const std::vector<Element*>& ZOrderedOutOfFlow(const Element* e, uint32_t depth);
// 布局期统计是否存在非默认 zIndex 的 Absolute：为 0 时绘制/命中完全不进排序路径
void ResetZIndexCount() noexcept;
void NoteZIndex() noexcept;
uint32_t ZIndexCount() noexcept;
// 是否要走有序脱离流列表：树里有 Absolute（需按包含块提升）或用了 zIndex。
bool UseOrderedOutOfFlow() noexcept;

// 元素自身 Paint 级变换（布局绝对 DIP 空间的仿射矩阵，含级联层 1 动画值）。
// App/Frame.cpp 收集与 UI/Event.cpp 命中测试逆变换共用；实现在 UI/StyleResolve.cpp。
Transform2D ElementTransform(const Element* e) noexcept;

}  // namespace dui
