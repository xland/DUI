# DUI 开发任务清单（Todo）

> 最后更新：2026-10-28。
> 当前进度：**M1 已完成**（API 骨架冻结，两个示例编译/链接/运行通过）。
> 换电脑后从 **M2** 开始。本文档自包含：环境、约定、每阶段的步骤与验收标准都在这里。

---

## 0. 环境与构建（换电脑先做这个）

| 项 | 要求 |
| --- | --- |
| IDE / 工具链 | Visual Studio 18（PlatformToolset **v145**），x64 仅一个平台 |
| Windows SDK | 10.0（`WindowsTargetPlatformVersion=10.0`） |
| 编译基线 | `Build/DUI.props`：/MT(/MTd)、/EHs-c-（无异常）、/GR-（无 RTTI）、/utf-8、/W4、C++20 —— 所有工程共同 import，不要单独改选项 |

命令行构建与验证（MSBuild 路径因机器而异，用 vswhere 定位）：

```cmd
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe

cd /d <项目根>
<MSBuild路径> DUI.slnx -p:Configuration=Debug   -p:Platform=x64 -m -v:m -nologo
<MSBuild路径> DUI.slnx -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
x64\Release\Samples.exe
x64\Release\ListSelect.exe
```

M1 完成的验收标志：三个工程全部构建成功，两个 exe 运行打印
`[DUI] M1 skeleton: window/message-loop not implemented yet (M2).` 后返回 0。

> 注意：直接编译单个 vcxproj 时 `$(SolutionDir)` 未定义（Samples 找不到 `DUI/DUI.h`），
> **必须编译 `DUI.slnx`**。

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

## M2 — 平台窗口与消息循环

**目标**：两个示例运行后出现真实窗口（内容可以先全黑），关闭返回退出码 0。

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

## M3 — 样式解析 + Flex 布局 + 矩形绘制

**目标**：`ListSelect` 示例的界面按样式正确显示（布局、颜色、圆角矩形、hover/selected 手动可用），点击事件能触发（命中测试按 Rect）。

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

## M4 — Dispatcher + 线程池 + 输入事件

**目标**：`Samples.exe`（三栏阅读器）的异步加载链路真实工作；鼠标点击/hover 全部真实生效。

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

## M5 — DirectWrite 文本 + DPI 切换全流程

**目标**：真实文字渲染；跨显示器拖动 DPI 切换不糊、不重排。

步骤：

1. `DUI/Text/`：FontCollection、Shaping（`IDWriteTextAnalyzer`）、Layout、Raster（Alpha 图集，ADR-003）、GlyphAtlas（LRU）。
2. `Text` 控件接入排版结果（measure 缓存键：fontSize/fontFamily）。
3. `WM_DPICHANGED` 完整流程（12-Windowing §4 七步）：建议矩形 SetWindowPos → ResizeBuffers → 字形重光栅 → 分级失效 → `OnDpiChanged` → 鼠标坐标重算。
4. `pixelSize = round(fontSizeDip × scale)`，< 9px 向上夹紧（12-Windowing §9-3）。

验收：150%/200% 下文字清晰；跨屏拖动不糊不闪；DIP 尺寸不变时不重排（日志断言）。

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

## 备忘（换电脑提醒）

- 构建命令见 §0；**别改 `Build/DUI.props` 的编译开关**，除非文档有对应 ADR。
- M2 起新建文件记得同步 `DUI.vcxproj`（和 `.filters`）；示例间 IntDir 已按工程名隔离，新示例工程照抄 `Samples.vcxproj` 的写法（含 `<IntDir>$(ProjectName)\$(Platform)\$(Configuration)\</IntDir>`）。
- 每完成一个里程碑：更新本文档勾选状态 + `00-Overview §6` 工程状态。
- 全部构建产物（`x64/`、各 `x64/`、`*.obj` 等）不要提交；`DUI/x64` 等构建中间目录按需加入 `.gitignore`。
