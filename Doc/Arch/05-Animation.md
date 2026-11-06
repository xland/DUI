# 动画系统（命令式）

> 状态：v0.2 — 已落地。命令式属性动画 + 统一 ticker 已实现，契约以本文为准。
> 相关：[03-Style.md](./03-Style.md)（级联第 1 层）、[16-Image.md](./16-Image.md)（GIF 帧动画）、[06-Events.md](./06-Events.md)（光标闪烁）、[15-Properties.md](./15-Properties.md)（属性主表）、ADR-013（按需呈现）、ADR-020。

## 1. 决策：只做命令式动画

**不在样式里支持声明式动画。** 先前的 CSS 风格草案（v0.1：`Style::Transition()` / `KeyframesBuilder` / `animation` 样式属性 / `transition` 元属性 / 分期 P0–P2 / 待确认 D1–D4）全部作废。与之配套的只存不读代码 `Style::Transition()`、`TransitionSpec`、`Style::transitions_` 已删除。

- `Element::When(State, ...)`（[03-Style.md](./03-Style.md) §6.2）与动画无关：状态变体是立即生效的差异集查表，不带任何过渡；需要补间时由业务代码显式调用 `Animate()`。
- 属性表不再有 `Animatable` 标志，也没有"过渡旧值槽"；某个属性能否动画由 §2 的强类型标签白名单决定。
- 框架里有三类随时间变化的效果，只有第一类是"样式值补间"：

| 类型 | 载体 | 是否走样式系统 | 文档 |
| --- | --- | --- | --- |
| 命令式属性动画 | `Element::Animate()` | 是，写入级联第 1 层 | 本文 |
| GIF 帧动画 | `Gif::LoadFile()` / `Image::LoadAnimatedFile()`（播放控制由 `dui::Gif` 提供） | 否，换同一张纹理的像素 | [16-Image.md](./16-Image.md) §4.2 / §4.3 |
| 编辑控件光标闪烁 | `Element::OnBlink()` | 否，切换自身可见性 | [06-Events.md](./06-Events.md) |

三者共用一个时间源，但**只统一时间，不统一值**。

### 1.1 明确不做

声明式 `transition` / 关键帧 / `animation` 样式属性、迭代次数、方向、延迟、可配置 Fill、暂停/恢复、seek、Spring、自定义贝塞尔、`DUI_ENABLE_ANIMATION` 编译期裁剪、窗口级 DirectComposition 动画、元素级合成树 / 离屏层。

**注意**：Paint 级 2D transform（`TranslationX` / `TranslationY` / `Rotation` / `Scale`）不是声明式动画能力，而是四个普通 Paint 类属性，已在 §2.1 白名单开放；实现与边界见 [10-Rendering.md](./10-Rendering.md) §6.4 与 ADR-020 补充。

---

## 2. 公共 API

```cpp
auto h = card->Animate(dui::Opacity, 0.f,
    { .durationMs = 200, .easing = dui::Easing::EaseOut });
h.OnFinished(fn);
h.Cancel();   // 幂等；元素销毁后自动失效
```

定义于 `UI/Animation.h`，经 `UI/UI.h` / `DUI/DUI.h` 聚合，业务侧只需 include 公共入口。

### 2.1 `AnimProp<T>`：强类型属性标签

`template <class T> struct AnimProp { Prop prop; };` —— 标签携带属性 ID，**模板参数 `T` 决定目标值类型**，编译期拒绝不可动画的属性（`static_assert`，不用 RTTI）。目前开放 Paint 类标量，以及布局尺寸 / 定位偏移对照标签：

| 标签 | 值类型 | 属性 | 插值 |
| --- | --- | --- | --- |
| `dui::Opacity` | `float` | `Prop::Opacity` | 线性标量 |
| `dui::BackgroundColor` | `dui::Color` | `Prop::BackgroundColor` | ARGB 四通道分别线性插值 |
| `dui::TextColor` | `dui::Color` | `Prop::Color` | 同上 |
| `dui::SelectionColor` | `dui::Color` | `Prop::SelectionColor` | 同上 |
| `dui::CaretColor` | `dui::Color` | `Prop::CaretColor` | 同上 |
| `dui::TranslationX` | `float` | `Prop::TranslationX` | 线性标量（DIP） |
| `dui::TranslationY` | `float` | `Prop::TranslationY` | 同上 |
| `dui::Rotation` | `float` | `Prop::Rotation` | 线性标量（度，绕元素盒中心） |
| `dui::Scale` | `float` | `Prop::Scale` | 线性标量（等比，1 = 原始大小） |
| `dui::Width` | `float` | `Prop::Width` | 线性标量（`kFlagLayout`，每帧重排） |
| `dui::Height` | `float` | `Prop::Height` | 同上 |
| `dui::Left` | `float` | `Prop::Left` | 同上（仅定位元素生效，见 04-Layout §4.3） |
| `dui::Top` | `float` | `Prop::Top` | 同上 |

- `Color` 约定 `0xAARRGGBB`（[Core/Core.h](../DUI/Core/Core.h)），逐通道在 sRGB/gamma 空间插值，与 ADR-013 的混合空间一致。
- 前四个 transform 标签与 `Opacity` 一样是 `kFlagPaint`：动画每帧只重收集顶点，不重排；`Bounds()` 与布局结果不变（[10-Rendering.md](./10-Rendering.md) §6.4）。
- `Width` / `Height` 是布局尺寸对照，`Left` / `Top` 是定位偏移对照（仅 `Relative` / `Absolute` / `Fixed` 生效，`Static` 忽略，见 04-Layout §4.3）：级联层 1 的值会被 `Layout.cpp` 读到，但每帧 `kFlagLayout` 全量重排、兄弟元素跟着回流，不宜用于高频动画；轻量位移动画用 `dui::TranslationX`（`kFlagPaint`）。
- 文本、Thickness（四边）、枚举、字体族类属性**不开放**：它们每帧重排版，或没有自然的插值定义。
- 第二参数是 `std::type_identity_t<T>`，只从标签推导 `T`，目标值允许隐式转换（如整数字面量传给 `Color` 标签）。

### 2.2 `AnimConfig`：聚合配置

```cpp
struct AnimConfig {
    uint32_t durationMs = 200;          // 0 按 1ms 处理（下一次 tick 即完成）
    Easing   easing     = Easing::EaseOut;
};
```

聚合类型，用 C++20 指派初始化；**成员声明顺序固定为 `durationMs` → `easing`**，指派初始化的书写顺序必须一致。`Easing` 定义在 `UI/Style.h`：`Linear / EaseIn / EaseOut / EaseInOut`。

### 2.3 `AnimationHandle`

| 方法 | 语义 |
| --- | --- |
| `OnFinished(std::function<void()> fn)` | 注册完成回调。动画进行中注册则完成时调用一次；已正常完成后注册则**立即调用一次**；已取消或元素已销毁则忽略。重复注册以后者覆盖前者。返回自身便于链式。 |
| `Cancel() noexcept` | 取消动画、清除动画槽、回落基值并标脏。**幂等**；元素销毁后为 no-op。 |

回调在 **UI 线程**、统一 ticker 的 `Tick()` 内执行，且延后到该元素本轮所有补间推进结束之后，避免重入打乱遍历。回调里可以启动新动画、取消其它动画或销毁元素。

---

## 3. 语义契约

1. **起点 = 当前显示值**：启动时调 `ResolveProperty(element, prop)`（遍历外路径），它已经包含级联第 1 层，因此中断旧动画时从旧动画的当前值继续，快速反向不跳变。
2. **同 `(元素, 属性)` 唯一**：新动画替换旧动画 —— 包括已完成但仍保留终值的那条。旧句柄整体作废、不触发其 `OnFinished`。
3. **动画值不污染本地覆盖**：补间结果写入 `Element::animated_`（稀疏数组），作为级联**第 1 层（最高优先级）**；`TryResolveElement()` 最先查它。遍历内 `StyleContext` 与遍历外 `Style::Resolve` 都经过 `TryResolveElement`，同帧结果一致。`Element::Set()` 的 `overrides_`（第 3 层）不被改写。
4. **标脏按属性标志**：每次写入调 `MarkDirty(PropFlagsOf(prop))`。Paint 标量 / transform 标签为 `kFlagPaint` → 每帧仅重绘、不重排；`Width` / `Height` / `Left` / `Top` 为 `kFlagLayout` → 每帧全量重排，兄弟元素跟着回流。
5. **完成 = fill forwards**：终值留在动画槽，不再 tick，直到被新动画替换、`Cancel()` 或元素销毁。回调只触发一次。
6. **取消 / 销毁**：`Cancel()` 移除该属性的动画槽并回落基值；元素析构时 `Lifetime::Kill()` + 从 ticker 注销，`AnimState` 里的生命周期令牌让所有残留句柄自动失效，回调不会被派发到已销毁元素。

---

## 4. 缓动

`Easing` 用二次多项式近似（不是 CSS cubic-bezier 的精确解），`t` 为 `clamp(elapsed / durationMs, 0, 1)`：

| 枚举 | 公式 |
| --- | --- |
| `Linear` | \(t\) |
| `EaseIn` | \(t^2\) |
| `EaseOut` | \(1-(1-t)^2\) |
| `EaseInOut` | \(2t^2\) （\(t<0.5\)），\(1-2(1-t)^2\) （\(t\ge 0.5\)） |

---

## 5. 时间源与按需呈现

`AnimationClock`（`UI/Image.h` / `UI/Image.cpp`）是进程级单例，统一驱动三类时间行为；App 层 `Window::SyncTickTimer()` 只启停**一个**窗口定时器（`kTickTimerId`）：

```
[输入][分发] → [WM_TIMER → AnimationClock::Tick]
   ├─ 光标闪烁（500ms 半周期）
   ├─ GIF 帧动画（按各帧自身延迟）
   └─ 命令式属性动画（每个补间按绝对时刻求 t）
 → [标脏] → [布局/收集/光栅/呈现] → [SyncTickTimer 重新判定启停与周期]
```

| 状态 | `NeedsTick()` | `TickPeriodMs()` |
| --- | --- | --- |
| 有 GIF 帧动画或有活动属性动画 | true | 20 ms |
| 只有可闪烁的焦点元素 | true | 500 ms（不因统一 ticker 退化成 20ms 空转） |
| 都没有 / 窗口最小化 | false | 停表 |

`Tick()` 的顺序是闪烁 → 帧动画 → 属性动画；属性动画放最后，因为完成回调可能改动图像控件或子树。全部补间结束后 `HasActiveAnimations()` 转 false，下一轮 `SyncTickTimer()` 停表，回到 ADR-013 的空闲零功耗。`startMs` 在首次 tick 时取当前时刻（首拍 \(t=0\)），不引入额外的时间 API。

---

## 6. 实现落点

| 文件 | 职责 |
| --- | --- |
| `UI/Animation.h` | `AnimProp<T>` 与标签白名单、`AnimConfig`、`AnimationHandle`、`detail::AnimState/AnimSlot`、ticker 桥接声明 |
| `UI/Animation.cpp` | 注册表、插值/缓动、`Element::StartAnimation` / `Element::TickAnimations`、句柄取消与失效 |
| `UI/Element.h/.cpp` | `Animate()` 模板、`animated_`（层 1）、`anims_`、析构时注销 |
| `UI/StyleResolve.cpp` | `TryResolveElement()` 的层 1 查询；`ElementTransform()` 合成 transform 矩阵 |
| `Core/Core.h` | `Transform2D`（仿射矩阵 / 组合 / 求逆 / DPI 共轭） |
| `Graphics/Graphics.h/.cpp` | `Painter::SetTransform()` 与四批 quad 的发射期顶点变换 |
| `App/Frame.cpp` | `Collector` 逐元素变换栈、裁剪 AABB、绘制单元下传 |
| `UI/Event.cpp` | `HitTest` 逐层逆变换 |
| `UI/Image.cpp` | `AnimationClock::{NeedsTick, TickPeriodMs, Tick}` 接入属性动画 |
| `App/App.cpp` | 单定时器启停 / 换周期（对外无变化） |

---

## 7. 已知边界

- 20 ms 是 Windows 定时器粒度，不是 VSync 对齐的精确帧；补间按 `GetTickCount64()` 的绝对时刻推进，掉帧不会拖长总时长。
- `durationMs = 0` 等价于 1 ms：仍要等下一次 tick 才写终值、发回调。
- 颜色在打包的 8 位通道上插值，不做线性光空间混合。
- GIF 帧动画与光标闪烁不是样式动画，不读写动画槽，也不受 `Cancel()` 影响。
- transform 动画只改绘制顶点：`Bounds()` / 布局结果 / `Scroller` 可滚动范围不变；`PointerEvent::pos` 仍是客户区绝对 DIP，旋转的交互文本框光标定位列为后续项（[10-Rendering.md](./10-Rendering.md) §6.4）。
- 等比 `Scale` 动画下字形是被线性拉伸的图集位图（ADR-006 按离散尺度光栅化），放大略糊；SDF 圆角的 1px AA 过渡带会按比例变宽。
- 命令式属性动画的完整交互演示见 `Samples/Animation`（按功能拆成多个自包含的极简示例函数，可单独阅读）；GIF 帧动画与播放控制见 `Samples/Image`。库侧不再保留额外的 API 形状契约函数。
