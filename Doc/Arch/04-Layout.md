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
| R5 | 绝对定位 | 脱离文档流，相对最近定位祖先（无则根）定位 |
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
| `position: absolute` / `fixed` | `Position::Absolute` / `Fixed`（§4.3） |
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
- **`margin` 运行时已落地（M5.22）**：`Style::Margin(float)` / `Margin(Thickness)` 声明四边外边距（左/上/右/下，DIP，默认 0）。`width` / `height` / `minWidth` / `maxWidth` 仍指元素盒；Measure 结果为**外边距盒**（元素盒 + margin），父布局的聚合、grow/shrink、Grid 轨道、Wrap 装箱与滚动容器 `extent` 都按它计算，Arrange 再从槽位收缩出 `Bounds()`。因此 margin 区域不画背景、也不响应命中；相邻 margin 直接叠加、不塌陷（§7-1）。

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

### 4.2 Grid（运行时已落地）

```cpp
grid->SetStyle(dui::Style()
    .Display(dui::Display::Grid)
    .GridTemplateColumns({dui::Fixed(120.f), dui::Flex(1.f), dui::Auto, dui::Percent(20.f)})
    .GridTemplateRows({dui::Fixed(52.f), dui::Flex(1.f)})
    .Gap(8.f));
child->Set(dui::Prop::GridColumn, 1);       // 从 0 计
child->Set(dui::Prop::GridColumnSpan, 2);
```

- 轨道类型：`Fixed`（DIP）/ `Flex`（份数）/ `Auto`（该轨道内容最大值）/ `Percent`（百分数 0..100）。结构化定义，不做 CSS 字符串解析。
- `TrackList` 是变长声明，实体存在 `Style` 内（`FindTracks` 沿派生链由近及远找），不走定长 `StyleValue`；因此不可动画、无状态变体、无 `Element::Set` 运行时覆盖。两个 `gridTemplate*` `Prop` 仅作名义声明与失效标志。
- 放置：**按序自动放置** + 显式 `gridRow` / `gridColumn`（0 基）+ `gridRowSpan` / `gridColumnSpan`（默认 1）；显式格子被占时放宽为按序自动放置。行不足时追加隐式行（未声明 `gridTemplateRows` 时全部按 `Auto`）。不做命名区域。
- 轨道求解：`Auto` 先取子项自然尺寸（跨格按均摊），再定 `Fixed` / `Percent`，最后把剩余空间按 `Flex` 份数分配；测量阶段（`MeasureGridContent`）与排布阶段共用 `ComputeGrid` 同一套规则，避免测量与排布尺寸不一致。
- 行列间距统一用 `gap`（`rowGap`/`columnGap` 未做）；子项默认 `AlignItems::Stretch` 占满格子，`Start`/`Center` 用自然尺寸；`JustifyContent` 不参与 Grid（格子已定）。
- 自然尺寸阶段没有确定容器尺寸，`Percent` / `Flex` 轨道保留内容下界；不设 `gridTemplateColumns` 时退化为单列。示例见 `Samples/Layout` 末段。

### 4.3 定位（运行时已落地）

```cpp
// Relative：占着流内原位，只有自己挪（Bounds 真偏移）
row->AddChild<dui::Panel>(dui::Style()
    .Position(dui::Position::Relative).Left(24.f).Top(16.f)
    .Width(120.f).Height(48.f));

// Absolute：脱离流，相对最近 position != Static 的祖先 padding 盒。
// 想让直接父成为基准，就给父设 Relative；否则一路回溯到根（窗口客户区）。
parent->SetStyle(dui::Style().Position(dui::Position::Relative));
parent->AddChild<dui::Panel>(dui::Style()
    .Position(dui::Position::Absolute).Right(12.f).Bottom(12.f)
    .Width(150.f).Height(36.f));

// Fixed：钉在窗口客户区（可挂在任意层级，含 Scroller 内部）
anyParent->AddChild<dui::Panel>(dui::Style()
    .Position(dui::Position::Fixed).Right(16.f).Bottom(16.f)
    .Width(180.f).Height(44.f));
```

| 值 | 占位 | 基准 | 滚动 |
| --- | --- | --- | --- |
| `Static`（默认） | 是 | 父内容盒 | 随内容 |
| `Relative` | 是，兄弟不动 | 自身流内位置 + `Left/Top` | 随内容 |
| `Absolute` | 否 | 最近 `position != Static` 祖先的 padding 盒；无则根（窗口客户区） | 随 CB 所在滚动上下文；非 CB 的中间 Scroller 不裁它 |
| `Fixed` | 否 | 窗口客户区（忽略 Root padding） | 不随滚动，也不随祖先 transform |

- `Left/Top/Right/Bottom` 单位 DIP；**未设的一边 = Auto**（用元素自然尺寸），同设 `Left+Right` / `Top+Bottom` 则拉伸。百分比未做。
- `Relative` 只认 `Left/Top`（`Right/Bottom` 忽略）；改的是 `Bounds()`，故绘制、命中、焦点链自动一致，偏移也会撑大父的 `extent`（滚动范围，同 CSS overflow region）。
- `Static` 下四边属性一律忽略 —— 要移动普通元素用 `TranslationX/Y`（Paint 级变换，不改 `Bounds()`、不重排）。
- 脱离流子项仍被测量（自然尺寸），但不参与父的 measure 聚合、grow/shrink、Wrap 与 `extent`。
- `Absolute` 的定位基准与层叠归属都是 **CB（最近定位祖先）**：布局期把每个 Absolute 登记到 CB 名下的锚点表，绘制 / 命中在该 CB 的脱离流趟统一消费（流内子项按树序先画、脱离流项按 `zIndex` 升序后画，命中倒序）。因此可**跨中间容器提升**：非 CB 的中间容器（含 `Scroller`）既不改变基准，也不施加滚动原点 / 视口 scissor / 内容裁剪；CB 自身的 overflow 仍裁它，CB 在滚动内容里则随内容滚动。`Bounds()` 已是绝对布局坐标，跨层无需坐标累加。
  - 跨层项在 CB 处绘制时，样式继承仍沿**真实父链**重建（`Collector::ContextOf`）；transform 祖先不成为 CB，故中间祖先的变换不施加到跨层项（绘制与命中两侧一致，见 §7-9）。
  - 锚点表按布局遍历顺序分组、跨帧复用（`clear` 保留容量），稳态零分配；`UseOrderedOutOfFlow()`（存在 Absolute 或使用 zIndex）为 false 时绘制 / 命中四处都走原两趟，未使用 Absolute 的应用零额外遍历与解析。
- `Fixed` 可挂在**任意层级**（含 Scroller 内部），整棵子树延后到**窗口级 Fixed 层**：主树（含滚动条）全部画完后统一绘制，因此所有 Fixed 都盖过普通内容。绘制与命中都用窗口客户区坐标：
  - 不受祖先 `TranslationX/Y/Rotation/Scale` 与 Scroller 内容原点（`-off`）影响；Fixed **自身**的 transform 仍生效。与 CSS 不同：带 transform 的祖先**不**成为 containing block（§7-9）。
  - 祖先 `Display::None` 与 `ContentClipRect` 仍约束它（用记录时的祖先裁剪快照），Scroller 的**视口 scissor 不裁它** —— 否则挂在滚动容器里就退化成 Absolute。
  - `Opacity` 当前是逐元素、非继承属性，祖先 opacity 不累积到 Fixed（15-Properties §4.1）。
  - 多个 Fixed 按遍历顺序（流内子项先、`Absolute` 子树后；同组按树序）记录，倒序命中即"后画者在上"。Fixed 子树内再嵌套 Fixed 不二次提升，就地绘制。
  - 布局期用 `FixedCount()` 统计；计数为 0 时绘制与命中两侧都整段短路，未使用 Fixed 的应用零额外遍历与属性解析（10-Rendering §6.3）。
- `zIndex`（Int，默认 0，可为负）**只对同一 CB 内的 `Absolute` 生效**：按值升序绘制（小值在下），同值保持锚点表顺序（≈ 树序）；命中按同一有序列表倒序，保证后画者先命中。
  - 排序由 `ZOrderedOutOfFlow()` 统一提供（数据源为归属该 CB 的锚点 + 直接 `Fixed` 子项），`Frame.cpp` 的 `Collect` / `CollectScroller`、`Event.cpp` 的 `HitTestInner` / `CollectFixedLayer` 四处消费同一顺序，故 Fixed 层收集顺序也不会漂移。
  - **不改**"脱离流子项整体后画"：负 `zIndex` 只沉到同一 CB 的其他 Absolute 之下，不会沉到流内内容之下（与 CSS 的 stacking context 不同，见 §7-10）。
  - `Fixed` 由窗口级 Fixed 层管理，`zIndex` 对它无效（恒视为 0）；流内的 `Static` / `Relative` 子项忽略 `zIndex`。不建 stacking context。
  - 布局期 `ZIndexCount()` 统计非默认 `zIndex`、`AbsoluteCount()` 统计 Absolute；两者都为 0 时四处都走原两趟、不进排序路径。有序列表缓冲按递归深度分层跨帧复用（同 `Layout.cpp ItemsAt` 的 `unique_ptr` 模式）。
  - `zIndex` 标脏 `kFlagLayout`（不改几何，只为刷新上述统计），无动画标签。
- `Left/Top` 有动画标签 `dui::Left` / `dui::Top`，每帧 `kFlagLayout` 全树重排；轻量视觉变换仍用 `dui::TranslationX`（`kFlagPaint`）。

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
| 9 | transform / `contain` 祖先**不**成为 `Absolute` / `Fixed` 的 containing block | 项目无 `contain` 属性；transform 是 Paint 级顶点变换（不改 `Bounds`、不重排、scissor 轴对齐），引入旋转后的布局基准会造成语义撕裂。`Fixed` 保留"永远钉在窗口"的直觉，跨层 Absolute 也不受中间 transform 祖先的变换影响（§4.3） |
| 10 | 不做 stacking context：`zIndex` 只排同一 CB 内的 `Absolute`，负值也不沉到流内之下 | 保持"脱离流统一后画"的简单层叠模型，避免为 zIndex 引入层树与逐层排序（§4.3） |

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
| `margin` | Thickness | 四边外边距；外边距盒参与占位、不塌陷（§3/§7-1），运行时已落地 |
| `boxSizing` | Enum | BorderBox（默认）/ ContentBox |
| `position` | Enum | Static（默认）/ Relative / Absolute / Fixed（运行时已实现） |
| `left` / `top` / `right` / `bottom` | Float（DIP） | 未设 = Auto；仅定位元素生效，Static 忽略 |
| `zIndex` | Int | 默认 0；仅同一 CB 内的 `Absolute` 生效，按值升序（同值按树序），Fixed / 流内忽略（§4.3） |
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

已按此边界落地（§4.2，M5.21）；另不做 CSS 字符串解析、`rowGap` / `columnGap`、隐式列。

### D5 滚动模型

建议：`overflow: Scroll` + 容器内容偏移（不重排）+ 独立滚动条控件；惯性/平滑滚动与 `UI.Animation` 联动，列为 P1。

---

## 10. 分期

| 期 | 内容 |
| --- | --- |
| **P0** | 盒模型（含 box-sizing，**`margin` 已落地，§3**）、Flex 全量（§4.1）、**`position` 四值 + 四边定位（已落地，§4.3）**、auto/固定/百分比、min/max、`display: None` |
| **P1** | **Grid（§4.2，已落地）**、`overflow: Scroll` + 滚动条、RTL（`direction` 与 flex 主轴联动，依赖 Text 层） |
| **P2** | 普通流的 `zIndex`、`alignItems: Baseline`、嵌套百分比与浏览器对齐的精化（§7-7） |

---

## 11. 待确认

1. D1–D5 是否按建议拍板。
2. 默认值组合（`Column + Stretch`）是否作为全局默认 —— 影响所有控件的书写习惯。
3. Grid 是否进 P0 —— 已按 P1 边界落地（§4.2），不再待确认。
4. 富文本 / inline 混排是否进 MVP（影响 §4.4 与 ADR-017）。
5. `alignItems: Baseline` 是否需要（文本混排场景常用，但实现涉及跨元素基线对齐）。
6. 滚动的具体形态：像素滚动 vs 行滚动、滚动条常显/悬浮。
