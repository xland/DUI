# 布局系统（HTML 风格）

> 状态：草案 v0.1 — 待讨论确认。
> 需求来源：支持**类似 HTML 的布局模式** —— 写界面时获得与写 HTML/CSS 相近的直觉：块级流、自动换行、盒模型、百分比尺寸、网格与绝对定位。
> 相关：ADR-005（保留模式）、ADR-006（布局算法，本文细化）、[03-Style.md](./03-Style.md)（样式属性与失效标志）。

## 1. 需求

| 编号 | 需求 | 验收口径 |
| --- | --- | --- |
| R1 | 块级流 | 子元素默认垂直堆叠，宽度 `auto` 时拉伸填满父内容盒（`div` 的直觉） |
| R2 | 内联流与换行 | 行内排列，空间不足自动换行（`span` / 文本的直觉） |
| R3 | 弹性布局 | 主轴/交叉轴对齐、`grow` / `shrink` / `basis`、换行、`gap` |
| R4 | 网格 | 定义轨道（固定/比例/自动），子项占格、可跨行跨列 |
| R5 | 绝对定位 | 脱离文档流，相对父容器（或定位祖先）定位 |
| R6 | 盒模型 | `content + padding + border + margin`，可切 `box-sizing` |
| R7 | 尺寸表达 | `auto` / 固定 / 百分比 / `min`·`max` 约束 |
| R8 | 溢出与滚动 | 内容超出容器可隐藏或滚动 |
| R9 | 只付必要代价 | 改布局属性才触发布局，改颜色不重排（与 [03-Style.md](./03-Style.md) R7 一致） |

### 1.1 约束（沿用既定决策）

- 热路径零分配：measure / arrange 全程不分配，中间量走布局 Arena 或栈。
- 无运行时解析：不提供 CSS 字符串（`"1fr auto 1fr"` 之类），轨道/约束用结构化 C++ API。
- 内部单位 DIP（ADR-014）；样式属性挂接走 [03-Style.md](./03-Style.md) 的属性表与失效标志。

---

## 2. 总体思路

**定位：借鉴 HTML 的直觉，不追求与浏览器逐项一致。** §7 的差异清单是**既定设计取舍**，不是待补的欠账。

**不做独立的 block / inline 布局引擎**，而是：`display` 驱动的多模式布局（Flex 子集 + Grid + 绝对定位），HTML 的块级流语义通过**默认值组合**呈现：

```
display: Flex; flexDirection: Column; alignItems: Stretch   ← 默认值
```

这套默认值对写界面的开发者来说，行为与 HTML 块级流一致：子元素竖着排、宽度自动撑满 —— 不需要学新概念，需要弹性时再显式改属性。

| HTML/CSS 概念 | DUI 中的对应 |
| --- | --- |
| `div`（块级流） | 默认值组合（§2 开头），无需任何设置 |
| `flex` 容器 | `display: Flex` + `flexDirection: Row` |
| `inline` / 文本混排 | 交给 Text 层排版（§4.4），容器级内联 = `flexWrap: Wrap` 的行布局 |
| `grid` | `display: Grid`（§4.2） |
| `position: absolute` | `position: Absolute`（§4.3） |
| `table` | 不做，Grid 覆盖（§7） |
| `float` | 不做（§7） |

> 与 ADR-006 的关系：ADR-006 已定"自研简化 Flex + 堆叠 + 绝对 + 网格"。本文不推翻它，而是把"HTML 风格"落为其上的**语义层**（默认值组合 + 盒模型 + 尺寸系统），并在 §7 明确与 CSS 的差异。

---

## 3. 盒模型

```
┌─────────────────────────────────────┐
│ margin                              │
│  ┌───────────────────────────────┐  │
│  │ border                        │  │
│  │  ┌─────────────────────────┐  │  │
│  │  │ padding                 │  │  │
│  │  │  ┌───────────────────┐  │  │  │
│  │  │  │ content           │  │  │  │
│  │  │  └───────────────────┘  │  │  │
│  │  └─────────────────────────┘  │  │
│  └───────────────────────────────┘  │
└─────────────────────────────────────┘
```

- `width` / `height` 指哪一盒由 `boxSizing` 决定；**默认 `BorderBox`**（与 CSS 默认 `content-box` 不同，但与业界实践一致，写界面更直觉）。
- `padding` / `borderWidth` / `margin` 为 `Thickness`（四边可独立）。
- `cornerRadius`（[03-Style.md](./03-Style.md)）只影响绘制裁剪，不影响布局矩形。

---

## 4. 布局模式

### 4.1 Flex（默认模式）

| 属性组 | 内容 |
| --- | --- |
| 方向 | `flexDirection: Row / Column`；主轴方向受 `direction`（RTL）影响 |
| 换行 | `flexWrap: NoWrap / Wrap`（对应 R2 的容器级行为） |
| 主轴对齐 | `justifyContent: Start / Center / End / SpaceBetween / SpaceAround / SpaceEvenly` |
| 交叉轴对齐 | `alignItems` / `alignSelf: Stretch / Start / Center / End / Baseline?`（Baseline 待定） |
| 弹性 | `flexGrow` / `flexShrink` / `flexBasis` |
| 间距 | `gap`（主轴）/ `rowGap`·`columnGap`（换行或 Grid） |

块级流 = `Column + Stretch` 默认组合；行内流 = `Row + Wrap`。

### 4.2 Grid

```cpp
// 结构化轨道定义（不做 CSS 字符串解析）
gridTemplateColumns({ Flex(1), Fixed(120), Auto, Flex(2) });
child->Set(Prop::GridColumn, 1);   // 从 0 计
child->Set(Prop::GridRowSpan, 2);
```

- 轨道类型：`Fixed` / `Flex`（份数）/ `Auto`（取该轨道内容最大值）/ `Percent`。
- 放置：MVP 支持**按序自动放置** + 显式 `gridRow` / `gridColumn` + `RowSpan` / `ColumnSpan`；不做"命名区域"。
- 先算 `Auto` 轨道（需要子项自然尺寸），再按剩余空间分 `Flex` 份数。

### 4.3 绝对定位

```cpp
overlay->Set(Prop::Position, Position::Absolute);
overlay->Set(Prop::Left, 16.f);
overlay->Set(Prop::Top, Length::Percent(50));
```

- 相对**父容器 padding 盒**定位（比 CSS 的"最近定位祖先"规则简单，够用；引入中间定位容器即等价）。
- 脱离文档流：不参与兄弟的 measure / arrange；四边可同时设置以实现"拉伸"。
- `zIndex`：MVP 仅对 `Absolute` 生效（树序 + zIndex 排序）；普通流内提升 Z 序列为 P2。

### 4.4 内联混排与文本的关系

文本段落内的混排（文字 + 行内图标 + 行内按钮，随文本换行）**归 Text 层**，不属于容器布局：

- `Text.Layout`（基于 `IDWriteTextLayout`，见 ADR-004）已支持 inline object：富文本场景由文本元素承接，行内对象实现文本层的内联接口即可。
- 容器布局不复制这套规则，避免两套换行算法。

> 这是"HTML 风格"与浏览器实现的最大分工差异：**容器布局不处理 inline 流，inline 流是文本排版的延伸**。富文本模型已定（见 [11-Text.md](./11-Text.md) §4）：行内混排由富文本文档模型 + 排版引擎承接，本结论不变。

---

## 5. 尺寸与约束

```cpp
struct Constraints { float minW, maxW, minH, maxH; };   // 传给子树的可用空间
```

- `auto`：内容自然尺寸（文本 = 排版结果，图片 = 解码尺寸，容器 = 子项聚合）。
- 百分比：宽相对父**内容盒宽**，高相对父**内容盒高**；父该方向为 `auto` 时按 CSS 惯例解析为 `auto`（列于 §7 差异 6）。
- `min*` / `max*` 在排列结果上夹紧，并参与约束传递。
- 约束向下传递时按模式收紧（`Exactly` / `AtMost`），与 Flutter 的单遍约束模型同构。

---

## 6. 算法

### 6.1 两遍遍历

```
[Measure] 自底向上：Constraints 传入 → 返回自然 Size
   每元素缓存 (constraints 归一化 key → Size)，脏时失效
[Arrange] 自顶向下：按自身模式把确定的 Rect 分给子项，写回 (x, y, w, h)
```

- 与 CSS 完整规范（多遍 intrinsic sizing）的差别：**单遍近似**。绝大多数界面一致；个别嵌套百分比/复杂 `Auto` 轨道场景与浏览器有出入，见 §7 差异 7。
- 布局结果（`x/y/w/h` + measure 缓存）存于元素固定字段与布局 Arena，**无堆分配**；Arena 每帧布局后整体重置（与 [01-Layering.md](./01-Layering.md) §4 的内存策略一致）。

### 6.2 失效

| 来源 | 范围 |
| --- | --- |
| 样式属性带 `Layout` 标志 | 自身 + 子树（[03-Style.md](./03-Style.md) §7） |
| 子树尺寸可能变化（文本重排、图片加载完成） | 自底向上沿 measure 链上溯到首个"尺寸未变"的祖先即停 |
| 容器尺寸变化 | 全部后代重排，但 measure 缓存按 constraints 命中可跳过 |

- 滚动（`overflow: Scroll`）**不触发布局**：只改内容偏移 + 脏区，与 `UI.Invalidate` 联动；滚动条控件属 `Widgets`。

### 6.3 滚动容器的布局规则（M5.6 落地）

控件：`dui::Scroller`（派生 `Panel`，`Kind::Scroller`）。滚动**只改绘制坐标，不改布局结果**：
子项仍按自然尺寸排布在内容盒里，溢出部分由绘制期的 scissor 裁掉（10-Rendering §6.3）。

| 轴 | 内容盒长度 | 说明 |
| --- | --- | --- |
| 主轴（`FlexDirection` 方向） | 内容自然尺寸 | 可溢出 → 产生滚动量；子项**不参与** flex 收缩 |
| 交叉轴 | 视口尺寸 | 子项无固定交叉尺寸时拉满视口宽；固定尺寸且大于视口时溢出 → 横向也可滚 |

- 测量期记录自然尺寸（`Scroller::NaturalSize()`），排布期回写**内容实际范围**
  （子项外接盒，`Scroller::ContentSize()`）→ 滚动量 = 内容范围 − 视口尺寸。
  用实际范围而非自然尺寸，嵌套滚动容器才不会把外层的滚动量算大。
- 滚动量随内容变化收敛：`Scroller::ClampScroll()` 在收集期把偏移夹到合法区间
  （内容变短 / 视口变大时越界）。
- 命中测试须手动补偿偏移（滚动不改 `Bounds()`）：`Event.cpp` 进入滚动容器时把
  指针换算到内容坐标（`+滚动偏移`），视口外归容器自身。

---

## 7. 与 HTML/CSS 的已知差异（显式声明）

| # | 差异 | 理由 |
| --- | --- | --- |
| 1 | **margin 不塌陷**（相邻/父子 margin 直接相加或叠加占位，不合并） | 塌陷是 HTML 历史包袱，与"显式优于隐式"冲突；用 `gap` 表达间距更直白 |
| 2 | 无 `float` | 现代场景由 flex/grid 覆盖 |
| 3 | 无表格布局 | Grid 覆盖（R4） |
| 4 | 无选择器与层叠 | 样式复用走 [03-Style.md](./03-Style.md)（共享/派生/继承） |
| 5 | 容器布局不处理 inline 流 | 内联混排归 Text 层（§4.4） |
| 6 | 父尺寸 `auto` 时百分比解析为 `auto` | 与 CSS 主流行为一致，显式列出避免歧义 |
| 7 | 单遍约束近似，不做完整 intrinsic sizing 多遍求解 | 换取实现复杂度大幅下降（"不追求通用"原则）；**有意取舍，不追求与浏览器逐项一致** |
| 8 | `box-sizing` 默认 `BorderBox` | 业界实践，写界面更直觉 |

---

## 8. 布局子系统消费的属性

属性定义、类型与默认值统一见 [15-Properties.md](./15-Properties.md) §4.4（唯一事实来源，ADR-024）；本节只记录**取值语义**（均带 `Layout` 标志，`display`/`visibility` 例外说明同前）：

| 属性 | 类型 | 说明 |
| --- | --- | --- |
| `display` | Enum | `Flex`（默认）/ `Grid` / `None` |
| `flexDirection` | Enum | Row / Column（默认 Column） |
| `flexWrap` | Enum | NoWrap（默认）/ Wrap |
| `justifyContent` | Enum | 主轴对齐 |
| `alignItems` / `alignSelf` | Enum | 交叉轴对齐，默认 Stretch |
| `flexGrow` / `flexShrink` | Float | 默认 0 / 1 |
| `flexBasis` | Length | 默认 Auto |
| `gap` / `rowGap` / `columnGap` | Length | 默认 0 |
| `boxSizing` | Enum | BorderBox（默认）/ ContentBox |
| `position` | Enum | Static（默认）/ Absolute |
| `left` / `top` / `right` / `bottom` | Length | 仅 Absolute 生效 |
| `zIndex` | Int | 仅 Absolute 生效（MVP） |
| `overflow` | Enum | Visible（默认）/ Hidden / Scroll |
| `gridTemplateColumns` / `gridTemplateRows` | TrackList | 轨道定义 |
| `gridRow` / `gridColumn` / `gridRowSpan` / `gridColumnSpan` | Int | 显式放置与跨格 |

---

## 9. 决策点

### D1 块级流的实现方式：默认值组合（建议）vs 独立 block 引擎

| 方案 | 体 | 性 | 复 | 说明 |
| --- | --- | --- | --- | --- |
| **Flex 子集 + 默认值组合**（建议） | ++ | ○ | ++ | 一套引擎覆盖块流/行流/弹性；块流语义 = `Column + Stretch` |
| 独立 block 布局引擎 + flex 引擎 | -- | + | -- | 与 CSS 一致性更高，但两套算法、两套失效路径 |

### D2 内联混排的归属：Text 层（建议）vs 容器 inline 布局

**建议：Text 层。** 复用 `IDWriteTextLayout` 的换行与 inline object，避免两套换行算法；富文本需求与 ADR-017 联动，未定前容器布局不预埋。

### D3 margin 塌陷：不做（建议）

见 §7-1。若后续有强需求，再评估"仅相邻兄弟塌陷"的受限版本。

### D4 Grid 的 MVP 边界

建议：轨道（Fixed/Flex/Auto/Percent）+ 自动放置 + 显式行列 + 跨格；**不做**命名区域、嵌套 grid 专项优化、`minmax()` / `fit-content()` 等函数式轨道（可用 `min/max` 属性近似）。

### D5 滚动模型

建议：`overflow: Scroll` + 容器内容偏移（不重排）+ 独立滚动条控件；惯性/平滑滚动与 `UI.Animation` 联动，列为 P1。

---

## 10. 分期

| 期 | 内容 |
| --- | --- |
| **P0** | 盒模型（含 box-sizing）、Flex 全量（§4.1）、绝对定位、auto/固定/百分比、min/max、`display: None` |
| **P1** | Grid（§9-D4 边界）、`overflow: Scroll` + 滚动条、RTL（`direction` 与 flex 主轴联动，依赖 Text 层） |
| **P2** | 普通流的 `zIndex`、`alignItems: Baseline`、`position: Relative`、嵌套百分比与浏览器对齐的精化（§7-7） |

---

## 11. 待确认

1. D1–D5 是否按建议拍板。
2. 默认值组合（`Column + Stretch`）是否作为全局默认 —— 影响所有控件的书写习惯。
3. Grid 是否进 P0（若控件库对表单/列表有强需求则提前）。
4. 富文本 / inline 混排是否进 MVP（影响 §4.4 与 ADR-017）。
5. `alignItems: Baseline` 是否需要（文本混排场景常用，但实现涉及跨元素基线对齐）。
6. 滚动的具体形态：像素滚动 vs 行滚动、滚动条常显/悬浮。
