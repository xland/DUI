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
#include "UI/Animation.h"
#include "UI/Style.h"

namespace dui {

namespace graphics {
// Graphics 层前向声明（公共头不含 windows.h / d3d11.h）
class Painter;
}

// 元素类别（收集时区分文本/容器/滚动容器/图像；/GR- 下不用 RTTI，用虚函数判定）
enum class Kind : uint8_t { Node, Text, Scroller, Image };

// ---------------------------------------------------------------------------
// 事件（06-Events §3）：可路由事件走隧道（根 → 命中）+ 冒泡（命中 → 根）。
// Phase 只用于可路由事件；Enter/Leave 是直达事件，没有阶段。
// ---------------------------------------------------------------------------
enum class Phase : uint8_t { Bubble, Tunnel };

// 指针事件：移入/移出/移动/按下/弹起，以及合成的点击（坐标为客户区绝对 DIP）
// Click 与指针事件共用同一事件对象（06-Events §4.3）：Click 时 button = Left、
// pos = 抬起处坐标；控件内部 Handle* 钩子不参与 Click 派发。
struct PointerEvent {
    Element* source = nullptr;   // 处理器宿主元素（冒泡中当前收到事件的元素）
    Vec2     pos{};               // 客户区绝对坐标（DIP）
    // 触发事件的按键（按下/弹起有效；移动/移入/移出为 None）
    PointerButton button = PointerButton::None;
    bool     handled = false;     // 置 true 终止冒泡（移入/移出不冒泡，该字段无意义）
};

// 滚轮事件（delta 为格数：+ 向上 / − 向下，Windows 约定；Shift + 滚轮 → horizontal）
struct WheelEvent {
    Element* source = nullptr;
    Vec2     pos{};
    float    delta = 0.f;
    bool     horizontal = false;
    bool     handled = false;     // 置 true 终止冒泡并阻止滚动容器消费
};

// 键盘事件：key = 虚拟键码（VK_*）；ch 仅 OnChar 有效（可含控制字符）
struct KeyEvent {
    Element* source = nullptr;
    int      key = 0;              // VK_* 虚拟键码
    wchar_t  ch = 0;               // 字符（OnChar）
    bool     ctrl = false;
    bool     shift = false;
    bool     alt = false;
    bool     handled = false;      // 置 true 终止冒泡
};

// ---------------------------------------------------------------------------
// 焦点服务（UI/Focus.h；焦点唯一，挂在元素上 —— 06-Events §6）
// ---------------------------------------------------------------------------
class Element;

// 当前焦点元素（无焦点 = nullptr）
Element* FocusedElement() noexcept;
// e 是否为当前焦点元素
bool IsFocused(const Element* e) noexcept;
// 设置焦点：旧元素清 State::Focused，新元素置 State::Focused；e 为 nullptr 则清焦点
void SetFocus(Element* e);

// ---- 指针捕获（06-Events §5）----------------------------------------------
// 窗口级唯一目标，Element::SetCapture() / ReleaseCapture() 的服务入口。
// 捕获期间 Move/Up 直达该元素（仍从其开始冒泡），抬起自动释放。
void SetCaptureElement(Element* e) noexcept;
Element* CapturedElement() noexcept;
void ClearCaptureIf(Element* e) noexcept;

// ---------------------------------------------------------------------------
// Element：树节点 + 状态位 + 本地覆盖
// ---------------------------------------------------------------------------
class Element {
public:
    Element() = default;
    virtual ~Element();

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    // 静态外观：一律拷贝持有（Style 轻量、SetStyle 仅构建期调用）。
    // 拷贝语义使 `dui::Style().Gap(20.f)` 这类链式临时可以直接传入不悬空；
    // 派生链（parent_）与字体指针仍指向外部，生命周期契约不变（03-Style §2）
    void SetStyle(const Style& s) {
        ownedStyle_ = std::make_unique<Style>(s);
        style_ = ownedStyle_.get();
        MarkDirty(kFlagLayout | kFlagPaint);
    }
    const Style* StylePtr() const noexcept { return style_; }

    // 运行时动态值（当前帧）；静态外观不要走这里（03-Style §6.1）。
    // 标量 / 枚举 / 颜色写 f·u 槽；Thickness 写 f + extra 三边；字体族写零拷贝指针。
    // 整体替换同 Prop 的 StyleValue，避免 Thickness / family 的旧槽残留。
    template <class V>
    void Set(Prop p, V v) {
        StyleValue nv;
        nv.prop = p;
        if constexpr (std::is_enum_v<V> || std::is_integral_v<V>) {
            nv.u = static_cast<uint32_t>(v);
        } else if constexpr (std::is_floating_point_v<V>) {
            nv.f = static_cast<float>(v);
            if (IsThicknessProp(p)) nv.extra[0] = nv.extra[1] = nv.extra[2] = nv.f;
        } else if constexpr (std::is_same_v<V, dui::Thickness>) {
            nv.f = v.left;
            nv.extra[0] = v.top;
            nv.extra[1] = v.right;
            nv.extra[2] = v.bottom;
        } else if constexpr (std::is_convertible_v<V, const wchar_t*>) {
            nv.family = v;   // 零拷贝：指针须存活到覆盖被替换（建议字面量）
        } else {
            static_assert(!sizeof(V), "dui::Element::Set: unsupported value type");
        }
        SetOverride(nv);
    }

    // 命令式属性动画（05-Animation）：启动一条补间，返回可取消的句柄。
    // 起点 = 当前显示值（含正在进行的动画值）；同 (元素, 属性) 新动画替换旧动画。
    // 动画值写入级联第 1 层，不改动 Set() 的本地覆盖。
    template <class T>
    AnimationHandle Animate(AnimProp<T> prop, std::type_identity_t<T> target,
                            AnimConfig cfg) {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, dui::Color>,
                      "dui::Element::Animate: only float / Color properties are animatable");
        StyleValue to;
        to.prop = prop.prop;
        if constexpr (std::is_same_v<T, dui::Color>) to.u = target;
        else to.f = target;
        return StartAnimation(prop.prop, std::is_same_v<T, dui::Color>, to, cfg);
    }

    // 状态变体（03-Style §6.2）：每个状态一份差异集，状态位成立时立即生效、
    // 不带过渡。同状态后设覆盖先设；返回 *this 便于连写多个状态。
    // 变体随元素（不随 Style），因此可以用运行时数据构造，且不影响其它元素。
    Element& When(State s, const Style& variant);
    const Style* StateVariant(State s) const noexcept;

    // 状态位：相同值幂等（03-Style §6.2）；标脏按该状态变体的属性标志取并集
    void SetState(State s, bool on);
    bool HasState(State s) const noexcept { return (states_ & static_cast<uint32_t>(s)) != 0; }

    // 生命周期令牌（06-Events D7）：M2 起在延迟删除时置 false
    // 注：返回类型写全限定 —— 方法名 Lifetime 遮蔽同名类型
    dui::Lifetime Lifetime() const noexcept { return lifetime_; }

    // 点击绑定（06-Events §4.1）：默认冒泡；Phase::Tunnel 时在隧道阶段先收到。
    // 事件对象与指针事件统一为 PointerEvent（button 恒为 Left，pos = 抬起处）。
    template <class Ctx>
    void OnClick(Ctx* ctx, void (Ctx::*fn)(PointerEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelClick : h.click, ctx, fn);
    }
    void OnClick(std::function<void(PointerEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelClick : h.click) = std::move(fn);
    }

    // ------------------------------------------------------------------
    // 指针事件绑定（与 OnClick 同构：成员函数 + 上下文 / std::function）
    //   OnPointerEnter / OnPointerLeave —— 直达事件：命中链差分驱动，无阶段。
    //   OnPointerMove / Down / Up / OnWheel —— 双阶段：Phase::Tunnel 时根先收到，
    //     默认 Phase::Bubble 从命中元素向上；任一阶段置 handled 终止其余派发。
    //   OnWheel 置 ev.handled 还能阻止滚动容器消费该次滚轮。
    // 不做防抖/节流（由开发者自行处理）。
    // ------------------------------------------------------------------
    template <class Ctx>
    void OnPointerEnter(Ctx* ctx, void (Ctx::*fn)(PointerEvent&)) {
        BindMember(MutableHandlers().enter, ctx, fn);
    }
    void OnPointerEnter(std::function<void(PointerEvent&)> fn) { MutableHandlers().enter = std::move(fn); }

    template <class Ctx>
    void OnPointerLeave(Ctx* ctx, void (Ctx::*fn)(PointerEvent&)) {
        BindMember(MutableHandlers().leave, ctx, fn);
    }
    void OnPointerLeave(std::function<void(PointerEvent&)> fn) { MutableHandlers().leave = std::move(fn); }

    template <class Ctx>
    void OnPointerMove(Ctx* ctx, void (Ctx::*fn)(PointerEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelMove : h.move, ctx, fn);
    }
    void OnPointerMove(std::function<void(PointerEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelMove : h.move) = std::move(fn);
    }

    template <class Ctx>
    void OnPointerDown(Ctx* ctx, void (Ctx::*fn)(PointerEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelDown : h.down, ctx, fn);
    }
    void OnPointerDown(std::function<void(PointerEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelDown : h.down) = std::move(fn);
    }

    template <class Ctx>
    void OnPointerUp(Ctx* ctx, void (Ctx::*fn)(PointerEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelUp : h.up, ctx, fn);
    }
    void OnPointerUp(std::function<void(PointerEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelUp : h.up) = std::move(fn);
    }

    template <class Ctx>
    void OnWheel(Ctx* ctx, void (Ctx::*fn)(WheelEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelWheel : h.wheel, ctx, fn);
    }
    void OnWheel(std::function<void(WheelEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelWheel : h.wheel) = std::move(fn);
    }

    // ------------------------------------------------------------------
    // 键盘事件绑定（与 OnClick 同构：成员函数 + 上下文 / std::function）
    //   路由：从焦点元素出发，双阶段（隧道 根 → 焦点，冒泡 焦点 → 根）。
    //   OnKeyDown / OnKeyUp / OnChar 均支持 Phase 参数（默认 Phase::Bubble）。
    //   OnKeyDown / OnKeyUp：ev.key = VK_* 虚拟键码，ev.ctrl/shift/alt = 修饰键；
    //   OnChar：ev.ch = 字符（UTF-16，IME 提交亦走此路）。
    //   处理器置 ev.handled 可终止后续阶段。
    // ------------------------------------------------------------------
    template <class Ctx>
    void OnKeyDown(Ctx* ctx, void (Ctx::*fn)(KeyEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelKeyDown : h.keyDown, ctx, fn);
    }
    void OnKeyDown(std::function<void(KeyEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelKeyDown : h.keyDown) = std::move(fn);
    }

    template <class Ctx>
    void OnKeyUp(Ctx* ctx, void (Ctx::*fn)(KeyEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelKeyUp : h.keyUp, ctx, fn);
    }
    void OnKeyUp(std::function<void(KeyEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelKeyUp : h.keyUp) = std::move(fn);
    }

    template <class Ctx>
    void OnChar(Ctx* ctx, void (Ctx::*fn)(KeyEvent&), Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        BindMember(phase == Phase::Tunnel ? h.tunnelChar : h.ch, ctx, fn);
    }
    void OnChar(std::function<void(KeyEvent&)> fn, Phase phase = Phase::Bubble) {
        Handlers& h = MutableHandlers();
        (phase == Phase::Tunnel ? h.tunnelChar : h.ch) = std::move(fn);
    }

    // 指针/点击/滚轮/键盘处理器集合：按需分配 —— 未绑定任何事件的元素不占这部分内存。
    // tunnel* 为隧道阶段专用槽，与冒泡槽互不覆盖（同一元素可两阶段各绑一个）。
    struct Handlers {
        std::function<void(PointerEvent&)> enter, leave, move, down, up, click;
        std::function<void(WheelEvent&)>   wheel;
        std::function<void(KeyEvent&)>     keyDown, keyUp, ch;
        std::function<void(PointerEvent&)> tunnelMove, tunnelDown, tunnelUp, tunnelClick;
        std::function<void(WheelEvent&)>   tunnelWheel;
        std::function<void(KeyEvent&)>     tunnelKeyDown, tunnelKeyUp, tunnelChar;
    };
    // 事件派发只读访问（Event.cpp）
    const Handlers* HandlersPtr() const noexcept { return handlers_.get(); }

    // ------------------------------------------------------------------
    // 焦点与 Tab 链（06-Events §6）
    //   可聚焦：默认 false；编辑控件（TextBox/TextArea）置 true。
    //   tabIndex：>0 排在 Tab 链前面（数值小者先）；0 = 文档序（默认）；
    //             <0 排在最后（数值小者先）。
    // ------------------------------------------------------------------
    void SetFocusable(bool on) noexcept { focusable_ = on; }
    bool IsFocusable() const noexcept { return focusable_; }
    void SetTabIndex(int index) noexcept { tabIndex_ = index; }
    int  TabIndex() const noexcept { return tabIndex_; }

    // 取得焦点（旧焦点自动清 State::Focused）；Blur 主动放弃焦点
    void Focus();
    void Blur();
    bool HasFocus() const noexcept;

    // 指针捕获（06-Events §5）：捕获期间 Move/Up 不再命中测试，直达本元素，
    // 仍从其开始冒泡；抬起时框架自动释放，元素销毁时自动失效。
    void SetCapture();
    void ReleaseCapture();
    bool HasCapture() const noexcept;

    // 树结构
    Element* Parent() const noexcept { return parent_; }
    const std::vector<Element*>& Children() const noexcept { return children_; }
    // 布局结果（相对父元素，DIP；RunLayout 写入）
    const Rect& Bounds() const noexcept { return bounds_; }
    // Measure 结果（外边距盒 = 元素盒 + margin）：Grid / 自定义布局读子项占位用
    Vec2 Measured() const noexcept { return measured_; }

    // 失效查询 / 粗粒度入口（窗口尺寸、DPI 变化等）
    bool SubtreeDirty() const noexcept { return subtreeDirty_; }
    void InvalidateLayout() noexcept { MarkDirty(kFlagLayout); }
    void InvalidatePaint() noexcept { MarkDirty(kFlagPaint); }   // 只重绘不重排（12-Windowing §4-5）

    virtual Kind GetKind() const noexcept { return Kind::Node; }

    // 自定义绘制钩子（ADR-018 定制点 2 / 03-Style §6.3）：收集期在背景之后、
    // 子元素之前调用；Painter 的形状/矩形指令收 DIP 坐标 + scale。
    // 默认空实现——普通元素不参与。
    virtual void OnPaint(graphics::Painter&, const StyleContext&, float) {}

    // 内容裁剪矩形（绝对 DIP）：返回 true 时，元素自身的绘制内容（OnPaint）
    // 被裁剪到该矩形内（TextBox/TextArea 的文本溢出裁剪）。
    // 裁剪在窗口空间求交（嵌套滚动容器时由收集器叠加父级原点）。
    virtual bool ContentClipRect(Rect&) const { return false; }

    // 焦点变化钩子（SetFocus 时调用；focused = true 表示取得焦点）
    virtual void OnFocusChanged(bool focused) { (void)focused; }

    // 闪烁节拍（框架定时器驱动）：参与节拍的元素（如编辑控件的光标）在此
    // 切换可见性并标脏；返回 true 表示需要继续节拍，false 表示可以停表。
    virtual bool IsBlinkable() const { return false; }
    virtual bool OnBlink() { return false; }

    // 插入点（光标）矩形：客户区 DIP 绝对坐标。返回 true 表示该元素提供插入点
    // （编辑控件）；IME 候选窗口据此定位。
    //
    // 矩形须**覆盖光标所在行的行首到基线**（而不是一个孤立的点）：IME 依系统
    // caret 的**底边**摆放候选窗口，若 caret 高只有 1px，输入法会认为插入点
    // "零高度"，把候选窗口下移一整行（观感：候选窗离光标很远）。
    virtual bool InsertionPoint(Rect& outRect) const { (void)outRect; return false; }

    // 是否使用文本插入光标（IBEAM）：命中编辑控件时鼠标指针应变为 I-beam。
    virtual bool WantsTextCursor() const { return false; }

    // ------------------------------------------------------------------
    // 控件内部事件钩子（子类重写）：返回 true = 已消费，事件不再向上冒泡。
    // 与 OnXxx 绑定并存 —— 绑定是外部回调，钩子是控件自身行为；
    // 同一元素上钩子先于绑定执行，绑定仍会收到该事件（可见 ev.handled）。
    // ------------------------------------------------------------------
    virtual bool HandleKeyDown(KeyEvent&) { return false; }
    virtual bool HandleKeyUp(KeyEvent&) { return false; }
    virtual bool HandleChar(KeyEvent&) { return false; }
    virtual bool HandlePointerDown(PointerEvent&) { return false; }
    virtual bool HandlePointerMove(PointerEvent&) { return false; }
    virtual bool HandlePointerUp(PointerEvent&) { return false; }

protected:
    void AttachChild(Element* child);
    void SetOverride(const StyleValue& v);
    void MarkDirty(uint32_t flags);

    // 内容自然尺寸（盒模型内容盒，不含 padding）。默认 = 容器子项聚合；
    // Text 覆盖为排版占位（M3 估算，M5 换 DWrite）。
    virtual Vec2 MeasureContent(const StyleContext& ctx, const Vec2& contentAvail) const;

    const Style*               style_ = nullptr;
    std::unique_ptr<Style>     ownedStyle_;   // SetStyle 拷贝持有的宿主
    std::vector<StyleValue>    overrides_;    // 本地覆盖（级联第 3 层，稀疏）
    std::vector<StyleValue>    animated_;     // 动画值（级联第 1 层，稀疏）
    uint32_t                   states_ = 0;
    dui::Lifetime              lifetime_;

protected:
    Handlers& MutableHandlers() {
        if (!handlers_) handlers_ = std::make_unique<Handlers>();
        return *handlers_;
    }

private:
    // 命令式动画（实现在 UI/Animation.cpp）
    AnimationHandle StartAnimation(Prop p, bool color, const StyleValue& to,
                                   const AnimConfig& cfg);
    void TickAnimations(uint64_t nowMs);
    uint32_t StateVariantFlags(State s) const;   // 变体属性的失效标志并集

    // 成员函数 + 上下文 → std::function 的通用绑定（与 OnClick 同构）
    template <class E, class Ctx>
    static void BindMember(std::function<void(E&)>& slot, Ctx* ctx, void (Ctx::*fn)(E&)) {
        slot = [ctx, fn](E& e) { (ctx->*fn)(e); };
    }

    std::unique_ptr<Handlers> handlers_;

    bool focusable_ = false;   // 可聚焦（Tab 链成员）
    int  tabIndex_ = 0;        // Tab 链次序（见 SetTabIndex 注释）

private:
    // 布局 / 解析 / 事件实现（Layout.cpp、StyleResolve.cpp、Event.cpp）
    friend bool TryResolveElement(const Element* e, Prop p, StyleValue& out);
    friend bool SubtreeLayoutDirty(const Element* e);
    friend Vec2 MeasureElement(Element* e, const Vec2& avail, const StyleContext& parentCtx);
    friend void ArrangeElement(Element* e, const Rect& slot, const StyleContext& parentCtx, unsigned depth);
    friend void ClearDirtyTree(Element* root);
    friend void detail::TickAnimations(uint64_t nowMs);
    friend void detail::CancelAnimation(
        const std::shared_ptr<detail::AnimState>& state) noexcept;

    Element*                   parent_ = nullptr;
    std::vector<Element*>      children_;     // 父持有子，析构递归删除
    std::vector<std::pair<State, Style>> stateVariants_;   // 状态变体（级联第 2 层）
    std::vector<detail::AnimSlot> anims_;    // 活动补间（完成即移除，终值留在 animated_）
    Rect                       bounds_;       // Arrange 结果（相对父，DIP）
    Vec2                       measured_{};   // Measure 结果（外边距盒，含 margin）
    uint32_t                   dirty_ = 0;    // 自身待重算标志（PropFlags）
    bool                       subtreeDirty_ = false;   // 子树有待处理（上溯快速判定）
};

// 布局实现入口（UI/Layout.cpp；Element::MeasureContent 聚合子项时调用）
Vec2 MeasureElement(Element* e, const Vec2& avail, const StyleContext& parentCtx);
// Grid 容器的自然尺寸（04-Layout §4.2）；MeasureContent 在 Display::Grid 分支调用
Vec2 MeasureGridContent(const Element* e, const StyleContext& ctx, const Vec2& avail);

}  // namespace dui
