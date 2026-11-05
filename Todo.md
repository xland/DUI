# DUI 开发任务清单（Todo）

> 最后更新：2026-11-05。
> 当前进度：**M5 已完成**（DirectWrite 真实文字渲染 + 灰度字形图集 + DPI 切换分级失效）；
> **M5.5 已完成**（2D 绘图控件 `dui::Canvas` + Graphics 形状批 + `Samples/Canvas` 示例）；
> **M5.6 已完成**（滚动容器 `dui::Scroller`：横纵滚动条 + 绘制单元裁剪 + `Samples/Scroller` 示例）；
> **M5.7/M5.8/M5.9 已完成**（自定义字体 + FlexWrap + 样式 API 合并 / 元素级指针事件 /
> 键盘事件 + 焦点 Tab 链 + `dui::TextBox`/`dui::TextArea`）；
> **M5.10/M5.11 已完成**（图像控件 `dui::Image` / 无边框窗口与自定义标题栏
> `Chrome::Borderless` + `Samples/CustomFrame`）。
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
DUI/UI/UI.h            聚合头（Style.h / Element.h / Panel.h / Text.h + 布局/事件入口声明）：
                       Prop/State 枚举、Style（声明集 + 链式构造合一）、Element、Panel、Text/Button、ClickEvent
DUI/App/App.h/.cpp     WindowOptions、Window（Root/Scale/OnCreate/OnDpiChanged/OnClosing）、Application
DUI/Platform/Window.h/.cpp  NativeWindow（RegisterClass/CreateWindowExW/窗口过程/WM_DPICHANGED）
                       NativeWindowHost 事件接口（App 层适配到 dui::Window，保住分层单向依赖）
```

### 关键约定（编码时随时对照）

1. **API 冻结**：`Samples/` 两个示例是"可执行规格"，改动公共签名 = 违规；新能力先改示例和文档，再改实现。
2. **无异常 / 无 RTTI**（/EHs-c-、/GR-）：错误走 `Result<T>`；不用 typeid/dynamic_cast/std::variant。
3. **类作用域同名遮蔽坑**：方法名与类型同名（`Display`/`Color`/`FontWeight`/`Lifetime`）时，类内引用类型必须写全限定 `dui::Xxx`（UI.h 已有注释示例）。
4. **Style 链式返回左值**：`When` 等收样式的参数一律 `const Style&`，不要写 `&&`。
5. **SetStyle 一律拷贝持有**（经 `ownedStyle_`）：链式临时可直传不悬空；派生链 `parent_` 与字体指针仍指向外部，生命周期契约不变（历史：曾存裸指针致临时悬空崩溃，见 M4 踩坑）。
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
  ④ 渲染模式：~~分档（<20px `NATURAL` / ≥20px `NATURAL_SYMMETRIC`）~~ → **统一
      `DWRITE_RENDERING_MODE_NATURAL`**（不做 symmetrization）。
      **原因**：`NATURAL_SYMMETRIC` 会**改写字形轮廓**（把上半部镜像对称到下半部），
      对上下结构的汉字破坏明显 —— 实测 24px 的「第」字下部两横被挤到几乎粘连。
      该缺陷**只在新的字号档位暴露**：symmetrization 的结果依赖字号，而系统 DPI
      变化才会产生新的 pixelSize 档位去重光栅 → 表现为「只有 DPI 变化后文字才
      出问题」，很难联想到是渲染模式。汉字为主的界面不需要 symmetrization，统一
      `NATURAL` 后跨字号形态一致。
      排查手法：向窗口发 `WM_DPICHANGED`（wParam=新 DPI，lParam=建议矩形）即可
          模拟 DPI 变化，无需真的改系统缩放。
      - **图集字节预算修正**：`GlyphAtlas::bytes_` 原按「字形字节」加、按「整页字节」减
        → 计数会变负，4MB 预算永不触发 → 图集无上限增长（DPI 换档批量新增字形时更易
        暴露）。改为**按页占用**统计（开页时加 1MB，整页释放时减 1MB），预算 ≈ 4 页。
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

## M5.6 — 滚动容器 Scroller + 绘制单元裁剪 ✅（2026-10-29 完成）

**目标**：提供基础的滚动容器控件（支持横向与纵向滚动条），并配套示例 `Samples/Scroller`。

完成情况与实现说明：
- **控件 `dui::Scroller`**（`DUI/UI/Scroller.h/.cpp`，派生 `Panel`，`Kind::Scroller`）：
  滚动**只改绘制坐标、不改布局结果**——子项按自然尺寸排布并溢出，绘制期用 scissor 裁到视口。
  - API：`ScrollOffset/SetScrollOffset/ScrollBy`、`Viewport`、`ContentSize`（内容实际范围）、
    `NaturalSize`（内容自然尺寸）、`MaxScrollX/Y`、`HitScrollBar`、
    `BeginBarDrag/UpdateBarDrag/EndBarDrag`、`ClampScroll`、`PaintScrollBars`；
  - 滚动条外观接入样式系统（Track* / Thumb* 属性：厚 10 DIP 默认、直角无边框，
    可设宽度/高度/背景色/圆角/边框色/边框粗细）；边距 2 DIP 与滑块最小长 28 DIP 仍为常量。
- **Graphics 支持多绘制单元 + 裁剪**：
  - `Painter::SetOrigin(dipX, dipY, scale)`：整段绘制平移（滚动内容用）；
  - `struct DrawUnit { const Painter* painter; Rect clipPx; }`，
    `Device::Render(const DrawUnit*, uint32_t)` 逐单元 `RSSetScissorRects` + 三批绘制；
  - 光栅状态开启 `ScissorEnable`；一次 Map/Unmap 上传多单元顶点（按各批真实 stride 累加字节偏移）。
- **App 层收集改造**（`App/Frame.cpp`）：`DrawList`（`Painter` 池跨帧复用 + 单元表，单元顺序即 Z 序）
  + `Collector`：
  - 滚动容器 → 封存父级单元 → 内容单元（原点 = −滚动偏移，裁剪 = 视口 ∩ 外层裁剪）
    → 回到父级单元 → 画滚动条（压在内容之上）；
  - 嵌套滚动容器自然形成"内容被切成多个单元"的序列，Z 序正确。
- **布局**（`UI/Layout.cpp`）：滚动容器主轴 = 内容自然尺寸（子项不参与 flex 收缩），
  交叉轴 = 视口尺寸；排布后回写**内容实际范围**（子项外接盒）→ 滚动量 = 内容范围 − 视口。
  用实际范围而非自然尺寸，嵌套时不会把外层滚动量算大。
- **事件**（`Platform/Window.*` + `App/App.cpp` + `UI/Event.cpp`）：
  - `WM_MOUSEWHEEL` → `NativeWindowHost::OnMouseWheel(ptDip, delta, horizontal)`；
    App 沿祖先链找最近的 `Scroller`（`FindScroller`）→ 纵向滚动，Shift + 滚轮横向；
  - 滚动条滑块拖拽：按下命中滚动条 → `BeginBarDrag`（轨道空白处跳页）→ 移动 `UpdateBarDrag`
    → 抬起 `EndBarDrag`；拖拽中不参与 Click 合成；
  - 命中测试进入滚动容器时把指针换算到内容坐标（`+滚动偏移`），视口外归容器自身；
    滚动条区域优先命中容器本身（不穿透到被裁剪的内容）。
- **示例 `Samples/Scroller`**：纵向长列表 / 双向滚动（横向卡片行，卡片高于视口）/ 嵌套滚动
  （外层纵向容器内嵌两个横向容器），按 HelloWorld 模板建工程并挂入 `DUI.slnx`。
- **踩坑记录**：
  - `D3D11_RASTERIZER_DESC` **没有** `ScissorBox` 字段，裁剪矩形只能用
    `ID3D11DeviceContext::RSSetScissorRects`（复数）设置；
  - 收集结束必须提交**最后一个绘制单元**，否则末尾元素与滚动条会整段丢失；
  - 命中测试曾把绝对坐标与相对坐标混用（递归时 `pt - b.x` 与 `Bounds()` 混算），
    嵌套两层以上命中错位 —— 已统一为全程绝对坐标；
  - **多绘制单元渲染曾整屏错乱**（对角条纹、后续单元全乱）：`DrawIndexed` 的
    `BaseVertexLocation` 必须与 VB 绑定方案配套。本实现按"每批绑到自己的字节段起点"，
    索引缓冲按全局 quad 序号编排，故 base 必须取**负值** `-全局quad基×4` 把索引拉回
    本批顶点段；若误用"绑定偏移 0"方案的公式（base=0），u0 恰好重合看不出问题，
    u1+ 会从字节段之后读垃圾顶点。三批 stride 不同（32/20/48B），线性字节布局下
    字形/形状数据无法用"全局顶点号×stride"寻址，两方案不可混用；
  - **嵌套滚动容器的绘制原点与裁剪都是"绝对量"，必须叠加父级偏移**
    （`App/Frame.cpp` CollectScroller）：`Painter::SetOrigin` 是绝对原点，
    内层要用 `父级origin − 自身offset`（否则外层滚动对内层内容无效）；
    scissor 裁剪同理，内层视口要先加父级 origin 平移到窗口空间再求交
    （否则裁剪带留在原位，背景被裁没、只有文字看似在动）。
- **已知限制**（后续项）：
  - 无平滑滚动 / 惯性（`UI.Animation` 联动列 P1）；无键盘滚动（PageUp/Down / 方向键）；
  - 滚动条不随悬停淡出（可经 `When(State::Hover, ...)` 变体 + 颜色过渡自行实现）；
  - 滚动量按像素（无行滚动 / 吸附）；
  - **视口外的内容仍参与收集**（MVP 未做视口剔除，10-Rendering §2 的"裁剪剔除"待补）
    → 收集量与内容总量成正比，长列表需自行控制条目数；
  - 静态索引缓冲上限 16384 quad（沿用 M5.5 的既有约束，超出需扩索引缓冲）；
  - 三批分离导致跨批 Z 序不严格（沿用 M5.5 的既有结论）。
- Debug + Release 全量重建零警告。

参考：`04-Layout §6.3`、`06-Events §3.1`、`10-Rendering §6.3`。

---

## M5.7 — 自定义字体文件 + 布局/样式增强 ✅（2026-11-04 完成）

**目标**：支持加载未安装的字体文件（图标字体），并补齐布局换行、样式 API 简化。

完成情况与实现说明：
- **自定义字体**（`DUI/Text/TextEngine.h/.cpp` + `DUI/DUI.h`）：新增公共 API
  `dui::LoadFontFamily(const wchar_t* path)` → 用 DWrite（`IDWriteFactory3` 的
  `CreateFontFaceReference` → `FontSetBuilder` → `CreateFontCollectionFromFontSet`）
  把字体文件装进进程内字体集合，返回字体族名；`Shape` 里族名命中该集合时
  同时 `SetFontCollection` + `SetFontFamilyName`（只设族名会被 DWrite 静默回退）。
  - **格式限制**：DirectWrite 只认 SFNT 容器 → 仅支持 ttf/otf/ttc；
    **woff/woff2 不支持**（Web 字体封装：Brotli 压缩 + 表变换），需先用
    woff2/fontTools 转成 ttf；决定不做内嵌解码器。
  - 配套示例 `Samples/CustomFont`（字体文件随构建拷到 exe 目录，按 exe 目录定位加载）。
  - **坑**：`Style().FontFamily(const wchar_t*)` 只存指针不拷字符串 → 族名串必须活过
    样式使用期（局部 String 出函数即悬空 → 图标渲染成方框 □）。
- **布局换行 `FlexWrap`**（`Prop::FlexWrap`，NoWrap 默认）：Row + Wrap 时按可用主轴宽
  贪心断行——测量（`Element::MeasureContent`）与排布（`ArrangeElement`）同一规则，
  行高 = 行内最高子项、行间同 gap、交叉轴对齐沿用 `alignItems`；换行模式跳过 grow/shrink。
  Column 方向不支持换行（MVP）。
- **样式 API 合并**：`StyleBuilder` 与属性袋 `Style` 合并为单一 `dui::Style`
  （既是声明集又是链式构建器，`dui::Style().Gap(20.f)` 临时可直传，`.Build()` 已删）；
  派生用 `Style::Derive(base)`、具名用 `Style("name")`；`SetStyle` 一律拷贝持有
  （链式临时不悬空；派生链 `parent_` 与字体指针仍指向外部）。
- **UI 层文件拆分**（`DUI/UI/`）：`Style.h/.cpp`、`Element.h/.cpp`、`Panel.h`、
  `Text.h/.cpp`，`UI.h` 变聚合头；原 `UI.cpp` 已删。
- **已知限制**（后续项）：
  - 自定义字体仅支持 ttf/otf/ttc，woff2 不支持；
  - `FlexWrap` 仅 Row 方向；
  - 字体族名指针（`FontFamily`）生命周期由调用方保证。
- Debug + Release 全量重建零警告。

参考：`03-Style §6`、`15-Properties §4.3.1/§4.4`、`04-Layout §4.1`。

---

## M5.8 — 元素级指针事件 ✅（2026-11-04 完成）

**目标**：元素除点击外，支持鼠标移入/移出/移动/按下/弹起与滚轮回调。

完成情况与实现说明：
- **绑定 API**（`DUI/UI/Element.h`）：`OnPointerEnter / OnPointerLeave / OnPointerMove /
  OnPointerDown / OnPointerUp`（`PointerEvent`）+ `OnWheel`（`WheelEvent`），
  形态与 `OnClick` 同构（成员函数 + 上下文 / `std::function`）。
  处理器存储**按需分配**（元素未绑定任何事件时不占这份内存）。
- **路由**（`DUI/UI/Event.cpp`）：
  - 移入/移出为**直达事件**：按"悬停链"差分（新链元素 → 移入；旧链元素 → 移出），
    移到子元素上不算离开父元素；
  - 移动/按下/弹起/滚轮**冒泡**（命中 → 根），`ev.handled` 终止冒泡；
  - 派发**只经过有绑定者的元素**，无绑定直接跳过；不做防抖与节流（开发者自理）。
- **接入**（`DUI/App/App.cpp`）：
  - 滚轮顺序改为：先派发元素级 `Wheel`，无人 `handled` 才由滚动容器按"最近可滚祖先"消费；
  - 滚动条上的按压走滑块拖拽，**不派发**元素级按下/弹起（与 Click 合成口径一致）；
  - 指针离开窗口 → 悬停链上全部元素触发移出。
- **配套示例** `Samples/PointerEvents`（6 类事件日志演示；已实测
  Enter / Leave / Move / Wheel / Down / Up 全部触发）。
- **已知限制**（后续项）：
  - 无指针捕获（拖出元素后 Move/Up 仍按当前命中位置派发）；
  - `PointerEvent` 无按键掩码/修饰键字段；
  - 无防抖/节流（按约定由开发者处理）。
- Debug + Release 全量重建零警告。

参考：`06-Events §3.2`、`06-Events §10`。

---

## M5.9 — 键盘事件 + 焦点/Tab 链 + 文本编辑控件 ✅（2026-11-04 完成）

**目标**：`dui::TextBox`（含密码掩码）与 `dui::TextArea` 控件；并落地其底座——键盘转发、
焦点系统、Tab 链、元素级键盘事件。

完成情况与实现说明：
- **键盘转发**（`DUI/Platform/Window.h/.cpp`）：`NativeWindowHost` 新增
  `OnKeyDown/OnKeyUp/OnChar`；窗口过程处理 `WM_KEYDOWN/WM_KEYUP/WM_SYSKEYDOWN/WM_SYSKEYUP/
  WM_CHAR/WM_SYSCHAR/WM_IME_CHAR`，修饰键状态取自 `GetKeyState`。
  **`WM_IME_CHAR` 必须吃掉（return 0）** —— 否则 `DefWindowProc` 会再生成一个 `WM_CHAR`，
  同一个字符被插入两次（IME 提交）。
- **焦点服务**（新建 `DUI/UI/Focus.h/.cpp`）：焦点唯一，挂在元素上。
  `FocusedElement()` / `IsFocused()` / `SetFocus()`；`Element::Focus()/Blur()/HasFocus()`。
  换焦点时旧元素清 `State::Focused`、新元素置位 → 样式状态变体自动生效；
  另有 `Element::OnFocusChanged(bool)` 钩子（控件据此把光标放到文本末尾；
  失焦时 `TextBox` 清除选区 —— 否则选区底色留在失焦控件上，且不可见的选区会让
  再次聚焦后的 Delete/输入替换掉用户看不见的内容）。
- **Tab 链**：树序（深度优先）收集 `IsFocusable()` 元素，按 `tabIndex` 稳定排序：
  `>0` 组（数值升序）→ `0` 组（文档序）→ `<0` 组（数值升序）；`Shift+Tab` 反向、到头循环。
  `Tab` 键在焦点元素未消费时由框架默认处理（移动焦点）；点击命中可聚焦元素（或其可聚焦
  祖先）时取得焦点（`FindFocusable`，`UI/Event.cpp`）。
- **元素级键盘事件**（`DUI/UI/Element.h` + `Event.cpp`）：`OnKeyDown/OnKeyUp/OnChar`
  （`KeyEvent`：VK 码 + 字符 + Ctrl/Shift/Alt），形态与 `OnClick` 同构；
  路由为**先到焦点元素再沿祖先链冒泡**，`ev.handled` 终止冒泡。
- **控件内部行为钩子**（新增）：`Element::HandleKeyDown/HandleKeyUp/HandleChar/
  HandlePointerDown/HandlePointerMove/HandlePointerUp`（受保护虚函数）。
  派发时钩子先于外部绑定执行；钩子消费（返回 true）则不再向上冒泡，
  但**同一元素上的外部绑定仍会收到该事件**（可见 `ev.handled` 已置位）——
  控件自身行为与用户绑定互不覆盖。
- **文本编辑控件**（新建 `DUI/UI/TextBox.h/.cpp`）：
  - `dui::TextBox`（单行）/ `dui::TextArea`（多行，继承）；`SetPassword(true)` → `•` 掩码。
  - 编辑核心：文本 / 光标 / 选区（锚点模型）/ 撤销栈（100 步）/ 剪贴板
    （`Ctrl+C/X/V/A`、`Ctrl+Z/Y`）；方向键、`Home/End`、`Backspace/Delete`、回车
    （单行 → `OnSubmit`，多行 → 插入换行）、`Shift+` 组合扩展选区、`Ctrl+Home/End` 全文首尾。
  - 鼠标：点击定位光标、按住拖动选区（拖动期间靠 `State::Pressed` 判定）。
  - 渲染：选区矩形 → 文本 → 光标（光标仅在有焦点时显示；选区在失焦时清除）；
    内容溢出裁剪 + 滚动，光标自动滚入可见区。
  - **光标持续闪烁**：统一 ticker（只有闪烁时 500ms）驱动 —— `Element::IsBlinkable()/OnBlink()`
    钩子 + `NeedsBlinkTick()/DispatchBlink()`（`UI/Focus.h`）+ `Window::SyncTickTimer()`；
    焦点离开可闪烁控件即停表；交互（点击/输入/移动光标）重置闪烁相位（光标立即可见）。
  - **IME 候选窗口跟随光标**：用**系统 caret** 机制 —— 平台层建一个 1×N 全透明位图的
    隐形 caret（`EnsureImeCaret/UpdateImeCaretPos/RemoveImeCaret`），位置来自 UI 层
    `Element::InsertionPoint()`（返回**矩形**：行顶 → 行底）→ `QueryInsertionPoint()` →
    App 层 `Window::SyncImeCaret()`（键盘/字符/指针事件后同步）；
    IME 依 `GetCaretPos()` 把候选窗口定位到光标处，不出现双光标。
    **caret 必须覆盖整行**：IME 依 caret **底边**摆候选窗，caret 只有 1px 高时输入法
    会把候选窗整体下移一整行（实测候选窗落到下一行行首、离光标很远）；
    高度变化要 `DestroyCaret` + `CreateCaret` 重建位图。
    **大坑（已回退）**：在 `WM_IME_COMPOSITION/WM_IME_NOTIFY` 处理期间调用
    `ImmGetContext/ImmReleaseContext` 会**重建输入上下文、打断正在进行的 IME 组合**
    → 表现为「中文打不出来、输入框里没有文字、候选窗口也不定位」。
    现已**不吞任何 IME 消息**，定位改走系统 caret。
  - **文本框内 I-beam 光标**：`WM_SETCURSOR` → `NativeWindowHost::OnSetCursor(pt)` →
    命中测试 → `Element::WantsTextCursor()`（`TextBox` 为 true）→ `SetCursor(IDC_IBEAM)`。
    必须在 `WM_SETCURSOR` 里设置（系统每次鼠标移动后把光标重置为箭头）。
    **坑**：`WM_SETCURSOR` 的 lParam 坐标在本机不可靠（实测为负值），必须用
    `GetCursorPos()` + `ScreenToClient()` 换算客户区坐标。
  - **密码框光标偏粗**：掩码字符由 `●`(U+25CF) 改为 `•`(U+2022) —— 大实心圆与 1px
    光标条同色紧邻会连成一条粗竖线；另提供 `SetMaskChar(wchar_t)` 自定义。
    另一根因是 `HitTestTextPosition` 的 `isTrailingHit`：末尾位置必须传 **TRUE**，
    否则返回最后一个字符的**前缘**，光标压在最后一个字符上（连带显得粗）。
  - **掩码圆点垂直居中**：bullet 类字符（`•`）的字形墨迹紧贴基线（圆心几乎与基线
    齐平），按基线对齐绘制会**明显偏下**（实测圆心比输入框中心低 4~5px，且与贯穿
    整行的光标条错位）。修法：`TextBox::UpdateMaskOffset()` 量出掩码字形的**真实
    墨迹中心**（相对基线），把整段掩码文本补偿到**行高中心** —— 补偿量并入
    `TextOrigin()`，绘制/命中/光标/IME 共用同一原点。
    - 墨迹测量（`Engine::GlyphInkCenterDip`）：本机 SDK 的 `dwrite.h` 是**旧版结构**
      —— **没有** `IDWriteFontFace::GetGlyphOutline`、`IDWriteGlyphRunAnalysis::GetAlphaBounds`、
      `IDWriteFont::GetFontFace`；`GetGlyphIndices` 也只有 3 参数（无 `exists` 输出）。
      可行路径：**探针渲染**（自定义 `IDWriteTextRenderer` 只捕获 `run->fontFace`，不绘制）
      拿到 layout **实际使用**的字体面（缺字回退时与请求族名无关），再用
      `IDWriteGlyphRunAnalysis::GetAlphaTextureBounds` 取真实墨迹盒（按 4 倍字号测量
      再折回 DIP 提精度）。**不要用 `GetDesignGlyphMetrics`** —— 其 vertical-origin
      语义与实际基线位置对不上，实测会把补偿算偏 4px 以上。
  - 语义事件：`OnTextChanged(const String&)` / `OnSubmit()`。
- **排版几何 API**（`DUI/Text/TextEngine.h/.cpp`）：`HitTestPoint`（坐标 → 插入点）、
  `CaretRect`（位置 → 光标矩形）、`SelectionRects`（区间 → 逐行高亮矩形）、
  `LineInfos`（逐行字符区间与行几何）。
  - **踩坑**：本机 SDK 的 `dwrite.h` 是旧版结构（`DWRITE_LINE_METRICS` 无 `lineLength`/
    `top/left`，无 `DWRITE_CARET_RECT`，`IDWriteTextLayout1` 只有 kerning/spacing），
    `IDWriteTextLayout1` 的 `GetLineCount/HitTestPosition` **不可用**。
    改用基类接口实现：`HitTestPoint`（5 参版）、`HitTestTextPosition`、
    `HitTestTextRange`（选区逐行矩形）、`GetLineMetrics`（数组版，`E_NOT_SUFFICIENT_BUFFER`
    时扩容重试）。
  - **踩坑**：`HitTestTextPosition` 的 `isTrailingHit` 必须按位置给：文本末尾
    （index == len）传 **TRUE**，否则返回的是**最后一个字符的前缘** → 光标压在最后
    一个字符上（位置错，且与掩码/文字粘连显得粗）。中间位置传 FALSE 才是插入点语义。
- **内容裁剪**（`DUI/App/Frame.cpp`）：`Element::ContentClipRect(Rect&)` 钩子 —— 元素声明
  内容矩形后，收集期把该元素的内容切到独立绘制单元（scissor = 内容矩形 ∩ 当前裁剪，
  原点不变）。**内容矩形是布局空间绝对 DIP，须叠加父级原点平移到窗口空间**
  （与 `CollectScroller` 的视口同理），否则嵌套滚动容器内裁剪带留在原位。
- **剪贴板分层**：UI 层不依赖 Platform —— 平台实现（`CF_UNICODETEXT`，`Platform/Window.cpp`）
  由 App 层在 `Application::Run` 注入 UI 层（`SetClipboardService` 函数指针对）。
- **样式属性**：`Prop::SelectionColor`（默认半透明蓝）、`Prop::CaretColor`（默认取文本色）；
  链式方法 `SelectionColor()/CaretColor()`。
- **示例** `Samples/TextInput`（单行 / 密码 / 多行 + 焦点与 Tab 链 + 键盘事件日志）。
- **已知限制**（后续项）：
  - 无 IME 组合串内联预览（`GCS_COMPSTR` 丢弃，拼音不在编辑框内显示；候选窗口定位已解决）；
  - 无单词级移动（`Ctrl+←/→`）、无自绘选区手柄、无富文本/拼写检查；
  - 密码模式以 `●`（U+25CF）逐字符掩码（不保留原宽）；
  - 拖选依赖指针未移出控件（无指针捕获）。
- **修复：失焦清除选区** —— `TextBox::OnFocusChanged(false)` 清除选区：否则选区底色留在
  失焦控件上；且不可见的选区会让再次聚焦后的 Delete/输入替换掉用户看不见的内容。
- **修复：最小化 + 文本框聚焦 → CPU 占用飙高** —— 主循环按"子树脏"决定进帧，而最小化时
  客户区尺寸为 0 → `RenderFrame` 第一行直接 return 且**不清脏标记**（`ClearDirtyTree`
  在 return 之后）→ 脏标记永远为真 → 主循环永远进不了 `WaitMessage()` → 退化成忙转。
  **只在文本框聚焦时暴露**：光标闪烁节拍每 500ms 标一次脏（不聚焦时无脏 → 阻塞等消息
  → CPU 归零）。
  修法：① 主循环最小化时直接 `WaitMessage()` 阻塞（恢复时由 `WM_SIZE` → `OnResized`
  触发重排 + 重绘，脏标记保留不丢）；② `SyncTickTimer` 最小化时停表（窗口不可见，
  无需闪烁节拍；恢复后主循环每轮重新同步 → 自动重启）；③ 平台层新增
  `NativeWindow::IsMinimized()`（`IsIconic`）。
- **窗口失活 → 文本框失焦**（光标停止闪烁）—— `WM_ACTIVATE(WA_INACTIVE)` → 平台层上抛
  `NativeWindowHost::OnActivated(false)` → App 层 `SetFocus(nullptr)`：编辑控件随之失焦，
  选区消失、光标隐藏、闪烁停表（`NeedsBlinkTick()` 依赖焦点非空）。窗口重新激活**不自动
  恢复焦点**（用户重新点击）。
  **最小化是例外**：最小化同样发 `WA_INACTIVE`，但应保留焦点（还原后可直接继续输入）——
  靠"延迟一轮判定"区分（下一轮主循环时 `WM_SIZE(SIZE_MINIMIZED)` 已到达，`IsIconic()`
  才为准），见 `WindowHostAdapter::pendingDeactivate_/TakeDeactivate()`。
- **鼠标光标形态（样式属性 `cursor`）** —— 普通 Element 可设置光标（语义同 CSS 的
  `cursor`）：`Prop::Cursor` + `Style::Cursor(dui::Cursor)`，枚举
  `Arrow/IBeam/Hand/Wait/Crosshair/Move/SizeAll/NotAllowed/Help`。
  生效规则：**命中链就近者胜**（子元素未指定 → 沿链取父级的，同 CSS 继承；
  子元素显式设 `Arrow` 可覆盖父级的 `Hand`）；都没指定 → 回退
  `Element::WantsTextCursor()`（TextBox 的 I-beam）→ 默认箭头。
  **不需要 hover 变体** —— 光标只在指针位于元素上时可见，而"指针在元素上"与
  hover 状态在 DUI 里是同一件事（hover 位由指针进入/离开差分驱动）。
  平台层 `NativeWindow::SetCursorIBeam()` → `SetCursorShape(LPCWSTR idc)`（IDC_* 宏
  是 `LPWSTR` 表达式，不能当整数传）；App 层做 `dui::Cursor → IDC_*` 映射。
  踩坑：`WM_SETCURSOR` 的 lParam 坐标不可靠，必须 `GetCursorPos` + `ScreenToClient`。
- **M5.10 图像控件 `dui::Image`** —— WIC 解码（Windows 内置解码器：PNG/JPEG/BMP/DIB/
  GIF(首帧)/TIFF/ICO…）+ Graphics 层**第四批（图像批）** + 纹理仓库（进程级单例，
  App 层每帧 `Sync`）。架构详见 `Doc/Arch/16-Image.md`；示例 `Samples/Image`。
  要点：
  - 解码放 Graphics 层（WIC 是平台能力）；UI 层不碰 D3D（控件持 `shared_ptr<void>`
    pimpl，公共头不引入 windows.h —— 与 `TextBox::para_` 同模式）。
  - 图像批顶点格式同字形批 → 复用 `vsG_`/`layoutG_`，只换 PS（彩色采样）+ 线性采样器
    （字形用 POINT，图像用 LINEAR）；按纹理分组（`ImageGroup`）。
  - 批序 矩形→字形→形状→**图像** → 同单元内图像会盖住文字 → `Kind::Image` 元素在
    收集期**单独切绘制单元**（单元顺序即元素树顺序 → z 序与兄弟元素一致）。
  - DPI：自然尺寸 = 像素 ÷ scale（`StyleContext` 新增 `scale` 字段，`RunLayout` 多一个
    scale 参数，App 层传 `native.Scale()`）→ 1 图像像素 = 1 物理像素，跨 DPI 不糊。
  - 踩坑：① `IWICFormatConverter::Initialize` 是 **6 参数**版（Win7+ 签名，不是 3/5）；
    ② SDK 里**没有 `wincodec.lib`**，WIC 的 import lib 叫 **`windowscodecs.lib`**，用
    `#pragma comment(lib, ...)` 随编译单元携带（不动 `DUI.props` 的公共编译基线）；
    ③ 项目编译基线 `/EHs-c-`（异常关闭）→ `std::ofstream` / `std::vector::insert`
    会触发 **C4530**（示例改用 Win32 文件 API + 裸数组）；④ **颜色约定是 0xAARRGGBB**
    （低字节 R、高字节 A）—— 传 `0xFF000000` 是"不透明黑"而不是白色，图像会被乘成全黑，
    白色应写 `0xFFFFFFFF`（或 `(a<<24) | 0x00FFFFFF`）；⑤ 示例自造 BMP 时头部按规范
    精确写 14+40 字节（`biPlanes`/`biBitCount` 各 2 字节）—— 头部多写 6 字节会越界写坏堆
    （表现为 `free_dbg` 报堆损坏）。
  - **图像不该被布局拉伸变形** —— `AlignItems` 默认 Stretch 会把图像横向拉满、flex-shrink
    会把它压扁（跨轴比例失真）。三处处理：`Kind::Image` 子项未显式给该轴尺寸时交叉轴用
    自然尺寸；图像从主轴收缩计算中排除；`Image::OnPaint` 按 contain 等比缩放 + 居中。
  - **GIF 动画**：不自己写 GIF 解码器（LZW / 调色板由 WIC 负责）。`DecodeAnimatedImageFile`
    用 `GetFrameCount` / `GetFrame(i)` + GIF 元数据（`/grctlext/Delay|Disposal`、
    `/imgdesc/Left|Top`、`/logscrdesc/Width|Height`）把局部帧按 disposal 合成为统一尺寸的
    全画布 BGRA8；`Image` 复用同一张纹理换帧（不重复 `Register`）；`AnimationClock`
    统一驱动帧动画与光标闪烁，App 层只留一个窗口定时器（有帧动画 20ms / 只有闪烁
    500ms，最小化停表），帧时间用绝对时刻判断。
  - 踩坑：**命令行 msbuild 全量重建会刷 LNK4020**（"pdb 类型记录已损坏"）—— `/MP` 多进程
    编译 + `/Zi` 缺 `/FS` 所致；VS 内构建由 `Microsoft.CppCommon.targets` 在 `UseMultiToolTask`
    为真时自动加 `/FS`，所以 VS 里看不到。规避：命令行加 `-p:UseMultiToolTask=true`
    （等价于 VS 内构建；`-p:AdditionalOptions=/FS` 对并行编译**无效**，因为它是全局属性、
    进不了 ClCompile 的项元数据）。不改 `DUI.props` 的公共编译基线。另：不要用
    `-p:OutDir=...` 覆盖构建（会把 obj 与 pdb 写散，引发 IPDB/IOBJ mismatch）。
- Debug + Release 全量重建零警告。

参考：`06-Events §3.3/§6`、`15-Properties §4.1.1`、`11-Text`。

---

## M5.11 — 无边框窗口与自定义标题栏 ✅（2026-11-05 完成）

**目标**：支持 `Chrome::Borderless`（无系统边框）—— DWM 阴影、自绘标题栏、拖标题栏移动窗口、
拖边框缩放窗口。

完成情况与实现说明：
- **窗口形态声明**（`DUI/App/App.h`）：`WindowOptions.chrome`（`Chrome::System` /
  `Chrome::Borderless`）→ App 层建窗时传给平台层。另加 `WindowOptions.center`（初始居中）。
- **初始居中（框架窗口能力）**：`Application::Run` 在 `OnCreate`/`Show` **之前**按
  `options_.center` 调 `NativeWindow::CenterOnScreen()` —— `MonitorFromWindow` +
  `MONITORINFO.rcWork`（工作区，排除任务栏）按整窗物理像素算中心，跨屏时以窗口所在
  显示器为准；`SWP_NOSIZE` 只移动，不改尺寸也不激活窗口（构建过程仍不可见）。
  默认 `true`（所有窗口初始居中）；设 `false` 沿用原有 `CW_USEDEFAULT` 层叠摆放。
- **平台层（`DUI/Platform/Window.cpp`）**：
  - 无边框样式 `WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX`
    + `WS_EX_APPWINDOW`（`WS_THICKFRAME` 必须保留 —— `HTLEFT` 等缩放命中码靠它生效）。
  - **DWM 阴影**：`DwmExtendFrameIntoClientArea(hwnd, {-1,-1,-1,-1})`（边框外扩 1px →
    DWM 在窗口外侧画投影）；Win11 另设圆角 `DWMWA_WINDOW_CORNER_PREFERENCE=33` 与
    去边框色 `DWMWA_BORDER_COLOR=34`（Win10 SDK 无常量，手写）。
    `#pragma comment(lib, "dwmapi.lib")` 随编译单元携带（不动 `DUI.props` 公共基线）。
  - **`WM_NCCALCSIZE`**：无边框时**原样保留 `rgrc[0]` 返回 0**（`wParam=TRUE` 时入参
    `rgrc[0]` = 系统建议的新窗口矩形、出参 = 客户区矩形；不调 `DefWindowProc` 扣边框
    → 客户区 = 整窗）。否则 `WS_THICKFRAME` 的系统边框会在内容外留一圈非客户区，
    边框色被设为"无"时那圈透出桌面。**不能写 `rgrc[0] = rgrc[1]`** —— `rgrc[1]` 是
    **上一次**的窗口矩形（详见下方踩坑）。
  - **尺寸语义**：无边框时 `width/height` 就是**整窗**尺寸，**不做** `AdjustWindowRect`
    边框补偿（补了整窗会大出左右各 8px，业务按 DIP 布局就错位）。
  - **`WM_GETMINMAXINFO`**：最大化时把 `ptMaxPosition`/`ptMaxSize` 约束到工作区 ——
    否则系统把窗口放到比工作区还大（边框外扩），底边/右边被屏幕切掉。
  - **`WM_NCHITTEST`**：屏幕坐标 → 客户区 DIP → 上抛业务；返回 `HTCAPTION`/`HT*`，
    **移动与缩放全部由系统接管**，业务不处理鼠标与窗口几何。
  - **右键标题栏 → 系统菜单**：`WS_POPUP` 没有系统标题栏，`DefWindowProc` 不会自己弹菜单。
    `WM_NCRBUTTONUP` 且 `wParam == HTCAPTION` 时用 `GetSystemMenu(hwnd, FALSE)` +
    `TrackPopupMenuEx(..., TPM_RETURNCMD)` 弹出（还原 / 移动 / 大小 / 最小化 / 最大化 /
    关闭），选中项 `SendMessage(WM_SYSCOMMAND, cmd)` 交给 `DefWindowProc` 执行。
    菜单项启用/置灰由系统在 `WM_INITMENUPOPUP` 自动维护（实测：非最大化时"还原"灰，
    其余可用），业务不必手动 `EnableMenuItem`。系统边框窗口不受影响（原来就由
    `DefWindowProc` 弹菜单）。
- **App 层（`DUI/App/App.h/.cpp`）**：
  - `Window::OnNcHitTest(Vec2, HitTestCode&)`（业务重写，报标题栏/边框热区）；
    `HitTestCode` 枚举与 Win32 `HT*` 一一对应（公共头不带 windows.h，故自定义）。
  - `Window::OnStateChanged()`（尺寸/最大化还原变化 → 业务换自绘按钮图标）；
  - `Window::Minimize / MaximizeOrRestore / Close / IsMaximized / ClientSizeDip()`。
- **配套示例** `Samples/CustomFrame`：自绘标题栏 + iconfont 图标按钮（最小化 / 最大化或还原 /
  关闭）+ 阴影 + 框架默认初始居中 + 拖标题栏移动 + **拖四边 / 四角缩放** +
  右键标题栏系统菜单；字体文件随构建拷到 exe 目录。
- **踩坑**：
  - **缩放热区必须先于标题栏判定** —— 上边这条窄带压在标题栏上，若先判 `HTCAPTION`，
    上边与左上 / 右上角全都只能移动窗口、不能缩放（用户实测反馈）。正确顺序：
    四边四角 `HT*` → 按钮区 `HTCLIENT` → 标题栏其余 `HTCAPTION` → 内容区 `HTCLIENT`。
    热区之外显式返回 `HTCLIENT`（别 `return false` 落到 `DefWindowProc`，否则
    `WS_THICKFRAME` 会在最外圈另报一套 `HT*`，最大化时误触缩放）。
  - **标题栏按钮区必须排除在 `HTCAPTION` 热区之外** —— 命中标题栏的点击被系统当成
    "拖标题栏"，按钮收不到 `WM_LBUTTONDOWN`，永远点不动。按钮区让出最上面
    `kResizeBorder`（8 DIP）给上边 / 右上角缩放（原生窗口同理），其余部分返回
    `HTCLIENT` 照常可点。
  - `MINMAXINFO` **没有** `rcWork`/`rcWindow` 字段（那是 `MONITORINFO` 的）→
    `WM_GETMINMAXINFO` 要设 `ptMaxPosition`/`ptMaxSize`。
  - 拖边框缩放时 Windows 进入模态循环（主消息循环挂起、`WM_SIZE` 连续到达）→ 框架在
    `OnResized` 里就地渲染，否则内容不跟手。
  - 最大化后 `IsMaximized()` 为真 → 缩放热区需自行禁用。
  - `Style().FontFamily` 只存指针 → iconfont 族名串必须长期存活（static 局部变量）。
  - **最小化 → 还原后窗口一片空白**（无边框窗口特有）：最初误判为"DXGI flip model 在
    最小化时释放后台缓冲、缓存的 RTV 失效"→ 加 `Device::Resize(force)` 强制重建，
    实测**无效**（已回退，公共代码恢复原样）。真实原因是 `WM_NCCALCSIZE` 里写了
    `rgrc[0] = rgrc[1]`：还原时 `rgrc[1]` 是最小化的**离屏图标矩形**
    （实测 `in1=(-32000,-32000,-31801,-31966)`，199×34 物理像素），客户区被压成
    199×34 → `WM_SIZE` 的 lParam 与 `GetClientRect()` 都报 199×34、交换链也缩到
    199×34，渲染出的画面是一片白。且之后**不会再有** `WM_SIZE`，靠"还原后强制再画
    一帧"补不回来。修法：原样保留 `rgrc[0]` 返回 0。
    诊断手段：临时日志打印 `WM_NCCALCSIZE` 的 `rgrc[0]/rgrc[1]`、
    `WM_WINDOWPOSCHANGED` 的窗口/客户矩形、`WM_SIZE` 的 lParam，配合
    PowerShell `ShowWindow(SW_MINIMIZE/SW_RESTORE)` + 屏幕截图对比还原前后。
- **已知限制**（后续项）：
  - 标题栏双击最大化未实现（框架暂无双击事件）；
  - 阴影依赖 DWM 合成（无 DWM 的环境不可见），阴影样式不可定制；
  - 缩放热区会占用客户区边缘几像素，布局需留余量。
- Debug + Release 全量重建零警告。

参考：`12-Windowing §6.1`、`06-Events §3`（点击冒泡）。

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
