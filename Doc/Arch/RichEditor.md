# 富文本编辑器（RichEditor）

> 状态：**设计中 v0.1**（2026-11-10）。本文件逐节确认，全部定稿后才开始实现；
> 未标「已定」的条目一律不得进代码。
> 相关：[11-Text.md](./11-Text.md)（文本地基）、[02-Decisions.md](./02-Decisions.md) ADR-009 / ADR-013 / ADR-016 / ADR-018、[16-Image.md](./16-Image.md)（图像链路）、[04-Layout.md](./04-Layout.md)。

## 1. 范围与边界

### 1.1 本期做

- 识别一种 JSON 文档结构（见 §2），载入为内存文档模型。
- 按数据内声明的样式渲染：块级（标题 / 段落 / 列表 / 引用 / 代码块 / 图片 / 分割线）+ 行内（粗体 / 斜体 / 下划线 / 删除线 / 超链接 / 文字颜色 / 背景高亮）。
- 接收键盘输入并改写文档模型中的文字（含中文 IME）。
- 编辑后可序列化回同一结构（往返一致）。

### 1.2 本期不做

| 项 | 理由 |
| --- | --- |
| **表格（table）** | **已定：暂缓**。跨行跨列 + 列宽分配是独立算法，等正文编辑通路稳定后再做，见 §5 P6 |
| 工具条 / 按钮控件 | 明确由开发者自行组装，框架只提供文档模型与编辑原语 |
| 插入链接 / 插入图片的交互 | 同上，开发者通过 API 改模型 |
| 语法高亮（codeBlock 的 language 字段） | 只做等宽字体 + 行号，不做着色 |
| 网络图片下载 | 框架无网络栈，由宿主下载后注入，见 §3.5 |
| HTML / RTF / Markdown 互转 | 与 ADR-023 的远期项一致 |
| 协同编辑、批注、查找替换 | 非本期 |
| **`settings` 的 `theme` / `locale` / `spellCheck` / `autoSave*`** | **已定：本期不做**。属宿主应用配置（主题切换、拼写检查、自动保存定时器），与渲染和编辑语义无关，解析层直接丢弃 |

## 2. 输入数据格式

### 2.1 结构概览

```
{ settings: {...}, blocks: [ Block ] }

Block.type ∈ heading | paragraph | list | quote | codeBlock | image | horizontalRule
  heading        : level(1-6) + content(string) + styles
  paragraph      : content(string | Inline[]) + styles
  list           : listType(unordered|ordered) + items[](listItem) + styles
  quote          : content(Inline[]) + attribution + styles
  codeBlock      : language + code(string) + showLineNumbers + styles
  image          : src + altText + width + height + alignment + styles
  horizontalRule : styles

Inline.type ∈ text | bold | italic | underline | strikethrough | hyperlink | text
  除 text 外均为容器：{ type, content: Inline[], styles?, href? }
```

### 2.2 已识别的形态不一致（解析期必须兼容）

| 现象 | 处理 |
| --- | --- |
| `heading.content` / `paragraph.content` 既可能是 **string** 也可能是 **Inline[]** | 解析期统一包成一个 `text` run |
| `listItem.content` 里可以混入嵌套 `list` 节点 | listItem 拆成「自己的 runs」+「子列表」两段 |
| `table.rows/columns` 与 `cells` 二维数组冗余 | 本期不解析 table（§1.2） |
| `styles` 里既有数值也有 CSS 字符串（`"32px"` / `"1.6"` / `"4px solid #007bff"`） | 走 §3.3 的白名单解析 |
| `image.width/height` 是字符串，`styles.height` 可能是 `"auto"` | 解析为 `float` + `Auto` 标记 |
| `settings` 里只有 `defaultFont` / `defaultFontSize` 进模型（run 无显式字体时的兜底）；`theme` / `locale` / `spellCheck` / `autoSave*` **本期不解析、直接丢弃** |

## 3. 架构决策

> 以下每条都标了状态；`待确认` 的条目定稿后才能开工对应模块。

### 3.1 D1 渲染架构：RichEditor 自绘（建议，待确认）

**结论**：`Scroller > RichEditor`，RichEditor 内部自带块级排版器与行内排版器，**不为每个 block 生成 Element 子树**。

理由：编辑器的命脉是**统一坐标系** —— 光标、命中、选区、复制区间必须落在同一套偏移上。若段落 / 列表项各自是 Element，跨块光标要在元素边界间反复换算，复杂度远高于自己写一遍块级垂直堆叠。

- RichEditor 只做排版 + 绘制 + 编辑，自身不画滚动条；视口与滚动条由外层 `Scroller` 提供（`MeasureContent` 返回文档总高）。
- 不生成元素子树 ⇒ 不能白拿 `Flex` / `Grid` / 样式级联；块级样式在 RichEditor 内部按 `BlockStyle` 直接参与排版（不值当为此搭一套级联）。

### 3.2 D2 行内树展平为 run 序列（已定）

`bold → italic → text` 这类嵌套在进入排版前压成线性序列：

```cpp
struct Run { std::wstring text; RunStyle style; };
```

嵌套容器**只贡献样式、不产生 run**，样式按「父为基、子覆盖」合并。渲染、光标、选区、序列化全部只认 run 序列。

**与排版引擎的衔接（P0 已落地）**：`TextEngine::TextRun` 只描述与段落默认值的**差异**（字号/字重/族名/颜色/斜体/下划线/删除线，未设置的字段继承），`ShapeInput::runs` 直接指向文档模型持有的数组、引擎**不拷贝**；内容变化由调用方递增 `runVersion` 告知引擎重建 layout（`Measure` 每帧调用，不做 run 数组深比较）。`runCount == 0` 时走单样式路径，与 run 化前完全同路。

### 3.3 D3 样式值：CSS 白名单子集（建议，待确认）

框架内部尺寸全是不带单位的 `float` DIP，没有 `px/em/%` 单位系统，因此必须有一层解析：

| CSS 形态 | 解析结果 |
| --- | --- |
| `"32px"` / `"32"` | `32.f` DIP（`px` 与 DIP 在 96dpi 下等同，非整数 DPI 由 `scale` 统一处理） |
| `"1.6"`（lineHeight） | 倍数，直接参与排版的 `SetLineSpacing` |
| `"#2c3e50"` | `Color`（`0xAARRGGBB`，无 alpha 视为 `0xFF`） |
| `"4px solid #007bff"` | `{ width, style, color }`，只支持 `solid` / `none` |
| `"2px 6px"`（padding） | `Thickness`，按 CSS 的 1/2/3/4 值展开规则 |
| `"Arial, sans-serif"` | 取**第一个本机可用**的族名给 DWrite（DWrite 只吃单族名） |

白名单之外：**静默忽略 + 计数**，通过 `RichDoc::Diagnostics()` 暴露（不静默画歪、也不抛错）。

### 3.4 D4 JSON：复用框架级 `Core/Json`（已定，已落地）

原计划在 RichEditor 内自写解析器，实际**提升为框架级通用能力**：`DUI/Core/Json.h`（配套 `Core/Utf8.h` 转码工具）。RichEditor 直接复用，不再自带任何解析代码。

选型依据 —— 排除 WinRT `Windows.Data.Json`（原生 Win32 没有别的内置 JSON）：

| 项 | 影响 |
| --- | --- |
| 异常模型 | C++/WinRT 以 `winrt::hresult_error` 报错，官方要求 `/EHsc`；本项目基线是 `DUI.props:19` 的 `ExceptionHandling=false`（`/EHs-c-`），`throw` 不展开栈 —— **硬冲突** |
| 依赖面 | `<winrt/Windows.Data.Json.h>` 会拉入 `winrt/base.h`、链接 `windowsapp.lib`、并在运行期用到激活工厂，编译时间与体积都涨 |
| 数值模型 | 全部 `double`；整数辅助层仍得自己写，等于没省事 |
| 错误处理 | 异常 + `HSTRING`，与框架 `bool + String&` 的风格相反 |
| 版本 | 自 Win10 起可用，与 ADR-009（1703）不冲突 —— 它唯一的优势 |

接口形态（完整约定见 `DUI/Core/Json.h` 头注释）：

```cpp
json::Error err;
json::Value doc = json::Parse(text, err);        // 失败返回 Null，err.Failed() 为真
if (err.Failed()) { Log(err.ToString()); return; }   // 带行号列号与数据路径

const json::Value* blocks = doc.Find(L"blocks");
for (size_t i = 0; i < (blocks ? blocks->Size() : 0); ++i) {
    const json::Value& b = *blocks->At(i);
    const String type = b.GetString(L"type");    // 缺失 / 类型不符 → 默认值
}
if (const auto* px = doc.Path(L"blocks[0].styles.fontSize")) { /* … */ }

String out = json::Write(doc, { .indent = 2 });
```

关键约定：严格 JSON（不支持注释与尾逗号）；字符串全程 `dui::String`（UTF-16）；对象保序（序列化顺序 = 解析顺序，往返可比对）；数字统一 `double` 存储，`Int()` / `AsInt()` 表达整数形态；`Find` / `At` 缺失返回 `nullptr`，取值全走默认值。独立编译单元，未引用即被静态库裁掉（ADR-016）。

### 3.5 D5 图像：来源是「引用」而非「字节内联」（已定）

渲染层只需要一件事：**给定来源，拿到解码后的位图**。所以不要把「base64 还是文件」这条选择混进渲染代码。

数据模型只存来源描述：

```cpp
enum class ImageSourceKind { File, Url, DataUri, Resource };
struct ImageRef {
    ImageSourceKind kind;
    String uri;                 // File: 路径；Url: https；DataUri: data:image/png;base64,...；Resource: 资源 id
    Vec2   sizeDip;             // Auto 时按宽度与宽高比推
    bool   heightAuto;
    Align  align;
};
```

解码交给 `ImageResolver`（宿主可替换）：

| 来源 | 内建实现 | 说明 |
| --- | --- | --- |
| `File` | `Image::LoadFile`（WIC） | 同步解码；失败画占位框 + `altText` |
| `Resource` | `Image::LoadMemory`（WIC 嗅探格式） | 编辑期粘贴/拖入的字节放文档资源池，按 id 取 |
| `Url` | **不内建** | 框架无网络栈；宿主下载后转成 `Resource`，或落盘后改成 `File` |
| `DataUri` | 可选工具（约 40 行 base64） | 独立可裁剪单元，默认不编入 |

**用户粘贴/拖入的图（编辑期）**：字节进入 `DocumentResources`（`id → bytes`），block 记为 `Resource`；纹理按 id 进 `TextureStore` 共享，同一张图多次出现不重复解码。

**存盘/序列化时（由宿主选，框架提供两种可选工具）**：

| 方案 | 优点 | 缺点 |
| --- | --- | --- |
| base64 内联（data URI） | 文档自包含、单文件可迁移 | JSON 膨胀 33%；每次加载要 base64 + WIC 两次解码；大图内存峰值高；文档不可读不可 grep |
| 缓存文件 + 相对路径 | JSON 小、WIC 直接读、大图友好、纹理可复用 | 需目录管理与清理；文档搬移会丢图；绝对路径不可移植 |

**已定（2026-11）：保存用 base64 内联**（`data:image/png;base64,...`）。编辑期仍走内存 `Resource`，序列化时统一编码成 data URI —— 文档自包含、可单文件迁移，与 `settings` 一起就是一份完整文档。

已知代价（接受）：JSON 体积膨胀约 33%；每次加载要 base64 + WIC 两次解码；大图内存峰值高。因此约定**单图上限**（建议 2 MB 原始字节，超出则拒绝插入并计数），并且保留切换到「缓存文件 + 相对路径」的余地 —— `ImageSourceKind` 已把两者分开，换存储方式不动模型。

第一版**不做异步解码**，但 `ImageResolver` 按可异步设计（返回句柄，允许先给占位再替换），避免以后改签名。

### 3.6 D6 编辑语义（已定）

- **回车 = 分裂新段落**。有选区先删选区，再在光标处切分：切分点之前的 run 留在原块、之后的进新块；新块继承原块的块级样式与切分点处的字符样式。列表内的回车走"新增列表项"（P4）。
- **块内不留 `\n`**：换行即块边界，段落是纯单行文本。好处是 `Cursor{blockIndex, offset}` 的 offset 永远是段内偏移，不会与自动折行混淆；代价是粘贴多行文本时要拆块（P3）。**例外：引用块与代码块**（见下两条），它们的 `\n` 留在块内。
- 段首退格 = 与上一段合并（P4）；空段落退格 = 删除该块。
- **图片块的光标位（已定，2026-11-11）**：图片不是可编辑文本，但是一个光标定位点，有两个位置 —— `offset 0` = 图前（贴图片左缘）、`offset 1` = 图后（贴图片右缘）。命中测试按图片盒中点分：点在图片右半（含图片右侧的空白）落图后，点左半落图前。因此"点击图像后面的空白 → 光标出现在图像右侧"，此时 Backspace 删除该图；Del 则在图前删图、图后删下一块。方向键在图前 ↔ 图后之间移动，End 落图后。**删图后光标落在图片原来的位置**：后面还有可编辑块 → 该块开头；图片是最后一个可编辑块 → 前一个可编辑块的**末尾**（示例文档里图片紧跟代码块之后，删图后光标停在代码最后一行末，而不是被拽回代码块行首）。**命中带**：图片的命中带 = 图片盒 + 上一块内容底到图片盒之间那一条（上一块的下内边距 / 间隙归「图片这一行」）—— 代码块盒底紧贴图片顶边（图片 `margin.top = 0`），中间那条淡灰内边距若归代码块，点图片右上方的空白光标就会跳进代码块；文本块的命中带则是**文字区**（不含内边距），点在内边距里不算落在该块。
- **代码块可编辑（已定，2026-11-11）**：代码块不再是只读块 —— 光标可进入、可增删字符。代码内容存放在 `Block::code`（整段字符串，与 `runs` 分离），因此模型层编辑原语（`BlockText` / `InsertText` / `DeleteRange` / `StyleAt` / `SplitBlock` / `AppendRuns`）按 `kind` 分流：代码块直接改 `code`，其余走 `runs`。
  - **回车 = 块内换行**：在光标处往 `code` 里插 `\n`，不分裂成两个块（代码块的换行是代码的一部分）。
  - **粘贴 = 整段插入**：多行文本原样进 `code`（保留换行），不拆成段落。
  - **块边界不跨块合并**：代码块与相邻块之间的 Backspace / Del 不合并、不吞字（代码不与正文混排）；跨块选区删除时两块各自清空内容、保持独立。
  - **不折行**：代码块按 `kNoWrapWidth` 排版，`\n` 由 DWrite 强制换行，多行各自成"视觉行"，上下方向键 / Home / End 在行内生效。

## 4. 数据模型（草案）

```cpp
struct RunStyle {                       // 强类型，解析后不再有字符串
    const wchar_t* family;  float size;  FontWeight weight;
    bool italic, underline, strike;
    Color color, bgColor;   Thickness bgPadding;  float bgRadius;
    const wchar_t* href;                 // 仅 hyperlink 产出
};

struct Run { std::wstring text; RunStyle style; };

struct Block {
    enum class Kind { Heading, Paragraph, List, Quote, Code, Image, Rule };
    Kind kind;  BlockStyle style;
    std::vector<Run> runs;               // 文本类 block
    // List : items[]（每项 runs + 子 list）
    // Image: ImageRef
};

struct Settings {                       // 仅取这两个；theme/locale/spellCheck/autoSave* 本期不解析
    const wchar_t* defaultFont;  float defaultFontSize;
};
struct RichDoc { Settings settings; std::vector<Block> blocks; };
```

**位置模型**：`Cursor{ blockIndex, offset }`，选区为两个 Cursor；全局连续偏移由块长度累加得出（复制、序列化、跨段删除共用）。

## 5. 分期路线

> 已落地（2026-11）：`Core/Json` + `Core/Utf8`（框架级 JSON 与转码）、**P0 文本 run 化**。下表 P1 起为 RichEditor 本体。

| 期 | 内容 | 验收 | 状态 |
| --- | --- | --- | --- |
| P0 | `TextEngine` run 化：`TextRun` / `ShapeInput`、分段字号/字重/族名/斜体、按 run 取色（drawing effect）、下划线/删除线（DWrite 回调） | `runCount == 0` 走原单样式路径，零额外分配与 COM 开销 | ✅ 已落地 |
| P1 | 文档模型 + CSS 白名单（JSON 复用 `Core/Json`） | 示例 JSON 完整解析，坏字段进 Diagnostics | ✅ 已落地（2026-11，`UI/RichDocJson`） |
| P2 | 只读渲染：heading/paragraph/list/quote/code/image/hr | 静态截图逐块对得上 | ✅ 已落地（2026-11，`UI/RichEditor`；图片为占位框） |
| P3 | 编辑：光标/选区/拖选/输入/删除/回车/复制粘贴/撤销/IME preedit | 中英文混录、段内编辑正确 | 待确认 |
| P4 | 跨段选择与删除、列表回车续项、样式继承 | 往返一致、跨段删除不崩 | 待确认 |
| P5 | 序列化回 JSON | 与输入结构往返一致 | 待确认 |
| P6 | **表格**（含 rowSpan/colSpan、列宽分配） | 暂缓 | 暂缓 |

规模感受：P0 中、P1 中、P2 中大、**P3 最大**（富文本编辑的复杂度几乎都在这里）。

## 6. 待确认清单

| # | 问题 | 当前建议 |
| --- | --- | --- |
| 1 | 样式白名单：只覆盖示例出现过的属性，还是更宽的 CSS 子集 | 只覆盖示例 + 少量常见项 |
| 2 | 是否需要只读模式（同一控件 `readonly`） | 需要，`metadata.readonly` 已在数据里 |

> 已定（2026-11）：回车 = 分裂新段落（D6）；图片保存 = base64 内联（D5）。
