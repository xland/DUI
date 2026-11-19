# DUI 输入控件使用指南（Input 开发者文档）

## 1. 选型：四种输入控件

| 需求                                  | 控件         | 类                   | 说明                    |
| ------------------------------------- | ------------ | -------------------- | ----------------------- |
| 一行文本                              | 单行输入框   | `dui::TextBox`       | 回车触发 `OnSubmit`     |
| 密码                                  | 密码框       | `dui::TextBox(true)` | 单行 + 掩码显示         |
| 多行纯文本                            | 多行输入框   | `dui::TextArea`      | 按宽度自动换行          |
| 富文本（标题 / 列表 / 引用 / 代码块） | 富文本编辑器 | `dui::RichEditor`    | 块级文档模型 + 行内样式 |

```cpp
dui::TextBox*  single = root->AddChild<dui::TextBox>(fieldStyle);  // 单行
dui::TextBox*  secret = root->AddChild<dui::TextBox>(fieldStyle);  // 密码
secret->SetPassword(true);
dui::TextArea* area   = root->AddChild<dui::TextArea>(fieldStyle); // 多行
dui::RichEditor* rich = root->AddChild<dui::RichEditor>();        // 富文本
```

![示例 IN1-1：三种文本控件](Input/IN1-1.png)

### 1.1 共同点

- **外观完全由盒模型样式决定**：`Padding` / `BorderWidth` / `BorderColor` / `CornerRadius` / `Background` / `Color` / `FontSize` 等，与普通 `Panel` 一模一样；输入框没有单独的"皮肤"。
- **可聚焦、进 Tab 链**：构造即 `SetFocusable(true)`；`Tab` / `Shift+Tab` 在可聚焦控件间移动焦点（控件自己不消费 `VK_TAB`）。
- **选区与光标可配**：`SelectionColor`（选区底色，默认 `0x803399FF` 半透明蓝）、`CaretColor`（光标色，未设时取文本色 `Color`）。
- **拖选可拖出控件**：按下即取**指针捕获**，拖到控件边界外仍继续扩选，并自动把光标滚进视野（多行纵向滚动 / 单行横向滚动）。
- **文本样式走继承**：`FontFamily` / `FontSize` / `FontWeight` / `Italic`（斜体）/ `TextDecoration`（下划线、删除线）都能用，写在父容器上对子输入框生效。
- **不支持 `TextAlign`**：属性存在，但输入框控件不读它。
- **有自然尺寸下限**：单行至少 120 DIP 宽、高至少是"字号 × 行高"；多行至少 200 × 80 DIP。下限只作用于**自然尺寸** —— 显式写了 `Width` / `Height` 就不受它约束。
- **单行文本垂直居中**：文本在**内容盒**内垂直居中（行盒居中 + 墨迹视觉居中补偿，与 `Text` 元素同一口径）—— 内容盒比行高小则上下对称裁剪，不会只裁底部。
- **显式高度不够会被裁**：控件按**内容盒**裁剪绘制，而内容盒高 = `Height` − `Padding` 上下 − 边框上下。若它小于文字的实际行高（默认 `LineHeight = 1.2`，配上雅黑行距系数后 14 DIP 的行高约 **22.2 DIP**），**文字底部（以及选区色带底部）会被切掉**。所以 14 DIP 文字配 `Padding(8)` 时，`Height` 要给到 **40 DIP**（内容盒 24 DIP ≥ 22.2）；或者干脆**不设 `Height`**，让控件按自然尺寸自适应。

### 1.2 焦点

```cpp
dui::FocusedElement();      // 当前焦点元素（nullptr = 无焦点）
element->Focus();           // 让某个元素取得焦点
dui::SetFocus(element);     // 同上（会置/清 State::Focused 状态）
```

焦点态样式用状态变体写：

```cpp
input->When(dui::State::Focused, dui::Style().Background(0xFFDCEEFF));
```

获得焦点时若没有选区，**光标会跳到文本末尾**；失去焦点时**选区被清除**（避免留下看不见的选区）。

### 1.3 静态文本的选择（`user-select`）

输入框能选自己框内的文字；**普通 `dui::Text`（静态文本）默认不能选** —— 与浏览器一致，需要时用样式显式打开：

```cpp
dui::Text* label = root->AddChild<dui::Text>(L"按住拖动即可选中这段文字", dui::Style()
    .UserSelect(true)          // 关闭（默认）= 不允许选择
    .Background(0xFFFFFFFF));
```

打开后：

| 操作         | 行为                                          |
| ------------ | --------------------------------------------- |
| 按住拖动     | 选区随鼠标扩展；拖出文本框仍能继续扩选        |
| 双击         | 选词（中文逐字、英文按单词）                  |
| 三击         | 选段（整行）                                  |
| `Ctrl+C`     | 复制选中文本到系统剪贴板                      |
| `Ctrl+A`     | 全选                                          |
| `←` `→`      | 移动光标（`Shift` 扩展选区、`Ctrl` 按词移动） |
| `Home` `End` | 行首 / 行尾（`Shift` 扩展选区）               |

- 选中的文本**会取得焦点**（才能收 `Ctrl+C` 等键盘事件），但**不进 Tab 链** —— `Tab` 键不会跳到它（CSS `tabindex=-1` 语义）。
- 焦点离开时选区清空；选区底色取 `selectionColor`（默认 `0x803399FF` 半透明蓝），画在文字之下。
- 查询与控制：`Selectable()` / `SelectionStart()` / `SelectionEnd()` / `HasSelection()` / `SelectedText()` / `SelectAll()` / `Deselect()`。
- 选区落在**排版结果**上：设了 `text-overflow: ellipsis` 时，选中并复制到的是**截断后**那段文字。

---

## 2. 单行输入框 `TextBox`

### 2.1 读写文本

```cpp
dui::String s = box->GetText();   // 真实文本
box->SetText(L"你好，DUI");
box->Clear();                     // 等价于 SetText(L"")
```

`SetText` 的三个要点：

- **换行归一**：`\r\n` / `\r` 统一成 `\n`；
- **清空撤销栈**（`Ctrl+Z` 不能再撤回到 `SetText` 之前）；
- **光标只做 clamp，不归零也不移到末尾** —— `SetText` 之后光标仍在原下标（可能超出新长度则被夹住）。要显式定位用 `SetCaret()`。

### 2.2 事件

```cpp
// 文本变化：参数是新的真实文本（密码模式也传明文）
box->OnTextChanged([](const dui::String& text) { /* ... */ });

// 回车提交：只在单行 TextBox 上触发（TextArea 的回车是插入换行）
box->OnSubmit([]() { /* ... */ });
```

`OnTextChanged` 在**输入、退格、删除、剪切、粘贴、撤销、重做**时都会触发 —— 撤销也会回调，别在里面做重活。

元素级 `OnKeyDown` / `OnChar` / `OnPointerDown` 与这两个语义事件**并存**：控件内部先处理（消费），绑定仍然能收到。

### 2.3 光标与选区

```cpp
uint32_t caret = box->Caret();          // 光标下标
box->SetCaret(5);                       // 定位光标（会清掉选区）
uint32_t a = box->SelectionStart();     // 选区起点
uint32_t b = box->SelectionEnd();       // 选区终点（不含）
bool has = box->HasSelection();
dui::String picked = box->SelectedText();
box->SelectAll();                       // 全选
box->Deselect();                        // 取消选区（光标不动）
```

**下标口径是 UTF-16 码元**（不是"第几个汉字"）：`GetText().size()` 就是下标上限；代理对（emoji 等）占 2 个码元，光标可能落在代理对中间。

![示例 IN2-1：事件与光标选区](Input/IN2-1.png)

### 2.4 键盘与鼠标

| 按键                       | 行为                                                     |
| -------------------------- | -------------------------------------------------------- |
| `←` / `→`                  | 按码点移动；**有选区时先跳到选区端点**；`Shift` 扩展选区 |
| `↑` / `↓`                  | **仅多行**；单行不消费（继续冒泡）                       |
| `Home` / `End`             | 行首 / 行尾；`Ctrl+Home` / `Ctrl+End` = 全文首 / 尾      |
| `Backspace` / `Delete`     | 删除选区或前 / 后一个码点                                |
| `Enter`                    | 多行插入 `\n`；**单行触发 `OnSubmit`**                   |
| `Ctrl+A` / `C` / `X` / `V` | 全选 / 复制 / 剪切 / 粘贴（**没有 Ctrl 不消费**）        |
| `Ctrl+Z` / `Ctrl+Y`        | 撤销 / 重做                                              |
| `Tab`                      | **不消费** → 框架移动焦点（不会插入制表符）              |

鼠标：**单击定位光标**、**按住拖动选区**（可拖出控件边界）。注意：

- **双击选词 / 三击选行**（M5.73 起支持）：双击选中所在词（中文逐字、英文按单词，规则与 `dui::Text` 的 `user-select` 选词完全一致），三击选中所在**显示行**（软换行后的视觉行）；双击后按住拖动 = 从词首扩选。
- **滚动条与滚轮**（M5.74 起支持，**仅多行 `TextArea`**）：内容高度超出控件时右侧出现**纵向滚动条**（可拖滑块、点轨道跳页），**滚轮直接滚 `TextArea` 自己的内容**（不再冒泡给祖先 `Scroller`）。滚动条浮在内容之上（overlay），不占布局宽度。内容没溢出时滚轮照常冒泡给祖先 `Scroller`。
  - **滚动条贴在元素右缘**（M5.75）：贴**边框内侧**、贯穿整个元素高度（浏览器语义：滚动条在 padding box 外缘），**不**贴内容盒右缘 —— 后者在有 `Padding` 时会离右边缘隔一段（看着不贴边）。`Scroller` 同款规则。
  - 单行 `TextBox` **不画横向滚动条**：条会盖住垂直居中的文字；单行的横向滚动仍由"光标跟随可见"驱动（长文本可用方向键 / End 键查看）。

---

## 3. 密码框

```cpp
dui::TextBox* pwd = root->AddChild<dui::TextBox>(fieldStyle);
pwd->SetPassword(true);        // 也可构造时传参：dui::TextBox(true)
pwd->SetMaskChar(L'*');        // 自定义掩码字符（默认是 U+2022 '•'）
bool on = pwd->IsPassword();
wchar_t ch = pwd->MaskChar();
```

![示例 IN3-1：密码框](Input/IN3-1.png)

要点：

- **只挡显示**：真实文本不变，`GetText()` 拿到的仍是明文，`OnTextChanged` 的参数也是明文。
- **剪贴板里也是明文**：`Ctrl+C` / `Ctrl+X` 复制的是真实文本 —— 密码框只防"看肩膀"，不防剪贴板。
- 默认掩码用 `•`（U+2022）而不是 `●`（U+25CF）：大实心圆和 1px 光标条同色紧邻时会看成一条粗竖线；框架还对掩码字形做了垂直居中补偿。
- `SetPassword` 切换不会丢文本，只重建显示串。

---

## 4. 多行输入框 `TextArea`

`TextArea` 继承 `TextBox`，只是把多行打开：

```cpp
dui::TextArea* area = root->AddChild<dui::TextArea>(dui::Style()
    .Width(440.f).Height(140.f)
    .Padding(8.f)
    .Background(0xFFFFFFFF)
    .BorderWidth(1.f).BorderColor(0xFFD0D7DE)
    .CornerRadius(6.f));
area->SetText(L"第一行\n第二行");
```

![示例 IN4-1：多行输入框](Input/IN4-1.png)

与单行的差别：

|           | 单行 `TextBox`     | 多行 `TextArea`                                                 |
| --------- | ------------------ | --------------------------------------------------------------- |
| 回车      | 触发 `OnSubmit`    | 插入 `\n`                                                       |
| `↑` / `↓` | 不消费             | 在行间移动光标（记住目标列）                                    |
| 折行      | 不折行（横向溢出） | **按宽度自动折行**（换行宽取 `Width` 减 padding，否则取可用宽） |
| 溢出      | 横向内部滚动       | 纵向内部滚动                                                    |

内容超出视口时，控件**按光标位置内部滚动**（光标始终可见），但**没有滚动条**；滚轮不会滚它（会冒泡给祖先 `Scroller`）。

> 撤销栈上限 100 步，且**每个按键一步**（不像 `RichEditor` 那样把连续输入合并成一步）。

---

## 5. 富文本编辑器 `RichEditor`

`RichEditor` 作用于 **`richtext::RichDoc` 文档模型**：块级结构（标题 / 列表 / 引用 / 代码块 / 图片 / 分割线）+ 行内 run 样式（粗 / 斜 / 下划线 / 删除线 / 链接 / 高亮）。

### 5.1 结构：必须外套 `Scroller`

`RichEditor` **不生成块级子元素、也不画滚动条**（视口与滚动条归外层 `Scroller`）。它只暴露文档总高，外层据此算滚动范围。

```cpp
dui::Scroller*  page  = root->AddChild<dui::Scroller>(dui::Style()
    .FlexGrow(1.f)
    .Background(0xFFF5F6F8));
dui::Panel*     sheet = page->AddChild<dui::Panel>(dui::Style()
    .Padding(dui::Thickness(24.f, 28.f, 24.f, 32.f))
    .Background(0xFFFFFFFF));
dui::RichEditor* view = sheet->AddChild<dui::RichEditor>();
view->SetDoc(doc);
```

**没有祖先 `Scroller` 就不会滚动**（连"光标跟随"也失效：光标滚不进视口）。

### 5.2 文档模型 `RichDoc`

```cpp
namespace rt = dui::richtext;

rt::RichDoc doc;                 // 文档
doc.defaultFont = L"Microsoft YaHei";
doc.defaultFontSize = 15.f;

rt::Block block;                 // 块
block.kind  = rt::BlockKind::Heading;   // Paragraph/Heading/List/ListItem/Quote/Code/Image/Rule
block.level = 1;                          // Heading 的 1..6 级

rt::Run run;                     // 行内片段
run.text  = L"粗体";
run.style.weight = 700;          // 粗体（字重；0 = 继承）
run.style.italic = true;         // 斜体
run.style.underline = true;      // 下划线
run.style.strike = true;         // 删除线
run.style.href = L"https://…";   // 链接
run.style.background = 0xFFFFF3CD;  // 高亮底色（A == 0 = 无）
run.style.padding = dui::Thickness(2.f, 6.f);
run.style.radius = 4.f;               // 高亮盒圆角
run.style.color = 0xFF1A1A1A;         // 文字色（A == 0 = 继承）

block.runs.push_back(run);
doc.blocks.push_back(block);
```

块级样式在 `BlockStyle`：`align`（`Start`/`Center`/`End`）、`lineHeight`（倍数）、`lineHeightDip`（绝对行高，优先）、`margin`、`indent`、`background`、`borderLeftWidth` / `borderLeftColor`（引用块左边框）。

**样式"空值即继承"**：`fontSize == 0` / `weight == 0` / `color.A == 0` / `family` 空串 → 沿用文档默认或父级。

![示例 IN5-2：用代码构造富文本](Input/IN5-2.png)

### 5.3 喂数据：JSON 导入

```cpp
rt::RichDoc doc;
rt::ParseRichDoc(dui::Utf8ToWide(kDocJson), doc);   // 返回 false = 根结构非法
if (!doc.Diagnostics().Clean()) {
    // doc.report：未知块 / 未知行内 / 未知样式键 / 非法值的计数与首条问题
}
```

支持的 JSON 形态：

| JSON                                                                                  | 含义               |
| ------------------------------------------------------------------------------------- | ------------------ |
| `paragraph` / `heading` / `quote` / `codeBlock` / `image` / `horizontalRule` / `list` | 块                 |
| `text` / `bold` / `italic` / `underline` / `strikethrough` / `hyperlink`              | 行内容器           |
| `settings.defaultFont` / `defaultFontSize`                                            | 文档默认字体与字号 |

解析策略是**坏数据不中断**：未知键跳过并计数，文档继续解析。

![示例 IN5-1：JSON 导入的富文本](Input/IN5-1.png)

> **只有导入，没有导出** —— 框架目前**没有**把 `RichDoc` 序列化回 JSON 的 API。

### 5.4 编辑交互

| 操作                                   | 行为                                                                              |
| -------------------------------------- | --------------------------------------------------------------------------------- |
| 回车                                   | 分裂块；列表项 → 新增同级项；空列表项回车 → **退出列表**；代码块内 → 只换行不拆块 |
| 退格                                   | 段内按码点删；块首退格 → 与**上一块合并**（代码块不合并）                         |
| Delete                                 | 段内后删；块尾 Delete → 与**后一块合并**（代码块不合并）                          |
| `Ctrl+A` / `C` / `X` / `V` / `Z` / `Y` | 全选 / 复制 / 剪切 / 粘贴 / 撤销 / 重做（粘贴：有文本贴文本，无文本则贴剪贴板图像）|
| 拖入图像文件                           | 在光标处插图（PNG/JPG/BMP/GIF/TIFF/ICO；走元素级 `DropFilesEvent`，见 §5.7）      |
| `Tab`                                  | 不消费 → 框架切焦点                                                               |

- **撤销粒度 = 整文档快照**（上限 100 步），但**连续输入会合并成一步**。
- 分割线是只读装饰块（只能被相邻的退格 / Delete 删掉）；图片不可编辑内容，但可作光标定位点（图前 / 图后）。
- 代码块的正文放在 `block.code`（不在 `runs` 里），`showLineNumbers` 控制行号。

### 5.5 只读模式

```cpp
view->SetEditable(false);       // 默认 true
bool editable = view->IsEditable();
```

只读后：不可聚焦（不进 Tab 链）、不响应点击聚焦、键盘事件照常冒泡但不处理、不画光标、不闪烁。

### 5.6 取回文档

```cpp
const rt::RichDoc& d = view->Doc();     // 内部副本的引用（编辑后随之变化）
float h = view->ContentHeight();        // 排版后的文档总高（DIP，不含 padding）
```

`Doc()` 返回的是**内部副本的引用** —— 它会跟着编辑变化，但**不能用来回写宿主的模型**；要拿当前内容得自己拷一份。

> `SetDoc()` 是**值语义拷贝**，并且会**清空撤销栈**、复位光标。

### 5.7 插入图片

```cpp
view->InsertImage(L"D:\\pics\\photo.png");   // 图像文件路径
view->InsertImage(L"data:image/png;base64,iVBORw0KGgo...");  // 或 data URI
```

在当前光标处插一个图片块，**保留撤销栈与光标位置**（不走 `SetDoc`）：新块插在光标所在块之后，光标落在**图后**，可继续打字；`Ctrl+Z` 能撤回。只读（`SetEditable(false)`）时返回 `false`。

尺寸由框架按图像自然尺寸决定，调用方**不用传 size**：图片块的 `width`/`height` 留 0，排版时按解码纹理的像素宽高比自动定盒（像素 ÷ DPI 缩放 = DIP）。路径为空、或文档中已无可用文本块时返回 `false`。图片格式为 WIC 支持的 PNG/JPEG/BMP/GIF/TIFF/ICO；解码失败显示占位框 + `altText`。

**三种自动插图方式**（都走 `InsertImage` 内部通路，保留撤销栈、光标落图后）：

- **`Ctrl+V` 图像位图**：剪贴板有文本时照常贴文本；无文本但有位图（截图工具、浏览器复制的图）时，框架把 CF_DIB 位图编码成 PNG → `data:image/png;base64,...` 内联插入。
- **`Ctrl+V` 图像文件**：在资源管理器里选中图像文件（PNG/JPG 等）`Ctrl+C`，剪贴板是文件路径列表（`CF_HDROP`），框架按文件路径逐个插入。多个文件只插图、忽略非图像。
- **拖入图像文件**：从资源管理器拖 PNG/JPG/BMP/GIF/TIFF/ICO 到编辑器，**鼠标未释放的悬停阶段就显示一条蓝色插入光标**（网页式体验，指示将插到何处）；释放即插到该落点（所见即所得）。走 OLE `IDropTarget`（`DragOverFilesEvent` 预览 → `DropFilesEvent` 插入，见 [Window.md](Window.md) §6.2）；非图像文件不消费、光标显示禁止符号。

> 暂未设单图大小限制：内联 data URI 会让文档 JSON 膨胀约 33%，撤销是整文档快照，大图会让撤销栈涨得快。

---

## 6. 已知限制与坑

**TextBox / TextArea**

1. **单行 `TextBox` 没有横向滚动条**（多行 `TextArea` 有纵向滚动条，见 §2.4）：条会盖住垂直居中的文字；单行的横向滚动只由"光标跟随可见"驱动。
2. 不支持 `TextAlign`。
3. 自然尺寸有下限（单行 120 DIP 宽、多行 200 × 80 DIP），显式设 `Width` / `Height` 才不受限。
4. **显式 `Height` 小于"内容盒所需高度"时，文字底部与选区色带会被裁掉**（内容盒高 = `Height` − `Padding`×2 − 边框；需 ≥ 文字实际行高，14 DIP 雅黑约 22.2 DIP）。要么给足高度，要么不设 `Height` 让它自适应。
5. 撤销栈上限 100 步，且**每个按键一步**（连续输入不合并）。
6. `SetText` **清空撤销栈**、**不重置光标**（只 clamp）。
8. **密码模式下复制到剪贴板的是明文**。
9. 没有 placeholder、没有 maxLength、没有输入校验。
10. IME 只走系统隐形 caret，**没有内联 preedit**（组合中的文字不在控件里显示）。

**RichEditor**

11. **必须外套 `Scroller`** 才能滚动；否则连"光标跟随可见"都没有。
12. 撤销是**整文档快照**（上限 100 步）→ 大文档内存与重排成本高。
13. **没有 JSON 导出 API**。
14. **没有变更事件**（无 `OnTextChanged` / `OnChange`），宿主只能轮询 `Doc()` 对比。
15. 代码块正文在 `block.code` 而非 `runs`；JSON 的 `language` 字段被丢弃。
16. 分割线不可编辑；图片只有"图前 / 图后"两个光标位，不能改尺寸（但可用 `InsertImage` 插入，见 §5.7）。
17. 引用块的 `attribution` 字段只在模型里，**渲染层暂不绘制**。
18. `Doc()` 返回内部副本的引用，不能用于回写宿主模型；`SetDoc()` 会清空撤销栈。
19. IME 同样没有内联 preedit。
