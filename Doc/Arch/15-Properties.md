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
| `fontFamily` | String（`const wchar_t*` 指针） | ✓ | Layout·Text | ✓ | Microsoft YaHei | 框架默认微软雅黑；开发者可指定系统已安装任意字体族，指针生命周期由调用方保证（建议字面量）；未安装时 DWrite 自动回退默认。未安装字体经 `dui::LoadFontFamily`（文件）或 `dui::LoadFontFamilyFromMemory`（资源嵌入 exe 的内存字节）装入，可装多个、各自独立生效；仅支持 ttf/otf/ttc（DWrite 只认 SFNT，woff2 不支持，需先转换） |
| `fontStyle` | Enum（`dui::FontStyle`） | ✓ | Layout·Text | ✗ | Normal | 排版缓存键；运行时已接入：`Normal / Italic`（CSS `oblique` 归 Italic）；链式方法 `FontStyle()`；DWrite `SetFontStyle` |
| `textAlign` | Enum | ✓ | **Paint** | ✗ | Start | 运行时已接入；`dui::TextAlign::Start/Center/End` + `Style::TextAlign()`；静态 `Text` 在内容盒内对齐（绘制期平移，不改 DWrite 排版，故标 Paint），`TextBox` 不参与；多行按最长行整体对齐 |
| `lineHeight` | Length（倍数） | ✓ | Layout·Text | ✓ | 1.2 | |
| `letterSpacing` | Length（DIP） | ✓ | Layout·Text | ✓ | 0 | **已落地**：继承属性；DWrite `IDWriteTextLayout1::SetCharacterSpacing`（CSS 的"字符后加距"对应 `trailingSpacing`）；**数值单位由运行时实测标定**（不同 dwrite 运行时分别按 DIP 或 1/1000 em 解释，硬编码换算会差 1000 倍 → 排版被打散成多行，详见 11-Text §字距单位）；进排版缓存键；`Text` / `TextBox` / `RichEditor` 均生效 |
| `wordSpacing` | Length（DIP） | ✓ | Layout·Text | ✓ | 0 | **已落地（M5.55）**：继承属性；**DWrite 无此接口，但 `SetCharacterSpacing` 可按区间下发** —— 对"词分隔符"（半角/全角空格）区间额外加宽（同区间覆盖字距），即 CSS `word-spacing` 语义；加宽后的宽度由 DWrite 自己算进 layout，**测量与折行天然一致**（无需字形级自绘）；进排版缓存键；`Text` / `TextBox` / `RichEditor` 均生效 |
| `textDecoration` | 位标志（`dui::TextDecoration`） | ✗ | Layout·Text | ✗ | None | 运行时已接入：`Underline / LineThrough` 可组合（`operator\|`）；**不继承**（同 CSS，装饰线不传给子元素）；链式方法 `TextDecoration()`；DWrite `SetUnderline` / `SetStrikethrough`，线位与厚度由 DWrite 算出、走渲染器回调画线（不占行高） |
| `textOverflow` | Enum（`dui::TextOverflow`） | ✓ | Layout | ✗ | Clip | **已落地（M5.56）**：继承属性（同 CSS）；`Clip` = 直接裁掉（默认，裁剪由 `overflow` 负责）、`Ellipsis` = 单行排版 + 超出时末尾加省略号 `…`（**只做单行**，不做多行 clamp）。实现：单行排版 → 二分求最长可容纳前缀 → 拼 `…` 再排一次（DWrite 的 layout 无 trimming API）；前缀长度按**字形簇边界**校正（不会切出半个汉字）。CSS 要求配合 `overflow: hidden`，本框架不强制 —— 截断后文本本身就不溢出 |
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
| `margin` | Thickness | ✗ | Layout | ✓ | 0 | 运行时已接入；`Style::Margin(float)` / `Margin(Thickness)`；作为**外边距盒**参与父布局占位，`Bounds()` 仍是元素盒；不做塌陷（04-Layout §3/§7-1） |
| `width` / `height` | Length | ✗ | Layout | ✓ | Auto | measure 缓存键；**`Text` 的换行宽（M5.59）**：显式 `width` 优先 —— 内容排版宽 = `width` − padding − (border-box 的边框)，再与父给的内容可用宽取小；否则用父给的内容可用宽。此前 `Text` 一律按父可用宽排版 → 设了 `width` 也不折行、文本溢出元素盒（`TextBox` 多行早有"显式 Width 优先"的同款做法） |
| `minWidth` / `maxWidth` / `minHeight` / `maxHeight` | Length | ✗ | Layout | ✓ | Auto | **已落地**（M5.52 补齐高度向）：在测量期夹紧元素盒尺寸 |
| `boxSizing` | Enum（`dui::BoxSizing`） | ✗ | Layout | ✗ | PaddingBox | **已落地（M5.58）**：`PaddingBox`（默认）= 宽高含内边距、不含边框 —— 边框是**内描边**，画在盒内缘但**不占布局空间**（与内容重叠，这是框架的既有语义，保持不变）；`BorderBox` = 宽高含内边距**与边框** → 边框占盒内空间，内容区 = 盒 − 内边距 − 边框（等价 CSS `box-sizing: border-box` 的效果）。命名与 CSS 的 `content-box` 不同：框架的 `width` 一贯是"含内边距的盒宽"，故默认档叫 `PaddingBox`；`BorderBox` 才是 CSS 那个"宽高含边框" |

### 4.3 背景与装饰

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `backgroundColor` | Color | ✗ | Paint | ✓ | 透明 | Brush（渐变/图片）列为 P2 |
| `opacity` | Float | ✗ | Paint | ✓ | 1.0 | |
| `cornerRadius` | CornerRadii | ✗ | Paint | ✓ | 0 | **已落地**：四角独立圆角（CSS `border-radius` 顺序：左上/右上/右下/左下）。`Style::CornerRadius(float)` = 四角同值；`Style::CornerRadius(CornerRadii)` = 四角独立。绘制走 Graphics 的逐角 SDF；相邻两角半径之和超出该边长时按 CSS 规则同比例缩小全部半径；每角半径夹到 `min(w,h)/2`（超出则圆弧中心跑到盒外，SDF 不再是真实距离） |
| `borderWidth` | Thickness（四边） | ✗ | Paint | ✓ | 0 | **已落地（M5.57）**：实线 / 虚线 / 点线、`borderStyle` 可选；**分边宽度**（`Style::BorderWidth(float)` = 四边同值，`Style::BorderWidth(Thickness)` = 左/上/右/下独立，顺序同 `Thickness`）。**内描边**（画在盒内缘）。等宽时走单个圆角矩形描边（圆角精确）；分边时四条边各自成带、角部按直角对接（不做 CSS 斜接），**分边时圆角不生效**。要边框占盒（挤压内容）需配 `boxSizing: border-box` |
| `borderColor` | Color | ✗ | Paint | ✓ | 透明 | 透明 = 无边框；跟随元素 `opacity` |
| `borderStyle` | Enum（`dui::BorderStyle`） | ✗ | Paint | ✗ | Solid | **已落地（M5.57）**：`Solid` 实线 / `Dashed` 虚线（实 3w 空 3w）/ `Dotted` 点线（实 1 DIP 空 2w，CSS 未规定，取浏览器观感）。虚线在 CPU 侧沿路径切段描边（零 GPU 改动）；等宽时沿圆角矩形中心线（每角 8 段离散），分边时每条边独立起段 |
| `shadow` | 标量组（`f`=blur DIP、`extra[0/1]`=offsetX/Y、`u`=颜色） | ✗ | Paint | ✗ | 无（颜色 0） | **已落地**：`Style::Shadow(blur, color, offsetX, offsetY)`；只投元素边框盒（圆角矩形），Shape 批 kind=3 的 SDF 衰减带近似高斯，无纹理/离屏。不支持 inset/spread/多阴影，不随元素 Opacity 变化 |

### 4.3.1 滚动条（dui::Scroller 自绘轨道/滑块；MVP 子集，运行时已接入）

轨道（Track）与滑块（Thumb）各自独立一组；宽度 = 纵向条厚度，高度 = 横向条厚度
（滑块长/轨道长仍由视口与内容比决定，不可直接设置）。

**几何**：轨道**贴视口边缘**——纵向条占视口最右 `TrackWidth` DIP、横向条占最下 `TrackHeight` DIP，
均沿另一轴铺满视口，**不留内缩间隙**；滚动条**叠加在内容之上**，布局不为它预留空间
（内容宽度仍按整个视口算，被压住的部分需靠内容自身 padding 避让）。

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `scrollbarTrackWidth/Height` | Length | ✗ | Paint | ✓ | 8 | 条厚（DIP） |
| `scrollbarTrackBackgroundColor` | Color | ✗ | Paint | ✓ | 0x14000000 | 轨道默认极浅（约 8% 黑） |
| `scrollbarTrackCornerRadius` | Length | ✗ | Paint | ✓ | 0 | 默认直角 |
| `scrollbarTrackBorderColor` | Color | ✗ | Paint | ✓ | 透明 | |
| `scrollbarTrackBorderWidth` | Length | ✗ | Paint | ✓ | 0 | 无边框 |
| `scrollbarThumb*`（同上 6 项） | 同上 | ✗ | Paint | ✓ | 厚 8 / 色 0x30000000 / 直角 / 无边框 | 滑块厚可异于轨道（居中）；指针悬停在滑块上时加深（alpha ×1.6，RGB 不变），由 Scroller 自绘实现 |

**悬停加深**：滑块默认色较浅（约 19% 黑）；指针落在滑块矩形上时加深到约 30% 黑。
滑块是自绘图形、没有独立元素，因此不走状态变体 —— 由 `Scroller` 在命中测试时记录
指针位置，配合容器自身的 Hover 状态判定，变化时标脏重绘（`Element::SetState` 为 virtual）。

### 4.4 布局

| 属性 | 类型 | 继承 | 失效 | 可动画 | 默认 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `display` | Enum | ✗ | Layout | ✗ | Flex | Flex / Grid / None |
| `flexDirection` | Enum | ✗ | Layout | ✗ | Column | 块级流语义来源 |
| `flexWrap` | Enum | ✗ | Layout | ✗ | NoWrap | NoWrap / Wrap（Wrap 仅 Row；运行时已实现，贪心断行） |
| `justifyContent` | Enum（`dui::Justify`） | ✗ | Layout | ✗ | Start | **已落地**：`Start / Center / End / SpaceBetween / SpaceAround / SpaceEvenly`（CSS `justify-content` 全集）；剩余空间为负（溢出）时退化为 `Start` |
| `alignItems` | Enum（`dui::Align`） | ✗ | Layout | ✗ | Stretch | **已落地**：`Stretch / Start / Center / End / Baseline`。`Start` 不拉伸（CSS `flex-start` 语义）；`Baseline` 按首行文本基线对齐，无基线的元素退化为外边距盒底边 |
| `alignSelf` | Enum（`dui::Align`） | ✗ | Layout | ✗ | 未设 | **已落地**：单项覆盖父的 `alignItems`；**未设置**（链上没有该属性）即取父值（同 CSS `auto`） |
| `flexGrow` | Float | ✗ | Layout | ✓ | 0 | |
| `flexShrink` | Float | ✗ | Layout | ✓ | 1 | |
| `flexBasis` | Length（DIP） | ✗ | Layout | ✓ | 未设 | **已落地**：主轴基准尺寸，替代主轴上的 `width`/`height` 与内容自然尺寸；**按元素盒语义**（与 `width` 一致），非 CSS 的 content-box；测量期生效，故父的自然尺寸也按它算 |
| `gap` / `rowGap` / `columnGap` | Length | ✗ | Layout | ✓ | 0 | **已落地**：`gap` 是通用值；`rowGap`（行间距）/ `columnGap`（列间距）未设则各自回落 `gap`。Flex：`columnGap` = Row 主轴项间距、`rowGap` = Column 主轴项间距与换行行间距；Grid：分别为行 / 列间距 |
| `position` | Enum | ✗ | Layout | ✗ | Static | Static / Relative / Absolute / Fixed；非 Static 即成为后代 Absolute 的 CB（04-Layout §4.3） |
| `left` / `top` / `right` / `bottom` | Float（DIP） | ✗ | Layout | ✓（`left`/`top`） | 未设 = Auto | 仅 Relative / Absolute / Fixed 生效；Static 忽略；Relative 只认 left/top |
| `zIndex` | Int | ✗ | Layout | ✗ | 0 | 仅同一 CB 内的 `Absolute` 生效；按值升序绘制、同值按树序；Fixed / 流内忽略（04-Layout §4.3） |
| `overflow` | Enum（`dui::Overflow`） | ✗ | **Paint** | ✗ | Visible | **已落地**：`Visible` 不裁剪 / `Hidden` 把子内容裁到元素盒（CSS 裁 padding box；本框架边框内描边不占盒，故元素盒即 padding box）/ `Scroll` 与 `Hidden` 同义（滚动条由 `dui::Scroller` 控件负责，本属性不画滚动条）。裁剪在绘制期用 scissor 实现，**不改布局盒** |
| `gridTemplateColumns` / `gridTemplateRows` | TrackList | ✗ | Layout | ✗ | 空 | 变长声明存 `Style`（`FindTracks` 沿派生链），不走 `StyleValue`；04-Layout §4.2 已落地 |
| `gridRow` / `gridColumn` | Int | ✗ | Layout | ✗ | Auto | 显式放置（0 基）；已落地 |
| `gridRowSpan` / `gridColumnSpan` | Int | ✗ | Layout | ✗ | 1 | 跨格；已落地 |

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
| `visibility` | Enum（`dui::Visibility`） | ✓ | Layout | ✗ | Visible | **已落地**：**继承属性**（同 CSS）；`Hidden` = 占位但不画、不命中；子元素可显式设 `Visible` 反转父的隐藏。命中测试没有 `StyleContext`，故可见性在布局期算好后落到元素上（`Element::Hidden()`），绘制与命中读同一份结果 |
| `cursor` | Enum | ✓ | — | ✗ | Default | 由 Event 层读取 |

### 4.7 动画相关属性

**没有动画元属性**。`transition` / `animation` 从未进入运行时属性表（`Prop` 枚举），早期草案的这两项已作废。命令式动画直接对普通属性补间；当前开放的是 `opacity` / `backgroundColor` / `color` / `selectionColor` / `caretColor`、§4.5 的四个 transform 标量，以及 `width` / `height` / `left` / `top`（布局尺寸与定位偏移对照，每帧重排）；多通道值 `padding` / `margin`（Thickness 四边）与 `cornerRadius`（四角）也开放 —— 四通道各自插值、共用一条时间轴，其中 `padding` / `margin` 每帧重排、`cornerRadius` 只重绘。标签、插值与语义见 [05-Animation.md](./05-Animation.md) §2–§3。

### 4.8 暂不做

渐变/图片背景（需 Brush 与资源引用）、非等比缩放 / `TransformOrigin` / 矩阵形态 transform、多 Style 引用（class 列表，见 03-Style D3）、`will-change` 类提示、RTL `direction`（依赖文字排版层）。

> **已在本轮补齐**（M5.55–M5.58）：`wordSpacing`（§4.1）、`textOverflow: ellipsis`（§4.1，单行）、`borderWidth` 分边宽度 / `borderStyle` 虚线点线（§4.3）、`boxSizing`（§4.2）。
> 仍未做：**外描边**（CSS `outline`，本轮按约定不做）、分边宽度 + 圆角（分边时圆角不生效）、多行省略号（`-webkit-line-clamp`）。

---

## 5. 运行时读路径（ADR-024）

属性值有两个读取入口，**同一帧内必须给出相同结果**：

### 5.1 遍历内（布局 / 绘制）

```cpp
// 真实签名（已落地，Element/UI.h）
void Widget::OnPaint(graphics::Painter& p, const StyleContext& ctx, float scale) {
    const StyleValue radius = ResolveProperty(this, Prop::CornerRadius, ctx);
    const dui::CornerRadii r = ValueAsCornerRadii(radius);   // 四角：f=左上，extra=右上/右下/左下
    p.FillRoundedRect(Bounds(), r, ResolveProperty(this, Prop::BackgroundColor, ctx).u, 1.f, scale);
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

**写入口**（级联第 3 层）：`Element::Set(Prop, v)` 支持枚举 / 整型 / `Color`（`u`）、浮点（`f`）、`dui::Thickness`（`f` + `extra[3]`）、`dui::CornerRadii`（`f` + `extra[3]`，四角圆角）、`const wchar_t*`（`family`，零拷贝指针）。`padding` / `margin` 传标量时按四边同值展开（`IsThicknessProp()`），`cornerRadius` 传标量时按四角同值展开（`IsCornerRadiusProp()`）；同 `Prop` 的新值整体替换 `StyleValue`，不残留旧槽。字体族指针的生命周期契约见 [03-Style.md](./03-Style.md) §6.1。

### 5.3 动画值槽（级联第 1 层）

- 命令式 `Animate()` 把每帧插值结果稀疏写入 `Element::animated_`（`std::vector<StyleValue>`）；`TryResolveElement()` 最先查它，因此遍历内 `StyleContext` 与遍历外 `Style::Resolve` 同帧一致。
- **不设过渡旧值槽**，也没有动画池 / 活动动画链表：属性基值变化（`Set` / `SetStyle` / 状态切换 / 主题替换）不会隐式触发动画。只有显式调用 `Animate()` 才建补间，起点取当前显示值（含已有的第 1 层值）。
- 动画完成后终值留在槽中（fill forwards），`Cancel()` 清除并回落基值；元素销毁时注销，残留句柄失效。详见 [05-Animation.md](./05-Animation.md) §3。

---

## 6. 不属于本表的属性

富文本的**字符级/段落级样式**是内容属性（随文本复制、序列化），不进样式系统，见 [11-Text.md](./11-Text.md) §4.1。衔接点只有一处：文档默认样式由所在元素的样式注入。

---

## 7. 待确认

1. ~~`shadow` 是否进 MVP~~ 已落地（box-shadow 子集，见 §4.3）；`textDecoration` 是否进 MVP。
2. `zIndex` 是否扩展到普通流（当前仅同一 CB 内的 Absolute，已落地，见 04-Layout §4.3）。
3. ~~`alignItems: Baseline` 是否纳入~~ 已落地（M5.52：按首行文本基线对齐，`Text` / `TextBox` 提供基线）。
4. ~~是否需要 `letterSpacing` 之外的 `wordSpacing`~~ 已落地（M5.55：按区间下发 `SetCharacterSpacing`，不必自绘）。
