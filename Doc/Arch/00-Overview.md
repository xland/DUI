# DUI 架构总纲

> 状态：草案 v0.1 — 待讨论确认。
> 本文件只描述"要做什么、受什么约束"，具体取舍见 [01-Layering.md](./01-Layering.md) 与 [02-Decisions.md](./02-Decisions.md)。

## 1. 定位

仅 Windows 的 GUI 框架。目标按优先级排序：

1. **开发体验** —— API 易用、易学、易诊断（链式 API、控件自足、错误显式）。**开发者的使用方便优先于一切内部指标。**
2. **功能完备度** —— 能力覆盖优先。
3. **效率与资源** —— 内存、性能、体积**尽量做好**，不设硬性数值门槛，以观测指标跟踪回归（见 ADR-015）。

**无依赖**为硬性约束（不参与排序）：不引入任何第三方库，只依赖操作系统自带组件（d3d11 / dxgi / dwrite / dcomp / user32 / kernel32 等）。

当各项冲突时的默认取向：**开发体验 > 功能完备度 > 内存 ≈ 性能 ≈ 体积**。

## 2. 范围

### 2.1 平台

| 项 | 取值 | 备注 |
| --- | --- | --- |
| 操作系统 | Windows 10 起（含 Windows 11） | 具体最低版本见 ADR-009 |
| 架构 | **仅 x64**（不兼容 x86） | 工程需移除 Win32 配置（ADR-009） |
| 语言标准 | C++20 | 工程已设置 `stdcpp20` |
| 渲染 | Direct3D 11 + DXGI | 见 ADR-001 |
| 文本 | DirectWrite | 见 ADR-003 / ADR-004 |
| 窗口/输入 | Win32 | 见 ADR-010 |

### 2.2 能力清单（MVP）

- 窗口（含自定义标题栏、圆角、阴影）
- 常用控件：容器、按钮、文本、编辑框、滚动视图、列表（虚拟化）
- 布局、[样式](./03-Style.md)、主题
- 文本排版（含字体回退、复杂脚本）与[富文本](./11-Text.md)
- 图片显示
- [动画](./05-Animation.md)
- 输入与事件（[见 06-Events.md](./06-Events.md)）
- 高 DPI（Per-Monitor）

## 3. 设计原则

- **显式优于隐式**：对象生命周期、所有权、失效（invalidate）由调用方明确表达，框架不做隐式缓存失效扫描。
- **批量化**：所有绘制先收集成显示列表，再一次性光栅化，避免逐元素 Draw Call。
- **热路径零分配**：消息处理、布局、绘制主循环内不做堆分配；内存从预分配池/竞技场获取。
- **可裁剪**：按功能编译期开关（文本/图片/动画/无障碍…），未启用模块不进入二进制。
- **不追求通用**：不为极低概率场景写容错，不为一次性功能搭抽象层。

## 4. 非目标

- 跨平台（不抽象 Linux/macOS，不为未来跨平台预留接口层）
- 内置浏览器 / WebView
- 设计器 / 可视化编辑器
- 脚本语言绑定（如 Lua/JS）
- XAML / HTML / CSS 类声明式界面描述语言——开发者直接用 C++ 组合控件，简化手段是提供大量可高度定制的控件（见 ADR-018）

> 提供 HTML 风格的**布局语义**（[04-Layout.md](./04-Layout.md)）与 CSS 风格的**动画语义**（[05-Animation.md](./05-Animation.md)），但不提供声明式**描述语言**（无运行时解析器）—— 两者不矛盾。

> 无障碍（UIA）、SVG：待定，见 ADR-017；富文本编辑已定为目标（分阶段，见 [11-Text.md](./11-Text.md) 与 ADR-023）。

## 5. 文档地图

| 文件 | 内容 |
| --- | --- |
| [00-Overview.md](./00-Overview.md) | 目标、范围、原则、非目标 |
| [01-Layering.md](./01-Layering.md) | 分层与模块划分、一帧的生命周期 |
| [02-Decisions.md](./02-Decisions.md) | ADR 决策清单（待拍板） |
| [03-Style.md](./03-Style.md) | 样式系统：属性表、级联、复用、失效 |
| [04-Layout.md](./04-Layout.md) | 布局系统：HTML 风格语义、Flex/Grid/绝对定位、约束与失效 |
| [05-Animation.md](./05-Animation.md) | 动画系统：过渡、关键帧、缓动、时序引擎 |
| [06-Events.md](./06-Events.md) | 输入与事件：绑定、路由、焦点、命中测试 |
| [07-Threading.md](./07-Threading.md) | 线程与异步：Dispatcher、线程池、协程回投 |
| [10-Rendering.md](./10-Rendering.md) | 渲染：显示列表、脏区、批合并、按需呈现 |
| [11-Text.md](./11-Text.md) | 文本：排版引擎、富文本文档模型、编辑器控件分层 |
| [12-Windowing.md](./12-Windowing.md) | 窗口与高 DPI：感知级别、三套坐标、DPI 切换时序 |
| [15-Properties.md](./15-Properties.md) | 属性主表（唯一事实来源）：属性全集、标志位、运行时读路径 |

随讨论推进，将与实现相关的细节拆分为：`13-Memory.md`、`14-Build.md`。

## 6. 当前工程状态

```
DUI.slnx                 解决方案：x64 单一平台，四个工程
DUI/                     库工程（DUI.vcxproj，静态库）
  DUI.h                  公共入口（业务代码唯一 include）
  Core/                  Result、String/Color/Rect/Thickness、协程 Task（M1 骨架）
  UI/                    Element、Style/StyleBuilder、Panel/Text/Button/ScrollView/Canvas、事件（M1 骨架）
  App/                   Application、Window/WindowOptions（M1 骨架）
  Platform/              NativeWindow（HWND 封装、窗口过程、DPI 感知清单）（M2）
  Graphics/              Device（D3D11/Flip Model/BGRA8）+ Painter（SDF 圆角矩形 + 字形批 + 形状批）（M3/M5）
  Text/                  DWrite 排版 + GlyphAtlas 灰度图集（M5）
Samples/                 示例工程（每个示例一个子目录 = 一个工程）
  HelloWorld/            最小示例：窗口正中一行“你好，World”
  LayoutFlex/            Flex 布局能力演示（grow/shrink/对齐/gap/嵌套）
  Canvas/                2D 绘图控件演示（直线/折线/多边形/矩形/圆角矩形/圆/椭圆/圆弧）
Build/                   产物根：Build\<工程>\<配置>\ 最终产物；Build\Temp\<工程>\<配置>\ 中间产物
DUI.props                公共编译基线（库与子工程共同 import，根目录）
Doc/Arch/                本文档集
```

**里程碑 1–4 已完成**（M1 API 骨架；M2 窗口/消息循环/PMv2 清单；M3 样式级联 + Flex +
D3D11 矩形渲染 + 最小输入；M4 Dispatcher + 线程池 + 真协程 + Click 阈值合成）。
示例界面按样式真实呈现（圆角矩形、hover/selected 状态切换、点击命中），
异步加载经真实线程池回投，空闲时零 CPU（按需渲染 + WaitMessage）。

后续里程碑（每个里程碑不改变既有签名，只填实现）：

| 里程碑 | 内容 | 对应文档 |
| --- | --- | --- |
| M2 | ✅ Platform.Window（HWND）+ 消息循环 + OnCreate/Show 时序（交换链归 M3） | 12-Windowing |
| M3 | ✅ 样式解析（级联/状态优先级）+ Flex 布局 + 失效标脏 + 矩形绘制 | 03-Style / 04-Layout / 10-Rendering |
| M4 | ✅ Dispatcher + 线程池 + 协程真实异步 + 输入命中/路由（隧道部分顺延，见 Todo M4 偏差 2） | 06-Events / 07-Threading |
| M5 | ✅ DirectWrite 文本（shape/layout/raster + 灰度图集）+ DPI 切换分级失效（完整字形重光栅随窗口 DPI 走，见 Todo M5） | 11-Text / 12-Windowing |
| M6 | 过渡动画 + 观测指标 | 05-Animation / 10-Rendering |

编译基线**已落地并验证**（Debug/Release 均可构建，产物 `DUI.lib`）：

| 项 | 设置 | 来源 |
| --- | --- | --- |
| 平台 | x64 only（无 Win32/x86 配置） | ADR-009 |
| 产物 | 静态库 `DUI.lib` | ADR-016 |
| CRT | `/MT`（Release）/ `/MTd`（Debug） | ADR-008 |
| 异常 | 无 `/EH` 开关（关闭） | ADR-007 |
| RTTI | `/GR-` | ADR-007 |
| 标准 | `/std:c++20`、`/permissive-` | 00-Overview §2.1 |
| 警告 | `/W4`、`/sdl` | — |
| 源码编码 | `/utf-8` | — |
| 宏 | `WIN32_LEAN_AND_MEAN`、`NOMINMAX`、`_UNICODE`（无 `_CONSOLE`） | — |
