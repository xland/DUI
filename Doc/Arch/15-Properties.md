# 属性主表（唯一事实来源）

> 状态：草案 v0.1 — 已合并 03/04/05 三处属性清单。
> 定位：全框架**唯一**的样式属性定义处。[03-Style.md](./03-Style.md) / [04-Layout.md](./04-Layout.md) / [05-Animation.md](./05-Animation.md) / [11-Text.md](./11-Text.md) 只描述**消费哪些属性与语义**，不再各自维护清单（ADR-024）。

## 1. 维护约定

- **新增/修改属性只改本文件 + 属性表宏**，其他文档只引用，不复制表格。
- 每个属性必须有：ID（编译期枚举）、类型、继承性、失效标志、默认值。**可动画性不在本表定义**，见 [05-Animation.md](./05-Animation.md) §2 的标签白名单。
- 属性一旦发布，ID 不复用、不重排（序列化与调试名依赖它）。
- 涉及缓存键的属性（如 `fontSize` / `fontFamily` 影响文本排版缓存、`width` 影响 measure 缓存）必须在备注列注明。

## 2. 值类型

```cpp
struct Length    { float value; Unit unit; };   // Dip | Px | Percent | Em | Auto
struct Thickness { Length left, top, right, bottom; };
struct Vec2      { float x, y; };
using  Color     = PremultipliedRGBA8;          // 8 B，全程预乘（ADR-013）
using  StringId  = uint32_t;                    // 驻留字符串（字体族等）
```

- **Dip**：默认单位，与 ADR-014 一致。
- **Percent**：相对父容器对应边（尺寸 / margin / padding）。
- **Em**：相对本元素 `fontSize`。
- **Auto**：尺寸类的自然大小。
- 动画不引入任何元属性：没有 `TransitionList` / `AnimList`（[05-Animation.md](./05-Animation.md) §1）。
- `TrackList`：Grid 轨道定义（固定 / 比例 / 自动 / 百分比），结构化，不做字符串解析。

## 3. 标志位

| 标志 | 含义 | 用途 |
| --- | --- | --- |
| `Inherit` | 沿元素树向下继承 | 遍历期由父计算值下传 |
| `Layout` | 改变后需重新度量/布局 | 标 `LayoutDirty` |
| `Text` | 需重新排版文本（`Layout` 子集） | 标 `TextDirty` |
| `Paint` | 仅需重绘 | 标 `PaintDirty` |
| ~~`Animatable`~~ | 已取消：不由属性表标志表达 | 命令式 `Animate()` 用 `AnimProp<T>` 标签在编译期限定可用属性 |

> §4 各表保留的"可动画"列是草案期标志残留，**不是运行时约束**；实际可动画的只有 [05-Animation.md](./05-Animation.md) §2 标签白名单列出的属性（Paint 标量、Paint 级 transform、布局尺寸对照）。

---

## 4. 属性清单

### 4.1 文本与字体

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `color` | Color | ✓ | Paint | ✓ | 主题 | |
| `fontFamily` | StringId | ✓ | Layout·Text | ✗ | 系统 UI 字体 | 排版缓存键 |
| `fontSize` | Length | ✓ | Layout·Text | ✓ | 14 dip | 排版缓存键；动画慎用 |
| `fontWeight` | Enum | ✓ | Layout·Text | ✗ | Normal | 排版缓存键 |
| `fontFamily` | String（`const wchar_t*` 指针） | ✓ | Layout·Text | ✓ | Microsoft YaHei | 框架默认微软雅黑；开发者可指定系统已安装任意字体族，指针生命周期由调用方保证（建议字面量）；未安装时 DWrite 自动回退默认。未安装字体经 `dui::LoadFontFamily` 装入，仅支持 ttf/otf/ttc（DWrite 只认 SFNT，woff2 不支持，需先转换） |
| `fontStyle` | Enum | ✓ | Layout·Text | ✗ | Normal | 排版缓存键 |
| `textAlign` | Enum | ✓ | Layout·Text | ✗ | Start | |
| `lineHeight` | Length（倍数） | ✓ | Layout·Text | ✓ | 1.2 | |
| `letterSpacing` | Length | ✓ | Layout·Text | ✓ | 0 | |
| `textDecoration` | Enum | ✓ | Paint | ✗ | None | |
| `direction` | Enum | ✓ | Layout | ✗ | LTR | BiDi / Flex 主轴方向 |

### 4.1.1 文本编辑（dui::TextBox / dui::TextArea 自绘；运行时已接入）

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `selectionColor` | Color | ✗ | Paint | ✓ | 半透明蓝 `0x803399FF` | 选区底色；链式方法 `SelectionColor()` |
| `caretColor` | Color | ✗ | Paint | ✓ | 取 `color`（文本色） | 光标色；链式方法 `CaretColor()` |

### 4.1.2 指针形态（鼠标光标；语义同 CSS 的 cursor，运行时已接入）

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `cursor` | Enum（`dui::Cursor`） | ✓ | 无（不影响布局/绘制） | ✗ | Arrow | `Arrow / IBeam / Hand / Wait / Crosshair / Move / SizeAll / NotAllowed / Help`；链式方法 `Cursor()` |

- **生效条件**：指针位于该元素上时（命中链就近者胜；元素未指定则沿链取父级的，同 CSS 的 `cursor` 继承）。不需要 hover 变体 —— 指针不在元素上时任何形态都不可见。
- 只能在 `WM_SETCURSOR` 里设置（系统每次鼠标移动后把光标重置为箭头）；`WM_SETCURSOR` 的 lParam 坐标不可靠，须 `GetCursorPos` + `ScreenToClient`。

### 4.2 盒模型与尺寸

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `padding` | Thickness | ✗ | Layout | ✓ | 0 | |
| `margin` | Thickness | ✗ | Layout | ✓ | 0 | 不做塌陷（04-Layout §7） |
| `width` / `height` | Length | ✗ | Layout | ✓ | Auto | measure 缓存键 |
| `minWidth` / `maxWidth` / `minHeight` / `maxHeight` | Length | ✗ | Layout | ✓ | Auto | |
| `boxSizing` | Enum | ✗ | Layout | ✗ | BorderBox | |

### 4.3 背景与装饰

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `backgroundColor` | Color | ✗ | Paint | ✓ | 透明 | Brush（渐变/图片）列为 P2 |
| `opacity` | Float | ✗ | Paint | ✓ | 1.0 | |
| `cornerRadius` | Thickness | ✗ | Paint | ✓ | 0 | |
| `borderWidth` | Thickness | ✗ | Layout·Paint | ✓ | 0 | |
| `borderColor` | Color | ✗ | Paint | ✓ | 透明 | |
| `shadow` | ShadowRef | ✗ | Paint | ✗ | None | |

### 4.3.1 滚动条（dui::Scroller 自绘轨道/滑块；MVP 子集，运行时已接入）

轨道（Track）与滑块（Thumb）各自独立一组；宽度 = 纵向条厚度，高度 = 横向条厚度
（滑块长/轨道长仍由视口与内容比决定，不可直接设置）。

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `scrollbarTrackWidth/Height` | Length | ✗ | Paint | ✓ | 10 | 条厚（DIP） |
| `scrollbarTrackBackgroundColor` | Color | ✗ | Paint | ✓ | 0x1F000000 | |
| `scrollbarTrackCornerRadius` | Length | ✗ | Paint | ✓ | 0 | 默认直角 |
| `scrollbarTrackBorderColor` | Color | ✗ | Paint | ✓ | 透明 | |
| `scrollbarTrackBorderWidth` | Length | ✗ | Paint | ✓ | 0 | 无边框 |
| `scrollbarThumb*`（同上 6 项） | 同上 | ✗ | Paint | ✓ | 厚 10 / 色 0x73000000 / 直角 / 无边框 | 滑块厚可异于轨道（居中） |

### 4.4 布局

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `display` | Enum | ✗ | Layout | ✗ | Flex | Flex / Grid / None |
| `flexDirection` | Enum | ✗ | Layout | ✗ | Column | 块级流语义来源 |
| `flexWrap` | Enum | ✗ | Layout | ✗ | NoWrap | NoWrap / Wrap（Wrap 仅 Row；运行时已实现，贪心断行） |
| `justifyContent` | Enum | ✗ | Layout | ✗ | Start | |
| `alignItems` | Enum | ✗ | Layout | ✗ | Stretch | |
| `alignSelf` | Enum | ✗ | Layout | ✗ | Auto | 未设则取父 `alignItems` |
| `flexGrow` | Float | ✗ | Layout | ✓ | 0 | |
| `flexShrink` | Float | ✗ | Layout | ✓ | 1 | |
| `flexBasis` | Length | ✗ | Layout | ✓ | Auto | |
| `gap` / `rowGap` / `columnGap` | Length | ✗ | Layout | ✓ | 0 | |
| `position` | Enum | ✗ | Layout | ✗ | Static | Static / Relative / Absolute / Fixed（运行时已实现，见 04-Layout §4.3） |
| `left` / `top` / `right` / `bottom` | Float（DIP） | ✗ | Layout | ✓（`left`/`top`） | 未设 = Auto | 仅 Relative / Absolute / Fixed 生效；Static 忽略；Relative 只认 left/top |
| `zIndex` | Int | ✗ | Layout | ✗ | 0 | 仅同一父容器内的 `Absolute` 生效；按值升序绘制、同值按树序；Fixed / 流内忽略（04-Layout §4.3） |
| `overflow` | Enum | ✗ | Layout·Paint | ✗ | Visible | Visible / Hidden / Scroll |
| `gridTemplateColumns` / `gridTemplateRows` | TrackList | ✗ | Layout | ✗ | 空 | |
| `gridRow` / `gridColumn` | Int | ✗ | Layout | ✗ | Auto | 显式放置 |
| `gridRowSpan` / `gridColumnSpan` | Int | ✗ | Layout | ✗ | 1 | 跨格 |

### 4.5 变换（Paint 级；运行时已接入）

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `TranslationX` / `TranslationY` | Float（DIP） | ✗ | Paint | ✓ | 0 | 链式方法同名；平移不改 `Bounds()` |
| `Rotation` | Float（**度**） | ✗ | Paint | ✓ | 0 | 支点 = 元素盒中心；正角度 = 视觉顺时针 |
| `Scale` | Float（等比倍率） | ✗ | Paint | ✓ | **1** | 不做非等比 / `TransformOrigin`（非等比会破坏 SDF 的 1px AA 过渡带） |

四个属性由 `ElementTransform()`（`UI/StyleResolve.cpp`）合成仿射矩阵：绘制期烘进顶点，命中测试逐层逆变换。实现与限制见 [10-Rendering.md](./10-Rendering.md) §6.4、ADR-020 补充。滚动容器的视口 scissor 仍是窗口空间轴对齐矩形，不随容器 transform 旋转；`PointerEvent::pos` 仍是客户区绝对 DIP，旋转的交互文本框光标定位列为后续项。

### 4.6 交互与可见性

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `visibility` | Enum | ✓ | Layout | ✗ | Visible | 隐藏但仍占位 |
| `cursor` | Enum | ✓ | — | ✗ | Default | 由 Event 层读取 |

### 4.7 动画相关属性

**没有动画元属性**。`transition` / `animation` 从未进入运行时属性表（`Prop` 枚举），早期草案的这两项已作废。命令式动画直接对普通属性补间；当前开放的是 `opacity` / `backgroundColor` / `color` / `selectionColor` / `caretColor`、§4.5 的四个 transform 标量，以及 `width` / `height` / `left` / `top`（布局尺寸与定位偏移对照，每帧重排），标签、插值与语义见 [05-Animation.md](./05-Animation.md) §2–§3。

### 4.8 暂不做

渐变/图片背景（需 Brush 与资源引用）、非等比缩放 / `TransformOrigin` / 矩阵形态 transform、多 Style 引用（class 列表，见 03-Style D3）、`will-change` 类提示。

---

## 5. 运行时读路径（ADR-024）

属性值有两个读取入口，**同一帧内必须给出相同结果**：

### 5.1 遍历内（布局 / 绘制）

```cpp
// 真实签名（已落地，Element/UI.h）
void Widget::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
    const StyleValue radius = ResolveProperty(this, Prop::CornerRadius, ctx);
    p.FillRoundedRect(Bounds(), radius.f, ResolveProperty(this, Prop::BackgroundColor, ctx).u, 1.f, scale);
}
```

- 继承属性：`StyleContext` 已携带父计算值，读取为 O(1)。
- 非继承属性：`ResolveProperty` 查元素自身链（本地覆盖 → Style 派生链 → 主题），通常 1–3 跳。
- 钩子默认空实现；收集期在背景之后、子元素之前调用（`App/Frame.cpp`）。

### 5.2 遍历外（事件 / 命中测试 / 动画 / 任意代码）

```cpp
Color c = Style::Resolve(element, Prop::Color);
StyleValue opacity = ResolveProperty(element, Prop::Opacity);
```

- **非继承属性**：只查元素自身链，O(1–3)。
- **继承属性**：自元素向上找最近设置了该属性的祖先，O(树深)；调用频率低（事件类路径），不做缓存。
- 两条路径都先查级联第 1 层动画值槽；transform 属性的遍历外读取由 `ElementTransform()` 完成，含该动画值槽（[10-Rendering.md](./10-Rendering.md) §6.4）。

### 5.3 动画值槽（级联第 1 层）

- 命令式 `Animate()` 把每帧插值结果稀疏写入 `Element::animated_`（`std::vector<StyleValue>`）；`TryResolveElement()` 最先查它，因此遍历内 `StyleContext` 与遍历外 `Style::Resolve` 同帧一致。
- **不设过渡旧值槽**，也没有动画池 / 活动动画链表：属性基值变化（`Set` / `SetStyle` / 状态切换 / 主题替换）不会隐式触发动画。只有显式调用 `Animate()` 才建补间，起点取当前显示值（含已有的第 1 层值）。
- 动画完成后终值留在槽中（fill forwards），`Cancel()` 清除并回落基值；元素销毁时注销，残留句柄失效。详见 [05-Animation.md](./05-Animation.md) §3。

---

## 6. 不属于本表的属性

富文本的**字符级/段落级样式**是内容属性（随文本复制、序列化），不进样式系统，见 [11-Text.md](./11-Text.md) §4.1。衔接点只有一处：文档默认样式由所在元素的样式注入。

---

## 7. 待确认

1. `shadow` / `textDecoration` 是否进 MVP（当前列在表内但排期靠后）。
2. `zIndex` 是否扩展到普通流（当前仅同一父容器内的 Absolute，已落地，见 04-Layout §4.3）。
3. `alignItems: Baseline` 是否纳入（文本混排场景常用）。
4. 是否需要 `letterSpacing` 之外的 `wordSpacing`。
