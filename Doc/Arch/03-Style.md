# 样式系统

> 状态：草案 v0.1 — 待讨论确认。
> 需求来源：框架要有自己的样式规则（`backgroundColor` / `color` / `textAlign` / `fontSize` …），并且样式能在界面元素之间复用。
> 相关：[00-Overview.md](./00-Overview.md)（原则、非目标）、[01-Layering.md](./01-Layering.md)（UI 层模块）、ADR-018（纯 C++ 写界面）。

## 1. 需求

| 编号 | 需求 | 验收口径 |
| --- | --- | --- |
| R1 | 元素可设置样式属性 | 任意元素都能设，不必为此派生新控件类 |
| R2 | 样式可在元素间复用 | 一份样式定义被 N 个元素引用，不产生 N 份数据拷贝 |
| R3 | 复用时能局部覆盖 | 引用同一样式的两个元素可各自改掉其中一项 |
| R4 | 控件自带默认外观 | 一行样式都不写，界面也能正常显示 |
| R5 | 主题可整体替换 | 换配色/换字号不需要改界面代码 |
| R6 | 交互态可换外观 | 同一按钮 hover / pressed / disabled 呈现不同背景 |
| R7 | 改样式只付必要代价 | 改颜色不触发布局，改字号才触发 |

### 1.1 约束（来自已定决策）

- **无描述语言**（ADR-018）：样式用 C++ 表达，不做运行时解析，因此不引入 CSS/XAML 的选择器与层叠复杂度。
- **无异常**（ADR-007 **已定**）：错误经 `Result<T>` / `bool` 传递；**无 RTTI**（同条 ADR 的草案项，待拍板）：属性用整数 ID 标识，值用自定义 tagged union，不用 `typeid`、`std::any`、`std::variant`（若最终采用 STL 可放宽）。
- **保留模式 + 脏区**（ADR-005）：改样式必须能精确标脏，不允许"改一个颜色就全量重排"。
- **热路径零分配**（00-Overview §3）：样式解析全程不分配内存。
- **可裁剪**（00-Overview §3）：模块关闭时其属性不进二进制。
- **内部单位 DIP**（ADR-014）：样式中的长度默认 DIP。

---

## 2. 概念模型

```
Theme ──┐
        │  默认值兜底
        ▼
Style（命名 / 不可变 / 可共享 / 可派生）
        │  SetStyle 引用
        ▼
Element ── 本地覆盖（稀疏，最高优先级）
        │
        ├─ 继承属性：由父元素计算值下传
        └─ 非继承属性：自身链上解析
```

| 概念 | 定义 | 生命周期 |
| --- | --- | --- |
| `Property` | 单个样式属性：整数 ID + 类型 + 标志位 + 名字（调试用） | 编译期常量表 |
| `StyleValue` | 属性值，定长 8 字节 tagged union | 值类型 |
| `Decl` | 稀疏声明：`SmallVec<{PropId, StyleValue}, N>` | 值类型 |
| `Style` | **不可变、可共享、可派生**的 `Decl`；**名字可选**（仅注册表条目与调试标识） | 匿名不进注册表；具名才注册在 `StyleRegistry` |
| `Theme` | 每个属性的默认值 + 各控件的默认 `Style` + 资源（字体、图标） | 应用级，可整体替换 |
| 本地覆盖 | 挂在单个元素上的 `Decl`，优先级高于 `Style` | 随元素 |
| `StyleContext` | 遍历期携带的解析上下文（继承态 + 当前元素链） | 栈上，遍历结束即弃 |

**复用的三个层次**（对应 R2 / R3）：

1. **共享** —— 多个元素 `SetStyle` 同一个 `Style`，数据只有一份。
2. **派生** —— `Style B` 派生自 `A` 只存差异，改 `A` 时 `B` 的差异仍生效。
3. **继承** —— 部分属性沿元素树向下传递，父设一次，整棵子树生效。

---

## 3. 属性表

属性在编译期用宏注册，是"样式系统"的唯一事实来源：

```cpp
// 宏展开出：ID 枚举项、类型萃取、标志位、调试名
DUI_PROPERTY(Color,           Color,   Inherit | Paint)
DUI_PROPERTY(BackgroundColor, Color,   Paint)
DUI_PROPERTY(FontSize,        Length,  Inherit | Layout | Text)
```

### 3.1 标志位

| 标志 | 含义 | 用途 |
| --- | --- | --- |
| `Inherit` | 沿元素树向下继承 | 解析时从父计算值取 |
| `Layout` | 改变后需重新度量/布局 | 标 `LayoutDirty` |
| `Text` | 改变后需重新排版文本 | 标 `TextDirty`（`Layout` 的子集，可单独做增量） |
| `Paint` | 仅需重绘 | 标 `PaintDirty` |
| `Animatable` | 可被动画驱动 | 动画系统校验 |

> `Layout` 与 `Paint` 可同时置位（如 `borderWidth` 影响内容盒也影响绘制）。

### 3.2 属性清单

**属性清单已集中到 [15-Properties.md](./15-Properties.md)（唯一事实来源，ADR-024）。** 本节不再维护表格，只说明样式系统对属性的处理：

- 标志位含义见 §3.1；属性全集、类型、默认值、缓存键影响见主表。
- **布局子系统**消费：`display` / `flex*` / `gap` / `position` / `left-top-right-bottom` / `zIndex` / `overflow` / `grid*` / 盒模型与尺寸类。
- **动画子系统**消费：`transition` / `animation`（元属性）+ 所有 `Animatable` 属性。
- **文本子系统**消费：`font*` / `text*` / `lineHeight` / `letterSpacing` / `direction`。

### 3.3 单位

值类型（`Length` / `Thickness` / `Color` 等）与单位语义统一定义在 [15-Properties.md](./15-Properties.md) §2。

---

## 4. 决策点

### D1 属性存储：稀疏属性数组（建议）vs 全字段结构体

| 方案 | 体 | 内 | 性 | 复 | 说明 |
| --- | --- | --- | --- | --- | --- |
| **稀疏 `{PropId, Value}` 数组**（建议） | ○ | +++ | + | ○ | 只存设置过的属性；元素的 1–2 项覆盖只占 ~16 B；"派生"天然只存差异；增删属性不改内存布局 |
| 全字段结构体 + `present` 位掩码 | ++ | -- | +++ | +++ | 访问 O(1) 无查找、类型安全最强；但每份 `Style` 都要占满全部字段（MVP 约 200 B），元素本地覆盖同样变胖 |

**建议：稀疏数组。** 内存优先级高于性能，且"共享 + 派生 + 局部覆盖"这个核心需求本质就是表达"差异集"，稀疏结构与之同构。查找成本可忽略（单个 `Decl` 通常 2–6 项，线性扫描命中 L1）。

配套：`StyleValue` 定长 8 字节（`float` / `u32` / `Color` / `2×u32`），避免变长存储与二次分配。

### D2 解析结果：遍历期上下文（建议）vs 每元素缓存

| 方案 | 体 | 内 | 性 | 复 | 说明 |
| --- | --- | --- | --- | --- | --- |
| **遍历期 `StyleContext` 栈**（建议） | ++ | +++ | ○ | ++ | 布局与绘制都是自上而下遍历，继承态随递归参数下传；无覆盖的子树直接透传同一个 `const&`（零拷贝，天然共享）；常驻内存开销为 0，且无引用计数、无哈希、无失效维护 |
| 每元素 `ComputedStyle` 缓存 | ○ | -- | +++ | + | 每个元素常驻 ~48 B 继承态；解析一次到处可用；但要在改样式时维护子树失效，且内存随元素数线性增长 |
| 缓存 + 去重表（Blink 式 style sharing） | -- | ++ | +++ | --- | 内存最优，但引入哈希表与样式匹配逻辑，复杂度最高 |

**已定（拍板）：混合方案。**

1. **继承类属性**走遍历期 `StyleContext` 栈：布局与绘制本就是自上而下遍历，继承态搭便车，常驻内存为零，也不需要维护缓存失效。
2. **非继承属性**（`translation` / `scale` / `rotation`、`backgroundColor` 等）提供**按需 `Style::Resolve(element, Prop)`**：只查元素自身链，O(1–3)，供事件、命中测试、动画与任意代码在遍历外读取。
3. **过渡旧值槽**：仅对声明了 `Transition` 的属性在元素上保留旧值（内联容量 2，溢出到 Arena），无声明者零开销。

读路径细则见 [15-Properties.md](./15-Properties.md) §5。代价：遍历外读**继承**属性需 O(树深) 上溯（频率低，不缓存，量可忽略）；收益是两条路径语义明确、互不依赖遍历时机。

### D3 选择器机制：不做（建议）

| 方案 | 说明 |
| --- | --- |
| **不做选择器**（建议） | 复用由"共享 Style + 派生 + 控件默认 Style"三件事覆盖，不需要类型/ID/类选择器与层叠匹配 |
| 类型选择器（对所有 `Button` 生效） | 需为控件引入 TypeId 匹配与优先级仲裁，与 ADR-018"无描述语言"的精神相冲 |
| 多 class 列表（一个元素引用多个 Style） | 表达力最强，但解析链变长、优先级规则变复杂 |

**建议：不做。** 控件在内部持有一个默认 `Style`（该控件类的 `StyleRegistry` 具名条目），用户改写该条目即可全局生效 —— 这已经等价于类型选择器，且成本为零。

**名字的唯一用途**是注册表键（供主题覆盖、控件默认样式替换）与调试标识，**不参与任何匹配**。因此名字是**可选**的：匿名 `Style` 不注册、零字符串驻留，适用于文件内的局部样式；只有需要被外部按名寻址时才具名。默认匿名的另一个好处是避免静态初始化期做名称驻留。

### D4 主题形态

| 方案 | 说明 |
| --- | --- |
| **编译期常量 + 运行时替换接口**（建议 MVP） | 默认主题是 `constexpr` 表；`Application::SetTheme(Theme*)` 整体换指针；不做文件解析 |
| 运行时资源文件（自定义二进制/JSON） | 需要解析器和资源加载，与"体积小"冲突；列为远期 |

**建议：MVP 走前者**，把"是否需要文件形态"留在 ADR-018 的待定项里继续挂起。

---

## 5. 级联顺序

解析单个属性时，从高到低依次查找，首次命中即返回：

| 层 | 来源 | 说明 |
| --- | --- | --- |
| 1 | 动画值 | 动画系统写入；元素持有活动动画链表头指针，`StyleContext` 与 `Style::Resolve` **均经该链表**查此层（[15-Properties.md](./15-Properties.md) §5.3） |
| 2 | 状态变体 | 每个状态一份 `Decl`（全集见 §6.2）；多状态同时成立时按 `disabled > pressed > selected > focused > hover` 合并 |
| 3 | 元素本地覆盖 | `element->Set(Prop::X, v)` |
| 4 | 元素引用的 `Style` | 沿派生链由近及远上溯 |
| 5 | 父元素计算值 | **仅 `Inherit` 属性** |
| 6 | 主题默认值 | `Theme::defaults[Prop]` |
| 7 | 属性表内建默认 | 编译期兜底常量 |

---

## 6. API 形态

### 6.1 定义与复用

```cpp
// 匿名（默认，绝大多数场景）：不进注册表、无字符串驻留、不用起名
static const Style kTitle = StyleBuilder()
    .FontSize(20.f)
    .FontWeight(FontWeight::SemiBold)
    .Color(0xFF1A1A1A)
    .TextAlign(TextAlign::Center)
    .Build();

// 派生：只存差异
static const Style kTitleAccent = StyleBuilder(kTitle)
    .Color(0xFF2D7FF9)
    .Build();

// 具名：只在"要全局覆盖控件默认外观"或"要被主题按名替换"时需要
static const Style kButtonBase = StyleBuilder("button")
    .Padding({ 8.f, 16.f })
    .CornerRadius(4.f)
    .Build();

// 使用
auto* a = panel->AddChild<Text>(L"标题");
a->SetStyle(kTitleAccent);                 // 复用
a->Set(Prop::Color, 0xFFC00000);           // 单元素覆盖，优先级更高

auto* b = panel->AddChild<Text>(L"副标题");
b->SetStyle(kTitle);                       // 同一份 Style，无拷贝
```

### 6.2 状态变体

```cpp
static const Style kButton = StyleBuilder("button")
    .Background(0xFF2D7FF9).Color(0xFFFFFFFF)
    .CornerRadius(4.f).Padding({8.f, 16.f})
    .When(State::Hover,    StyleBuilder().Background(0xFF1A6FE0))
    .When(State::Pressed,  StyleBuilder().Background(0xFF1560C0))
    .When(State::Disabled, StyleBuilder().Background(0xFFCCCCCC).Color(0x80FFFFFF))
    .Build();
```

`When` 的第二参数是 **`StyleBuilder`** 而非 `Style`：状态变体只是一份"该状态成立时要叠加的差异集"，最终并入外层 `Style` 的状态表，不注册、不被元素引用，因此不需要 `Build()`。`Build()` 在整条链末尾出现一次。

静态外观由样式描述，状态位由框架或业务切换，样式层只做查表 —— 不需要为每个状态新建元素或重新设置属性（输入与事件机制见 [06-Events.md](./06-Events.md)）。

#### 状态全集（已定）

| 状态 | 写入方 | 触发 |
| --- | --- | --- |
| `hover` | 框架（Input） | 指针进入 / 离开差分 |
| `pressed` | 框架（Input） | 按下未抬起，含指针捕获 |
| `focused` | 框架（焦点服务） | 点击 / Tab 链 |
| `disabled` | 框架（由 `SetEnabled(false)` 推导） | **不开放直接写**，避免状态位与实际行为不一致 |
| `selected` | **业务代码** | 业务语义：哪一项被选中（**合并了原 `checked`**） |
| `readonly` | 控件属性 | 可聚焦、可选中、不可编辑 |
| `invalid` | 业务 | 校验失败 |
| `open` | 控件 | 展开态（ComboBox / TreeItem / Accordion），与 `selected` 语义不同 |

- `selected` 与 `checked` 合并：两者视觉语义一致，分开只会让写样式的人猜该用哪个，且双位同时成立时还得再定一套合并规则。
- **不做** `dragover`（拖放未进 MVP）、`indeterminate`（三态勾选待真实需求）、`first-child` 类结构状态（等价于选择器，与 D3 冲突）。
- `selected` 维持**纯状态位**（已定）：框架不提供容器级的"单选归属"，业务决定"选中谁"，框架只负责解析样式与标脏。

#### 多状态合并（已定）

多个状态位可同时成立（如 `hover` + `selected`），按**固定优先级从高到低**合并，同一属性由高优先级者胜：

```
disabled  >  pressed  >  selected  >  focused  >  hover
```

`disabled` 必须在视觉上压过一切；`pressed` 比 `selected` 更即时。固定顺序可预测、可写进文档，不需要 CSS 那套特异性计算。声明了 `Transition` 的属性仍按合并后的最终值补间。

#### 写入契约（已定）

- **相同值幂等**：`SetState(s, v)` 在 `v` 与当前状态位相同时不做任何事 —— 不标脏、不重建过渡。
- **标脏帧末合并**：状态切换只标脏，帧末统一解析与重绘；同一帧内的多次写入只产生一次重绘，**不出现中间态**。因此"单选"不需要"先摘旧的、再戴新的"，业务侧一次全量写即可：

```cpp
for (int i = 0; i < n; ++i) {
    items[i]->SetState(State::Selected, i == current);   // 旧项被覆盖为 false，结构上不可能漏
}
```

### 6.3 读取（自定义绘制钩子，对应 ADR-018 定制点 2）

```cpp
void MyWidget::OnPaint(Painter& p, const StyleContext& s) const {
    p.RoundedRect(Bounds(),
                  s.Get<Prop::CornerRadius>(),
                  s.Get<Prop::BackgroundColor>());
}
```

`s.Get<Prop::X>()` 为编译期模板，返回类型由属性表萃取 —— 类型安全且不依赖 RTTI。

**遍历外**（事件处理器、命中测试、动画器）：

```cpp
Color c = Style::Resolve(element, Prop::Color);   // 非继承属性 O(1–3)
```

两条路径的规则与边界见 [15-Properties.md](./15-Properties.md) §5。

> 链式 Builder 在此仅限于 `Style` 构造期。控件属性是否也提供链式（`btn->Text("OK").Padding(8)`）仍属 ADR-018 待定项，倾向提供（返回 `Element&`）。

---

## 7. 失效与重算

| 动作 | 标脏范围 | 依据 |
| --- | --- | --- |
| `Set(Prop::X, v)` 且 `X` 带 `Layout` | 自身 `LayoutDirty` + 子树 | 尺寸/内边距变化影响后代排列 |
| 同上，且 `X` 带 `Inherit` | 整棵子树 | 继承值下传 |
| 同上，仅带 `Paint` | 自身矩形加入脏区 | 不触发布局 |
| `SetStyle(s)` | 按两Style差异集的属性标志取并集 | 简单实现可先按 `Layout|Paint` 最坏情况处理 |
| 状态切换 | 按该状态 `Decl` 中属性的标志 | 状态变体通常只有 `Paint` 属性 |
| 动画每帧 | 按被动画属性的标志 | 数值类动画多为 `Paint`（见 [05-Animation.md](./05-Animation.md) §7.3） |
| 换主题 | 整树全量标脏 | 低频大动作，走"全失效"路径 |

**`Style` 不可变**是关键约束：不允许原地改一个被 N 个元素共享的 `Style`。需要变更时构造新 `Style` 并 `StyleRegistry::Replace(id, newStyle)`，MVP 直接触发全树失效；精确做法（维护 `Style → 元素` 反向侵入链表，只失效引用者）列为后续优化。

---

## 8. 内存与体积

| 对象 | 开销 | 备注 |
| --- | --- | --- |
| `StyleValue` | 8 B | 定长 tagged union |
| `Decl` 条目 | 8 B（u16 id + u16 类型 + 4 B 值） | 值超 4 B 时占 12 B，按 8 B 对齐 |
| `Style` 对象 | ~40 B | 名字 StringId + 父引用 + 内联 4 条 `Decl`；超过 4 条溢出到 Arena |
| 元素（无覆盖） | 4 B | 只存 `StyleId` |
| 元素（1–2 项覆盖） | ~20 B | 内联 `SmallVec<条目, 2>` + 计数 |
| 继承态 | 0 B 常驻 | 遍历期栈上，约 48 B × 树深 |

- 属性表本身是编译期常量数组，`DUI_ENABLE_TEXT` 关闭时字体/文本类属性由 `#ifdef` 剔除，不进二进制。
- 动画槽仅在 `DUI_ENABLE_ANIMATION` 开启时存在。

---

## 9. 与其它模块的接口

| 模块 | 依赖方向 | 内容 |
| --- | --- | --- |
| `UI.Layout` | 消费 | 读取 `padding` / `margin` / 尺寸 / 字体类属性；文本元素传给 `Text` 层 |
| `UI.Event` | 消费 | 维护**输入类** `State` 位（`hover` / `pressed` / `focused`）；`disabled` 由属性推导，`selected` 等语义位由业务写入；`cursor` 属性 |
| `UI.Invalidate` | 消费 | 按属性标志标脏 |
| `UI.Animation` | 消费 | 动画写入最高优先级槽 |
| `UI.Widgets` | 双向 | 控件提供默认 `Style`；控件绘制读 `StyleContext` |
| `Text` | 无直接依赖 | 由 `UI` 转交 `fontSize`/`fontFamily`/`textAlign` 等；`Text` 层不认识样式系统 |
| `App.Theme` | 提供 | 默认值表与控件默认 `Style` |

---

## 10. 待确认

1. **D1 / D2 / D3 / D4** 是否按建议拍板。
2. MVP 属性清单（§3.2）是否增减；`shadow`、`textDecoration` 是否进 MVP。
3. ~~已定~~：状态全集、多状态合并优先级、`SetState` 写入契约、`selected` 归属见 §6.2。
4. `background` 是否提前升级为 `Brush`（纯色 / 线性渐变 / 图片）—— 影响是否需要资源引用与资源管理。
5. 元素本地覆盖是否允许超过 2 条（内联容量取值，决定元素基础尺寸）。
6. `Style` 注册是进程级还是窗口级（多窗口不同主题时的行为）。
7. 主题切换是否需要过渡动画（与 `UI.Animation` 联动）。
8. 子元素不写 `color` 时随父级 `selected` 变体变色，一旦写了 `color` 即被第 4 层（元素引用的 `Style`）压住而不再跟随（§5）—— 该"陷阱"是否需要在文档中显式警示，或引入受限的状态下传。

