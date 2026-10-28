# 动画系统（CSS 风格）

> 状态：草案 v0.1 — 待讨论确认。
> 需求来源：支持**类似 CSS 的动画设置** —— 过渡（transition）与关键帧动画（keyframes），声明式、可中断、与样式系统集成。
> 相关：[03-Style.md](./03-Style.md)（级联第 1 层已预留"动画值"槽）、ADR-013（按需呈现）、[04-Layout.md](./04-Layout.md)（布局动画的失效代价）。

## 1. 需求

| 编号 | 需求 | 验收口径 |
| --- | --- | --- |
| R1 | 过渡 | 属性值变化时按声明的时长/缓动/延迟自动补间，无需手写逐帧代码 |
| R2 | 可中断、可反向 | 快速进出 hover 不跳变；新动画从**当前显示值**起播 |
| R3 | 关键帧动画 | 多属性、多个进度点、迭代次数、方向、填充模式、延迟 |
| R4 | 缓动曲线 | 常用预设 + 自定义贝塞尔；弹簧类曲线列为扩展 |
| R5 | 运行时控制 | 播放/暂停/取消/完成回调 |
| R6 | 与按需呈现协同 | 无动画且无脏区时 CPU 归零；动画期间按帧推进 |
| R7 | 与样式/布局协同 | 只按属性标志标脏；位移类动画**不触发重排** |
| R8 | 可裁剪 | `DUI_ENABLE_ANIMATION` 关闭后整套机制不进二进制 |

### 1.1 约束（沿用既定决策）

- 热路径零分配：活动动画存于每窗口固定容量池，tick 全程无堆分配。
- 按需呈现（ADR-013）：动画推进挂在帧循环里，是"保持唤醒"的唯一来源之一。
- 样式系统先行：动画值走 [03-Style.md](./03-Style.md) 级联第 1 层，不另起炉灶。
- 可裁剪：动画槽、tick、过渡逻辑全部由 `DUI_ENABLE_ANIMATION` 包裹。

---

## 2. 总体思路

**动画是样式系统的一个值源**，不是独立于样式之外的旁路：级联第 1 层（最高优先级）就是动画槽。动画推进时把计算出的属性值写进该槽并按属性标志标脏 —— 样式解析、失效传播、绘制全部复用现有机制。

两种机制，对应 CSS 的两个概念：

| CSS | DUI | 触发方式 |
| --- | --- | --- |
| `transition` | `Transition(Prop, duration, easing)`，声明在 `Style` 上 | **被动**：属性基值变化时自动补间 |
| `@keyframes` + `animation` | `KeyframesBuilder` + `element->Animate(...)` | **主动**：显式启动，独立时间线 |

```css
/* CSS 写法 */
button { transition: background-color 150ms ease-out; }
@keyframes pulse { 0% {opacity:1} 50% {opacity:.4} 100% {opacity:1} }
card { animation: pulse 600ms ease-in-out 3; }
```

```cpp
// 对应的 DUI 写法（无运行时解析，编译期构造）
static const Style kButton = StyleBuilder("button")
    .Background(0xFF2D7FF9)
    .Transition(Prop::BackgroundColor, 150, Easing::EaseOut)
    .When(State::Hover, StyleBuilder().Background(0xFF1A6FE0))
    .Build();
// hover 进入/离开时背景色 150ms 补间，起点为当前显示值

static const Keyframes kPulse = KeyframesBuilder("pulse")
    .Keyframe(0.f).Opacity(1.f)
    .Keyframe(0.5f).Opacity(0.4f)
    .Keyframe(1.f).Opacity(1.f)
    .Build();

auto h = anim.Animate(card, kPulse,
                      AnimConfig{}.Duration(600).Iterations(3).Easing(Easing::EaseInOut));
h.OnFinished([]{ /* animationend */ });
```

> 动画的**声明**也走样式属性（`transition` / `animation` 是属性表中的普通条目），因此天然获得状态变体联动（不同 `State` 可声明不同过渡）、主题替换时整体生效等能力。

---

## 3. 过渡（Transition）

### 3.1 语义

1. 元素某属性的**基值**（级联第 2–6 层的解析结果）发生变化；
2. 查该属性是否有 `Transition` 声明；
3. 有 → 创建补间：**起点 = 当前显示值**（含正在进行的动画值，见 3.2），终点 = 新基值；
4. 推进期间写入动画槽，结束后按填充模式处理。

### 3.2 中断语义（R2）

同一 (元素, 属性) 上的新过渡**替换**旧过渡，起点取旧过渡的当前值 —— 快速 hover 进出不会跳变，也天然支持"反向播放"效果（离开时声明的过渡把值带回原样）。

### 3.3 声明位置

- `Style` 上声明（常用，随状态变体生效）：`.Transition(Prop::BackgroundColor, 150, Easing::EaseOut)`
- 元素上声明（临时）：`element->SetTransition(Prop::Opacity, 200, Easing::Linear)`

`transition` 列表本身是**非继承、不可动画**的样式属性（与 CSS 一致）。

---

## 4. 关键帧动画（Keyframes）

### 4.1 定义与运行

- `Keyframes` 编译期构造；**匿名即可直接传给 `Animate()`**（与 `Style` 同理，名字非必需），只有需要在 `animation` 属性里按名引用时（P1）才具名并注册。
- 进度点之间的插值缓动**每段可独立指定**（CSS 允许 keyframe 内写 easing）。
- 运行配置：`Duration` / `Delay` / `Iterations`（含 `Infinite`）/ `Direction` / `Fill` / `Easing`（缺省段缓动）。

```cpp
.Keyframe(0.f,   /* 缓动默认 */).Opacity(0.f).TranslationY(8.f)
.Keyframe(0.5f,  Easing::EaseOut).Opacity(1.f)
.Keyframe(1.f).Opacity(1.f)
```

### 4.2 方向与填充（对齐 CSS）

| 配置 | 取值 |
| --- | --- |
| `Direction` | `Normal` / `Reverse` / `Alternate` / `AlternateReverse` |
| `Fill` | `None` / `Forwards` / `Backwards` / `Both` |

`Fill::Forwards` 的实现：动画结束后把终值**保留在动画槽**（不清除），直到被新动画替换或显式 `Clear`（见 D2）。

### 4.3 控制接口（R5）

`Animate()` 返回句柄：`Pause()` / `Resume()` / `Cancel()` / `OnFinished(fn)`；句柄在动画结束或元素销毁后失效（判空语义，不悬挂）。

---

## 5. 缓动曲线

| 预设 | 参数（cubic-bezier） | 用途 |
| --- | --- | --- |
| `Linear` | — | 匀速、进度类 |
| `Ease` | (0.25, 0.1, 0.25, 1) | CSS 默认 |
| `EaseIn` / `EaseOut` / `EaseInOut` | 标准 CSS 参数 | 进出场 |
| `Standard` | (0.8, 0, 0.2, 1) | Fluent 风格通用过渡 |
| `Decelerate` / `Accelerate` | Material 系参数 | 进场/出场 |
| `Bezier(x1,y1,x2,y2)` | 自定义 | 兜底 |

- 实现：共享曲线预计算 **32 项 LUT**（常量表，零分配），评估时线性插值；自定义曲线按 (元素, 属性) 临时缓存于动画池。
- `Spring`（弹簧/阻尼）：可中断场景手感更好（Flutter/WinUI 主流），但需要速度状态与积分器，列为 **P1**（D4）。

---

## 6. transform 属性引入（建议，D1）

位移/缩放/旋转是动画的主要题材。若没有 transform，移动动画只能动 `margin` / 位置属性 → **每帧重排**，代价不可接受。建议引入三个**绘制级**属性：

| 属性 | 类型 | 失效 | 说明 |
| --- | --- | --- | --- |
| `translation` | `Vec2`（Length × 2） | Paint | 平移偏移 |
| `scale` | `Vec2`（倍数） | Paint | 缩放 |
| `rotation` | Float（弧度） | Paint | 旋转 |

- 三者均为**非继承、可动画、不参与布局**；在 Paint 阶段作为 2D 仿射变换应用于该元素子树的绘制（变换中心默认元素中心）。
- **必须同步作用于命中测试**（`UI.Event`），否则动画中的按钮点不中 —— 列为与 `Event` 模块的硬性接口。
- 不提供 CSS `transform: matrix(...)` 字符串形态；分解属性便于单独动画与插值，与 Flutter 的做法一致。
- `transformOrigin`（默认中心）列为 P2 待定。

> 此前 [03-Style.md](./03-Style.md) §3.2 将 `transform` 列为"暂不做"，本文建议**升级为 MVP**，理由如上；拍板后回填 03 的属性表。

---

## 7. 时序引擎

### 7.1 帧内位置

```
[输入][分发] → [动画 tick] → [失效][布局][收集][光栅][呈现]
```

动画 tick 夹在事件分发与失效之间：推进所有活动动画 → 写动画槽 → 按属性标志标脏 → 后续布局/绘制正常走。动画活动期间窗口保持按帧呈现（R6）；全部结束后回到空闲零功耗。

### 7.2 数据结构

```cpp
struct AnimationEntry {        // 每窗口固定容量池（如 256），O(n) tick
    Element*    element;
    PropId      prop;
    StyleValue  from, to;       // 关键帧动画按当前段取 from/to
    float       t0, duration;   // 秒
    Easing      easing;
    uint16_t    iterations;     // 0 = infinite
    uint8_t     direction, fill, state;   // state: Running/Paused/Done
    FnRef<void()> onFinished;
};
```

- 同 (元素, 属性) 唯一：新动画替换旧动画（中断语义，§3.2）。
- 元素销毁时由 `Invalidate`/析构路径清理其条目（元素持活动动画计数或反向钩子）。

### 7.3 与布局的联动（R7）

| 被动画属性 | 每帧代价 | 说明 |
| --- | --- | --- |
| `Paint` 类（颜色、透明度、cornerRadius、transform 三件套） | 仅重绘 | 主力用法 |
| `Layout` 类（width、padding…） | 每帧重排 | 允许但慎用，文档明确提示 |
| `Text` 类（fontSize…） | 每帧重排 + 重排版 | 最贵，基本不用 |

---

## 8. 动画子系统消费的属性

属性定义、类型与默认值统一见 [15-Properties.md](./15-Properties.md) §4.5 / §4.7（唯一事实来源，ADR-024）；本节只记录动画相关约束：

| 属性 | 类型 | 继承 | 失效 | 可动画 |
| --- | --- | --- | --- | --- |
| `transition` | TransitionList | ✗ | —（元属性） | ✗ |
| `animation` | AnimList（P1 进样式声明） | ✗ | — | ✗ |
| `translation` | Vec2 | ✗ | Paint | ✓ |
| `scale` | Vec2 | ✗ | Paint | ✓ |
| `rotation` | Float | ✗ | Paint | ✓ |

可动画性仍以属性表 `Animatable` 标志为准：颜色/数值/Vec2 可插值；枚举类（`textAlign` 等）不可，动画 API 编译期拒绝（模板静态断言，无需 RTTI）。

---

## 9. 与 DirectComposition 合成器动画的关系（D3）

| 层级 | 说明 | 取舍 |
| --- | --- | --- |
| **UI ticker（MVP）** | 每帧重绘窗口内容 | 通用，覆盖所有属性；代价是动画期间持续渲染 |
| 窗口级 DComp 动画（P2） | 对承载交换链的视觉做 opacity/offset 动画，**不重绘** | 窗口淡入淡出/移动几乎零成本；ADR-010 已有 DComp 设备，接入成本低 |
| 元素级 DComp 动画 | 每元素一个 Visual | 需要保留合成树、双份变换/裁剪同步，**近期不做** |

**建议：MVP 全走 UI ticker**；窗口级 DComp 动画作为 P2 优化（透明度/位置动画期间可跳过重绘）。这与 [01-Layering.md](./01-Layering.md) 中 `UI.Animation` 的"合成器驱动"描述收敛为同一结论；渲染侧的衔接（动画期间不放弃脏区、但不靠脏区省事，动画帧照常全量光栅）见 [10-Rendering.md](./10-Rendering.md) §5。

---

## 10. 决策点

### D1 transform 三件套：MVP 引入（建议）
理由见 §6。反方案（推迟到 P2）会让移动动画只能走每帧重排，R7 名存实亡。

### D2 `Fill::Forwards` 的实现：动画槽持久保留（建议）
终值留在动画槽直到被替换/清除。替代方案是把终值提交为本地覆盖 —— 会污染用户设置的覆盖项，且回滚语义模糊。

### D3 合成器驱动边界（建议）
见 §9：MVP = UI ticker，P2 = 窗口级 DComp，元素级 = 非目标（近期）。

### D4 Spring 缓动：P1（建议）
手感好但需要速度状态与可中断积分器，先以贝塞尔覆盖 MVP；中断语义（§3.2）已为 spring 预留（from 取当前值 + 当前速度）。

---

## 11. 内存与体积

| 对象 | 开销 | 备注 |
| --- | --- | --- |
| `AnimationEntry` | ~48 B × 活动数 | 每窗口固定池，容量超限按"最旧优先"淘汰并告警 |
| `Keyframes` / `Easing` LUT | 编译期常量 | 零运行时分配 |
| 关闭 `DUI_ENABLE_ANIMATION` | 0 | 动画槽、tick、Transition/Animate API 全部编译掉 |

---

## 12. 分期

| 期 | 内容 |
| --- | --- |
| **P0** | ticker + 帧循环接入、`Transition`（含中断语义）、`Animate()` 关键帧（迭代/方向/Fill/延迟/完成回调）、预设 + 贝塞尔缓动、transform 三件套、`DUI_ENABLE_ANIMATION` 裁剪 |
| **P1** | `animation` 进样式声明（命名注册表）、Spring、窗口级 DComp 动画（§9）、暂停/恢复的持久化 |
| **P2** | `transformOrigin`、变速/seek、滚动联动（[04-Layout.md](./04-Layout.md) D5 惯性滚动） |

---

## 13. 待确认

1. D1–D4 是否按建议拍板（尤其 transform 是否进 MVP，需回填 [03-Style.md](./03-Style.md)）。
2. 动画池容量与超限策略（淘汰最旧 vs 拒绝新动画 + 告警）。
3. `OnFinished` 回调的线程与重入约束（建议：UI 线程、tick 内仅记录延后派发）。
4. CSS `will-change` 类提示是否需要（当前按属性标志自动判脏，暂无对应优化点，倾向不做）。
5. 关键帧的百分比进度点是否需要支持 `0%..100%` 字面书写风格（当前为 0..1 浮点，纯口味问题）。
