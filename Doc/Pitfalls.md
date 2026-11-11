# 踩坑与实现结论备忘（Pitfalls）

> 从 Todo.md 各里程碑的实现记录迁入（2026-11-07）。
> 每条 = 现象 → 根因 → 修法（或结论）。条目按主题归类，标注来源里程碑。
> 遇到"看起来没道理"的问题先查这里，再上网搜。

---

## 1. Win32 窗口与输入

### 1.1 WM_IME_CHAR 必须吃掉（M5.9）
不 return 0 的话 `DefWindowProc` 会再生成一个 `WM_CHAR`，同一个字符插入两次（IME 提交场景）。

### 1.2 WM_NCCALCSIZE 无边框：原样保留 rgrc[0]（M5.11）
- 无边框窗口（`WS_POPUP`）在 `wParam=TRUE` 时**原样保留 `rgrc[0]` 返回 0**，不调 `DefWindowProc` 扣边框 → 客户区 = 整窗。
- **不能写 `rgrc[0] = rgrc[1]`**：`rgrc[1]` 是**上一次**的窗口矩形。最小化时它是离屏图标矩形（实测 `(-32000,-32000,-31801,-31966)`，199×34 物理像素）→ 还原后客户区被压成 199×34，渲染一片白，且**之后不会再有 WM_SIZE**，补画救不回来。
- 诊断手段：打印 `rgrc[0]/rgrc[1]`、`WM_WINDOWPOSCHANGED` 矩形、`WM_SIZE` lParam，配合 PowerShell `ShowWindow(SW_MINIMIZE/SW_RESTORE)` + 截图对比。

### 1.3 WM_GETMINMAXINFO（M5.11）
结构体**没有** `rcWork`/`rcWindow` 字段（那是 `MONITORINFO` 的）→ 最大化约束只能设 `ptMaxPosition`/`ptMaxSize`，否则窗口比工作区还大、底边被任务栏切掉。

### 1.4 WM_SETCURSOR 的 lParam 坐标不可靠（M5.9）
实测为负值。必须 `GetCursorPos()` + `ScreenToClient()` 换算客户区坐标。且必须在 `WM_SETCURSOR` 里设光标——系统每次鼠标移动后会把光标重置为箭头。

### 1.5 IME 候选窗定位走系统 caret（M5.9）
- 用 1×N 全透明位图的隐形 caret（`CreateCaret`），位置来自 UI 层 `Element::InsertionPoint()`；`WM_IME_COMPOSITION/WM_IME_NOTIFY` 期间**不要**调 `ImmGetContext/ImmReleaseContext` —— 会重建输入上下文、打断正在进行的组合（表现：中文打不出、候选窗不定位）。
- **caret 必须覆盖整行**：IME 按 caret **底边**摆候选窗，1px 高的 caret 会让候选窗整体下移一行。高度变化要 `DestroyCaret` + `CreateCaret` 重建。

### 1.6 无边框窗口的 WM_NCHITTEST 顺序（M5.11）
**缩放热区先于标题栏判定**：四边四角 `HT*` → 按钮区 `HTCLIENT`（让出最上 8 DIP 给上边/角缩放）→ 标题栏其余 `HTCAPTION` → 内容区 `HTCLIENT`。热区之外显式返回 `HTCLIENT`，别落到 `DefWindowProc`（`WS_THICKFRAME` 会另报一套 `HT*`）。按钮区不让出 `HTCAPTION` 的话，点击被系统当成"拖标题栏"，按钮收不到 `WM_LBUTTONDOWN`。

### 1.7 拖边框缩放是模态循环（M5.11）
拖缩放期间主消息循环挂起、`WM_SIZE` 连续到达 → 框架须在 `OnResized` 里就地渲染，否则内容不跟手。

### 1.8 窗口失活/最小化与焦点（M5.9）
- `WM_ACTIVATE(WA_INACTIVE)` → 失焦（选区消失、光标隐藏、闪烁停表）。
- **最小化是例外**：也发 `WA_INACTIVE` 但应保留焦点——靠"延迟一轮判定"（下一轮主循环时 `WM_SIZE(SIZE_MINIMIZED)` 已到达，`IsIconic()` 为准）。

### 1.9 最小化 + 文本框聚焦 → CPU 忙转（M5.9）
根因链：最小化时客户区 0 尺寸 → `RenderFrame` 首行 return 但**不清脏标记** → 主循环永远进不了 `WaitMessage`。修法：① 最小化时主循环直接 `WaitMessage()`（恢复由 `WM_SIZE` 触发重排，脏标记保留）；② `SyncTickTimer` 最小化停表。

---

## 2. D3D11 / DXGI

### 2.1 顶点结构偏移 ↔ 输入布局必须逐字节对应（M5.5）
`ShapeVertex` 的 `kind` 曾放结构体末尾而布局里读到的是别的字段 → 全部图元进错误 SDF 分支（填充椭圆被当胶囊）。**新增顶点类型时务必核对"结构体偏移 ↔ 输入布局偏移"表。**

### 2.2 SDF 抗锯齿过渡带不能用 fwidth（M5.5）
`fwidth = |ddx|+|ddy|`，斜向梯度算出 1.41 倍宽度 → 同一多边形"斜边粗、直边细"。本框架 SDF 是**像素空间真实距离场**（梯度模长=1），过渡带固定 1 像素：`cov = 1 - smoothstep(-0.5, 0.5, d)`。
（与 `10-Rendering §6.2` 的结论关系：那里的 `fwidth` 是针对"变换缩放下自适应"的早期结论，形状批实现时以像素距离场为准——两者并存，注意区分使用场景。）

### 2.3 多绘制单元渲染：BaseVertexLocation 与 VB 绑定方案必须配套（M5.6）
按"每批绑到自己的字节段起点"时，索引按全局 quad 序号编排，base 必须取**负值** `-全局quad基×4`；误用"绑定偏移 0"方案的 base=0 会从段后读垃圾顶点（整屏对角条纹）。三批 stride 不同（32/20/48B）时两方案不可混用。
（M5.17 指令化后改为"顶点按段线性排布、逐段 `DrawIndexed`"，此坑随旧架构消灭，但原理保留。）

### 2.4 裁剪矩形只能 RSSetScissorRects（M5.6）
`D3D11_RASTERIZER_DESC` 没有 ScissorBox 字段；光栅状态开 `ScissorEnable`。

### 2.5 收集结束必须提交最后一个绘制单元（M5.6）
否则末尾元素与滚动条整段丢失。

### 2.6 嵌套滚动容器的原点与裁剪都是"绝对量"（M5.6）
`Painter::SetOrigin` 语义为绝对原点：内层 origin = `父级origin − 自身offset`；scissor 同理要先加父级 origin 平移到窗口空间再求交。否则外层滚动对内层无效 / 裁剪带留在原位。

### 2.7 着色器入口名（M3/M4 隐患）
`CompileShader` 若写死 `"main"` 而实际入口是 `vsMain/psMain`，设备初始化会以 `error X3501` 失败——**且这类失败只看日志才发现**。教训：渲染验收必须先确认设备初始化成功。

### 2.8 GlyphAtlas 字节预算按"页"统计（M5）
按"字形字节加、整页字节减"会算成负数，预算永不触发、图集无上限增长。改为开页加 1MB、整页释放减 1MB。另：页 id 0 保留为空占位（`FindGlyphSrv(0)` 跳过）。

### 2.9 命令行 msbuild 的 LNK4020 / IPDB 损坏（M5.10）
`/MP` + `/Zi` 缺 `/FS`。命令行加 `-p:UseMultiToolTask=true`（等价 VS 内构建）；`-p:AdditionalOptions=/FS` **无效**（全局属性进不了 ClCompile 项元数据）。也不要用 `-p:OutDir=...` 覆盖构建（obj/pdb 写散 → IPDB/IOBJ mismatch）。

补充（2026-11-11）：**LNK4020「xxx.pdb 中的类型记录已损坏」是辅助警告**——MS 文档明确它是其它生成问题的伴随现象：先修真正的错误/警告，清空 Build 目录重编通常即可消除；并行构建时"库工程正在重写 PDB、示例链接步骤同时读旧类型流"也会触发。产物（exe）本身不受影响，只影响调试器里部分符号/类型的显示。

### 2.10 颜色约定 0xAARRGGBB（M5.10）
低字节 R、高字节 A。`0xFF000000` 是不透明黑不是白；白色写 `0xFFFFFFFF`。

---

## 3. DirectWrite 文本

### 3.1 灰度 AA 必须显式请求（M5，对标 Qt Quick 整改）
用 `IDWriteFactory2::CreateGlyphRunAnalysis`（dwrite_2.h）+ `DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE` + `DWRITE_TEXTURE_ALIASED_1x1`（1 字节/像素）。**老接口 + `CLEARTYPE_3x1` 取 G 通道 = 把子像素覆盖率当灰度** → 笔画失真、粗细不均、颗粒感。老接口请求不了灰度模式，其 `ALIASED_1x1` bounds 恒空——"ALIASED 名字误导但灰度 AA 必须用它"。

### 3.2 渲染模式统一 NATURAL，禁用 NATURAL_SYMMETRIC（M5）
`NATURAL_SYMMETRIC` 会**改写轮廓**（上半部镜像对称到下半部），对上下结构汉字破坏明显（实测 24px 的"第"字下部两横粘连）。且**只在新字号档位暴露**（DPI 变化才产生新档位）→ 表现为"只有 DPI 变化后才坏"，极难排查。
模拟 DPI 变化的手法：直接发 `WM_DPICHANGED`（wParam=新 DPI、lParam=建议矩形），无需改系统缩放。

### 3.3 亚像素定位：1/4 像素相位（M5，对标 Qt）
字形原点保留小数 → 量化到 1/4 像素求相位 → 相位进图集键 → 各相位按平移分别光栅，位图烘焙相位、quad 落整数像素、采样器 POINT。逐字形取整会让相邻字形相位失配。不做自造 gamma/对比度 LUT（Qt(FreeType)/Skia 都不做；Skia 的 pre-blend 方向相反且仅 sRGB surface 生效）。

### 3.4 本机 SDK 的 dwrite.h 是旧版结构（M5.9）
没有 `GetGlyphOutline`、`GetAlphaBounds`、`IDWriteFont::GetFontFace`；`GetGlyphIndices` 只有 3 参；`DWRITE_LINE_METRICS` 无 `lineLength/top/left`、无 `DWRITE_CARET_RECT`、`IDWriteTextLayout1` 的 `GetLineCount/HitTestPosition` 不可用。可行路径：基类接口（`HitTestPoint` 5 参版、`HitTestTextPosition`、`HitTestTextRange`、`GetLineMetrics` 数组版扩容重试）。

### 3.5 HitTestTextPosition 的 isTrailingHit（M5.9）
末尾位置（index == len）必须传 **TRUE**，否则返回最后一个字符的**前缘**（光标压在最后一字符上）。中间位置传 FALSE。

### 3.6 掩码字形垂直居中：探针渲染量墨迹（M5.9）
bullet 类字符（`•`）墨迹紧贴基线，按基线对齐绘制明显偏下。修法：自定义 `IDWriteTextRenderer` 只捕获 `run->fontFace`（不绘制），`GetAlphaTextureBounds` 取真实墨迹盒（4 倍字号测量再折回 DIP）。**不要用 `GetDesignGlyphMetrics`**（vertical-origin 与实际基线对不上，实测偏 4px+）。掩码字符用 `•`(U+2022)，别用 `●`（与光标条粘连显粗）。

### 3.7 自定义字体只认 SFNT（M5.7）
DirectWrite 仅支持 ttf/otf/ttc；woff/woff2 需先转 ttf。`Shape` 里族名命中自定义集合时要**同时** `SetFontCollection` + `SetFontFamilyName`（只设族名会被静默回退）。

---

## 4. 语言与工程

### 4.1 类作用域同名遮蔽（M1）
方法名与类型同名（`Display`/`Color`/`FontWeight`/`Lifetime`）时，类内引用该类型必须写全限定 `dui::Xxx`。

### 4.2 /EHs-c- 下禁用会抛异常的 STL 用法（M3/M5.10）
`std::vector::insert`、`std::ofstream` 等触发 C4530。改 `push_back`、Win32 文件 API。异常关闭时 `unhandled_exception` 一律 `std::terminate()`。

### 4.3 工程文件：IntDir 必须写在 Microsoft.Cpp.props **之后**（M3）
写在前 面 `$(ProjectName)` 为空 → 中间文件落盘符根目录 `\x64\Debug`、MSB8028 互踩。且用 `$(MSBuildProjectName)` 而非 `$(ProjectName)`。

### 4.4 WIC（M5.10）
- `IWICFormatConverter::Initialize` 是 **6 参数**版（Win7+ 签名）。
- SDK 里没有 `wincodec.lib`，import lib 叫 **`windowscodecs.lib`**——用 `#pragma comment(lib, ...)` 随编译单元携带，不动 `DUI.props` 公共基线。
- 自造 BMP 头精确 14+40 字节（`biPlanes`/`biBitCount` 各 2 字节），多写 6 字节越界写坏堆（`free_dbg` 报堆损坏）。

### 4.5 公共头边界
~~`DUI.h` 与 UI 层公共头不得引入 windows.h~~ **约束已取消**（2026-11-11）：公共头允许 windows.h（M5.27 起已实际引入）；d3d11 / dwrite 等重图形头仍只随实现层头出现。功能类能力归 Core 且允许直接用系统 API —— 剪贴板在 `Core/Clipboard`（原「App 层注入 UI 函数指针」方案废除）。import lib 用 `#pragma comment(lib)` 随编译单元携带，不动 `DUI.props`。

**d3d11.h 不能进公共头**（2026-11-11 实测）：`d3d11.h` 第 26-27 行 `#include "rpc.h"` / `#include "rpcndr.h"`，而 `rpcndr.h:201` 是 `#define small char` → 业务代码里任何叫 `small` 的标识符会被改写成 `char`（`Samples/Image` 的 `dui::Svg* small` 变成 `dui::Svg* char` → C2062「意外的类型 char」）。这类 MIDL 遗留宏**无法靠事后 `#undef` 补救**（无法预知用户代码用了哪些名字），只能从源头隔离：D3D 设备 `Device` 拆到内部头 `Graphics/Device.h`，公共的 `Graphics/Graphics.h` 只留 Painter / 顶点指令 / TextureStore（纯 C++，无 windows.h）。

### 4.6 API 冻结的执行方式（M4 教训）
公共 API 未设计的能力（如事件 Phase）不能私自实现——先改示例与文档，再动实现。M4 曾因此推迟隧道阶段，M6 随 `dui::Phase::Tunnel` 设计落地。

### 4.7 `MessageBox` 与 Win32 同名宏（M7）
`winuser.h`（UNICODE）定义 `#define MessageBox MessageBoxW`。成员方法 `Window::MessageBox` 若直接声明：含 `windows.h` 的 TU 会把声明/调用替换成 `MessageBoxW`，不含的 TU 仍是 `MessageBox` → 跨 TU 符号不一致（LNK2019 / ODR）。修法：`App.h` 顶部与 `App/Dialog.h` 末尾各 `#undef MessageBox`，`App/Dialog.cpp` 在全部 include 后再 `#undef` 一次；另给 `Window::MessageBoxW` 兼容别名（业务不主动调用），兜住「DUI.h 先、windows.h 后」的包含顺序。副作用：含 `DUI.h` 后调 Win32 原生弹窗要写 `::MessageBoxW`。`Samples/Dialog/MacroCompat.cpp` 是两种顺序的编译期守卫。

### 4.8 同工程内同名 .cpp 的 obj 冲突（M7）
`App/Dialog.cpp` 与 `App/Dialog.cpp` 同一库工程里同名 → MSB8027「两个或更多文件将生成到同一位置的输出」。修法：在 `DUI.vcxproj` 给后者加 `<ObjectFileName>$(IntDir)Core_Dialog.obj</ObjectFileName>`，不必改文件名；IntDir 按工程名隔离只能解决**跨工程**冲突，同工程仍会互踩。（2026-11-11 两文件已合并为单个 `App/Dialog.cpp`，该特例随之取消。）

### 4.9 单行注释行尾反斜杠吞掉下一行（M7）
`...;  // C:\` 结尾的 `\` 触发行继续（C4010），下一行也被当成注释 → 紧随其后的变量声明消失（C2065 未声明标识符）。注释不要以反斜杠结尾，改成文字描述。

### 4.10 Common Item Dialog 实现细节（M7）
- `ShObjIdl_core.h` 里 `FileOpenDialog` / `FileSaveDialog` coclass 带 `DECLSPEC_UUID` → `CoCreateInstance(__uuidof(FileOpenDialog), ...)` 即可，**无需 `uuid.lib`**；只需 `#pragma comment(lib, "ole32.lib")` + `"shell32.lib"`（`SHCreateItemFromParsingName`）。
- `COMDLG_FILTERSPEC` 定义在 `shtypes.h`，`SIGDN_FILESYSPATH` 在 `ShObjIdl_core.h`；过滤器数组必须活到 `Show()` 返回。
- `GetDisplayName(SIGDN_FILESYSPATH)` 返回的 `LPWSTR` 必须 `CoTaskMemFree`；COM 接口用 RAII `ComReleaser` 兜底，提前 return 不漏 `Release`。
- 取消是 `HRESULT_FROM_WIN32(ERROR_CANCELLED)`（`0x800704C7`），须单独映射成「用户取消」，别当成错误。
- UI 线程 COM **必须 STA**（`Application::Run` 用 `OleInitialize` 建立，见 ADR-027）；MTA 下 `Show()` 死锁，详见 §4.11。

### 4.11 Common Item Dialog 在 MTA 下死锁（M7.1，实测）
现象：`Samples/Dialog` 点击「打开文件…/保存文件…/选择文件夹…」后进程 Not Responding，且枚举不到任何对话框窗口（卡在 `IFileDialog::Show` 内部）；`MessageBoxW` 完全正常。根因：UI 线程被 `CoInitializeEx(COINIT_MULTITHREADED)` 建成 MTA，Common Item Dialog / Shell 扩展按 STA 假设工作。修法：`Application::Run` 起始 `OleInitialize(nullptr)`（必须早于 WIC 的 MTA 初始化），`App/Dialog.cpp` 弹窗前用 `CoGetApartmentType` 校验。
**二次踩坑**：`OleInitialize` 得到的是 **main STA（`APTTYPE_MAINSTA`）**，只判断 `APTTYPE_STA` 会把合法单元误判成错误 → 弹窗静默不出现、但进程响应正常（比死锁更难发现）。必须同时放行 `APTTYPE_STA` / `APTTYPE_MAINSTA`。
诊断手法：自动 `PostMessage` 注入点击 → `EnumWindows` 按 PID 找 `#32770` + `Get-Process.Responding`；能区分「弹窗没开」「卡在 Show」「关闭后没回主循环」三种情况。

### 4.12 入口函数的 SAL 批注要与 SDK 声明一致（C28251，2026-11-11）
SDK 的 `winbase.h:1075` 给 `wWinMain` 的声明加了 SAL 批注（`_In_ HINSTANCE` / `_In_opt_ HINSTANCE` / `_In_ LPWSTR` / `_In_ int`）。示例若只写 `int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)`，VS 的**实时代码分析**（/analyze）会报 **C28251**「"wWinMain" 的批注不一致: 此实例包含 无批注」——注意它**不是编译器警告**：命令行 msbuild（未开 /analyze）不报，只在 IDE 的错误列表里冒出来，容易被误当成构建失败。
修法：定义处照抄 SDK 的批注 —— `int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)`（19 个示例统一改）。批注不影响代码生成，纯粹是消除分析噪音。
