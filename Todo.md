# DUI 开发任务清单（Todo）

> 最后更新：2026-10-29。
> 当前进度：**M5 已完成**（DirectWrite 真实文字渲染 + 灰度字形图集 + DPI 切换分级失效）；
> **M5.5 已完成**（2D 绘图控件 `dui::Canvas` + Graphics 形状批 + `Samples/Canvas` 示例）。
> 示例已由 Samples（三栏阅读器 / 单选列表）**替换为 HelloWorld**（居中文字最小示例），
> 旧示例的交互验收描述见各里程碑历史记录。构建产物统一迁入 `Build\`（见 §0）。
> 从 **M6** 开始。本文档自包含：环境、约定、每阶段的步骤与验收标准都在这里。

---

## 0. 环境与构建（换电脑先做这个）

| 项 | 要求 |
| --- | --- |
| IDE / 工具链 | Visual Studio 18（PlatformToolset **v145**），x64 仅一个平台 |
| Windows SDK | 10.0（`WindowsTargetPlatformVersion=10.0`） |
| 编译基线 | 根目录 `DUI.props`：/MT(/MTd)、/EHs-c-（无异常）、/GR-（无 RTTI）、/utf-8、/W4、C++20 —— 所有工程共同 import，不要单独改选项 |

命令行构建与验证（MSBuild 路径因机器而异，用 vswhere 定位）：

```cmd
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe

cd /d <项目根>
<MSBuild路径> DUI.slnx -p:Configuration=Debug   -p:Platform=x64 -m -v:m -nologo
<MSBuild路径> DUI.slnx -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
Build\HelloWorld\Release\HelloWorld.exe
```

产物布局（2026-10-29 迁移，仅 x64 不再分平台子目录；对 `DUI.slnx` 根目录相对）：

```
Build\<工程名>\Debug|Release\   最终产物（DUI.lib / 各 exe，PDB 同目录）
Build\Temp\<工程名>\Debug|Release\   中间产物（obj 等）
```

> 注意：直接编译单个 vcxproj 时 `$(SolutionDir)` 未定义（找不到 `DUI/DUI.h` 与
> Build 目录会落错位置），**必须编译 `DUI.slnx`**。

---

## 1. 当前状态快照

### 已定案的文档（实现时以它们为准，不要重新发明决策）

| 文档 | 内容 |
| --- | --- |
| `Doc/Arch/03-Style.md` | 样式系统；§6.2 状态全集/合并优先级/SetState 契约 |
| `Doc/Arch/04-Layout.md` | Flex/Grid/绝对定位、失效规则 |
| `Doc/Arch/05-Animation.md` | 过渡动画 |
| `Doc/Arch/06-Events.md` | 事件路由、焦点、状态位归属 |
| `Doc/Arch/10-Rendering.md` | 显示列表/脏区/按需全量光栅/BGRA8/gamma 混合/fwidth |
| `Doc/Arch/12-Windowing.md` | Per-Monitor V2、三套坐标、DPI 切换时序、高保真纪律 |
| `Doc/Arch/15-Properties.md` | 属性主表（唯一事实来源） |
| `Doc/Arch/02-Decisions.md` | 全部 ADR |

### M1 已有代码（API 已冻结，后续只填实现、不改签名）

```
DUI/DUI.h              公共入口
DUI/Core/Core.h        Result<T>、String/Color/Rect/Thickness
DUI/Core/Threading.h   Lifetime、Background/Foreground、Task<T>（同步 stub）
DUI/UI/UI.h/.cpp       Prop/State 枚举、Style/StyleBuilder、Element、Panel/Text/Button/ScrollView、ClickEvent
DUI/App/App.h/.cpp     WindowOptions、Window（Root/Scale/OnCreate/OnDpiChanged/OnClosing）、Application
DUI/Platform/Window.h/.cpp  NativeWindow（RegisterClass/CreateWindowExW/窗口过程/WM_DPICHANGED）
                       NativeWindowHost 事件接口（App 层适配到 dui::Window，保住分层单向依赖）
```

### 关键约定（编码时随时对照）

1. **API 冻结**：`Samples/` 两个示例是"可执行规格"，改动公共签名 = 违规；新能力先改示例和文档，再改实现。
2. **无异常 / 无 RTTI**（/EHs-c-、/GR-）：错误走 `Result<T>`；不用 typeid/dynamic_cast/std::variant。
3. **类作用域同名遮蔽坑**：方法名与类型同名（`Display`/`Color`/`FontWeight`/`Lifetime`）时，类内引用类型必须写全限定 `dui::Xxx`（UI.h 已有注释示例）。
4. **StyleBuilder 链式返回左值**：接收 Builder 的重载一律 `const StyleBuilder&`，不要写 `&&`。
5. **SetStyle 双形态**：共享样式存指针（零拷贝）；`SetStyle(StyleBuilder)` 时元素经 `ownedStyle_` 持有值拷贝。
6. **状态位契约**：`SetState` 相同值幂等；标脏帧末合并（M3 实现）。
7. **硬性红线**：不做 System DPI aware 回退、不做位图缩放、非整数档自绘、描边像素对齐（12-Windowing §7.1）。

---

## M2 — 平台窗口与消息循环 ✅（2026-10-29 完成）

**目标**：两个示例运行后出现真实窗口（内容可以先全黑），关闭返回退出码 0。

完成情况：四个步骤全部落地；清单经 `Build/DUI.props` 注入并已验证内嵌 exe
（PerMonitorV2 + true/pm）。一处与计划的偏差：窗口过程 GWLP_USERDATA 存的是
`NativeWindow*`（非 `dui::Window*`）——Platform 层不得依赖 App/UI（01-Layering §1），
事件经 `NativeWindowHost` 接口上抛、由 App.cpp 的 `dui::WindowHostAdapter` 适配。
验收全过：Debug+Release 零警告构建，两窗口可拖动/关闭，DPI 切换日志正常。

步骤：

1. `DUI/Platform/Window.cpp`（新建；头可先并入 App 或独立 `Platform/Window.h`）：
   - `RegisterClass` + `CreateWindowExW`，客户区尺寸 = DIP × scale；
   - 窗口过程：`WM_DESTROY`/`WM_CLOSE`（接 `OnClosing()`）、`WM_DPICHANGED`（本阶段可先记录 scale，M5 完整实现）；
   - 每窗口存 `dui::Window*`（`GWLP_USERDATA` 或 thunk）。
2. 应用清单：Per-Monitor V2（`12-Windowing §2` 的 XML）。建议放 `DUI/Platform/DUI.manifest`，
   并在 `Build/DUI.props` 用 `<Manifest>` 注入（或各示例 vcxproj 挂 Manifest 工程项）。
3. `App/App.cpp`：实现 `Application::Run(Window&)` 真实时序——
   建 HWND → `window.OnCreate()` → `ShowWindow` → 消息循环（`GetMessage/TranslateMessage/DispatchMessage`）→ `WM_QUIT` 返回退出码；
   失败写 `lastError_` 并返回非 0（契约见 12-Windowing §6 与 App.h 注释）。
4. `Window` 增加 `NativeHandle()`（M2 内部用，别暴露 HWND 到公共头——保持 DUI.h 无 windows.h）。

验收：Debug+Release 构建零警告；两个 exe 出现窗口、可拖动、关闭退出；`WM_DPICHANGED` 到达时 scale 正确更新（日志验证）。

参考：`12-Windowing §2/§4`、`01-Layering §2 Platform`。

---

## M3 — 样式解析 + Flex 布局 + 矩形绘制 ✅（2026-10-29 完成）

**目标**：`ListSelect` 示例的界面按样式正确显示（布局、颜色、圆角矩形、hover/selected 手动可用），点击事件能触发（命中测试按 Rect）。

完成情况与实现说明：
- 新文件：`UI/StyleResolve.cpp`（级联层 2–7 + 状态优先级合并 + PropFlags + ClearDirtyTree）、
  `UI/Layout.cpp`（Measure/Arrange + grow/shrink/gap/MinWidth/Stretch）、`UI/Event.cpp`
  （HitTest/hover·pressed 状态链/Click 冒泡）、`Graphics/Graphics.h/.cpp`（D3D11 + Flip Model +
  BGRA8 + SDF 圆角矩形 + fwidth AA + 预乘 Alpha 混合）、`App/Frame.cpp`（收集 + 帧路径）。
- `Run` 改为 PeekMessage/WaitMessage 按需渲染循环（空闲 WaitMessage，CPU 归零，提前达成 M6 验收项）。
- **偏差 1**：文本 M3 完全不绘制（占位测量：CJK 1em、其余 0.55em；GDI/DDA 未用，Todo 允许空占位）。
- **偏差 2**：`StyleValue` 8B → 20B 内嵌 Thickness 四边；15-Properties §2/03-Style §8 的
  变长 Decl 形态列后续优化。
- **偏差 3**：`e.source` = 处理器宿主元素（两个示例的既有语义），06-Events §4.3 已同步修改。
- **偏差 4**：Opacity 以顶点 alpha 乘法近似，未做离屏层（10-Rendering P0 完整层机制随后续落地）。
- **顺带修复**：示例工程 `<IntDir>` 写在 Microsoft.Cpp.props 之前导致 `$(ProjectName)` 为空、
  中间文件落盘符根目录 `\x64\Debug`（隔离从未生效）——移到 Cpp.props 之后并用
  `$(MSBuildProjectName)`，MSB8028 消除；`vector::insert` 在 /EHs-c- 下触发 C4530，改 push_back。
- Debug + Release 零警告构建通过。

步骤：

1. `DUI/UI/Layout.cpp`（新建）：Flex 单遍约束（measure/arrange，04-Layout §4.1/§6）；
   只需 MVP：Row/Column、gap、padding、Width/Height/FlexGrow/FlexShrink/MinWidth、Stretch。
2. `DUI/UI/StyleResolve.cpp`（新建）：级联解析（03-Style §5 七层中的 2/3/4/5/6/7 层）
   + **状态合并优先级** `disabled > pressed > selected > focused > hover` + `SetState` 帧末合并标脏（§6.2 契约）。
3. `DUI/Graphics/`（新建）：D3D11 Device/SwapChain（Flip Model、BGRA8、VSync、按需 Present）+
   Painter（矩形/圆角矩形 SDF）+ 批合并。**首版全量光栅**（10-Rendering D2），脏区只做"是否进帧"判定。
4. UI 失效接线：`SetState`/`Set`/`SetStyle` 按属性标志标脏（03-Style §7 失效表）；帧末合并 → 触发重绘。
5. `Element::SetText` 触发 `TextDirty`（占位：先用 GDI/DDA 画文字或空占位，M5 换 DWrite）。

验收：`ListSelect.exe` 视觉正确（列方向卡片、选中蓝底白字、hover 变色）；点击切换选中（状态位驱动，暂无过渡动画）。

参考：`03-Style §5/§6/§7`、`04-Layout`、`10-Rendering §2/§3/§4/§6`。

---

## M4 — Dispatcher + 线程池 + 输入事件 ✅（2026-10-29 完成）

**目标**：`Samples.exe`（三栏阅读器）的异步加载链路真实工作；鼠标点击/hover 全部真实生效。

完成情况与实现说明：
- 新文件：`Core/Dispatcher.h/.cpp`（UI 队列 + PostMessage 唤醒 + 固定线程池 min(4, 核数)）；
  `Threading.h` 重写为真协程（eager Task<T> + Background/Foreground awaiter + Then 回投 UI 线程）。
- `Run` 循环接入 [调度] 步（每轮 Drain），帧触发改为 `framePending || SubtreeDirty()`——
  顺带修掉 M3 隐患：Dispatcher 任务里 SetText 标脏后 framePending 不会置位的问题。
- `Lifetime` 从值拷贝改为共享标志（shared_ptr<bool>），Element 析构置否——M1 的令牌是
  值拷贝，回投检查永远为真，M4 起真正生效。
- Click 合成加 4dip 拖出阈值（06-Events §3）。
- **偏差 1**：Dispatcher 按 Todo 放 Core，但含 PostMessage —— 与 01-Layering「Core 不依赖
  Win32」偏差；pimpl 把 windows.h 挡在 .cpp，DUI.h 无 windows.h 约束未破。
- **偏差 2**：隧道（Tunnel）阶段未实现——AddHandler/Phase 公共 API 尚未设计（API 冻结约束：
  新能力先改示例和文档），当前仅冒泡 + handled；该项与指针捕获一并顺延，落地前需先定 API。
- **偏差 3**：任务体用 std::function（有分配）；队列节点固定池（07-Threading D5）、退出排空
  （R6 完整版）、WhenAll/WhenAny 均列 P1。线程池析构 detach。
- **崩溃修复（2026-10-29）**：修完着色器入口名（CompileShader 增加 entry 参数，M3 起设备
  初始化一直因 `error X3501: 'main'` 失败——**M3/M4 的"渲染验收"此前实际从未达成**）后暴露
  两个启动即崩的悬空访问，均已修复：
  1. `Panel::AddChild` 的 StyleBuilder 重载原来调用 `e->SetStyle(b.Build())`——`Build()`
     返回临时 `Style`，重载决议选中 `SetStyle(const Style&)`（只存裸指针），临时在表达式
     结束即销毁，`style_` 全体悬空 → 首帧 Measure 在 `Style::Find` 读栈上垃圾崩溃
     （0xC0000005，读 0x3F99999A）。修复：改为 `e->SetStyle(b)` 走 `ownedStyle_` 拷贝路径。
  2. `ArrangeElement` 的函数级 `static items` 缓冲在子项循环内被递归调用
     `clear()/push_back()` 重分配，外层 range-for 迭代器悬空 → 读到 0xFEEEFEEE 填充崩溃。
     修复：缓冲按递归深度分层（`ItemsAt(depth)`），`ArrangeElement` 增加 depth 参数，
     跨帧复用的意图保留。
- Debug + Release 零警告构建通过；两示例进程存活、窗口标题正确（视觉交互验收待人工确认：
  点击卡片 → "加载中…" → 异步回投正文；hover/pressed；空闲 CPU 归零）。

步骤：

1. `DUI/Core/Dispatcher.cpp`：UI 线程队列（PostMessage 唤醒）+ 固定线程池（07-Threading §2/§4）。
2. `Threading.h`：`Task` 改为真实协程（挂起/恢复），`Background()/Foreground()` 真 awaiter；
   `Then` 回投 UI 线程；`Lifetime` 接延迟删除（元素销毁置 false）。
3. `DUI/UI/Event.cpp`：命中测试（transform 可后置）→ 冒泡/隧道 → `handled`；
   `Click` 合成（按下抬起同元素、阈值内）；hover/pressed 状态位由输入维护。
4. `Element::OnClick` 已有绑定，接入真实触发。

验收：`Samples.exe` 列表点击选中卡片、正文异步"加载"完成后回投显示；hover/pressed 过渡（若 M6 未到则无动画，但状态位正确）。

参考：`07-Threading`（P0 列表）、`06-Events`（P0 列表）。

---

## M5 — DirectWrite 文本 + DPI 切换全流程 ✅（2026-10-29 完成）

**目标**：真实文字渲染；跨显示器拖动 DPI 切换不糊、不重排。

完成情况与实现说明：
- 新文件：`Text/TextEngine.h/.cpp`（单例：系统字体集合、基座 TextFormat、
  Shape/Layout/Discard + Raster 回调：灰度 AA + 1/4 像素相位）、`Text/GlyphAtlas.h/.cpp`
  （(face, glyphIndex, pixelSize, phase) 键的 LRU 灰度图集，4MB 预算 1024×1024 页面）。
- `Text::MeasureContent` 换真实排版：缓存键（text/fontSize/weight/lineHeight/maxWidth）
  全等即复用，measure 每帧零 COM 开销；`ParaHandle()` 暴露段落给帧收集。
- `Painter` 扩展字形批：`GlyphVertex`（位置 + 图集 UV + 颜色 8bit）、按图集页分组、
  跨页才切纹理；`Device` 新增字形着色器（R8 图集采样）、独立输入布局与步长、
  `UploadGlyphPage/ReleaseGlyphPage`。渲染静态索引缓冲两批共用，字形批以
  **负 BaseVertexLocation 重定基**（两批步长不同：32B vs 20B）。
- `WM_DPICHANGED` 接入分级失效（§4-5）：客户区 DIP 变 → InvalidateLayout 重排；
  不变 → 只重绘；App 层 `OnDpiChanged` 末尾重算鼠标 hover 链（§4-7）。
  `NativeWindowHost::OnDpiChanged` 增加 `clientDipChanged` 参数。
- **偏差 1**：光栅走 `NATURAL` 模式 + `CLEARTYPE_3x1` 纹理取 G 通道转灰度
  （`ALIASED_1x1` 在 NATURAL 下 GetAlphaTextureBounds 恒为空矩形，实测确认），
  直接使用原始 coverage；11-Text 的 alpha 图集 + 缩放重光栅（ADR-003 的
  "非整数档重光栅"）按页 LRU 天然支持新档位按需生成，旧档位淘汰。
  （**该条已被"对标 Qt Quick 三次整改"取代**：真正原因是必须显式请求
  `DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE`，否则拿到的是子像素覆盖率。）
- **清晰度修订（验收反馈后）**：字形 quad 位置**取整到整像素**（基线与逐字形
  双重取整）、图集采样器换 **POINT**（1:1 映射零插值）、字形间 1px 空隙防
  UV 浮点误差渗色；三者叠加后文字锐利度显著提升。
- **锐度二次调优（验收反馈后）**：① coverage 后处理 LUT 恢复——Skia SkMaskGamma
  同思路、Chromium（SkSurfaceProps）经验值参数 gamma 1.4 + contrast 0.5
  （a^(1/1.4) + 0.5·a·(1−a) 中段 stem-darkening）；注意 DWrite 工厂 API 不接受
  自定义渲染参数（CreateGlyphRunAnalysis 无此入口），Chromium 也是 Skia 拿
  raw coverage 自做 pre-blend，故 LUT 是唯一正确路径。② 小字号分档——
  ≤16px 用 GDI_CLASSIC（带 hinting，图集键含 pixelSize 天然分档），大字保持
  NATURAL；光栅探测 CLEARTYPE_3x1 / ALIASED_1x1 双纹理类型取有内容者。
  真实 ClearType（三通道图集 + 底色感知混合）列后续备选。
  （**该条已被"对标 Qt Quick 三次整改"整体推翻**：LUT 方向与 Qt/Skia 相反，
  GDI_CLASSIC hinting 是"颗粒感"来源之一。）
- **对标 Qt Quick 三次整改（2026-10-29，验收反馈"笔画颗粒感/粗细不均"）**：
  调研 Qt 6.11（`qsgrhitextureglyphcache.cpp`：字形缓存键含 subPixelPosition；
  `qfontengine_ft.cpp`：`m_subPixelPositionCount = 4` → 1/4 像素 4 相位；
  hinting 走 `PreferNoHinting/VerticalHinting`）与 Skia DirectWrite 后端
  （Chromium/Avalonia 同源）后定位到三处根因并整改：
  ① **灰度 AA 必须显式请求**：改用 `IDWriteFactory2::CreateGlyphRunAnalysis`
     （dwrite_2.h，带 `antialiasMode` 参数），`DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE`
     + `DWRITE_TEXTURE_ALIASED_1x1`（1 字节/像素）。此前用老接口 +
     `CLEARTYPE_3x1` 取 G 通道 = 把**子像素**覆盖率当灰度用 → 笔画边缘失真、
     粗细不均、颗粒感（Skia 源码注释亦证：ALIASED_1x1 名字误导但灰度 AA
     必须用它；老接口无法请求灰度模式，故其 bounds 恒空——早前的实测结论
     正是漏了这个参数）。
  ② **亚像素定位**：字形原点保留小数 → 量化到 1/4 像素求相位 → 相位进图集键
     → 各相位按 transform 平移（单位=像素）分别光栅，位图烘焙相位、quad 落
     整数像素，采样器仍 POINT。逐字形取整会让相邻字形相位失配。
  ③ **去掉自造 coverage LUT**：Qt(FreeType)/Skia 都不做 gamma/对比度 S 曲线；
     Skia 的 pre-blend 是方向相反的线性化表且仅 sRGB surface 生效。
  ④ 渲染模式分档：<20px `NATURAL`（横向 AA，锐利），≥20px `NATURAL_SYMMETRIC`
     （横纵都 AA，曲线/斜线更平滑）——Skia 同款经验规则。
- **偏差 2**：`fontEmSize` 即像素尺寸（ppd=1）直接产出物理像素位图；未做
  `DWRITE_TEXT_METRICS` 级缓存与分段计时（M6 观测项）。
- **偏差 3**：`En` 默认家族写死 "Segoe UI"/"Microsoft YaHei" 回退由 DWrite 系统
  回退完成；`fontFamily` 样式属性未暴露（API 冻结：先改示例和文档再扩）。
- **字体族支持（验收反馈后新增）**：默认字体改为**微软雅黑**（引擎基座 TextFormat）；
  新增 `Prop::FontFamily` 继承属性——`StyleBuilder::FontFamily(const wchar_t*)` 指定
  系统已安装任意字体族（零拷贝指针，生命周期由调用方保证，建议字面量），入
  StyleContext 继承链、排版缓存键（Paragraph::family），layout 经
  `SetFontFamilyName` 覆盖基座；未安装时 DWrite 自动回退默认。15-Properties §4.1
  已同步；示例 kArticleTitle 用 SimSun 演示。StyleValue 加宽（+指针槽）属
  §2 变长 Decl 优化前的临时形态。
- 修复过程中确认的两处 M5 内 bug：图集纹理页 id 0 保留为空占位（FindGlyphSrv(0)
  跳过）；字形批顶点缓冲需按 20B 步长重绑并偏移到矩形顶点之后。
- **顺带新增（HelloWorld 示例驱动）**：Flex 布局补齐主轴/交叉轴对齐——
  `Prop::JustifyContent`（`Justify::Start/Center`，默认 Start）与
  `Prop::AlignItems`（`Align::Stretch/Start/Center`，默认 Stretch），
  15-Properties §4.4 已规划项的最小实现（End/Baseline 列后续）。
- Debug + Release 全量重建零警告；两示例文字真实渲染（标题/菜单/列表/正文/单选蓝底白字），
  空闲归零不进帧。

步骤：

1. `DUI/Text/`：FontCollection、Shaping（`IDWriteTextAnalyzer`）、Layout、Raster（Alpha 图集，ADR-003）、GlyphAtlas（LRU）。
2. `Text` 控件接入排版结果（measure 缓存键：fontSize/fontFamily）。
3. `WM_DPICHANGED` 完整流程（12-Windowing §4 七步）：建议矩形 SetWindowPos → ResizeBuffers → 字形重光栅 → 分级失效 → `OnDpiChanged` → 鼠标坐标重算。
4. `pixelSize = round(fontSizeDip × scale)`，< 9px 向上夹紧（12-Windowing §9-3）。

验收：150%/200% 下文字清晰；跨屏拖动不糊不闪；DIP 尺寸不变时不重排（日志断言）。
（视觉验收：文本已正确渲染；跨屏 DPI 拖动断言待人工多显示器环境确认。）

参考：`11-Text`（P0）、`12-Windowing §4/§5`、ADR-003。

---

## M6 — 过渡动画 + 观测指标

**目标**：`Transition(BackgroundColor, 120ms)` 生效；帧耗时/Draw Call 可观测。

步骤：

1. `DUI/UI/Animation.cpp`：属性基值变化时建过渡（起点=当前显示值）、UI ticker 每帧推进、写入动画槽（级联第 1 层）。
2. 观测面板：帧耗时分段计时（收集/光栅/Present）+ Draw Call 计数（10-Rendering §9-5 的分段缓存判定依赖它）。
3. 收尾：`00-Overview §6` 工程状态更新；README 补截图。

验收：示例中 hover/选中 120ms 补间；空闲时 CPU 归零（任务管理器验证）。

参考：`05-Animation`、`10-Rendering §5/§8`。

---

## M5.5 — 2D 绘图控件 Canvas + 形状批 ✅（2026-10-29 完成）

**目标**：提供可承载 2D 几何图形的绘图控件，并配套示例 `Samples/Canvas`。

完成情况与实现说明：
- **落地自定义绘制钩子**（ADR-018 定制点 2 / 03-Style §6.3 原为设计稿）：
  `Element::OnPaint(graphics::Painter&, const StyleContext&, float scale)`，
  收集期在背景之后、子元素之前调用（`App/Frame.cpp`）；默认空实现，
  普通元素零开销。`UI.h` 只前向声明 `graphics::Painter`（公共头不含 windows.h）。
- **Graphics 新增形状批**（第三批，复用同一动态 VB + 共享索引缓冲，靠 stride 重绑
  与负 BaseVertexLocation 重定基）：
  - `ShapeVertex` 48 B：`pos + local + params + extra/kind + halfStroke + color`；
  - 新 VS/PS：局部空间求有符号距离（kind 分支：胶囊线段 / 椭圆 / 圆角矩形），
    `halfStroke > 0` 走等距线偏移即描边；`fwidth` 自适应 AA，straight → 预乘；
  - `Painter` 新指令（**DIP + scale** 语义，与 `FillRoundedRect` 一致）：
    `StrokeLine` / `FillEllipse` / `StrokeEllipse` / `StrokeRoundRect`。
- **控件 `dui::Canvas`**（`DUI/UI/Canvas.h/.cpp`，派生 `Panel`，可放子元素）：
  持图元列表（`CanvasItem`），追加即 `InvalidatePaint()`；坐标为**画布局部 DIP**
  （原点 = 画布左上角，与父偏移无关），收集期逐项转 Painter 指令。
  API：`AddLine` / `AddPolyline` / `AddPolygon` / `AddRect(+Outline)` /
  `AddRoundRect(+Outline)` / `AddEllipse(+Outline)` / `AddCircle(+Outline)` /
  `AddArc` / `Clear`。
- **示例 `Samples/Canvas`**：网格 + 矩形/圆角矩形/圆/椭圆/直线/折线/多边形/圆弧，
  按 HelloWorld 模板建工程并挂入 `DUI.slnx`。
- **踩坑记录（形状批只显示矩形/圆角矩形）**：`ShapeVertex` 字段顺序与 D3D 输入布局
  必须**逐字节对应**——`kind` 曾放在结构体末尾（偏移 44，与 `color` 相邻），而布局里
  `TEXCOORD2 @32` 实际读到的是 `(extra, halfStroke)`，于是 PS 里 `extraKind.y` 拿到的是
  描边半宽而非 kind，全部图元进了错误的 SDF 分支（填充椭圆被当胶囊、矩形描边被当
  椭圆并填满整块）。修法：`kind` 改为 float 并紧邻 `extra`（32/36），`halfStroke` 40、
  `color` 44，结构体仍 48B。**新增顶点类型时务必核对"结构体偏移 ↔ 输入布局偏移"表。**
- **踩坑记录（多边形各边线宽不一）**：SDF 的抗锯齿过渡带**不能用 `fwidth`**——
  它等于 `|ddx|+|ddy|`，斜向梯度会算出 1.41 倍宽度，导致同一多边形"斜边粗、
  水平/垂直边细"。本框架的 SDF 一律是**像素空间的真实距离场**（梯度模长=1），
  故过渡带直接固定 1 像素：`cov = 1 - smoothstep(-0.5, 0.5, d)`（矩形批与形状批同）。
- **已知限制**（后续项）：
  - 多边形只支持描边（GPU 侧无解析式 AA 填充路径；三角形填充边缘锯齿）；
  - 圆弧按 6° 折线近似；
  - 形状批整体排在字形批之后 → Canvas 图形会盖住其子元素文字（重叠区域）；
  - 三批分离导致跨批 Z 序不严格（矩形批 → 字形批 → 形状批），非新引入问题。
- Debug + Release 全量重建零警告。

参考：`10-Rendering §3/§6.1/§6.2`、`03-Style §6.3`、`15-Properties §5.1`、ADR-018。

---

## 备忘（换电脑提醒）

- 构建命令见 §0；**别改根目录 `DUI.props` 的编译开关**，除非文档有对应 ADR。
- M2 起新建文件记得同步 `DUI.vcxproj`（和 `.filters`）。
- **新建子工程一律放 `Samples/<工程名>/`，照抄 `Samples/HelloWorld/HelloWorld.vcxproj`**：
  OutDir = `$(SolutionDir)Build\$(ProjectName)\$(Configuration)\`，IntDir =
  `$(SolutionDir)Build\Temp\$(ProjectName)\$(Configuration)\`（写在
  Microsoft.Cpp.props 之后），import 根目录 `DUI.props` 与 DUI 工程引用均用
  `..\..\` 相对路径，挂入 `DUI.slnx`。
- 每完成一个里程碑：更新本文档勾选状态 + `00-Overview §6` 工程状态。
- 构建产物整体在 `Build/`，已加入 `.gitignore`，不要提交。
