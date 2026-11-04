// ============================================================================
//  DUI/UI/Element.h — 元素树节点：树结构、状态位、本地覆盖、点击绑定
//  （实现拆在 UI/Element.cpp；布局/解析/事件实现分别在
//   Layout.cpp / StyleResolve.cpp / Event.cpp，经 friend 访问私有状态）
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "Core/Threading.h"
#include "UI/Style.h"

namespace dui {

namespace graphics {
// Graphics 层前向声明（公共头不含 windows.h / d3d11.h）
class Painter;
}

// 元素类别（收集时区分文本/容器/滚动容器；/GR- 下不用 RTTI，用虚函数判定）
enum class Kind : uint8_t { Node, Text, Scroller };

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

// 布局实现入口（UI/Layout.cpp；Element::MeasureContent 聚合子项时调用）
Vec2 MeasureElement(Element* e, const Vec2& avail, const StyleContext& parentCtx);

}  // namespace dui
