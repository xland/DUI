# 属性主表（唯一事实来源）

> 状态：草案 v0.1 — 已合并 03/04/05 三处属性清单。
> 定位：全框架**唯一**的样式属性定义处。[03-Style.md](./03-Style.md) / [04-Layout.md](./04-Layout.md) / [05-Animation.md](./05-Animation.md) / [11-Text.md](./11-Text.md) 只描述**消费哪些属性与语义**，不再各自维护清单（ADR-024）。

## 1. 维护约定

- **新增/修改属性只改本文件 + 属性表宏**，其他文档只引用，不复制表格。
- 每个属性必须有：ID（编译期枚举）、类型、继承性、失效标志、可动画性、默认值。
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
- 元属性类型：`TransitionList`、`AnimList`（不可插值）。
- `TrackList`：Grid 轨道定义（固定 / 比例 / 自动 / 百分比），结构化，不做字符串解析。

## 3. 标志位

| 标志 | 含义 | 用途 |
| --- | --- | --- |
| `Inherit` | 沿元素树向下继承 | 遍历期由父计算值下传 |
| `Layout` | 改变后需重新度量/布局 | 标 `LayoutDirty` |
| `Text` | 需重新排版文本（`Layout` 子集） | 标 `TextDirty` |
| `Paint` | 仅需重绘 | 标 `PaintDirty` |
| `Animatable` | 可被过渡/关键帧驱动 | 动画 API 编译期校验（模板静态断言，无 RTTI） |

---

## 4. 属性清单

### 4.1 文本与字体

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `color` | Color | ✓ | Paint | ✓ | 主题 | |
| `fontFamily` | StringId | ✓ | Layout·Text | ✗ | 系统 UI 字体 | 排版缓存键 |
| `fontSize` | Length | ✓ | Layout·Text | ✓ | 14 dip | 排版缓存键；动画慎用 |
| `fontWeight` | Enum | ✓ | Layout·Text | ✗ | Normal | 排版缓存键 |
| `fontFamily` | String（`const wchar_t*` 指针） | ✓ | Layout·Text | ✓ | Microsoft YaHei | 框架默认微软雅黑；开发者可指定系统已安装任意字体族，指针生命周期由调用方保证（建议字面量）；未安装时 DWrite 自动回退默认 |
| `fontStyle` | Enum | ✓ | Layout·Text | ✗ | Normal | 排版缓存键 |
| `textAlign` | Enum | ✓ | Layout·Text | ✗ | Start | |
| `lineHeight` | Length（倍数） | ✓ | Layout·Text | ✓ | 1.2 | |
| `letterSpacing` | Length | ✓ | Layout·Text | ✓ | 0 | |
| `textDecoration` | Enum | ✓ | Paint | ✗ | None | |
| `direction` | Enum | ✓ | Layout | ✗ | LTR | BiDi / Flex 主轴方向 |

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

### 4.4 布局

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `display` | Enum | ✗ | Layout | ✗ | Flex | Flex / Grid / None |
| `flexDirection` | Enum | ✗ | Layout | ✗ | Column | 块级流语义来源 |
| `flexWrap` | Enum | ✗ | Layout | ✗ | NoWrap | |
| `justifyContent` | Enum | ✗ | Layout | ✗ | Start | |
| `alignItems` | Enum | ✗ | Layout | ✗ | Stretch | |
| `alignSelf` | Enum | ✗ | Layout | ✗ | Auto | 未设则取父 `alignItems` |
| `flexGrow` | Float | ✗ | Layout | ✓ | 0 | |
| `flexShrink` | Float | ✗ | Layout | ✓ | 1 | |
| `flexBasis` | Length | ✗ | Layout | ✓ | Auto | |
| `gap` / `rowGap` / `columnGap` | Length | ✗ | Layout | ✓ | 0 | |
| `position` | Enum | ✗ | Layout | ✗ | Static | Static / Absolute |
| `left` / `top` / `right` / `bottom` | Length | ✗ | Layout | ✓ | Auto | 仅 Absolute 生效 |
| `zIndex` | Int | ✗ | Paint（Z 序） | ✗ | 0 | MVP 仅对 Absolute 生效 |
| `overflow` | Enum | ✗ | Layout·Paint | ✗ | Visible | Visible / Hidden / Scroll |
| `gridTemplateColumns` / `gridTemplateRows` | TrackList | ✗ | Layout | ✗ | 空 | |
| `gridRow` / `gridColumn` | Int | ✗ | Layout | ✗ | Auto | 显式放置 |
| `gridRowSpan` / `gridColumnSpan` | Int | ✗ | Layout | ✗ | 1 | 跨格 |

### 4.5 变换（绘制级）

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `translation` | Vec2 | ✗ | Paint | ✓ | (0,0) | 影响命中测试与裁剪 |
| `scale` | Vec2 | ✗ | Paint | ✓ | (1,1) | 同上 |
| `rotation` | Float（弧度） | ✗ | Paint | ✓ | 0 | 同上 |

### 4.6 交互与可见性

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `visibility` | Enum | ✓ | Layout | ✗ | Visible | 隐藏但仍占位 |
| `cursor` | Enum | ✓ | — | ✗ | Default | 由 Event 层读取 |

### 4.7 动画元属性

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `transition` | TransitionList | ✗ | — | ✗ | 空 | 元属性，见 05-Animation §3 |
| `animation` | AnimList | ✗ | — | ✗ | 空 | P1 进样式声明 |

### 4.8 暂不做

渐变/图片背景（需 Brush 与资源引用）、`transform` 的矩阵/原点形态（用三件套替代）、多 Style 引用（class 列表，见 03-Style D3）、`will-change` 类提示。

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
Vec2  t = Style::Resolve(element, Prop::Translation);
```

- **非继承属性**：只查元素自身链，O(1–3)。
- **继承属性**：自元素向上找最近设置了该属性的祖先，O(树深)；调用频率低（事件类路径），不做缓存。
- 命中测试只读 `translation` / `scale` / `rotation`（非继承），成本可控。

### 5.3 动画槽与过渡旧值槽

- 级联第 1 层"动画值"由动画系统维护：**元素内持有活动动画链表的头指针**，条目在动画池中；`StyleContext` 与 `Style::Resolve` 都通过该链表查第 1 层，保证两路径一致。
- **过渡旧值槽**：仅对**声明了 `Transition` 的属性**在元素上保留旧值，内联容量 2（`{PropId, StyleValue}`），溢出到 Arena；无过渡声明的元素零开销。
- 过渡触发：属性基值变化时（`Set` / `SetStyle` / 状态切换 / 主题替换统一走变更入口），与旧值比较 → 不同则按声明建过渡，起点取当前显示值（[05-Animation.md](./05-Animation.md) §3.2）。

---

## 6. 不属于本表的属性

富文本的**字符级/段落级样式**是内容属性（随文本复制、序列化），不进样式系统，见 [11-Text.md](./11-Text.md) §4.1。衔接点只有一处：文档默认样式由所在元素的样式注入。

---

## 7. 待确认

1. `shadow` / `textDecoration` 是否进 MVP（当前列在表内但排期靠后）。
2. `zIndex` 是否扩展到普通流（当前仅 Absolute）。
3. `alignItems: Baseline` 是否纳入（文本混排场景常用）。
4. 是否需要 `letterSpacing` 之外的 `wordSpacing`。
