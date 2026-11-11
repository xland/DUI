# 分层与模块

> 状态：草案 v0.1 — 待讨论确认。

## 1. 依赖规则

```
App ──→ UI ──→ Graphics ──→ Core
```

- **单向依赖**，下层不得引用上层，禁止跨层调用。
- **Core 是基础与通用服务层**：与界面无关的基础能力与通用服务（结果 / 字符串 / 数学 / 容器 / JSON / 转码、线程池与任务队列、剪贴板）都在 Core，允许直接使用系统 API（Win32）——2026-11-11 取消「Core 不依赖 Win32」约束，同日原 Platform 层并入 Core。可裁剪性由静态库按未引用裁剪保证（ADR-016），不依赖分层纯度。
- **Graphics 是渲染与文字排版层**：D3D11 设备与有序绘制指令收集（`Painter`）、图像解码（WIC、Direct2D SVG 光栅）、DirectWrite 排版与字形图集都在这一层——原独立的 `Text` 层 2026-11 并入（它只依赖 Graphics，不反向依赖）。
- **UI 是控件层**：元素树、样式、布局、事件与具体控件都在这里；富文本文档模型（原 `RichText` 层）2026-11 并入 UI。
- **App 是应用运行时层**：窗口壳（HWND / 消息过程）、系统对话框与原生菜单、应用主循环、渲染管线编排都在 App——它们都是「窗口模态的应用级交互」。

## 2. 模块清单

### Core

| 模块 | 职责 |
| --- | --- |
| Memory | Arena、Pool/Slab、对象池；热路径分配入口 |
| Containers | 内核内部用的侵入式链表、小容量向量、开放寻址哈希表（**公开 API 边界用标准库容器/字符串视图**，见 ADR-007） |
| String | UTF-16 为主（SSO），UTF-8 视图与转换只用于外部边界（ADR-026） |
| Math | Vec2、Rect、Mat3x2、Color（预乘 alpha） |
| Log / Assert | 日志与断言，Release 下可整体编译掉 |
| Ref | 非原子引用计数与作用域句柄；可引用对象仅 UI 线程持有（ADR-025） |
| Time | 时间戳、计时器、动画时钟基础 |
| Dispatcher / Threading | UI 任务队列 + 后台线程池（含 PostMessage 唤醒） |
| Clipboard | 系统剪贴板：文本（CF_UNICODETEXT）、图像（CF_DIB ↔ BGRA8）+ 任意格式（mime + 字节） |
| DragDrop | 拖放（远期；随窗口壳落 App） |

### Graphics（Direct3D 11 + DXGI）+ 文字排版（DirectWrite）

| 模块 | 职责 |
| --- | --- |
| Device | D3D11 设备/上下文、DXGI 工厂、适配器选择、设备丢失恢复（内部头 `Graphics/Device.h`，不进公共头） |
| SwapChain | Flip Model 交换链、尺寸变更、Present 策略 |
| Resource | Texture2D、Buffer、Shader、Sampler、状态对象 |
| Batcher | 顶点/实例环形缓冲、批合并、Draw Call 提交 |
| Painter | 绘制指令收集（显示列表） |
| Atlas | 通用纹理图集（字形/图标共用） |
| Clip | 矩形/圆角矩形裁剪栈 |
| FontCollection | 系统字体枚举 + 内嵌字体加载 |
| Shaping | 基于 `IDWriteTextAnalyzer` 的成形、换行、双向文本 |
| Layout | 段落排版、度量、命中测试（文本坐标 ↔ 字符索引） |
| Raster | 字形光栅化（方案见 ADR-003） |
| GlyphAtlas | 字形缓存与淘汰策略 |

### UI

| 模块 | 职责 |
| --- | --- |
| Element | 元素树（父子关系、Z 序、可见性、裁剪） |
| Layout | 布局算法（Flex 子集 + Grid + 绝对定位，HTML 风格语义见 ADR-005 / ADR-006 / [04-Layout.md](./04-Layout.md)） |
| Style | 样式属性表、级联解析、共享与派生、主题（见 [03-Style.md](./03-Style.md)） |
| Event | 事件路由、命中测试、焦点、键盘导航、处理器绑定（见 [06-Events.md](./06-Events.md)） |
| Invalidate | 脏区标记与合并、重绘调度 |
| Animation | 命令式属性动画（标签 / `AnimConfig` / 句柄）、缓动、动画值槽；与帧动画和光标闪烁共用统一 ticker（见 [05-Animation.md](./05-Animation.md)） |
| Widgets | 具体控件实现（可裁剪） |
| RichText | 富文本文档模型：属性区间、内联对象、序列化（见 [11-Text.md](./11-Text.md)）；落地于 `UI/RichDoc*`；编辑器控件 `UI/RichEditor.*`（见 [RichEditor.md](./RichEditor.md)） |

### App

| 模块 | 职责 |
| --- | --- |
| Application | 进程级初始化、全局资源、退出流程；`Application::Run` 消息循环（Dispatcher 排空 / 统一节拍） |
| Window | 窗口壳：`NativeWindow`（HWND 封装、消息过程、DPI 换算、IME 插入点 caret）+ `dui::Window` 公共基类（`OnCreate` / `OnDpiChanged` / `OnClosing` / `OnNcHitTest`，见 [12-Windowing.md](./12-Windowing.md) §6） |
| Dialog / Menu | 系统对话框（IFileDialog / MessageBoxW）与原生弹出菜单（TrackPopupMenuEx）；公共类型 + 实现 + `Window` 成员方法薄封装 |
| Frame | 渲染管线编排：`Collector`（元素树 → 绘制指令）+ `Application::RenderFrame`（排版 → 收集 → 光栅 → Present） |
| Theme | 默认主题与资源（颜色、字号、图标），属性默认值兜底（见 [03-Style.md](./03-Style.md)） |

## 3. 一帧的生命周期

```
[调度]  排空 UI Dispatcher 队列（后台完成回投的任务，见 [07-Threading.md](./07-Threading.md)）
   ↓
[输入]  Win32 消息 → 归一化输入事件
   ↓
[分发]  命中测试 → 事件沿元素树冒泡/隧道 → 处理器
   ↓
[动画]  推进活动动画 → 写入动画值槽（级联最高层）→ 按属性标志标脏
   ↓
[失效]  处理器修改状态 → 标记脏元素 / 脏矩形
   ↓
[布局]  仅对受影响的脏子树做 Layout（自底向上度量，自顶向下排列）
   ↓
[收集]  有脏区/动画 → Arena 重置 → 重建整帧显示列表（遍历可见树；见 10-Rendering §3）
   ↓
[光栅]  有序指令 → 流式顶点生成 + 段合并 → 提交 Draw Call
   ↓
[呈现]  Present（按需：无脏区则不呈现、不占用 CPU）
```

关键点：**"没有失效就没有工作"**。空闲时不做任何布局、绘制、Present，CPU 归零。

## 4. 内存策略（概要）

- 每个窗口持有固定容量的竞技场（Arena），元素/布局结果/绘制指令从中分配，窗口关闭时整体释放。
- 文本与图片等缓存为**进程级共享**、有容量上限、按 LRU 淘汰。
- 长生命周期对象（字体、纹理、着色器）用引用计数；短生命周期对象用竞技场，不逐个释放。
- 具体方案见后续 `13-Memory.md`。

## 5. 可裁剪性

编译期宏控制：`DUI_ENABLE_TEXT`、`DUI_ENABLE_IMAGE`、`DUI_ENABLE_A11Y` 等（动画未做编译期裁剪，见 [05-Animation.md](./05-Animation.md) §1.1）。
关闭后对应模块代码不参与编译，也不参与链接，直接体现在体积指标上。
