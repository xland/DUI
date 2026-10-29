// ============================================================================
//  DUI/UI/UI.h — 元素树、样式系统、控件、事件（01-Layering §2 UI）
//
//  M1 骨架：类型与签名即规格（对应 03-Style / 04-Layout / 06-Events /
//  15-Properties 的已定案内容）。布局、解析、标脏、绘制在 M3 落地；
//  输入与状态位自动维护在 M4/M5 落地。届时签名不变，只填实现。
//
//  已定案并在此落形的关键契约：
//    · 静态外观（含布局属性）全部在 Style 里；Set(Prop) 只承载运行时动态值
//    · AddChild<T>(..., style)：创建即定型，样式必须是最后一个参数（03-Style §6.1）
//    · 用一次的样式内联（StyleBuilder 右值），共享/带变体的才起名
//    · 状态位幂等写入 + 帧末合并标脏（03-Style §6.2）
//    · 合并优先级 disabled > pressed > selected > focused > hover（M3 解析时实现）
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "Core/Core.h"
#include "Core/Threading.h"

namespace dui {

namespace graphics {
// Graphics 层前向声明（公共头不含 windows.h / d3d11.h）
class Painter;
}

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

// 元素类别（收集时区分文本/容器；/GR- 下不用 RTTI，用虚函数判定）
enum class Kind : uint8_t { Node, Text };

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
    const char*  name_   = nullptr;       // 注册表键 / 调试标识；匿名样式为 nullptr
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

// Flex 单遍约束布局（UI/Layout.cpp，04-Layout §6）
void RunLayout(Element* root, const Vec2& sizeDip);
void ClearDirtyTree(Element* root);
// 子树是否含布局失效（Frame 决定是否重排：只重绘的帧跳过 Measure/Arrange）
bool SubtreeLayoutDirty(const Element* e);

// 命中测试与最小输入（UI/Event.cpp，06-Events M3 子集）
Element* HitTest(Element* root, const Vec2& ptDip);
void DispatchClick(Element* hit);
void UpdateHoverChain(Element* previous, Element* current);
void SetPressedChain(Element* hit, bool on);

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

// ---------------------------------------------------------------------------
// 事件（06-Events §3；M4 接入命中测试与路由，M1 仅承载绑定）
// ---------------------------------------------------------------------------
struct ClickEvent {
    Element* source = nullptr;   // 命中的目标元素（06-Events §4.3）
    bool     handled = false;    // 置 true 终止冒泡
};

// ---------------------------------------------------------------------------
// Element：树节点 + 状态位 + 本地覆盖
// ---------------------------------------------------------------------------
class Element {
public:
    Element() = default;
    virtual ~Element();

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    // 静态外观：共享形态只存指针，不拷贝（03-Style §2）
    void SetStyle(const Style& s) noexcept { style_ = &s; MarkDirty(kFlagLayout | kFlagPaint); }
    // 内联样式（AddChild<T>(..., StyleBuilder) 走这里）：Build 结果归元素持有
    void SetStyle(const StyleBuilder& b) {
        ownedStyle_ = std::make_unique<Style>(b.Build());
        style_ = ownedStyle_.get();
    }
    const Style* StylePtr() const noexcept { return style_; }

    // 运行时动态值（当前帧）；静态外观不要走这里（03-Style §6.1）
    template <class V>
    void Set(Prop p, V v) {
        if constexpr (std::is_enum_v<V>) {
            SetOverride(p, 0.f, static_cast<uint32_t>(v));
        } else if constexpr (std::is_integral_v<V>) {
            SetOverride(p, 0.f, static_cast<uint32_t>(v));
        } else if constexpr (std::is_floating_point_v<V>) {
            SetOverride(p, static_cast<float>(v), 0u);
        } else {
            static_assert(!sizeof(V), "dui::Element::Set: unsupported value type");
        }
    }

    // 状态位：相同值幂等（03-Style §6.2）；标脏按该状态变体的属性标志取并集
    void SetState(State s, bool on);
    bool HasState(State s) const noexcept { return (states_ & static_cast<uint32_t>(s)) != 0; }

    // 生命周期令牌（06-Events D7）：M2 起在延迟删除时置 false
    // 注：返回类型写全限定 —— 方法名 Lifetime 遮蔽同名类型
    dui::Lifetime Lifetime() const noexcept { return lifetime_; }

    // 点击绑定：成员函数 + 上下文（06-Events §4.1）；M4 起由命中测试触发
    template <class Ctx>
    void OnClick(Ctx* ctx, void (Ctx::*fn)(ClickEvent&)) {
        onClick_ = [ctx, fn](ClickEvent& e) { (ctx->*fn)(e); };
    }
    void OnClick(std::function<void(ClickEvent&)> fn) { onClick_ = std::move(fn); }

    // 树结构
    Element* Parent() const noexcept { return parent_; }
    const std::vector<Element*>& Children() const noexcept { return children_; }

    // 布局结果（相对父元素，DIP；RunLayout 写入）
    const Rect& Bounds() const noexcept { return bounds_; }

    // 失效查询 / 粗粒度入口（窗口尺寸、DPI 变化等）
    bool SubtreeDirty() const noexcept { return subtreeDirty_; }
    void InvalidateLayout() noexcept { MarkDirty(kFlagLayout); }
    void InvalidatePaint() noexcept { MarkDirty(kFlagPaint); }   // 只重绘不重排（12-Windowing §4-5）

    virtual Kind GetKind() const noexcept { return Kind::Node; }

    // 自定义绘制钩子（ADR-018 定制点 2 / 03-Style §6.3）：收集期在背景之后、
    // 子元素之前调用；Painter 的形状/矩形指令收 DIP 坐标 + scale。
    // 默认空实现——普通元素不参与。
    virtual void OnPaint(graphics::Painter&, const StyleContext&, float) {}

protected:
    void AttachChild(Element* child);
    void SetOverride(Prop p, float f, uint32_t u);
    void MarkDirty(uint32_t flags);

    // 内容自然尺寸（盒模型内容盒，不含 padding）。默认 = 容器子项聚合；
    // Text 覆盖为排版占位（M3 估算，M5 换 DWrite）。
    virtual Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const;

    const Style*               style_ = nullptr;
    std::unique_ptr<Style>     ownedStyle_;   // 内联样式的宿主（SetStyle(StyleBuilder) 时持有）
    std::vector<StyleValue>    overrides_;    // 本地覆盖（级联第 3 层，稀疏）
    uint32_t                   states_ = 0;
    dui::Lifetime              lifetime_;
    std::function<void(ClickEvent&)> onClick_;

private:
    // 布局 / 解析 / 事件实现（Layout.cpp、StyleResolve.cpp、Event.cpp）
    friend bool TryResolveElement(const Element* e, Prop p, StyleValue& out);
    friend bool SubtreeLayoutDirty(const Element* e);
    friend Vec2 MeasureElement(Element* e, const Vec2& avail, const StyleContext& parentCtx);
    friend void ArrangeElement(Element* e, const Rect& slot, const StyleContext& parentCtx, unsigned depth);
    friend void ClearDirtyTree(Element* root);
    friend void DispatchClick(Element* hit);

    Element*                   parent_ = nullptr;
    std::vector<Element*>      children_;     // 父持有子，析构递归删除
    Rect                       bounds_;       // Arrange 结果（相对父，DIP）
    Vec2                       measured_{};   // Measure 结果（外层盒）
    uint32_t                   dirty_ = 0;    // 自身待重算标志（PropFlags）
    bool                       subtreeDirty_ = false;   // 子树有待处理（上溯快速判定）
};

// ---------------------------------------------------------------------------
// 控件（Widgets 的最小集；默认外观与输入行为在 M4/M5 接入）
// ---------------------------------------------------------------------------

// 容器基类：AddChild "创建即定型"（03-Style §6.1）——样式必须是最后一个参数。
class Panel : public Element {
public:
    template <class T>
    T* AddChild() {
        static_assert(std::is_base_of_v<Element, T>, "AddChild<T>: T must derive from dui::Element");
        auto* e = new T();
        AttachChild(e);
        return e;
    }
    template <class T>
    T* AddChild(const Style& s) {
        auto* e = AddChild<T>();
        e->SetStyle(s);
        return e;
    }
    template <class T>
    T* AddChild(const StyleBuilder& b) {            // 一次性样式内联（链式返回左值，故收 const&）
        auto* e = AddChild<T>();
        e->SetStyle(b);                             // 传 Builder 走 owned 拷贝；Build() 临时会悬空
        return e;
    }
    template <class T>
    T* AddChild(const wchar_t* text) {              // 仅限有文本构造的控件（Text/Button）
        auto* e = new T(text);
        AttachChild(e);
        return e;
    }
    template <class T>
    T* AddChild(const wchar_t* text, const Style& s) {
        auto* e = AddChild<T>(text);
        e->SetStyle(s);
        return e;
    }
    template <class T>
    T* AddChild(const wchar_t* text, const StyleBuilder& b) {
        auto* e = AddChild<T>(text);
        e->SetStyle(b);                             // 同上：Build() 临时悬空
        return e;
    }
};

class Text : public Element {
public:
    Text() = default;
    explicit Text(const wchar_t* text) : text_(text ? text : L"") {}
    ~Text() override;    // 释放段落排版缓存（UI.cpp，经 text::Engine）

    void SetText(const wchar_t* text) { text_ = text ? text : L""; MarkDirty(kFlagLayout); }
    void SetText(const String& s)     { text_ = s; MarkDirty(kFlagLayout); }
    const String& Text_() const noexcept { return text_; }

    Kind GetKind() const noexcept override { return Kind::Text; }

    // 段落排版结果（text::Paragraph 的 pimpl，生命周期归本元素）。
    // 测量在 MeasureContent 内刷新；App 层收集时经此句柄绘制（M5）。
    void* ParaHandle() const noexcept { return para_; }

protected:
    // M5：DWrite 排版结果（11-Text §3）；DWrite 不可用时退回 M3 占位估算
    Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const override;

private:
    String text_;
    mutable void* para_ = nullptr;   // text::Paragraph（pimpl：公共头不引入 dwrite）
};

// 可点击文本控件（M4：hover/pressed 状态位由输入维护、焦点 + Tab）
class Button : public Text {
public:
    using Text::Text;
};

class ScrollView : public Panel {
public:
    // M3：overflow: Scroll + 内容偏移（04-Layout §6.1，滚动不触发布局）
};

}  // namespace dui
